# 2026-11-07 진행 정리 : 5a (Sender, WorkContext, Connection 평문, ConnectionHost)

기준 문서: `docs/plan/plan_base_v2.md` 5a, `docs/design/design_base_v4.md` 9.1 ~ 9.8, 11절, `docs/test/test_base_v0.html` 9절.

## 코드

- `base/connection/kiotty_sender.h`: `Sender::stage(StageBase&, span)` / `release(StageBase&, length)`. 상태 없음. release 는 늘 compact 한다.
- `base/connection/kiotty_work_context.h`: IDLE / REQUESTED / WORKING / DONE. 상태는 `std::atomic` 하나이고 전이는 CAS 다. `start` 가 generation 을 적은 뒤 WORKING 을 release store, `finish` 가 outcome 을 적은 뒤 WORKING → DONE CAS. `verify` 는 WORKING 이 아니면 BUSY, generation 이 다르면 PEER_CLOSED.
- `base/connection/kiotty_io_source.h`: 설계 9.1 그대로 (순수 가상 일곱).
- `base/connection/kiotty_connection.h/.cpp`: 9.2 의 멤버 (`_phase` atomic, `_deadline`, `_work`, `_read_event` / `_write_event`, `_plain` StageSet, `_tls`, `_socket`, `_id`, `_context`, `_info`, `_state` atomic), 9.4 의 인터페이스, 9.5 의 on* / adopt / drive / refill / extend / apply, 9.6 의 select / encoded / resolve. `Handler`, `ConnectionLimits`, `ConnectionConfiguration` 도 이 헤더에 있다. `is_reusable` 계약 (`code_type`, `configuration_type`, `TABLE_FULL`, `OUT_OF_MEMORY`).
- `base/connection/kiotty_connection_host.h`: `ConnectionHost<N>` (`ReusablePool<Connection, N>` 과 notify mutex 하나).
- `base/context/kiotty_owner.h/.cpp`: `KiottyNetConnectionOwner` (가상 함수 일곱, 설계 13절) 와 `kiotty::net::Owner` typedef. 사이클 7 의 파일을 앞당겼다. `Handler::owner` 와 handle 의 owner 에 타입이 필요하고, 테스트가 그것을 상속한 더블을 만든다.
- 옛 skeleton 의 주석만 있던 `kiotty_connection.h` / `kiotty_connection_host.h` 를 새 파일로 바꿨다.
- TLS 분기 (`onOpened` 의 handshake, `onHandshake`, refill 의 decode, select 의 cipherSend, `encoded`, onSent 의 cipherSend consume, `receiveStage`) 는 설계대로 써 두었다. 평문 테스트는 이 길을 지나지 않으므로 5b 의 테스트가 처음 지난다.

## 설계와 달라진 것 (설계 v4 9.4, 9.5, 9.7, 9.8 에 반영)

1. **`Result<KiottyNetCode, ConnectionStream>` 을 쓰지 않는다.** ConnectionStream 은 참조 넷을 들어 기본 생성이 안 되므로 Result 의 값이 될 수 없다 (R6 에서 남긴 것). Connection 은 `verifyWork(generation)` 과 `stream()` 으로 나누고, host 는 `working(PoolHandle)` 이 `Result<KiottyNetCode, Connection&>` 을 돌려준다. Owner 의 Stream 함수 다섯은 `working(id)` → `value().stream().read(...)` 가 된다.
2. **action 의 검사는 adopt 한 자리다.** `adopt` 는 `Result<KiottyNetCode, IoAction>` 을 돌려주고 콜백이 없다. 콜백의 return 은 `admit(returned, handler)` 가 받아 `INVALID_ARGUMENT` 일 때만 `onError` 를 낸 뒤 CLOSE 다. `exact > recv_capacity` 와 PROTOCOL_ERROR 는 drive 의 `satisfied` 가 본다.
3. **closeStream 도 adopt 를 지난다.** 그래서 `closeConnectionStream(stream, thenRead(0))` 같은 잘못된 action 은 `INVALID_ARGUMENT` 이고 Stream 이 남는다. requirement 5절의 표에는 `thenWork` 와 BUFFER_FULL 만 있다. 같은 규칙을 넓힌 것이다.
4. **READ_UNTIL 의 `max` 가 delimiter 보다 짧으면 `INVALID_ARGUMENT` 다.** requirement 에 없던 규칙이다. `max == 0` 이 데이터를 받기도 전에 PROTOCOL_ERROR 로 닫히는 것을 막는다.
5. **`Connection::close(reason, handler)` 를 공개했다.** Owner close 와 IoLoop shutdown 이 WORKING 인 연결에 `apply(CLOSE)` 를 하려면 들어갈 자리가 필요하다 (9.7). alive 가 아니면 NONE 이다.
6. **host 의 acquire 도 lock 안이다.** 아무 thread 의 notify 가 `find` 로 같은 상태표 (`_states`, `_generations`) 를 읽기 때문이다. info, full, empty 도 lock 을 잡는다. `working` / `closeStream` 은 find 만 lock 안이고 그 뒤 (verify, Sender::stage) 는 밖이다. WORKING 인 연결은 settle 이 반납하지 않으므로 그 사이에 풀리지 않는다.
7. **host 의 acquire 가 `configuration.limits` 를 자기 `_limits` 로 덮는다.** 부르는 쪽 (IoLoop) 은 limits 를 모른다.
8. **`Connection::initialize` 가 limits 를 본다.** 셋 중 하나라도 0 이거나 컴파일 상한 (`Stage::maximum`) 을 넘으면 `INVALID_ARGUMENT` 이고 풀이 reset 한다.
9. **dispatch 의 첫 줄은 `!alive()` 다.** CLOSING 뿐 아니라 IDLE 도 NONE 이다. IoLoop 가 빈 칸을 돌 때도 안전하다.
10. **extend 는 `HOLD(timeout)` 을 돌려준다.** 그래야 apply 가 deadline 을 건다. HOLD 는 submit 할 것이 없으므로 걸린 READ 는 그대로다. START_WORK 를 거절할 때는 `HOLD(0)`. 모르는 type 이면 `onError(INVALID_ARGUMENT)` 뒤 CLOSE.
11. **`onTimeout(handler)`, `onWorkReady(handler)`.** 쓰지 않는 completion 인자를 뺐다.
12. **`userActionPending()`, `notificationWritable()` 은 const 가 아니다.** StageBase 의 readable / writable 이 Consumer / Provider (비 const) 를 지나서다. core 는 바꾸지 않았다.
13. **Sender::stage 의 길이 0 은 아무것도 넣지 않는 SUCCESS, data 가 NULL 이고 길이가 있으면 INVALID_ARGUMENT 다.** 사용자의 `thenWrite` 0 바이트는 adopt 가 먼저 거르므로 이 길로 오지 않는다. notify 의 0 바이트는 SUCCESS 가 된다.
14. **onError 를 내는 거절.** INVALID_ARGUMENT (adopt), refill 의 REQUEST_TOO_LARGE (`"receive: stage is full before the read condition"`), BUSY (extend). `exact > recv_capacity`, PROTOCOL_ERROR, BUFFER_FULL 은 onDisconnected 의 이유만 있다 (requirement 5절의 "연결은 닫히고 onDisconnected(code)").

## 확인받을 계약

plan 5a 의 "테스트 v0 14절 1 ~ 2" 는 requirement 5절에 이미 있어 그대로 따랐다. READ_UNTIL 의 data 는 delimiter 를 포함하고, `exact == 0`, write 길이 0, delimiter 길이 0 또는 5 이상은 `onError(INVALID_ARGUMENT)` 뒤 닫는다.

## 테스트

107 개를 더했다 (plan 의 평문 96 에 더한 것 포함).

| 파일 | 수 |
|---|---|
| `kiotty_test_sender.cpp` | 8 (`StageOfNullDataWithLengthIsInvalidArgument` 를 더했다) |
| `kiotty_test_work_context.cpp` | 8 |
| `kiotty_test_connection.cpp` | 64 + TEST_P 14 (`ConnectionCompletionKind` 의 CLOSING 여섯, `ConnectionPendingUserAction` 넷 × 두 모양) |
| `kiotty_test_connection_host.cpp` | 13 (`AcquireRejectsLimitsAboveTheCompiledMaximum`, `WorkingAndCloseStreamGoThroughTheCurrentHandleOnly` 를 더했다) |

support (`tests/unit/support/`):

- `kiotty_test_handler.h/.cpp`: `Recorder` (RecordingHandler 와 ScriptedHandler 를 하나로. 콜백마다 기록하고 action 을 돌려주는 콜백은 큐에서 꺼낸다. 비면 HOLD(0)). `thenWrite` / `thenReadUntil` 의 바이트는 Recorder 가 든다. `FakeOwner` (Owner 의 일곱을 기본값으로).
- `kiotty_test_completion.h`: `completion::receive / sent / timeout / workReady / workDone`, `ConnectionBench<N>` (host 를 heap 에, profile, owner, recorder, handler 가 테스트의 지역 변수. `open`, `opened`, `deliver` = resolve 의 자리에 바이트를 쓰고 RECEIVE 를 dispatch, `pick`, `notify`). 가짜 listener 는 `readEventOf` / `writeEventOf` 로 IoEvent 의 `pending` 을 테스트가 세우고 내리는 것이다.

테스트 계획과 다른 것:

- `NotifyDuringHandshakingIsAccepted` 는 5b 로 옮겼다. 평문 연결에는 HANDSHAKING 이 없다.
- wake 는 unit 에서 보지 않는다 (테스트 계획 4절의 "Owner 의 wake"). `NotifyStagesPlaintextAndWakesTheListener` 는 `NotifyStagesPlaintextIntoTheNotifyStage`, `CloseStreamVerifyIsSuccessAndGoesDone` 은 wake 를 빼고 돈다.
- `ReadUntilDoesNotRescanBytesItAlreadyScanned` 는 1 MiB 가 아니라 1 KiB 셋이다. delimiter 가 없는 바이트는 소비되지 않으므로 Stage (상한 16 KiB) 를 넘겨 넣을 수 없다. 돌아온 READ 의 `scanned` 가 매번 `n - (delimiter 길이 - 1)` 이다.
- host 에는 공개 `find` 가 없다. `FindWith...` 둘은 notify, info, working, closeStream 으로 본다.
- `WriteOfZeroBytesFollowsTheConfirmedContract` 는 INVALID_ARGUMENT + CLOSE 다.

## 로컬

- MSVC Debug: TLS ON / OFF 292/292 (전 185 + 107).
- WSL: gcc / clang TLS ON / OFF, clang ASan+UBSan, TSan 293/293, clang Release TLS 292/292 (Debug 전용 death 테스트 하나 빠짐).
- thread 를 쓰는 11 개 (`ConnectionHost.ReleaseResetsInsideTheNotificationLock`, `Connection.NotifyFromAnotherThread...`, `WorkContext.FinishFromAnotherThread...`, `Connection.CloseStream*`) 를 MSVC 20 회, TSan / ASan / gcc 각 10 회 반복 초록. sanitizer 보고 0.
- WSL 배포판 이름이 이 머신에서는 `Ubuntu` 다 (build skill 의 `Ubuntu-24.04` 가 아니다). 저장소 경로도 `C:\repository\private\kiotty-net` 이다.

## 5b 로 넘긴 것

- `NotifyDuringHandshakingIsAccepted`.
- 위의 TLS 분기 전부와 handshake DONE 뒤의 decode 한 번 (설계 8절).
- handler 의 콜백 포인터 여덟의 NULL 검사는 Connection 에 없다. 사이클 7 의 `run` 이 handler 를 받을 때 본다.

## 다음 할 일

5b (Connection TLS 경로, Acceptor).

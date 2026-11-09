# 2026-11-09 진행 정리 : StateMachine 사이클 M3 (Handler)

기준 문서: `docs/design/design_statemachine_v0.md` 3절, 5절, 6절, 13절의 M3. 사용자 콜백을 부르는 자리를 `Handler` 인터페이스 하나로 모은다. 동작은 바뀌지 않는다 (CallbackHandler 가 지금 하던 일 그대로).

## 더한 것

| 파일 | 내용 |
|---|---|
| `domain/entity/kiotty_handler.h` | `Handler` 인터페이스. 순수 가상 다섯 (`validate`, `initialState`, `admit`, `dispatch`, `report`) 과 데이터 `owner&` 하나. `Caller` 는 앞 선언 |
| `domain/entity/kiotty_connection_event.h / .cpp` | `connectedEvent`, `readEvent`, `writtenEvent`, `workStartedEvent`, `timeoutEvent`, `closedEvent`. 공개 `KiottyNetConnectionEvent` 를 채운다 |
| `data/repository/connection/kiotty_callback_handler.h / .cpp` | `CallbackHandler final : Handler`. 사용자 `KiottyNetConnectionHandler` 를 아는 유일한 자리. `dispatch` 는 event type 의 switch 여섯 (default 없음). WORK_STARTED 는 `onWorkStarted` 뒤 START_WORK, CLOSED 는 `onDisconnected` 뒤 HOLD (버려진다) |

## 바꾼 자리

| 자리 | 전 | 지금 |
|---|---|---|
| `Connection::onReady / onWritten / onTimeout` | `handler.callbacks.onX(_context, handle, ...)` | `handler.dispatch(caller, xEvent(...))` |
| `ReceiveFlow::deliver` | `caller.handler.callbacks.onRead(...)` | `caller.handler.dispatch(caller, readEvent(data))` |
| `Connection::enterClosing` | `onDisconnected(app_context, ...)` | `dispatch(caller, closedEvent(reason))`, return 은 버린다 |
| `Caller::reject` | `callbacks.onError(app_context, ...)` | `handler.report(info, code, message)` |
| `IoLoop::welcome / open / reportAcceptFailure` | `callbacks.onConnected / onError` | `handler.admit(info)`, `handler.report(...)` |
| `Connection::onWorkReady` | `onWorkStarted(_context, stream)` 하고 끝 | `dispatch(caller, workStartedEvent(stream))`. return 이 START_WORK 면 stream 은 worker 의 것. 아니면 그 자리에서 `_work_flow.closeStream(generation, returned)`. 실패 (INVALID_ARGUMENT, BUFFER_FULL) 는 `reject` 로 알리고 stream 은 남는다. outcome 은 같은 `stepSlot` 의 `isFinished` 가 바로 집는다 |
| machine state (DV6) | `ReceiveFlow` 의 `std::atomic<int32_t>` 와 `machineState / setMachineState` | `Connection` 의 private `int32_t _machine_state`. `Caller` 가 `int32_t& machine_state` 로 든다 (`CALLER_BYTES` 를 고쳤다). IO thread 만 읽고 쓴다. DV6 은 코드에서 닫혔다 |
| `Server::run`, `EventClient::run` | `run(callbacks, app_context)` 가 안에서 `Handler` 를 만든다 | `run(const Handler&)`. 첫 줄이 `handler.validate()` (gate 에 들어가기 전) |
| 번역 층 `KiottyNet_runServer / runEventClient` | `server->run(*handler, app_context)` | stack 에 `CallbackHandler` 를 만들어 `run(callbacks)` |

## 설계와 다른 것

- 설계 3.4 는 `ConnectionConfiguration` 에 `int32_t machine_state` 를 더하고 `initialize` 가 그것으로 놓게 적었다. 대신 `Connection::onOpened(handler)` 의 첫 줄이 `_machine_state = handler.initialState()` 다. `onOpened` 는 연결마다 한 번, 어떤 dispatch 보다 먼저 불린다 (IoLoop::open, 테스트 bench 도 같다). `ConnectionConfiguration` 은 8 바이트 정렬 struct 라 `int32_t` 하나를 더하면 padding 4 바이트가 생겨 크기 static_assert 를 바꿔야 했다. IoLoop 의 `welcome` 은 그래서 바뀌지 않았다 (설계 5절의 두 번째 줄). `initialize` 와 `reset` 은 0 으로 놓는다.

## 테스트

- fixture 의 `Handler` 를 `CallbackHandler` 로 (`support/kiotty_test_completion.h`, `integration/kiotty_test_io_loop.cpp`). 생성자 인자는 같다.
- `integration/kiotty_test_endpoint.cpp` 는 내부 `run(callbacks, app)` 대신 공개 `KiottyNet_runServer / runEventClient` 를 부르는 `runOn` 둘을 쓴다.
- `Connection.MachineStateStartsAtZeroAndIsClearedOnRelease` 를 지웠다 (`ReceiveFlow::machineState` 가 없다). 새 넷은 테스트 안의 `StateProbe : Handler` 로 본다.
  - `MachineStateStartsFromTheHandlerAndTheCallerCarriesItBetweenEvents`: `initialState` 7 → CONNECTED 가 7 을 보고 8 로 → CLOSED 가 8 을 본다. 다시 연 slot 은 새 handler 의 초기값.
  - `WorkStartedReturningReadIsAppliedToTheStreamWithoutAWorker`: WORK_STARTED 가 `thenRead(4)` 를 돌려주면 work 가 DONE 이고 outcome 이 READ.
  - `WorkStartedReturningStartWorkLeavesTheStreamToTheWorker`.
  - `WorkStartedReturningAnInvalidActionIsReportedAndTheStreamStaysOpen`: `thenRead(0)` 은 INVALID_ARGUMENT 로 report, work 는 DONE 이 아니다.

| 환경 | 결과 |
|---|---|
| MSVC TLS ON | 463 / 463 |
| MSVC TLS OFF | 463 / 463 |
| WSL gcc TLS ON | 464 / 464 (ring 테스트 skip) |
| WSL clang ASan + UBSan TLS ON | 464 / 464. UBSan 보고는 googletest 안의 vptr 뿐 |
| WSL benchmark target | 빌드 |
| `check_rules.py src/implementation` | LINE 0, MACRO 18, PUBLIC 2 (전과 같다) |

## 벤치마크 (WSL2, Release, TLS ON, io_uring)

변경 전 (`82c066b`, worktree) 과 후를 번갈아 세 번 (`--benchmark_min_time=2s`). 평균이다. 같은 쪽의 회차 사이 차이가 10% 안팎이라 가상 호출 하나의 차이는 잡음 안이다.

| 벤치 | 전 | 후 |
|---|---|---|
| EchoRoundtrip clients 1, max 16 | 216 µs | 223 µs |
| EchoRoundtrip clients 1, max 1024 | 214 µs | 213 µs |
| EchoRoundtrip clients 128, max 1024 | 2.22 ms | 2.17 ms |

## 다음

M4: StatefulHandler (4절), `kiotty_net_statemachine.cpp` 의 `runStatefulServer / runStatefulEventClient`, 10절의 unit 과 integration, 구현 가이드 3절의 echo 예제를 테스트로.

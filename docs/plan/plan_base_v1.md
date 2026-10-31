# kiotty_net_base 구현 계획 v1

2026-10-31. 기준은 `docs/design/design_base_v4.md` (무엇을 만드나) 와 `docs/test/test_base_v0.html` (무엇으로 확인하나) 이다. `plan_base_v0.md` 의 사이클 0 ~ 3b 는 끝났다 (commit `0e45ee5` ~ `e195342`). 설계가 v2 에서 v4 로 바뀌었으므로 이 문서는 둘로 나눈다. **1부: 끝난 사이클에서 v4 에 맞게 고칠 것. 2부: 앞으로 할 사이클.** 한 사이클의 정의, 로컬 두 환경, CI 매트릭스, commit 규칙, 완료 조건은 v0 0 ~ 1절 그대로다.

v4 가 v2 와 다른 점 중 코드에 닿는 것 (설계 v4 17절):

| 바뀐 것 | 영향 받는 층 |
|---|---|
| action 함수 `then*`, `KiottyNetWorkHandle` → `KiottyNetConnectionStream`, Stream 동기 함수 다섯, onWorkStarted / onError 시그니처, `initializeContextWithTlsPool` | base.h, statemachine.h, C++ 래퍼, requirement, user_guide |
| `IoAction::as.write.length`, `IoKind::WORK_READY`, `IoCompletion::workReady / workDone(slot, outcome)`, "WRITE 완료 = payload 전부" 계약 | event (3b 코드와 테스트) |
| event 둘이 방향 (read / write) 으로, 송신 맥락 셋 (send / notify / cipherSend) 이 각자 Stage, write lane 하나 (`drain` → `pickWrite`), `drive` loop, `refill`, WorkContext 의 REQUESTED, ConnectionStream | connection, io_loop, tls, stream (아직 코드 없음. 2부) |
| 한 바퀴 `drain → settle → collectIoEvents → step → collectLocalEvents → step` | io_loop (코드 없음), 테스트 계획 10절 |
| max_clients 128 / 1024, TLS Pool 8 MiB 인자 | config, context (코드 없음), requirement |

## 1부. 끝난 사이클에서 고칠 것

세 사이클을 다시 연다. 셋 다 로컬에서 끝나고 commit 하나씩이다. 순서는 의존 순서다 (헤더 → event → 문서).

### R1. base.h 와 statemachine.h 를 v4 3절로 (사이클 1 재방문)

| 구현 | `include/kiotty_net_base.h`: action 함수 여섯을 `KiottyNet_thenHold / thenRead / thenReadUntil / thenWrite / thenWork / thenClose` 로. `KiottyNetWorkHandle`, `KiottyNet_finishWork`, `KiottyNet_isWorkConnected` 삭제. `KiottyNetConnectionStream` typedef 와 `KiottyNet_readConnectionStream / writeConnectionStream / writeConnectionStreamFile / isConnectionStreamOpen / closeConnectionStream` 다섯. `onWorkStarted(ctx, KiottyNetConnectionStream stream)`, `onError(app, KiottyNetConnectionInfo info, code, message)`. `KiottyNet_initializeContextWithTlsPool(size_t bytes)`. C++: `KiottyNet_Work` → `KiottyNet_ConnectionStream` (read / write / writeFile / open / close(action). 소멸자는 `close(thenClose(KIOTTY_SUCCESS))`). `include/kiotty_net_statemachine.h` / `.cpp`: `KiottyNet_holdCommunication(0)` → `KiottyNet_thenHold(0)`, WORK_STARTED 행의 entry action 은 `thenWork()`. http.h / websocket.h 는 바뀐 타입을 쓰면 같이 |
|---|---|
| 문서 | `requirement_v0.md` 5절 (action 표, Work 단락을 Stream 으로, 435줄 "Work 중에도 notify 가 나간다" → "쌓이고 Work 뒤에 나간다", "소유자 close 는 WORKING 도 바로 닫는다" → "shutdown 만, close 는 Work 가 끝난 뒤"), 6 ~ 7절 (onError 시그니처, Owner close 의 Work 대기 계약), 29 / 243줄 "최대 128 Client (설정으로 1024 까지)". `user_guide_http.md` 1.5 (Work 중 notify). `implementation_guide_v0.md` 1.5 ~ 1.6 은 v4 9.5 ~ 9.7 을 가리키는 한 줄로 바꾼다 (옛 DRAINING / drain 설명은 지운다) |
| 테스트 | `tests/unit/kiotty_test_headers_cpp.cpp`, `kiotty_test_headers_c.c` 의 struct 크기 / enum 비교를 새 시그니처로. smoke 와 benchmark smoke 의 `KiottyNet_moveToNextState` 호출이 `thenHold` 를 쓰는지 |
| 완료 | 네 runner 컴파일 초록. 헤더만 따로 (clang `-std=c++11 -fsyntax-only`, `-std=c99`; MSVC `/Zs /std:c++14 /permissive-`, `/TC /std:c11`) 경고 0 |
| commit | `api: then* actions, ConnectionStream for Work, onError with ConnectionInfo, TLS pool size` |

### R2. event 의 세 줄과 테스트 모양 (사이클 3b 재방문)

| 구현 | `kiotty_io_action.h/.cpp`: `as.write.length`, `IoAction::write(IoPurpose, uint32_t timeout_ms, size_t length)`, `IoAction::from` 이 WRITE 의 length 를 채운다. `kiotty_event.h`: `IoKind::WORK_READY`, `IoCompletion::workReady(slot)`, `IoCompletion::workDone(slot, IoAction outcome)`. IOCP backend 는 그대로 (`kindOf`, `completionOf` 가 바뀐 필드를 쓰지 않는다) |
|---|---|
| 테스트 | `kiotty_test_io_action.cpp`: `static_assert(sizeof(IoAction) < 64)` 유지, `write(purpose, timeout, length)` 의 필드 확인 하나. `kiotty_test_event_listener.cpp`: `WriteLargerThanSocketBufferCompletesPossiblyPartiallyAndTheRestIsResubmittable` → `WriteLargerThanSocketBufferCompletesOnceWithFullLength` (3b 계약. IOCP 는 이미 그렇게 동작한다. SO_SNDBUF 0 의 peer 가 읽기 시작하면 한 번의 완료에 전체 길이). `TwoSendsSubmittedInOrderArriveAtPeerInThatOrder` → read event + write event 모양: 한 event 에 READ, 다른 event 에 WRITE 를 동시에 걸고 둘 다 완료되는지 (`ReadAndWriteOnSeparateEventsCompleteIndependently`). `DetachWithPendingReadAndWriteDeliversTwoCancelled` 는 이미 read + write 모양이라 이름만 확인 |
| 문서 | `test_base_v0.html` 7절의 세 케이스 이름. `design_base_v2.md` 머리에 "v4 로 대체됨" 한 줄 |
| 완료 | MSVC Debug 전부 초록 (EventListener 5 회 반복 포함). Linux / macOS 는 unit + socket integration 그대로 |
| commit | `event: write length on IoAction, WORK_READY completion, full-length write contract in tests` |

### R3. 테스트 계획 9 ~ 10절을 v4 로

코드가 없는 층이지만 사이클 5 ~ 6 의 "무엇으로 확인하나" 가 v2 모양이라 먼저 고친다. 문서만, 하루 안.

| 바꿀 것 | `test_base_v0.html` 9절 (connection): 케이스를 v4 9.5 의 Phase × completion 표와 9.6 의 pickWrite 순서 (alive → working → pending → cipherSend → notify → send), 9.7 의 WorkContext 네 상태와 `verify`, Sender 의 stage / release / clear, `drive` 의 반복 상한, `refill` 의 compact / decode / REQUEST_TOO_LARGE, onTimeout 의 `userActionPending` 표 (11절) 로 다시 쓴다. 10절 (io_loop): 여섯 프로세스 각각의 input / output 과, "wait 전에 Stage 에 걸리지 않은 바이트가 없다", "CLOSE 뒤 같은 배치의 TIMEOUT 이 없다", "WORK_READY 는 같은 runOnce 안", "notify 송신 중 thenWrite 는 두 WRITE 로 순서대로" 네 성질을 케이스로. 14절의 확인받을 것에서 끝난 항목 (3, 5) 을 지운다 |
|---|---|
| 완료 | 9 ~ 10절의 케이스 수가 다시 세어져 있고, 사이클 5a / 5b / 6 의 표가 그 수를 가리킨다 |
| commit | `docs: test plan 9-10 for design v4 (connection, io_loop)` |

## 2부. 앞으로 할 사이클

v0 의 번호를 이어 간다. 3c 가 다음이다. 새로 끼는 사이클은 4b (stream) 하나다.

### 3c. event, epoll

| 구현 | `backend/kiotty_epoll_event_listener.cpp`. EPOLLONESHOT, event 마다 dup fd. READ: readable 뒤 recv → payload → 완료. **WRITE: 바로 send 하고 남으면 `EPOLLOUT | ONESHOT` 으로 다시 걸어 payload 전부를 보낸 뒤에만 완료 하나** (v4 7절 계약. v0 의 "남으면 EPOLLOUT" 에서 "끝까지" 로 바뀐 자리). wake 는 eventfd. detach 는 걸린 event 마다 CANCELLED 완료를 자기 배열에. 즉시 실패는 오류 완료로 |
|---|---|
| 테스트 | 3b 와 **같은 파일**. 추가는 `EventListener_Epoll::NoCompletionArrivesWithoutASubmitEvenIfPeerSends` 하나. R2 의 `WriteLargerThanSocketBufferCompletesOnceWithFullLength` 가 epoll 에서 진짜로 의미를 가진다 (peer 가 천천히 읽어도 완료는 한 번, 길이는 전체) |
| CI | WSL gcc 13 / clang 18 초록 → push. CI 의 Linux job 에 integration, ASan / TSan |
| 완료 | 두 backend 가 같은 테스트를 통과. 플랫폼 분기가 있는 테스트는 `_Iocp` / `_Epoll` 둘뿐 |
| commit | `event: epoll backend passes the same EventListener tests as IOCP` |

### 4. tls

| 구현 | `tls/kiotty_tls_profile.h`, `kiotty_tls_engine.h`, `backend/kiotty_stub_tls.cpp` 먼저, 그 다음 `backend/kiotty_openssl_tls.cpp`. v4 8절: cipher Stage 둘 (`cipherReceive`, `cipherSend`), `TlsEngine(pool, receive_capacity, send_capacity)`, `handshake()` 의 SEND 우선 규칙, `decode(plain)` 은 "cipherReceive 를 SSL 에 넣고 plain 이 찰 때까지 SSL_read, 끝에 cipherReceive compact, 꺼낸 길이를 Result 로", `encode(plain)` 은 실패하지 않는다 (SSL_OP_NO_RENEGOTIATION, debug assert). lastCode / lastMessage 의 오류 분류는 v2 8.1 그대로. `KIOTTY_NET_TESTING` 아래 `debugKeyUpdate()` / `debugShutdown()`. CMake: `KIOTTY_NET_USE_TLS` ON 이면 `find_package(OpenSSL 3)`. 테스트 인증서 `tests/certs/generate.sh` 와 PEM 다섯 벌 |
|---|---|
| 테스트 | 테스트 v0 8절 (TlsProfile 6, TlsEngine 23) 을 `TlsPump` (소켓 없이 두 engine 을 맞대는 더블) 로. 더하는 것: `DecodeDrainsPlaintextLeftInSslAcrossCalls` (16 KiB 레코드, 4 KiB plain 으로 네 번 꺼낸다), `DecodeCompactsCipherReceive`, `EncodeNeverLeavesCipherSendEmptyForNonEmptyPlain`. stub 빌드는 `StubBuildReturnsNotSupportedForBothOpens` 만 돌고 나머지 `GTEST_SKIP` |
| CI | 매트릭스에 `tls: [ON, OFF]` 축. OpenSSL 설치는 v0 6절 그대로 (첫 push 에서 확인) |
| 완료 | 8 job + sanitizers 초록. TLS ON 에서 `fallbackCount` 0 |
| commit | `tls: TlsProfile and TlsEngine over OpenSSL memory BIO with two cipher stages, stub backend, test certificates` |

### 4b. stream : ConnectionStream (새 사이클)

Connection 과 StreamClient 가 같이 쓰는 동기 통로다. 소켓 + TlsEngine 만 있으면 되므로 connection 앞에 끝낸다.

| 구현 | `stream/kiotty_connection_stream.h/.cpp`: v4 9.7. `ConnectionStream(socket, tls, receive, send)`, `read / write / writeFile / connected / handshake`, private `flush` (cipherSend 잔여를 먼저 sendAll). 모든 동기 함수의 첫 걸음이 flush. 평문 write 는 사용자 버퍼에서 바로 sendAll (복사 0). TLS write 는 send_capacity 씩 encode → cipherSend → sendAll. writeFile 은 파일을 send_capacity 씩 읽어 write 와 같은 길 (zero-copy 없음). `backend/kiotty_posix_file.cpp`, `kiotty_windows_file.cpp` 는 열기 / 읽기 / 닫기만. timeout 은 non-blocking + poll (v2 15-5) |
|---|---|
| 테스트 | integration (loopback). `ReadReturnsStagedPlaintextBeforeTouchingSocket`, `ReadBlocksUntilPeerSendsThenReturnsUpToLength`, `ReadTimesOut`, `WritePlaintextSendsAllWithoutCopy` (peer 가 전부 받는다), `WriteTlsEncodesInSendCapacityChunks` (TLS 더블), `WriteFileSendsExactRange`, `WriteFileMissingPathIsInvalidArgument`, `FlushSendsLeftoverCipherSendFirst`, `ConnectedIsFalseAfterPeerClose`, `HandshakeCompletesAgainstBlockingPeer` (TLS). 열 개 안팎 |
| 완료 | Windows / Linux integration 초록. TLS ON job 에서 TLS 케이스 포함 |
| commit | `stream: ConnectionStream synchronous channel shared by Work and StreamClient` |

### 5. connection

둘로 자른다. 소켓이 없어 로컬에서 전부 끝난다.

#### 5a. Sender, WorkContext, NotificationContext, IoSource, ConnectionHost, Connection 평문

| 구현 | `connection/kiotty_sender.h` (stage / readable / peek / release / clear. release 는 늘 compact), `kiotty_work_context.h` (IDLE / REQUESTED / WORKING / DONE, request / abandon / start / requested / finished / onFinished / working / reset / verify / finish), `kiotty_notification_context.h` (mutex, listener, io_thread. queued 없음), `kiotty_io_source.h` (eventFor 가 방향으로, shutdown), `kiotty_connection.h/.cpp` (v4 9.2 의 상태 전부, 9.4 의 인터페이스, 9.5 의 dispatch / apply / onOpened / onReceived / onSent / onTimeout / onWorkReady / onWorkFinished / onPeerGone / adopt / drive / refill / extend, 9.6 의 pickWrite / select / encoded / resolve, 9.7 의 stream / closeStream, 9.8 의 notify. TLS 분기 `_tls.active()` 는 자리만 두고 5b 에서 채운다), `kiotty_connection_host.h/.cpp` (acquire / release / full / empty / find / broadcast, Phase::IDLE) |
|---|---|
| 테스트 | R3 에서 다시 센 9절의 평문 케이스. 특히: `drive` 가 recv_capacity 번 안에 끝난다 (1 바이트 READ 를 가득 찬 Stage 에), `refill` 이 compact 뒤 writable 을 돌려준다, Stage 가 가득 찬 채 조건이 안 차면 REQUEST_TOO_LARGE, pickWrite 의 순서 여섯 (dead / working / pending / notify 먼저 / send / 없음), notify 송신 중 thenWrite 는 stage 와 deadline 만, SEND(NOTIFY) 가 `_notify` 만 release 하고 SEND(USER) 가 `_send` 만 release, onTimeout 의 다섯 행, apply(START_WORK) 는 REQUESTED 만, workReady 는 두 event 가 비었을 때만, apply(CLOSE) 가 REQUESTED 를 거둔다, closeStream 의 verify 셋 (BUSY / PEER_CLOSED / SUCCESS), CLOSING 중 closeStream 은 CLOSED 를 돌려주되 DONE 으로 간다. support: `RecordingHandler`, `ScriptedHandler`, `completion::*`, 가짜 listener (pending 을 세우고 내리는 더블) |
| 확인받을 계약 | 테스트 v0 14절 1 ~ 2 (READ_UNTIL 이 delimiter 포함, READ 0 / WRITE 0 / delimiter 0 또는 5 는 onError(INVALID_ARGUMENT) + CLOSE). 이 사이클 시작 전에 확정 |
| commit | `connection: Sender, WorkContext, Connection with drive/refill/pickWrite, ConnectionHost (plaintext unit tests)` |

#### 5b. Connection TLS 경로, Acceptor

| 구현 | `_tls.active()` 분기 네 자리 (onOpened 의 deadline + onHandshake, refill 의 decode, select 의 cipherSend 우선과 encoded, onSent 의 cipherSend consume), `receiveStage(HANDSHAKE / TLS USER)`. `connection/kiotty_acceptor.h/.cpp` + IOCP / epoll 쪽 resolve (AcceptEx 버퍼 / 빈 payload) |
|---|---|
| 테스트 | TLS 케이스 (TlsPump 로 DONE 까지 간 engine 을 심는다): handshake 의 SEND → NONE 과 drain 의 WRITE(HANDSHAKE), TLS 1.3 의 ticket 이 DONE 앞에 SEND 로 한 번 더, `as.write.length` 는 평문 / `completion.length` 는 암호문, encode 순서 = 송신 순서 (notify 와 send 가 같이 기다릴 때 notify 레코드가 먼저), KeyUpdate 답장이 cipherSend 에 남아 다음 drain 이 CONTROL 로 보낸다, decode 실패는 onError + CLOSE(RECORD_ERROR). Acceptor 3 (integration) |
| 완료 | unit 전부 매트릭스 초록 (TLS ON job 에서 TLS 케이스 포함). Acceptor integration 은 Windows / Linux |
| commit | `connection: TLS path in Connection, Acceptor as IoSource` |

### 6. io_loop

| 구현 | `io_loop/kiotty_io_loop.h/.cpp`: v4 10절 그대로. `run / runOnce / drain / settle / collectIoEvents / step / collectLocalEvents / access / admit / submit / waitTimeout / shutdown`. local completion 배열 3 × capacity. `submit` 의 WRITE case 가 listener 를 부르고 `drain` 은 `submit(c, c.pickWrite())` 만 |
|---|---|
| 테스트 | R3 에서 다시 쓴 10절 (integration). IoLoop 를 Server 없이 직접 조립 (ConnectionHost + EventListener + Acceptor + IoLoop), run 은 테스트 thread. 네 성질: `ClientHelloIsArmedBeforeFirstWait` (TLS EventClient 모양으로 admit 뒤 run), `TimeoutIsNotDeliveredAfterCloseInTheSameBatch`, `WorkStartedFiresInTheSameRunOnceWhenWriteLaneIsIdle`, `WorkStartedWaitsForInFlightNotifyThenFires`, `NotifyDuringUserWriteArrivesAsTwoWholeMessagesInOrder`, `OwnerCloseDuringWorkShutsDownSocketAndReleasesAfterCloseStream`. 벤치마크 `EchoRoundtrip1KiB`, `IdleCpu`, `NotifyLatencyFromOtherThread`, `NotifyFromHandlerLatency` 를 IoLoop 조립으로 먼저 |
| CI | Windows / Linux integration. TSan 이 notify 의 lock (stage / pick / release / clear) 과 closeStream 의 CAS 를 처음 본다. 플래키 방지는 v0 그대로 (`waitUntil`, 포트 0, 상한 넉넉히) |
| 완료 | integration 매트릭스 초록, TSan 경고 0. 벤치마크 넷의 첫 수치를 테스트 v0 12절에 |
| commit | `io_loop: drain/settle/collectIoEvents/step/collectLocalEvents cycle with loopback tests` |

### 7. server, client, context, config

| 구현 | `context/kiotty_owner.h`, `kiotty_context.h/.cpp` (`Context(tls_pool_bytes)`, 기본 8 MiB, Holder<Owner>[16], g_contexts[16], CRYPTO_set_mem_functions, WSAStartup), `config/kiotty_config.h/.cpp` (initialize* / configure*, `configureServerMaxClients` 의 [1, 1024] 검사, 기본 128, isInitialized 4 오버로드, mark), `config/kiotty_action.h` (then* 여섯 + accept / ignore 조립), `server/kiotty_server.h/.cpp`, `client/kiotty_event_client.h/.cpp` (run 전 admit → 첫 runOnce 의 drain 이 ClientHello 를 건다), `client/kiotty_stream_client.h/.cpp` (ConnectionStream 하나를 값으로. connect 는 connectWithRetry → `_stream.handshake`). Owner::shutdown 은 WORKING 연결의 closeStream 을 기다린다 (문서에 적힌 계약) |
|---|---|
| 테스트 | 테스트 v0 11절 중 Config (max_clients 0 / 1025 거부, 1024 허용, 기본 128), Action (then* 여섯). Context, Server, EventClient, StreamClient 를 내부 클래스로 먼저 (C API 는 8). 더하는 것: `ServerCloseWaitsForWorkerCloseStream`, `StreamClientSharesConnectionStreamCode` (같은 더블로 Connection WORKING 과 같은 케이스), `InitializeContextWithTlsPoolSizesThePool` |
| 완료 | 매트릭스 초록 (TLS ON / OFF). `OnDisconnectedIsCalledExactlyOncePerAcceptedConnectionOverAThousandConnections` 가 ASan 에서 누수 0 |
| commit | `owner: Server, EventClient, StreamClient over ConnectionStream, Context with TLS pool size, config bounds` |

### 8. base.cpp 번역과 C API 테스트

| 구현 | `src/kiotty_net_base.cpp`: v4 15절 표 그대로. `conn(h)` 의 네 단계, Stream handle 의 타입 비트 (bit 63) 검사, `stream(s)` 의 code 를 그대로 돌려주는 다섯 함수, IO Thread 검사 (`isIoThread` → BUSY). 로직 0 줄 (분기는 nullptr / isInitialized / 타입 비트) |
|---|---|
| 테스트 | 11절을 `include/kiotty_net_base.h` 만 포함하는 `tests/integration/kiotty_test_base_api.cpp` 로. handle 군 TEST_P { 0, 유효, 옛 generation, 옛 owner generation, 범위 밖, 랜덤, Connection handle 을 Stream 함수에, Stream handle 을 notify 에 }. `ReadConnectionStreamFromIoThreadIsBusy`. 벤치마크 12절 나머지 (EchoRoundtrip128Clients 로 수치 갱신, ReadHeavy*, WriteHeavy*, BroadcastFanOut, ConnectionChurn, TlsEchoOverhead, TlsHandshakeRate, WorkHandoff, `StreamWriteFile64MiB`) |
| CI | `benchmark` job (`workflow_dispatch` 와 main push). 결과 JSON 을 artifact 로 |
| 완료 | 매트릭스 초록. 벤치마크 수치를 테스트 v0 12절에 기준치로. `docs/progress/` 에 "base 완료" 와 kqueue / io_uring / HTTP 로 가는 다음 할 일 |
| commit | `base: kiotty_net_base.cpp translation layer, C API tests and benchmarks` |

## 3. 사이클 요약

| 사이클 | 설계 v4 | 테스트 v0 | 층 | 로컬에서 끝나나 | 상태 |
|---|---|---|---|---|---|
| 0 ~ 3b | — | — | build, api, core, socket, event IOCP | — | 끝 |
| R1 | 3절 | 15절 ① | base.h | 예 | 1부 |
| R2 | 5, 7절 | 7절 | event | 예 (Windows) | 1부 |
| R3 | 9 ~ 11절 | 9 ~ 10절 다시 쓰기 | 문서 | 예 | 1부 |
| 3c | 7절 | 7절 같은 파일 | event epoll | 예 (WSL) | 2부 |
| 4 | 8절 | 8절 + 3 | tls | 예 | 2부 |
| 4b | 9.7 ConnectionStream | 새 10 개 | stream | 예 | 2부 (새 사이클) |
| 5a | 9.2 ~ 9.8 평문 | 9절 | connection | 예 | 2부 |
| 5b | 9절 TLS, 9.9 Acceptor | 9절, 10절 Acceptor | connection | 예 | 2부 |
| 6 | 10 ~ 11절 | 10절, 벤치 4 | io_loop | 예 | 2부 |
| 7 | 12 ~ 13절 | 11절 (내부 클래스) | owner, config | 예 | 2부 |
| 8 | 15절 | 11절 (C API), 벤치 나머지 | base.cpp | 예 | 2부 |

## 4. 리스크와 대응

v0 12절의 일곱 가지는 그대로다. v4 로 더해진 것:

| 리스크 | 대응 |
|---|---|
| epoll 의 "WRITE 완료 = payload 전부" 가 느린 peer 에서 EPOLLOUT 을 여러 번 돈다. 그 사이 다른 fd 의 완료 처리와 섞이면 순서가 꼬일 수 있다 | event 하나에 "보낸 길이" 를 들고, EPOLLOUT 마다 이어 보내며, 완료 하나는 다 보낸 뒤에만. 테스트는 SO_SNDBUF 를 작게 한 peer 가 천천히 읽는 모양으로 (3c) |
| Owner close 가 IO 없는 worker 의 closeStream 을 기다린다. 테스트에서 worker 를 잊으면 run 이 끝나지 않아 테스트가 멈춘다 | 모든 Work 테스트는 `KiottyNet_ConnectionStream` (C++ 소멸자가 close) 또는 `waitUntil` 상한 안의 명시적 close 로. 계약은 requirement 7절에 (R1) |
| notify 의 lock 네 자리 (stage / pick / release / clear) 중 하나를 빼먹으면 TSan 만 잡는다 | 5a 의 Sender 테스트는 lock 을 모르는 Sender 를 그대로 두고, Connection 테스트에서 "notify 를 다른 thread 가 1000 번" 을 TSan job 에 넣는다 (6) |
| `drive` 의 반복이 recv_capacity 를 넘는 길이 있으면 (exact 0 을 거부하지 않는 등) 바쁜 loop 가 된다 | 5a 의 거부 계약 (14절 1 ~ 2) 을 먼저 확정하고, `DriveTerminatesWithinRecvCapacityIterations` 를 둔다 |
| TLS 1.3 의 ticket 이 DONE 앞에 SEND 로 한 번 더 나가는 바퀴를 테스트 더블이 흉내 못 하면 5b 가 실제 OpenSSL 에서만 깨진다 | TlsPump 가 실제 OpenSSL 두 engine 을 맞대는 더블이므로 소켓만 없다. 흉내가 아니다 |
| `encode` 가 "실패하지 않는다" 는 불변식이 OpenSSL 버전에 따라 깨질 수 있다 (renegotiation 옵션, BIO 용량) | 4 의 `EncodeNeverLeavesCipherSendEmptyForNonEmptyPlain` 을 TLS 1.2 / 1.3 × 가장 큰 평문 (send_capacity, push_capacity) 으로 돈다. debug assert 는 유지 |

## 5. 지금 할 일

R1. `include/kiotty_net_base.h` 를 v4 3절대로 고치고 statemachine.h / requirement / user_guide_http 를 맞춘다. 헤더 컴파일 검사 초록 → commit → R2 (event 의 세 줄과 테스트 셋) → R3 (테스트 계획 9 ~ 10절) → 3c.

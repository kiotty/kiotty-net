# kiotty_net_base 구현 계획 v2

2026-11-04. 기준은 `docs/design/design_base_v4.md` (2026-11-03 에 19절 메모리 할당과 그에 따른 1, 3, 9, 10, 12 ~ 16절을 고친 판) 와 `docs/test/test_base_v0.html` 이다. `plan_base_v1.md` 의 R1 ~ R3, 3c, 4 는 끝났다 (commit `09b7ab0` ~ `9f9f57f`). 사이클 4 에서 메모리 (Pool, StaticPool, Context 의 heap Pool, 정적 TLS Pool) 가 코드가 됐고, 그 뒤의 논의 (`design_base_v8_feedback.html`) 가 설계 v4 19절을 "멤버는 run 보다 오래 사는 것만, 풀 안의 객체는 바깥을 가리키지 않는다, OpenSSL 메모리는 TlsMemory 하나" 로 바꿨다. 그래서 이 문서도 v1 처럼 둘로 나눈다. **1부: 끝난 사이클에서 19절에 맞게 고칠 것. 2부: 앞으로 할 사이클.** 한 사이클의 정의, 로컬 두 환경, CI 매트릭스, commit 규칙, 완료 조건은 v0 0 ~ 1절 그대로다.

v1 과 달라진 것 중 코드에 닿는 것 (설계 v4 19.9 의 표를 층으로 다시 묶은 것):

| 바뀐 것 | 영향 받는 층 |
|---|---|
| handle 둘이 `int64` 에서 struct `{ owner, id }` 로. `KiottyNetConnectionOwner` 불투명 타입. `KiottyNet_serverMemorySize` / `clientMemorySize`. C++ `KiottyNet_Connection` 의 `operator==` / `operator<`. `initializeContext(void)` 는 그대로 | base.h, statemachine.h, C++ 래퍼, requirement (R5) |
| `Holder`, `ObjectPool` / `OwnerPool` 안, `g_contexts`, handle 의 context 비트, `MAX_OWNERS` / `MAX_TLS_CONTEXTS`, `ContextPoolLayout` / `TlsPoolLayout` 을 지운다. `core/pool/` 에 `is_reusable`, `ReusablePool`, `LazyReusablePool`, `MemberPool<Layout>`, `PoolBase`. `StageBase` + `Stage<N>`. `Parker`. `PoolHandle` 은 generation · slot 둘 | core, base/pool, 사이클 4 의 Context 코드 (R6) |
| `IoEvent` 가 backend 의 구체 타입이 되고 기본 생성자를 갖는다. `attach` 가 제자리에서 채운다 | event (R7) |
| `TlsEngine` 이 `Stage<N>` 둘을 멤버로. `open(TlsProfile&, hostname)`. free 함수 셋이 `TlsMemory` 클래스로 (전역 static, thread, Owner 서브풀, attach / detach 를 하는 `Scope`). OpenSSL 설정 셋 | tls (R8) |
| `NotificationContext` 삭제. `Sender` 는 static 함수 둘. `ConnectionStream` 은 참조 넷을 드는 값. `ConnectionConfiguration` 다섯 칸. `ConnectionHost<N>` 이 mutex 와 `ReusablePool<Connection, N>` 을 든다. `Handler { callbacks&, app_context, owner& }` | stream, connection (4b, 5a, 5b) |
| IoLoop 멤버 둘. `run(host, listener, acceptor&, profile&, handler, stop_when_empty)`. local completion 배열 없이 `stepLocalEvents` 반복. `isIoThread` | io_loop (6) |
| Context 는 static 객체 하나와 `Context::open`. 타입별 `LazyReusablePool` 셋. Owner 셋이 `is_reusable` 계약. `Parker`, EventClient 의 (열지 않는) Acceptor. `Owner` 의 가상 함수 일곱 | server, client, context, config (7) |
| base.cpp 는 `handle.owner->` 번역. static 에 닿는 줄은 `initializeContext` 하나 | base.cpp (8) |

사이클 4 노트의 "R4 (handle 함수 일곱이 Context 를 첫 인자로)" 는 설계 v8 피드백 11.12 의 결정 C (handle 이 owner 를 든다) 로 **없어졌다**. 아래의 R4 는 다른 것이다.

## 1부. 끝난 사이클에서 고칠 것

다섯을 다시 연다. 전부 로컬에서 끝나고 commit 하나씩이다. 순서는 의존 순서다 (문서 → 헤더 → core → event → tls). R6 이 끝나기 전에는 사이클 4 의 Context 테스트 셋과 `kiotty_net_tls_pool_tests` 가 깨진 채이므로 R6 ~ R8 은 사이에 push 하지 않고 R8 끝에 한 번 push 한다.

### R4. 문서 : 테스트 계획, requirement 를 19절로

| 바꿀 것 | `test_base_v0.html` 5절: `PoolHandle` 네 케이스를 generation · slot 둘 (`value` / `fromValue`, 0 은 무효, generation 은 slot 에 붙어 0 으로 돌아가지 않는다) 로. `Holder` 줄 삭제. Stage 케이스의 "Pool 에서 받는다" 를 `Stage<N>` 과 `limit(capacity)` / `clear()` 로 (`StageTakesItsBufferFromThePool...` 삭제, `StageLimitBelowCapacityMakesWritableTheLimit`, `StageClearResetsBothPositions` 추가). 5.1 의 `ContextPoolLayout` / `TlsPoolLayout` / `Context` 세 케이스 → `ReusablePool` (acquire 가 EMPTY 칸에 initialize, 실패면 reset 하고 code, release 는 reset + EMPTY, find 는 generation 불일치면 nullptr, `is_reusable` static_assert 가 복사 가능한 T 를 거부한다), `LazyReusablePool` (첫 acquire 가 new, release 는 RETIRED, 다음 acquire 가 같은 객체를 재사용, initialize 실패한 새 객체도 RETIRED 로 남는다, reserve(n), TABLE_FULL), `Parker` (park 가 ready 까지 기다린다, timeout 0 은 무한, unpark 의 change 가 ready 보다 먼저 보인다). 8절: "정적 TLS Pool" 을 `TlsMemory` 로 (installed, 전역 arena, 서브풀 Scope, lateFrees, 등록표). 10절: "여섯 프로세스" 표를 다섯 (drain, settle, collectIoEvents, step, stepLocalEvents) 으로, `collectLocalEvents` 의 케이스를 `stepLocalEvents` 로 옮기고 `WorkRequestedByATimeoutHandlerStartsInTheSameRunOnce` 를 더한다. 11절: handle 군을 `{ owner NULL, id 0, 유효, 지난 generation, 은퇴한 Owner 의 handle, 랜덤 id }` 로, "Connection handle 을 Stream 함수에" 는 compile 되지 않는다는 줄로. 설계 19.9 의 테스트 줄 (서브풀 바닥, 전역 꼬리표 격리, destroy 뒤 늦은 free 0, 은퇴 Owner 재사용, releaseContext 없이 종료, initializeContext 두 번) 을 11절에. 15절의 ① ~ ⑧ 표를 이 문서의 사이클로 |
|---|---|
| | `requirement_v0.md`: handle 이 struct 라 C 에서는 `owner` 와 `id` 둘을 비교하고 `owner == NULL` 이 무효라는 줄. `initializeContext` 가 `KIOTTY_OUT_OF_MEMORY` 를 돌려줄 수 있다는 줄 (있으면 그대로). `user_guide_http.md` 의 handle 비교 예가 있으면 같이 |
| 완료 | 5절, 8절, 9절, 10절, 11절의 케이스 수가 다시 세어져 있고 (9절 평문 96, 10절 35), 이 문서 3절의 표가 그 수를 가리킨다 |
| commit | `docs: test plan 5, 8, 10, 11 for design v4 section 19 (pools, TlsMemory, five processes, handle struct)` |

### R5. base.h 의 handle 을 struct 로 (사이클 1 재방문)

| 구현 | `include/kiotty_net_base.h`: `typedef struct KiottyNetConnectionOwner KiottyNetConnectionOwner;`, `KiottyNetConnectionHandle { owner, id }`, `KiottyNetConnectionStream { owner, id }` (같은 모양의 다른 struct). `KiottyNet_getEventClientConnection` 은 struct 값 (없으면 `{ NULL, 0 }`). `KiottyNet_serverMemorySize()` / `KiottyNet_clientMemorySize()` 선언 (구현은 사이클 8). `KiottyNet_initializeContext(void)` 는 그대로다 (11-04 확정. Context 설정은 라이브러리 안의 `ContextConfiguration` 기본값) C++: `KiottyNet_Connection` 에 `operator==` / `operator<` (owner 는 `std::less`, 그 다음 id), `KiottyNet_ConnectionStream` 은 struct 를 든다. `include/kiotty_net_statemachine.h`: WORK_STARTED event 의 `stream` 이 struct. http.h / websocket.h 는 바뀐 타입을 쓰면 같이 |
|---|---|
| 테스트 | `tests/unit/kiotty_test_headers_cpp.cpp`, `kiotty_test_headers_c.c` 의 struct 크기 / 필드 비교에 handle 둘. smoke 의 handle 초기화 `{ NULL, 0 }` |
| 완료 | 네 runner 컴파일 초록. 헤더만 따로 (clang `-std=c++11 -fsyntax-only`, `-std=c99`; MSVC `/Zs /std:c++14 /permissive-`, `/TC /std:c11`) 경고 0 |
| commit | `api: connection and stream handles carry their owner, owner memory size queries` |

### R6. core/pool, Stage, Parker, memory limits (사이클 2, 4 재방문)

> 11-05 정정: Stage 를 바꾸면 TlsEngine 이 바로 깨지므로 TlsEngine 의 `Stage<N>` 멤버와 `limit()` 을 R8 에서 R6 으로 당겼다. `kiotty_holder.h` 는 event listener 가 쓰므로 R7 에서 지운다. OpenSSL 의 정적 Pool 은 R8 까지 `ProcessTlsLayout` (전역 + Server 몫 × MAX_SERVERS + client 몫 × 나머지) 하나로 둔다. 그래서 R6 의 commit 도 매트릭스가 초록이다.

| 구현 | `core/pool/`: `kiotty_pool_handle.h` (`PoolHandle { generation, index }`, `value()` / `fromValue()`. type · owner · context 비트 없음), `kiotty_pool_layout.h` (이사만), `kiotty_member_pool.h` (`PoolBase` 가상 인터페이스 allocate / reallocate / release / holds 와 `MemberPool<Layout>`. arena 가 멤버. 지금의 `.cpp` 를 template 으로), `kiotty_global_pool.h` (이사만), `kiotty_reusable.h` (`is_reusable`, has_initialize / has_reset 검출), `kiotty_reusable_pool.h` (`ReusablePool<T, N>`: `std::array<T, N>`, EMPTY / ACTIVE, slot 에 붙은 generation, acquire / release / find / at / all / count), `kiotty_lazy_reusable_pool.h` (`LazyReusablePool<T, N>`: `std::array<std::unique_ptr<T>, N>`, EMPTY / ACTIVE / RETIRED, acquire / release / reserve / find / all). `core/kiotty_parker.h` (`park<Ready>(ready, timeout_ms)`, `unpark<Change>(change)`). `core/kiotty_stage.h`: `StageBase` (포인터 + capacity + limit, provider / consumer / compact / `limit(capacity)` / `clear()`) 와 `Stage<N>` (`std::array<char, N>` 위의 StageBase). `StageSet` 은 `Stage<MAX_RECV>`, `Stage<MAX_SEND>`, `Stage<MAX_PUSH>` 셋. `core/kiotty_holder.h` 삭제. `base/pool/kiotty_memory_limits.h`: `MAX_SERVERS` / `MAX_STREAM_CLIENTS` / `MAX_EVENT_CLIENTS` (`MAX_OWNERS`, `TLS_CONTEXTS` 삭제). `kiotty_memory_layout.h`: `GlobalTlsLayout`, `ServerTlsLayout`, `ClientTlsLayout`, `CIPHER_SEND_BYTES` (`ContextPoolLayout`, `TlsPoolLayout` 삭제). CMake cache 변수 같이. `base/context/kiotty_context.h/.cpp` 의 사이클 4 코드 (Pool 멤버, `useTlsStaticPool` 호출) 는 지운다. Context 는 사이클 7 에서 13절대로 새로 만든다. hook 설치는 R8 의 `TlsMemory::functions()` 로 간다 |
|---|---|
| 테스트 | R4 로 고친 5절: `PoolHandle` 넷, `Stage` (Pool 없는 `Stage<N>`, limit, clear), `StageSet`, `ReusablePool` 6, `LazyReusablePool` 6, `Parker` 3, `MemoryLimits`, 세 TlsLayout 의 등급이 16 B 배수이고 개수 > 0 (TLS OFF 면 ARENA_BYTES 0). `kiotty_test_holder.cpp`, `kiotty_test_context.cpp` 삭제 (Context 셋은 R8 의 TlsMemory 로). `support/kiotty_test_pool.h` 의 TestPool 은 `MemberPool<Layout>` 위에. `is_reusable` 이 복사 가능한 T 를 거부하는 것은 `static_assert` 가 아니라 `is_reusable<CopyableT>::value == false` 로 본다 |
| 완료 | MSVC Debug TLS ON / OFF, WSL gcc / clang TLS ON / OFF, clang ASan+UBSan, TSan 전부 초록 (tls_pool_tests 포함) |
| commit | `core: pool folder with ReusablePool and LazyReusablePool, StageBase and Stage<N>, Parker, PoolHandle without owner bits` |

### R7. event : IoEvent 구체 타입 (사이클 3b, 3c 재방문)

| 구현 | `kiotty_event_listener.h`: `IoEvent` 는 backend 가 정의하는 구체 struct (IOCP 는 OVERLAPPED + 방향 + listener 포인터 + slot, epoll 은 복제 fd + 상태 + ready 링크). 기본 생성자. `attach(IoEvent&, socket, slot)` 이 제자리에서 채우고 `detach(IoEvent&)` 가 비운다. `eventFor` 의 반환형이 `IoEvent&`. `Holder` 를 쓰던 자리는 전부 멤버 값. listener 의 완료 배열은 지금처럼 `open(capacity)` 의 heap (19.1 #1, Owner 의 create 때) |
|---|---|
| 테스트 | 7절 같은 파일. 모양만 바뀐다 (`IoEvent event;` 를 멤버 또는 지역으로). `DetachClearsTheEventSoItCanBeAttachedAgain` 하나 더 |
| 완료 | MSVC (IOCP 5 회 반복), WSL gcc / clang / ASan / TSan (epoll 10 회 반복) 초록 |
| commit | `event: IoEvent is a concrete backend type filled in place by attach` |

### R8. tls : TlsMemory (TlsEngine 의 Stage<N> 은 R6 에서 끝) (사이클 4 재방문)

> 11-05 철회: TlsMemory (hook, 전역 arena, Owner 서브풀, 예열, Scope) 를 만들고 보정까지 한 뒤 뺐다. OpenSSL 의 lazy 할당을 수명이 맞는 arena 로 보내는 규칙이 OpenSSL 내부 동작에 기대며 계속 늘었기 때문이다. 남긴 것: `num_tickets(0)`, `max_cert_list(16 KiB)`, 세는 hook 의 unit 두 테스트. 설계 v4 19.5, `docs/learning/openssl-memory-pitfalls.html`.

| 구현 | `kiotty_tls_engine.h`: `TlsEngine()` 기본 생성자, `Stage<MAX_RECV_CAPACITY>` cipherReceive 와 `Stage<CIPHER_SEND_BYTES>` cipherSend 를 멤버로 (TLS OFF 면 N 0). `open(TlsProfile&, const char* hostname)`, `decode(StageBase&)`, `encode(KiottyNetSpan)`. `kiotty_tls_memory.h`: `TlsMemoryFunctions`, `TlsMemory` (static: `functions`, `installed`, `detachThread`, `lateFrees`, `globalUsage`. private static: `thread()`, `attachThread`, `allocate` / `reallocate` / `release`, `arenaOf`, `GlobalArena : PoolBase` 하나, 등록표 `s_ranges[MAX_SERVERS + MAX_STREAM_CLIENTS + MAX_EVENT_CLIENTS]` 와 `atomic_flag`. 인스턴스: `Scope` (생성자가 attachThread → `_previous` 참조 → current 를 서브풀로), `failures()`, protected `TlsMemory(PoolBase&)` 와 `publish()`), `OwnerTlsMemory<Layout>`. backend: `installTlsMemory(functions)` (`CRYPTO_set_mem_functions`. 기본 malloc 이 아닌 hook 이 이미 있으면 덮지 않는다), `attachThread` 의 `ERR_clear_error` + `RAND_bytes`, `detachThread` 의 `OPENSSL_thread_stop`, `OPENSSL_INIT_NO_ATEXIT`, `SSL_CTX_set_num_tickets(0)`, `SSL_CTX_set_max_cert_list(16 KiB)`. `Context::open` 이 할 OpenSSL 초기화 + hook + 예열은 `TlsMemory::installGlobal()` 같은 static 하나로 두고 사이클 7 의 Context 가 부른다. 계측 도구 (`tests/measure`) 는 등급 표의 출력을 세 Layout 이름으로 |
|---|---|
| 테스트 | 8절 (TlsProfile 6, TlsEngine 23 + 3) 은 모양만 (`TlsPump` 가 `TlsEngine` 둘을 멤버로). `ProcessTlsLayout` 을 지우고 `kiotty_net_tls_pool_tests` 를 `TlsMemory` 로 다시: `InstalledIsFalseWhenAnotherAllocatorIsAlreadyThere` (unit 실행 파일의 세는 hook), `HandshakeInsideAScopeTakesBlocksFromThatOwnersSubpoolOnly`, `GlobalStateStaysInTheGlobalArenaAfterAttachThread` (전역 꼬리표 격리), `SubpoolExhaustionFailsOnlyThatHandshake` (서브풀 바닥), `ReleaseAfterPublishIsNotALateFree`, `DetachThreadReturnsCurrentToGlobal`. `TlsMemory(_pool)` 이 생성 전 멤버를 읽지 않는 것은 ASan / UBSan job 이 본다 |
| 완료 | TLS ON / OFF 매트릭스 초록. 여기서 R6 ~ R8 을 push |
| commit | `tls: TlsMemory routes OpenSSL into the global arena and owner subpools` |

## 2부. 앞으로 할 사이클

v1 의 번호를 이어 간다. 4b 가 다음이다.

### 4b. stream : ConnectionStream

| 구현 | `stream/kiotty_connection_stream.h/.cpp`: v4 9.7. `ConnectionStream(ActiveSocket&, TlsEngine&, StageBase& receive, StageBase& send)` 가 참조 넷을 묶는 값 (복사 생성만, 대입 없음). `read / write / writeFile / connected / handshake(timeout_ms)`, private `flush` (cipherSend 잔여를 먼저 sendAll). 모든 동기 함수의 첫 걸음이 flush. 평문 write 는 사용자 버퍼에서 바로 sendAll (복사 0). TLS write 는 send 의 capacity 씩 encode → cipherSend → sendAll. writeFile 은 파일을 send capacity 씩 읽어 write 와 같은 길. `backend/kiotty_posix_file.cpp`, `kiotty_windows_file.cpp` 는 열기 / 읽기 / 닫기만. timeout 은 non-blocking + poll (v2 15-5). 멤버로 두는 곳이 없다 |
|---|---|
| 테스트 | integration (loopback). `ReadReturnsStagedPlaintextBeforeTouchingSocket`, `ReadBlocksUntilPeerSendsThenReturnsUpToLength`, `ReadTimesOut`, `WritePlaintextSendsAllWithoutCopy`, `WriteTlsEncodesInSendCapacityChunks` (TLS 더블), `WriteFileSendsExactRange`, `WriteFileMissingPathIsInvalidArgument`, `FlushSendsLeftoverCipherSendFirst`, `ConnectedIsFalseAfterPeerClose`, `HandshakeCompletesAgainstBlockingPeer` (TLS). TLS 케이스는 테스트마다 engine 을 close 한다 |
| 완료 | Windows / Linux integration 초록. TLS ON job 에서 TLS 케이스 포함 |
| commit | `stream: ConnectionStream synchronous channel shared by Work and StreamClient` |

### 5. connection

둘로 자른다. 소켓이 없어 로컬에서 전부 끝난다.

#### 5a. Sender, WorkContext, IoSource, Connection 평문, ConnectionHost

| 구현 | `connection/kiotty_sender.h` (static `stage(StageBase&, span)` / `release(StageBase&, length)` 둘. release 는 늘 compact), `kiotty_work_context.h` (IDLE / REQUESTED / WORKING / DONE, request / abandon / start / requested / finished / onFinished / working / reset / verify / finish), `kiotty_io_source.h` (eventFor 가 방향으로, resolve, attach / detach, shutdown), `kiotty_connection.h/.cpp` (9.2 의 멤버 11 개, 9.4 의 인터페이스: `initialize(id, ConnectionConfiguration&&)` / `reset() noexcept` / `onOpened(handler)` / `dispatch(completion, handler, mutex&)` / `pickWrite(mutex&)` / `expireDeadline` / `workReady` / `workFinished` / `takeWorkOutcome` / `stageNotification` / `stream(generation)` / `closeStream(generation, then)`, 9.5 의 on* / adopt / drive / refill / extend / apply, 9.6 의 select / encoded / resolve, `handle(handler)` / `streamHandle(handler)`. TLS 분기 `_tls.active()` 는 자리만 두고 5b 에서 채운다), `kiotty_connection_host.h` (`ConnectionHost<N>`: `initialize(limits)`, `acquire(configuration&&)`, `release(c)` 가 lock 안에서 `c.reset()`, `at` / `capacity` / `full` / `empty` / `notificationMutex`, 아무 thread 의 `notify(PoolHandle, span)` / `broadcast` / `info`, worker 의 `stream(PoolHandle)` / `closeStream`). `Handler` 와 `ConnectionConfiguration`, `ConnectionLimits` 는 `kiotty_connection.h` 에. `NotificationContext` 는 없다 |
|---|---|
| 테스트 | R4 에서 다시 센 9절의 평문 케이스 96 개 (ConnectionHost 셋 포함). 특히: `drive` 가 recv capacity 번 안에 끝난다, `refill` 이 compact 뒤 writable 을 돌려준다, Stage 가 가득 찬 채 조건이 안 차면 REQUEST_TOO_LARGE, pickWrite 의 순서 여섯, notify 송신 중 thenWrite 는 stage 와 deadline 만, SEND(NOTIFY) 가 notify Stage 만 release 하고 SEND(USER) 가 send Stage 만 release, onTimeout 의 다섯 행, apply(START_WORK) 는 REQUESTED 만, workReady 는 두 event 가 비었을 때만, apply(CLOSE) 가 REQUESTED 를 거둔다, closeStream 의 verify 셋 (BUSY / PEER_CLOSED / SUCCESS), CLOSING 중 closeStream 은 CLOSED 를 돌려주되 DONE 으로 간다. 19절로 더한 것 (ConnectionHost): `ReleaseResetsInsideTheNotificationLock` (release 와 다른 thread 의 notify 가 겹쳐도 stage 가 reset 뒤에 남지 않는다. TSan), `NotifyWithAPastGenerationReturnsPeerClosed`, `AcquireFailureResetsTheConnectionAndLeavesTheSlotEmpty`. "Connection 은 바깥을 가리키지 않는다" 는 테스트 더블의 host / handler 가 테스트 함수의 지역 변수라는 것으로 본다. support: `RecordingHandler`, `ScriptedHandler`, `completion::*`, 가짜 listener (pending 을 세우고 내리는 더블) |
| 확인받을 계약 | 테스트 v0 14절 1 ~ 2 (READ_UNTIL 이 delimiter 포함, READ 0 / WRITE 0 / delimiter 0 또는 5 는 onError(INVALID_ARGUMENT) + CLOSE). 이 사이클 시작 전에 확정 |
| commit | `connection: Sender, WorkContext, Connection with drive/refill/pickWrite, ConnectionHost<N> (plaintext unit tests)` |

#### 5b. Connection TLS 경로, Acceptor

| 구현 | `_tls.active()` 분기 네 자리 (onOpened 의 deadline + onHandshake, refill 의 decode, select 의 cipherSend 우선과 encoded, onSent 의 cipherSend consume), `receiveStage(HANDSHAKE / TLS USER)`, handshake DONE 뒤의 decode 한 번. `connection/kiotty_acceptor.h/.cpp` (v2 9.8 + `listening()`. open 하지 않은 Acceptor 는 거짓) + IOCP / epoll 쪽 resolve (AcceptEx 버퍼 / 빈 payload) |
|---|---|
| 테스트 | 9절의 TLS 케이스 23 개 (TlsPump 로 DONE 까지 간 engine 을 심는다): handshake 의 SEND → NONE 과 drain 의 WRITE(HANDSHAKE), TLS 1.3 에 ticket 이 없다 (`SSL_CTX_set_num_tickets(0)`, R8. DONE 이 클라이언트 Finished 바로 뒤), `as.write.length` 는 평문 / `completion.length` 는 암호문, encode 순서 = 송신 순서, KeyUpdate 답장이 cipherSend 에 남아 다음 drain 이 CONTROL 로 보낸다, decode 실패는 onError + CLOSE(RECORD_ERROR). 9절의 Acceptor 3 (integration) + `UnopenedAcceptorIsNotListening` |
| 완료 | unit 전부 매트릭스 초록 (TLS ON job 에서 TLS 케이스 포함). Acceptor integration 은 Windows / Linux |
| commit | `connection: TLS path in Connection, Acceptor as IoSource with listening()` |

### 6. io_loop

| 구현 | `io_loop/kiotty_io_loop.h/.cpp`: v4 10절 그대로. 멤버 `_stopped`, `_io_thread` 둘. `run(host, listener, acceptor&, profile&, handler, stop_when_empty)` / `stop` / `isIoThread` / `admit(host, listener, profile&, socket&&, hostname, handler)`, private `runOnce / drain / settle / collectIoEvents / step / stepLocalEvents / shutdown / access / submit / waitTimeout`. local completion 배열 없음: `stepLocalEvents` 가 slot 마다 TIMEOUT / WORK_READY / WORK_DONE 을 만들어 바로 step 하고, runOnce 가 false 가 될 때까지 반복한다 (상한 slot 당 셋). `submit` 의 WRITE case 가 listener 를 부르고 `drain` 은 `submit(c, c.pickWrite(host.notificationMutex()), listener)` 만. `acceptor.listening()` 과 `profile.valid()` 가 v1 의 nullptr 분기를 대신한다. IoLoop 는 TLS 메모리를 모른다 |
|---|---|
| 테스트 | R4 로 고친 10절의 35 개 (integration). IoLoop 를 Server 없이 직접 조립 (ConnectionHost<N> + EventListener + Acceptor + TlsProfile + IoLoop. 전부 테스트 함수의 지역 변수), run 은 테스트 thread 에서. 네 성질 + 반복의 성질: `ClientHelloIsArmedBeforeFirstWait` (열지 않은 Acceptor 로 admit 뒤 run), `TimeoutIsNotDeliveredAfterCloseInTheSameBatch`, `WorkStartedFiresInTheSameRunOnceWhenWriteLaneIsIdle`, `WorkStartedWaitsForInFlightNotifyThenFires`, `WorkRequestedByATimeoutHandlerStartsInTheSameRunOnce` (onTimeout → thenWork 가 다음 OS 완료 없이 onWorkStarted. 19.9), `NotifyDuringUserWriteArrivesAsTwoWholeMessagesInOrder`, `OwnerCloseDuringWorkShutsDownSocketAndReleasesAfterCloseStream`, `IsIoThreadIsTrueOnlyOnTheRunningThread`. 벤치마크 `EchoRoundtrip1KiB`, `IdleCpu`, `NotifyLatencyFromOtherThread`, `NotifyFromHandlerLatency` 를 IoLoop 조립으로 먼저 |
| CI | Windows / Linux integration. TSan 이 notify 의 lock 네 자리 (host.notify 의 stage, pickWrite, onSent 의 release, host.release 의 reset) 와 closeStream 의 CAS, `_io_thread` 의 atomic 을 처음 본다. 플래키 방지는 v0 그대로 (`waitUntil`, 포트 0, 상한 넉넉히) |
| 완료 | integration 매트릭스 초록, TSan 경고 0. 벤치마크 넷의 첫 수치를 테스트 v0 12절에 |
| commit | `io_loop: drain/settle/collectIoEvents/step/stepLocalEvents cycle with loopback tests` |

### 7. server, client, context, config

| 구현 | `context/kiotty_owner.h` (`KiottyNetConnectionOwner` 가상 함수 일곱. `kiotty::net::Owner` typedef), `kiotty_context.h/.cpp` (`KiottyNetContextImpl` = Context. `static Result<code, Context&> open(ContextConfiguration)` 하나 (base.cpp 가 기본값으로) (함수 안의 static 객체. 두 번째 호출은 같은 객체와 SUCCESS), `release` (남은 Owner destroy → 세 풀 비움. 등록표 칸은 Owner 의 소멸자가 비운다 (R8). OPENSSL_cleanup 없음), `createServer / createStreamClient / createEventClient` (`LazyReusablePool::acquire`. 못 만들면 `KIOTTY_OUT_OF_MEMORY`, 칸이 없으면 TABLE_FULL), `destroy` 셋 (release → RETIRED), `_mutex`, `_initialized`. open 이 OpenSSL 초기화 (`OPENSSL_INIT_NO_ATEXIT`) + config 의 `reserve(n)` + WSAStartup), `config/kiotty_config.h/.cpp` (initialize* / configure*, `configureServerMaxClients` 의 `[1, MAX_CONNECTIONS_PER_SERVER]`, capacity 의 `[1, MAX_*_CAPACITY]`, 기본 128, isInitialized 오버로드, mark), `config/kiotty_action.h` (then* 여섯 + accept / ignore). `server/kiotty_server.h/.cpp`, `client/kiotty_event_client.h/.cpp`, `client/kiotty_stream_client.h/.cpp`: 19.3 의 멤버 표 그대로. 셋 다 `is_reusable` 계약 (`configuration_type`, `initialize(id, configuration)` 가 TlsProfile 을 열고 host.initialize(limits), listener.open, acceptor.open (Server 만), `reset() noexcept` 가 `SSL_CTX_free`). `run` 은 `Handler handler(callbacks, app_context, *this)` → `_loop.run(...)` → `_parker.unpark`. EventClient 는 `_loop.admit(...)` 뒤 `_loop.run(..., _acceptor (열지 않음), ..., true)`. StreamClient 는 함수마다 그 자리의 `ConnectionStream stream(_socket, _tls, _receive, _send)`. `await` 는 `_parker.park`. Owner 의 일곱: Server / EventClient 는 `_host.*` 뒤 `_listener.wake()`, StreamClient 는 전부 INVALID_ARGUMENT. `Owner::shutdown` 은 WORKING 연결의 closeStream 을 기다린다 |
|---|---|
| 테스트 | 테스트 v0 11절 중 Config (max_clients 0 / 상한 + 1 거부, 상한 허용, 기본 128. capacity 0 / 상한 + 1 거부), Action (then* 여섯). Context, Server, EventClient, StreamClient 를 내부 클래스로 먼저 (C API 는 8). 19.9 의 여섯: `OwnerSubpoolExhaustionFailsOnlyThatOwnersHandshake`, `GlobalTaggedAllocationsNeverLandInAnOwnerSubpool` (계측 hook), `LateFreesAreZeroAfterDestroy`, `RetiredOwnerIsReusedByTheNextCreateOfTheSameType` (같은 객체 주소, generation 다름), `ProcessExitWithoutReleaseContextDoesNotCrash` (별도 실행 파일 또는 death test), `InitializeContextTwiceReturnsTheSameContextAndSuccess` + `ReleaseOnceEndsBoth`. 더하는 것: `ServerCloseWaitsForWorkerCloseStream`, `StreamClientSharesConnectionStreamCode`, `CreateReturnsOutOfMemoryWhenNewFails` (nothrow new 를 실패시키는 더블은 어렵다. `MAX_SERVERS` 를 채운 뒤의 TABLE_FULL 로 대신하고 OUT_OF_MEMORY 는 코드 검토로), `DetachThreadAfterRunLeavesNoThreadStateInTheOwnerSubpool`. static Context 하나를 한 process 의 모든 테스트가 공유하므로 fixture 가 `release` 를 보장한다 |
| 완료 | 매트릭스 초록 (TLS ON / OFF). `OnDisconnectedIsCalledExactlyOncePerAcceptedConnectionOverAThousandConnections` 가 ASan 에서 누수 0, `lateFrees() == 0` |
| commit | `owner: Server, EventClient, StreamClient as reusable owners, static Context with open/release, config bounds` |

### 8. base.cpp 번역과 C API 테스트

| 구현 | `src/kiotty_net_base.cpp`: v4 15절 표 그대로. static 에 닿는 줄은 `initializeContext` 의 `Context::open(ContextConfiguration())` 하나. 나머지는 받은 포인터의 메서드 하나: `ctx->`, `server->`, `handle.owner->notify(handle.id, ...)`. NULL 검사 (context, owner, handler, `handle.owner`) 만 있고 로직 0 줄. Stream 함수 다섯은 `s.owner->readStream(s.id, ...)` 이고 BUSY / PEER_CLOSED 는 Owner 안에서 |
|---|---|
| 테스트 | 11절을 `include/kiotty_net_base.h` 만 포함하는 `tests/integration/kiotty_test_base_api.cpp` 로. handle 군 TEST_P { `{ NULL, 0 }`, `{ owner, 0 }`, 유효, 지난 generation, 은퇴한 Owner 의 handle (PEER_CLOSED 또는 INVALID_ARGUMENT. 설계 13절), 랜덤 id, Stream handle 을 notify 에 (compile 되지 않는다는 것은 헤더 테스트의 주석으로) }. `ReadConnectionStreamFromIoThreadIsBusy`, `ConnectionHandlesCompareByOwnerThenId` (C++ 래퍼의 map). 벤치마크 12절 나머지 (EchoRoundtrip128Clients 로 수치 갱신, ReadHeavy*, WriteHeavy*, BroadcastFanOut, ConnectionChurn, TlsEchoOverhead, TlsHandshakeRate, WorkHandoff, `StreamWriteFile64MiB`) |
| CI | `benchmark` job (`workflow_dispatch` 와 main push). 결과 JSON 을 artifact 로 |
| 완료 | 매트릭스 초록. 벤치마크 수치를 테스트 v0 12절에 기준치로. `docs/progress/` 에 "base 완료" 와 kqueue / io_uring / HTTP 로 가는 다음 할 일 |
| commit | `base: kiotty_net_base.cpp translation layer, C API tests and benchmarks` |

## 3. 사이클 요약

| 사이클 | 설계 v4 | 테스트 v0 | 층 | 로컬에서 끝나나 | 상태 |
|---|---|---|---|---|---|
| 0 ~ 3b, R1 ~ R3, 3c, 4 | — | — | build, api, core, socket, event (IOCP, epoll), tls, 사이클 4 의 메모리 | — | 끝 |
| R4 | 19.9 | 5, 8, 10, 11, 15절 다시 쓰기 | 문서 | 예 | 1부 |
| R5 | 3, 13절 | 15절 ① | base.h | 예 | 1부 |
| R6 | 4, 19.2 ~ 19.4 | 5절 | core, base/pool | 예 | 1부 |
| R7 | 7절 | 7절 같은 파일 | event | 예 (Windows + WSL) | 1부 |
| R8 | 8, 19.5 | 8절 | tls | 예 | 1부. 여기서 push |
| 4b | 9.7 ConnectionStream | 새 10 개 | stream | 예 | 2부 |
| 5a | 9.1 ~ 9.8 평문 | 9절 평문 96 | connection | 예 | 2부 |
| 5b | 9절 TLS, 9.9 Acceptor | 9절 TLS 23, Acceptor 3 + 1 | connection | 예 | 2부 |
| 6 | 10 ~ 11절 | 10절 35, 벤치 4 | io_loop | 예 | 2부 |
| 7 | 12 ~ 14절, 19.3, 19.5 | 11절 (내부 클래스) + 19.9 의 여섯 | owner, context, config | 예 | 2부 |
| 8 | 15절 | 11절 (C API), 벤치 나머지 | base.cpp | 예 | 2부 |

## 4. 리스크와 대응

v0 12절의 일곱 가지와 v1 4절의 다섯 가지는 그대로다. 19절로 더해진 것:

| 리스크 | 대응 |
|---|---|
| `stepLocalEvents` 의 반복이 끝나지 않는 길이 있으면 IO Thread 가 바쁜 loop 가 된다 | 끝나는 근거는 "한 바퀴 안에서 slot 의 상태는 한 방향으로만 간다" (10절). debug 는 slot 당 셋을 `assert`, 6 의 `StepLocalEventsTerminatesWithinThreeStepsPerSlot` 이 상한을 본다 |
| static Context 하나를 한 process 의 모든 테스트가 공유한다. 한 테스트가 release 를 빠뜨리면 다음 테스트가 그 Owner 를 본다 | 7 과 8 의 fixture 가 TearDown 에서 `release`. `ProcessExitWithoutReleaseContext` 만 별도 실행 파일 |
| handle 이 struct 가 되어 C++ 래퍼, statemachine, 예제, 헤더 테스트가 한 번에 바뀐다. 빠뜨린 자리는 컴파일 오류로 나오지만 `{ NULL, 0 }` 초기화를 빠뜨린 자리는 쓰레기 owner 다 | C++ 래퍼 (`KiottyNet_Connection`, `KiottyNet_ConnectionStream`) 는 기본 생성자가 없어 handle 없이 만들 수 없다. C 는 `{ NULL, 0 }` 로 초기화하라고 requirement 규약 8 에 적었다. 매크로는 두지 않는다 (R5) |
| `LazyReusablePool` 의 `new (std::nothrow)` 가 실패하는 경로는 테스트 더블로 만들기 어렵다 | `OUT_OF_MEMORY` 는 코드 검토로, 풀이 찬 경로 (`TABLE_FULL`) 만 테스트로 (7) |
| Windows 는 commit 기준 도구에 Server 하나가 약 30 MB 로 보인다 (19.8) | 기본 상한을 바꾸지 않는다. `KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER` 로 줄이는 방법을 requirement 에 적는다 (R4) |

## 5. 지금 할 일

R4, R5 (11-04), R6, R7, R8, 4b (11-05), 5a, 리팩터링 (`plan_refactor_v0.md`), 5b (11-07), 6, 7, 7b, 8 (11-09, `docs/progress/261109_progress_base_8.md`) 끝. R8 의 TlsMemory 는 만들었다가 뺐다 (OpenSSL 은 자기 malloc). 6 부터는 설계 v5 의 이름과 폴더로 읽는다 (IoLoop 은 `domain/usecases/io_loop/`, 인자는 `LoopParts`). base 뒤의 첫 backend 는 kqueue (11-09, `docs/progress/261109_progress_base_kqueue.md`). io_uring 은 11-09 (`docs/progress/261109_progress_base_io_uring.md`). StateMachine 층은 `docs/design/design_statemachine_v0.md` (11-09) 의 M1 ~ M5 (11-09 ~ 11-10, `docs/progress/261109_progress_statemachine_m1 ~ m4.md`, `261110_progress_statemachine_m5.md`) 로 끝났고 DV6 / DV12 를 닫았다 (DV13 은 유지). 남은 것은 HTTP 층. MCP 의 Streamable HTTP (SSE 응답, 긴 GET 스트림) 가 되도록 `include/kiotty_net_http.h` 와 `kiotty_net_websocket.h` 를 공개 인터페이스부터 다시 설계한다 (`docs/design/design_http_v0.html`, `design_websocket_v0.html`).

2026-11-10 HTTP 설계 v0 를 고쳤다 (`c0b8933`): 스트림은 Work 스트림 하나, status 와 content type 은 enum, 송신은 한 WRITE 에 send Stage 다음 notify Stage (gather), 송신 timeout 과 `KIOTTY_NET_CODE_PEER_NOT_READING`. 코드로는 base 사이클 B1 이 먼저이고 H1 ~ H6, W1 ~ W5 가 뒤다. design_http_v0.html 10절의 정할 것에 답을 받으면 B1 을 시작한다.

2026-11-11 C ABI 마샬링 수칙 5.7 (`development_rule.md`) 을 더하고 공개 헤더에 적용했다 (`91279fd`): `KiottyNetBool` (`int32_t`) 이 C `bool` 을 대신하고, `KiottyNetConnectionAction` / `KiottyNetConnectionEvent` 의 union `as` 를 순차 필드로 풀었다. H1, W1 의 새 struct 도 5.7 을 따른다.

2026-11-11 base 사이클 B1 (`docs/progress/261111_progress_base_b1.md`): 한 WRITE 에 send Stage 다음 notify Stage (IoPayload 조각 둘, backend 넷의 gather 송신, TLS 의 encode 순서), notify / broadcast 의 여러 조각 복사, `send_timeout_milliseconds` (기본 30000) 와 `KIOTTY_NET_CODE_PEER_NOT_READING`. requirement 4절의 송신 순서를 코드에 맞췄다. 다음은 H1.

2026-11-11 base 사이클 B2 (`docs/progress/261111_progress_base_b2.md`): 주소와 TLS 설정을 create 가 아니라 run / connect 가 받는다 (서버도). 설정 struct 의 `address` 제거, `initializeServer / ClientConfiguration`, `initializeTcp / PipeAddress`, TLS Stage 는 늘 최대치, SSL_CTX 는 run / connect 마다. DICOM 라이브러리의 Association 이 미리 만든 endpoint 위에서 다른 AE 와 TLS 설정으로 열리게 하기 위해서다 (`design_http_v0.html` 결정 18). 다음은 H1.

2026-11-12 HTTP 사이클 H1 (`docs/progress/261112_progress_http_h1.md`): `kiotty_net_http.h` 공개 API 판 v2. status / content type enum, `setHttpCustomContentType`, body setter 의 type, `setHttpBodyStream`, `writeHttpWorkChunk`, `KiottyNetHttpServerResult`, `KiottyNetHttpServerHandler`, limits 의 WebSocket 타이머 셋, `keep_alive` 를 `KiottyNetBool` 로. `user_guide_http.md` 를 v2 로. 구현은 H2 부터. 다음은 H2.

2026-11-12 HTTP 사이클 H2 (`docs/progress/261112_progress_http_h2.md`): `domain/usecases/http/` 의 파서. request line, header line, Content-Length, chunk 크기, body 경계 (Transfer-Encoding 과 Content-Length 의 모호함은 400), keep-alive, 라우트 경로와 매칭, content type 표와 확장자, CUSTOM 이름. 요청 파서는 실패가 곧 자동 응답의 status 다. unit 69 개. 다음은 H3.

2026-11-12 HTTP 사이클 H3 (`docs/progress/261112_progress_http_h3.md`): HttpServer 가 돈다. `HttpServer : Server`, Context 의 풀, HttpSite (라우트 표, 끊김 / 오류 action, Session 풀), 요청 / 응답 저장소, HTTP 표 (StateMachine rule 열하나), 자동 응답 (404 / 405 / OPTIONS / HEAD / 400 / 413 / 414 / 431 / 501), keep-alive, chunked 요청, Date 헤더. integration 21 개. 다음은 H4.

2026-11-12 HTTP 사이클 H4 (`docs/progress/261112_progress_http_h4.md`): Work 와 Work 스트림. `startHttpWork`, `onWorkStarted`, `setHttpBodyStream`, `writeHttpWorkChunk` (chunked, 응답 버퍼에서 조립하거나 나눠 쓰기), `finishHttpWork*`, `isHttpWorkConnected`. HEAD 스트림 라우트는 Work 없이 헤더만 (결정 16), Work 없는 스트림 body 는 500. 서버 close 중 Work 의 Session 반납을 worker 의 finish 까지 미룬다. integration 9 개. 다음은 H5.

2026-11-12 HTTP 사이클 H5 (`docs/progress/261112_progress_http_h5.md`): 정적 서빙. `serveHttpDirectory / File / Memory`, `setHttpFile`, 확장자로 content type, `..` / `\` / `:` 는 403, 없는 파일 404, 파일은 Session 의 `InputFile` 에서 `send_capacity` 조각으로. integration 7 개, unit 2 개. 다음은 H6.

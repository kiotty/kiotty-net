# kiotty_net_base.h 구현 설계 v4

2026-10-31. 2026-11-03 에 19절 (메모리 할당) 과 그에 따라 1, 3, 9, 10, 12 ~ 16절을 `design_base_v8_feedback.html` 의 결론으로 고쳤다. `design_base_v2.md` 위에 v6 / v7 피드백과 그 뒤의 논의 (송신 맥락의 분리, write lane 하나, Stream) 를 반영한 구현용 요약이다. 역사와 근거는 `design_base_v6_feedback.html`, `design_base_v7_feedback.html`, `design_base_v8_feedback.html` 에 있다. 이 문서는 클래스의 인터페이스와 한 바퀴의 순서만 적는다. 두 문서가 다르면 requirement 가, 흐름이 다르면 implementation_guide 가 우선한다. 사이클 0 ~ 3b 에서 이미 코드가 된 부분 (core, error, socket, event, IOCP) 은 바뀐 줄만 적고 나머지는 v2 를 가리킨다.

설계 방침 (이 문서가 지키는 세 가지)

1. **맥락은 맥락끼리.** 받는 맥락 (receive), 보내는 맥락 셋 (user send, notify, TLS 의 cipher 출력), Work 맥락, 수명 맥락 (phase) 은 각자 자기 Stage 와 자기 완료 처리를 가진다. 한 맥락의 코드가 다른 맥락의 변수를 읽거나 바꾸지 않는다. 맥락 사이를 잇는 것은 IoAction 과 IoCompletion 두 값 타입뿐이다.
2. **엣지 케이스는 프로세스가 삼킨다.** "W 가 1 이면 ...", "둘이 동시에 끝나면 ..." 같은 분기를 두지 않는다. 그런 상황이 생기지 않도록 프로세스를 놓는다. socket 당 WRITE 는 하나이므로 "동시에 끝남" 이 없고, 송신은 Stage 에 넣는 것과 Stage 에서 꺼내 거는 것이 다른 프로세스이므로 넣는 쪽이 "지금 걸 수 있나" 를 묻지 않는다. 늦은 완료와 닫힌 뒤의 local completion 은 dispatch 의 첫 줄 하나 (phase 라우팅) 가 전부 받는다.
3. **프로세스는 input → output 이다.** 한 바퀴는 다섯 프로세스 (drain, settle, collectIoEvents, step, stepLocalEvents) 이고 각 프로세스는 더 작은 프로세스 (dispatch → apply → submit, pick → submit) 로 쪼개진다. 각 단계의 input 과 output 은 값이고, side effect 는 그 단계가 소유한 Stage / event / 상태에만 생긴다. 다른 thread 가 쓰는 것은 세 자리 (notify Stage, WorkContext 의 CAS, listener.wake) 뿐이고 전부 "적고 깨운다" 이다.

원칙 (v2 그대로)

1. `src/kiotty_net_base.cpp` 는 번역만 한다. 검사, 분기, 상태 변경, 복사를 두지 않는다 (15절).
2. 클래스는 최소 함수와 최소 변수만 가진다. 상태를 읽어 판단하는 줄은 `pending()` 넷 (pickWrite 의 write event, onTimeout 의 userActionPending, stepLocalEvents 의 workReady, settle) 밖에 두지 않는다.
3. 의존은 아래로만 간다. OS 가 다른 부분은 그 폴더의 `backend/` 안에만, OpenSSL 은 `tls/` 안에만 있다. listener 는 바이트와 방향만 안다.
4. 한 줄에 문장 하나. 중첩 호출은 지역 변수로 푼다. cancel 은 없다. 요청은 completion 으로만 비워진다.

```
kiotty_net_base.cpp ── 번역. 받은 것 (Context, Owner 포인터, handle 의 owner) 을 통해서만 간다. static 에 닿는 함수는 initializeContext 하나
    ├── Context ───── static 객체 하나 (13절). 타입별 LazyReusablePool 셋 (19.3)
    ├── Server ──────┐
    ├── EventClient ─┼── Owner (= KiottyNetConnectionOwner) ── ConnectionHost + EventListener + IoLoop + Acceptor + TlsProfile + OwnerTlsMemory + Parker
    └── StreamClient ┘               IoLoop ── 멤버 둘. run(host, listener, acceptor, profile, handler) : drain (pick → submit) → settle → collectIoEvents → step (dispatch → apply → submit) → stepLocalEvents (반복)
                                     Connection ── StageSet (receive, send, notify), TlsEngine, WorkContext, IoEvent 둘 (read, write). 자기 바깥을 가리키지 않는다
                                     Acceptor ──── PassiveSocket, IoEvent 하나
                                     EventListener + IoEvent ── IOCP (있음) / epoll / kqueue / io_uring
                                     ActiveSocket / PassiveSocket (있음), TlsProfile / TlsEngine
core/       : StageBase / Stage<N>, StageSet, Span, Result, Clock, Parker
core/pool/  : PoolHandle, PoolLayout, Pool<Layout>, StaticPool, is_reusable, ReusablePool, LazyReusablePool (19절. Holder 는 지운다)
base/pool/  : MemoryLimits, GlobalTlsLayout / ServerTlsLayout / ClientTlsLayout (19절)
```

## 1. 폴더

```
src/
    kiotty_net_base.cpp
    implementation/
        core/                       namespace kiotty                        (있음. 무엇에도 의존하지 않는다)
                    kiotty_stage.h (StageBase, Stage<N>, StageSet), kiotty_span.h, kiotty_result.h, kiotty_clock.h,
                    kiotty_parker.h (12절. mutex + condition_variable 이 있는 유일한 자리)
            pool/   kiotty_pool_handle.h (PoolHandle { generation, index }), kiotty_pool_layout.h (PoolClass, BlockClass, PoolLayout),
                    kiotty_pool.h (Pool<Layout>. Layout 이 정한 멤버 배열 위의 등급 표. TLS 서브풀 전용), kiotty_static_pool.h (전역 TLS arena),
                    kiotty_reusable.h (is_reusable), kiotty_reusable_pool.h (ReusablePool<T, N> : 멤버 배열), kiotty_lazy_reusable_pool.h (LazyReusablePool<T, N> : unique_ptr, 은퇴)
        base/                       namespace kiotty::net
            pool/       kiotty_memory_limits.h (compile time 상한), kiotty_memory_layout.h (TLS Layout 셋). kiotty-net 의 크기를 아는 것만 여기 (19절)
            error/      (있음)
            socket/     (있음)
            event/      kiotty_io_action.h (as.write.length 추가), kiotty_event.h (IoKind::WORK_READY 추가), kiotty_event_listener.h (그대로),
                        backend/kiotty_iocp_event_listener.cpp (있음), kiotty_epoll_event_listener.cpp, kiotty_kqueue_*, kiotty_io_uring_*
            tls/        kiotty_tls_profile.h, kiotty_tls_engine.h, kiotty_tls_memory.h (TlsMemory, OwnerTlsMemory<Layout>, 19.5), backend/kiotty_openssl_tls.cpp | kiotty_stub_tls.cpp
            stream/     kiotty_connection_stream.h/.cpp                 동기 통로. 참조 넷을 드는 값. Connection (WORKING) 과 StreamClient 가 쓰는 자리에서 만든다
                        backend/kiotty_posix_file.cpp, kiotty_windows_file.cpp   writeFile 의 파일 열기 / 읽기
            connection/ kiotty_io_source.h, kiotty_sender.h (static 함수 둘), kiotty_work_context.h,
                        kiotty_connection.h/.cpp, kiotty_acceptor.h/.cpp, kiotty_connection_host.h/.cpp
            io_loop/    kiotty_io_loop.h/.cpp
            server/     kiotty_server.h/.cpp
            client/     kiotty_event_client.h/.cpp, kiotty_stream_client.h/.cpp
            context/    kiotty_context.h/.cpp (static 객체 하나와 타입별 LazyReusablePool, 13절 / 19.3), kiotty_owner.h (KiottyNetConnectionOwner 의 정의)
            config/     kiotty_config.h/.cpp, kiotty_action.h
```

- backend 는 전부 `.cpp`. OS struct 는 `.cpp` 안의 struct 에 두고 그 OS 의 `IoEvent` 구체 타입을 그대로 내보낸다 (19.3). `Holder` 는 없다.
- `KIOTTY_NET_USE_TLS` 가 보이는 자리는 tls/backend 의 파일 선택 하나.

## 2. 규약

v2 2절 그대로. 더하는 것:

- backend 선택은 빌드 시스템이 아니라 소스가 한다. CMake 는 모든 `.cpp` 를 나열하고, backend 파일은 전체를 운영체제 매크로로 감싼다 (`_WIN32` → windows / iocp, `!_WIN32` → posix, `__linux__` → epoll, 뒤에 `__APPLE__` → kqueue). backend 가 있는지는 `kiotty_event_listener.h` 의 `KIOTTY_NET_HAS_EVENT_BACKEND` 로 본다.

- Connection 안에서 Stage 를 직접 만지는 클래스는 그 Stage 의 소유자 하나다. receive Stage 는 Connection 의 수신 코드 (drive / refill) 와 ConnectionStream (WORKING) 만, send Stage 는 `Sender::stage / release(_plain.send, …)` 만, notify Stage 는 host 의 lock 안의 `Sender::stage / release(_plain.notify, …)` 만, cipher Stage 둘은 TlsEngine 과 ConnectionStream (WORKING) 만 만진다.
- 콜백은 IO Thread 의 step 안 (`onOpened`, `dispatch`) 에서만 나간다. drain, settle, collectIoEvents, resolve, pick 은 handler 를 받지 않는다. stepLocalEvents 는 step 을 부르는 자리라 받는다.
- IO Thread 밖에서 EventListener 에 들어가는 호출은 `wake()` 하나. 다른 thread 가 Connection 에 무엇을 적으면 (notify Stage, WorkContext) 반드시 wake 한다. IO Thread 안에서 적어도 wake 한다 (WAKE 완료 하나가 더 돌 뿐이다). 그래서 "IO Thread 인가" 를 보는 분기가 wake 경로에 없다.

## 3. base.h 변경

v2 3절에서 바뀐 것만 적는다. 나머지 (TLS 코드 8 개, no_delay, Decision, create 의 Result 넷) 는 사이클 1 에서 코드가 됐다.

```c
/* action 함수 여섯. "return 한 뒤 다음에 일어날 일" 이 이름에 들어간다 */
KiottyNetConnectionAction KiottyNet_thenHold(uint32_t timeout_ms);
KiottyNetConnectionAction KiottyNet_thenRead(size_t exact, uint32_t timeout_ms);
KiottyNetConnectionAction KiottyNet_thenReadUntil(KiottyNetSpan delimiter, size_t max, uint32_t timeout_ms);
KiottyNetConnectionAction KiottyNet_thenWrite(KiottyNetSpan data, uint32_t timeout_ms);
KiottyNetConnectionAction KiottyNet_thenWork(void);
KiottyNetConnectionAction KiottyNet_thenClose(KiottyNetCode reason);

typedef struct KiottyNetConnectionOwner KiottyNetConnectionOwner;   /* 불투명. 연결을 가진 Server / EventClient. C++ 에서는 Owner 클래스 그 자체 (13절) */
typedef struct KiottyNetConnectionHandle        /* 비동기. 모든 콜백에. getConnectionInfo / notifyConnection 만. 찾는 길은 owner 포인터 하나라 static 도 context 인자도 없다 */
{
    KiottyNetConnectionOwner* owner;            /* NULL 이면 무효 */
    int64_t                   id;               /* 상위 32 generation, 하위 16 slot. 0 이면 무효 */
} KiottyNetConnectionHandle;
typedef struct KiottyNetConnectionStream        /* 동기. onWorkStarted 가 준다. closeConnectionStream 까지 유효. 같은 owner 와 id 의 다른 타입. KiottyNetWorkHandle 은 없다 */
{
    KiottyNetConnectionOwner* owner;
    int64_t                   id;
} KiottyNetConnectionStream;

typedef struct KiottyNetConnectionHandler
{
    KiottyNetConnectionDecision (*onConnected)   (KiottyNetAppContext app, KiottyNetConnectionInfo info);
    KiottyNetConnectionAction   (*onReady)       (KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection);
    KiottyNetConnectionAction   (*onRead)        (KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection, KiottyNetSpan data);
    KiottyNetConnectionAction   (*onWritten)     (KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection, size_t length);
    KiottyNetConnectionAction   (*onTimeout)     (KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection);
    void                        (*onWorkStarted) (KiottyNetConnectionContext ctx, KiottyNetConnectionStream stream);        /* 이 순간부터 close 까지 연결은 worker 의 것 */
    void                        (*onDisconnected)(KiottyNetAppContext app, KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection, KiottyNetCode reason);
    void                        (*onError)       (KiottyNetAppContext app, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message);   /* 연결 없는 오류는 소유자의 endpoint */
} KiottyNetConnectionHandler;

/* 동기 통로. Work 를 받은 thread 만. IO Thread 에서 부르면 KIOTTY_BUSY */
KiottyNetResult KiottyNet_readConnectionStream     (KiottyNetConnectionStream stream, void* buffer, size_t length, uint32_t timeout_ms);
KiottyNetResult KiottyNet_writeConnectionStream    (KiottyNetConnectionStream stream, KiottyNetSpan data, uint32_t timeout_ms);
KiottyNetCode   KiottyNet_writeConnectionStreamFile(KiottyNetConnectionStream stream, const char* path, uint64_t offset, uint64_t length, uint32_t timeout_ms);
bool            KiottyNet_isConnectionStreamOpen   (KiottyNetConnectionStream stream);                                   /* recv(MSG_PEEK). 닫혔으면 false */
KiottyNetCode   KiottyNet_closeConnectionStream    (KiottyNetConnectionStream stream, KiottyNetConnectionAction then);   /* 한 번. thenWork 는 INVALID_ARGUMENT. v2 의 finishWork */

KiottyNetContextResult KiottyNet_initializeContext(void);   /* process 에 하나인 Context 를 연다. 두 번째 호출은 같은 handle 과 SUCCESS. 설정은 안의 기본값 (13절) */

size_t KiottyNet_serverMemorySize(void);       /* Server 하나의 sizeof. create 가 잡는 크기 (19.3) */
size_t KiottyNet_clientMemorySize(void);       /* StreamClient / EventClient 중 큰 쪽의 sizeof */
```

| 항목 | 값 |
|---|---|
| `max_clients` 기본 | 128 |
| `max_clients` 상한 | `KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER`. `configureServerMaxClients` 가 0 또는 그 값 초과면 `KIOTTY_INVALID_ARGUMENT` |
| `max_clients` 의 compile time 상한 | `KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER` (기본 128, 최대 1024). runtime 값이 넘으면 `KIOTTY_INVALID_ARGUMENT` |
| recv / send / push capacity 의 상한 | `KIOTTY_NET_MAX_RECV / SEND / PUSH_CAPACITY` (기본 16 KiB) |
| `KIOTTY_OUT_OF_MEMORY` | enum 끝에 더한다. `create*` 가 돌려준다 (Owner 하나를 만들 메모리가 없을 때) |
| `initializeContextWithTlsPool(bytes)` | 지운다. TLS 메모리는 19.5 의 arena 다 |

지우는 것: `KiottyNetWorkHandle`, `KiottyNet_finishWork`, `KiottyNet_isWorkConnected`, 옛 이름의 action 함수 여섯 (`holdCommunication`, `readConnection`, `readConnectionUntil`, `writeConnection`, `startWork`, `closeConnection`). C++ 의 `KiottyNet_Work` 는 `KiottyNet_ConnectionStream` 으로 이름을 바꾸고 `read / write / writeFile / open / close(action)` 을 가진다. 소멸자는 close 하지 않은 Stream 을 `thenClose(KIOTTY_SUCCESS)` 로 닫는다. `KiottyNet_Connection` 은 `operator==` 와 `operator<` 를 가진다 (15절).

| action | timeout_ms | 지나면 |
|---|---|---|
| READ, READ_UNTIL | 조건이 찰 때까지 | onTimeout (11절) |
| WRITE | onWritten 까지. notify 뒤에서 기다리는 시간을 포함한다 | onTimeout |
| HOLD | 아무것도 걸지 않고 기다리는 한도 | onTimeout |
| START_WORK | 없음. closeConnectionStream 의 action 이 다음 deadline | — |
| CLOSE | 없음 | — |

Stream 의 오용 (검사는 `WorkContext::verify` 한 자리다, 9.7):

| 오용 | 결과 |
|---|---|
| IO Thread 에서 read / write / writeFile | `KIOTTY_BUSY` (Owner 가 `IoLoop::isIoThread()` 로) |
| WORKING 이 아닌 연결의 Stream (close 한 뒤, 또는 Work 가 끝난 뒤) | `KIOTTY_BUSY` |
| 연결이 닫히고 slot 이 재사용됨 (generation 불일치) | `KIOTTY_PEER_CLOSED` |
| Owner 가 WORKING 중에 닫음 | read / write 는 socket 의 0 / 오류를 그대로 (`PEER_CLOSED` / `SOCKET_ERROR`). `isConnectionStreamOpen` 은 false. `closeConnectionStream` 은 `KIOTTY_CLOSED` 를 돌려주되 Work 는 끝난다 |
| `owner` 가 NULL 이거나 `id` 가 0 | `KIOTTY_INVALID_ARGUMENT`. Connection handle 을 Stream 함수에 넘기는 것은 타입이 달라 compile 되지 않는다 |
| `closeConnectionStream(stream, thenWork())` | `KIOTTY_INVALID_ARGUMENT`. Stream 은 유효하게 남는다 |
| `closeConnectionStream(stream, thenWrite(data))` 의 data 가 `send_capacity` 초과 | `KIOTTY_BUFFER_FULL`. Stream 은 유효하게 남는다 |

## 4. core

v2 4절 그대로. 코드가 있다. `StageSet { receive, send, notify }` 는 평문 Stage 셋의 저장 공간으로 그대로 쓰고, 9.3 의 `Sender` 함수 둘이 send / notify 를 인자로 받는다.

- `Stage(Pool& pool, size_t capacity)`, `StageSet(Pool& pool, receive, send, notify)`. 버퍼는 Pool 에서 받고 소멸자에서 돌려준다. Pool 이 못 주면 `valid() == false` 이고 capacity 0 이다. Pool 은 19절.

## 5. IoAction, IoPayload, IoCompletion

dispatch (completion → action) → apply (action → Connection 상태) → submit (action + payload → listener) → OS → completion (걸었던 action 을 싣고).

```cpp
enum class IoActionKind : uint8_t { NONE, HOLD, READ, READ_UNTIL, WRITE, START_WORK, CLOSE, ACCEPT };
enum class IoPurpose    : uint8_t { USER, HANDSHAKE, CONTROL, NOTIFY };    // 완료를 어느 맥락에 돌려주나. WRITE 와 READ 에 붙는다

struct IoAction
{
    IoActionKind kind;
    IoPurpose    purpose;
    uint32_t     timeout_ms;
    union
    {
        struct { size_t exact; } read;
        struct { char delimiter[4]; uint8_t length; size_t max; size_t scanned; } read_until;
        struct { size_t length; } write;         // 평문 길이. 완료가 돌려주면 그 맥락이 release 할 양이자 onWritten 의 값. TLS 의 completion.length 는 암호문 길이라 따로 든다
        struct { KiottyNetCode reason; } close;
    } as;
    static IoAction none();
    static IoAction hold(uint32_t timeout_ms);
    static IoAction read(IoPurpose purpose, uint32_t timeout_ms, size_t exact);       // HANDSHAKE 는 exact 0
    static IoAction readUntil(uint32_t timeout_ms, KiottyNetSpan delimiter, size_t max);
    static IoAction write(IoPurpose purpose, uint32_t timeout_ms, size_t length);     // 바이트는 이미 그 purpose 의 Stage 에
    static IoAction startWork();
    static IoAction close(KiottyNetCode reason);
    static IoAction accept();
    static IoAction from(const KiottyNetConnectionAction& action);                     // purpose USER. WRITE 는 length 만 (data 는 adopt 가 Stage 로)
};
struct IoPayload { void* buffer; size_t length; };
enum class IoKind : uint8_t { ACCEPT, RECEIVE, SEND, WAKE, TIMEOUT, WORK_READY, WORK_DONE };   // 뒤 셋은 stepLocalEvents 가 만든다 (local completion)
struct IoCompletion
{
    IoKind     kind;
    int32_t    slot;                            // ACCEPT / WAKE 는 -1
    SocketCode code;                            // SUCCESS, CLOSED (0 바이트), CANCELLED (닫힌 소켓의 늦은 완료), 오류
    size_t     length;                          // 전송된 바이트. TLS 면 암호문 길이
    IoAction   action;                          // RECEIVE / SEND / ACCEPT : 걸었던 action. WORK_DONE : worker 의 outcome. TIMEOUT / WORK_READY / WAKE : NONE
    static IoCompletion wake();
    static IoCompletion timeout(int32_t slot);
    static IoCompletion workReady(int32_t slot);
    static IoCompletion workDone(int32_t slot, IoAction outcome);
};
```

- IoAction 은 사용자 메모리를 가리키지 않는다. Connection 은 "걸린 read 조건" 도 "걸린 write 의 길이" 도 "worker 의 outcome" 도 들지 않는다. 전부 완료가 가져온다.
- 3b 코드에서 바뀌는 것: `as.write.length` 와 `IoAction::write` 의 셋째 인자, `IoKind::WORK_READY`, `IoCompletion::workReady`, `IoCompletion::workDone(slot, outcome)`. `static_assert(sizeof(IoAction) < 64)` 는 그대로 성립한다.

## 6. socket

v2 6절 그대로. 코드가 있다.

## 7. event

v2 7절 그대로 (attach / submit / detach / wait / wake, 3b 의 세부). 더하는 계약 한 줄:

- **WRITE 완료의 length 는 payload 전체다.** IOCP 는 overlapped WSASend 가 그렇게 완료한다. epoll 은 listener 가 send 하고 남으면 `EPOLLOUT | EPOLLONESHOT` 으로 다시 걸어 끝까지 보낸 뒤에 완료 하나를 만든다. 일부만 보낸 완료는 없다. 그래서 Connection 에 "남은 조각을 다시 건다" 는 길이 없다.

epoll backend (사이클 3c) 의 세부:

- event 마다 `F_DUPFD_CLOEXEC` 로 복제한 fd 를 쓰고 `EPOLLONESHOT` 으로 건다. `data.ptr` 은 submit 때마다 넣는다 (처음은 ADD, 그 뒤 MOD). event 는 Connection 의 멤버라 옮겨지지 않는다.
- READ / READ_UNTIL 은 readable 통지 뒤 한 번 recv 한다. WRITE 는 submit 에서 바로 send 하고, 남으면 `EPOLLOUT` 으로 이어 보내 전부 보낸 뒤에만 완료 하나를 만든다. ACCEPT 는 listening fd 가 readable 이면 길이 0 의 완료다 (payload 는 보지 않는다. accept 는 Acceptor 가 `PassiveSocket::accept` 로).
- submit 에서 바로 끝난 WRITE, 즉시 실패 (epoll_ctl 실패, send 오류), detach 의 CANCELLED 는 listener 의 ready 목록 (event 를 잇는 intrusive list) 에 들어가고 다음 wait 가 먼저 돌려준다. 할당은 open 의 배열 둘뿐이다.
- detach 는 `EPOLL_CTL_DEL` 뒤에 복제 fd 를 닫는다 (원래 fd 가 열려 있으면 close 만으로는 등록이 지워지지 않는다). wake 는 eventfd (`data.ptr == nullptr`).
- 복제 fd 라서 "원래 socket 을 닫은 뒤 submit" 은 즉시 실패가 아니다. 이 순서는 정상 경로에 없다 (detach 가 close 보다 먼저). 공통 테스트 `ImmediateFailure...` 는 `shutdown(양방향)` 뒤 submit 으로 두 backend 에서 같은 뜻을 본다.

3b 테스트에서 바뀌는 것: `WriteLargerThanSocketBufferCompletesPossiblyPartiallyAndTheRestIsResubmittable` 을 `WriteLargerThanSocketBufferCompletesOnceWithFullLength` 로. `TwoSendsSubmittedInOrderArriveAtPeerInThatOrder` 와 `DetachWithPendingReadAndWriteDeliversTwoCancelled` 는 event 둘을 read / write 모양 (`IoAction::read` 하나 + `IoAction::write` 하나) 으로 다시 쓴다. listener 구현은 바뀌지 않는다.

## 8. tls

v2 8절에서 cipher Stage 가 셋에서 둘로 준다. 송신 암호문은 하나다.

```cpp
static const uint32_t TLS_HANDSHAKE_TIMEOUT_MS = 10000;   // 설정에 handshake timeout 이 없으므로 상수
class TlsProfile { /* v2 그대로. SSL_OP_NO_RENEGOTIATION 을 켠다. 기본 생성자 */ };
enum class TlsStep : uint8_t { SEND, RECEIVE, DONE, FAILED };
class TlsEngine                                 // 연결 하나의 SSL + BIO 둘 + cipher Stage 둘
{
public:
    TlsEngine(Pool& pool, size_t receive_capacity, size_t send_capacity);   // cipher Stage 둘을 pool 에서. 둘 다 0 이면 평문 (active 늘 false)
    KiottyNetCode open(TlsProfile& profile, const char* hostname);
    void          close();
    bool          active() const;
    Stage&        cipherReceive();              // socket 이 받은 바이트. READ(HANDSHAKE / TLS USER) 의 payload
    Stage&        cipherSend();                 // socket 으로 나갈 바이트. handshake, control, encode 의 출력이 모두 여기. WRITE 의 payload
    TlsStep       handshake();                  // SSL_do_handshake. cipherSend 에 바이트가 있으면 SEND, 1 이면 DONE, WANT_READ 면 RECEIVE, 아니면 FAILED
    Result<KiottyNetCode, size_t> decode(Stage& plain);   // cipherReceive → SSL_read → plain. SSL 안에 남은 평문도 plain 에 자리가 있는 만큼 꺼낸다. 끝에 cipherReceive 를 compact. 꺼낸 길이
    void          encode(KiottyNetSpan plain);  // SSL_write → cipherSend. 호출자가 plain 을 자기 Stage 에서 release 한다
    KiottyNetCode lastCode() const;
    const char*   lastMessage() const;
};
```

- `cipherSend` 의 capacity 는 `max(send_capacity, push_capacity)` 의 레코드 오버헤드 포함 분량 + control 여유 (레코드 16 KiB 마다 64 바이트, 더해서 512 바이트). 한 번에 한 맥락만 encode 하므로 둘 분량이 필요하지 않다 (9.6).
- `encode` 는 실패하지 않는다. renegotiation 을 끄고 cipherSend 의 자리를 보장하므로 SSL_write 의 WANT_* 가 없고, SSL 의 fatal 오류는 decode / handshake 에서만 생겨 그 자리에서 닫힌다. debug 는 `assert`, release 는 cipherSend 에 아무것도 넣지 않는다 (그 WRITE 는 listener 가 payload 0 으로 거절하고, WRITE 의 timeout 이 사용자에게 알린다).
- `decode` 는 "cipherReceive 의 레코드를 SSL 에 넣고, plain 이 찰 때까지 SSL_read" 다. 레코드가 덜 찼으면 0. 이것이 refill (9.5) 의 한 걸음이다.
- 오류 분류 표는 v2 8절 그대로.
- decode 가 끝날 때 상대의 KeyUpdate 가 답장을 기다리면 (`SSL_get_key_update_type` 이 NONE 이 아니면) `SSL_key_update(NOT_REQUESTED)` + `SSL_do_handshake` 로 그 답장을 cipherSend 에 꺼내 둔다. SSL_read 만으로는 다음 SSL_write 전까지 나오지 않기 때문이다.
- handshake 가 DONE 을 돌려준 뒤에도 rbio 에 상대의 첫 응용 레코드나 NewSessionTicket 이 남아 있을 수 있다. 9.5 의 첫 READ 전에 decode 를 한 번 불러 소화한다 (사이클 5b).
- **SSL 과 cipher Stage 사이는 BIO pair 하나다** (`BIO_new_bio_pair`, 양쪽 버퍼 4 KiB 고정). SSL 은 안쪽 BIO 를 읽고 쓰고, engine 은 바깥쪽 BIO 하나 (`_network`) 만 든다. `feed()` 는 cipherReceive 에서 pair 에 들어갈 만큼만 넣고, `flush()` 는 pair 에서 cipherSend 에 들어갈 만큼만 꺼낸다. 둘 다 옮긴 길이를 돌려준다. handshake / decode / encode 는 OpenSSL 이 WANT_READ / WANT_WRITE 를 주면 feed / flush 로 자리를 만들고 다시 부르며, 아무것도 옮기지 못하면 멈춘다. 그래서 cipherReceive 에 레코드가 남은 채 decode 가 끝날 수 있고 (다음 decode 가 이어 넣는다), handshake 는 WANT_WRITE 도 SEND / RECEIVE 로 본다. memory BIO 는 메시지 크기만큼 자라고 줄지 않아 연결당 OpenSSL 메모리가 Stage 크기를 따라갔다 (64 KiB 에서 183 KiB). pair 로 바꾼 뒤에는 Stage 크기와 무관하게 약 68 KiB 다 (19절).
- **OpenSSL 의 메모리는 19.5 의 두 arena (전역 + Owner 서브풀) 다** (kiotty 가 OpenSSL 사본을 혼자 쓰는 build). 시스템 OpenSSL 을 공유하면 OpenSSL 의 malloc 그대로다.
- IO thread 가 끝날 때 `OPENSSL_thread_stop` 을 부른다 (사이클 6). Windows 정적 링크에서는 OpenSSL 이 thread 종료를 스스로 알지 못한다.

## 9. connection

### 9.1 IoSource

```cpp
class IoSource                                  // Connection, Acceptor 의 base. submit 이 보는 전부
{
public:
    virtual ~IoSource();
    virtual SocketHandle     socket() const = 0;
    virtual int32_t          slot() const = 0;
    virtual IoEvent& eventFor(const IoAction& action) = 0;   // 방향으로 고른다. WRITE → write event, 그 외 → read event. Acceptor 는 하나
    virtual IoPayload        resolve(const IoAction& action) = 0;    // action 의 purpose 가 가리키는 Stage 의 자리 (9.6)
    virtual SocketCode       attach(EventListener& listener) = 0;
    virtual void             detach(EventListener& listener) = 0;
    virtual void             shutdown() = 0;    // FIN. submit(CLOSE). socket.close 는 release 가 (9.5)
};
```

### 9.2 Connection 이 드는 것

Connection 은 풀 안의 객체다 (19.2). 자기 바깥 (host, listener, Owner, Handler) 을 가리키는 멤버가 없고, 그것들은 쓰는 함수가 참조 인자로 받는다 (19.3 의 표). 멤버는 "run 보다 오래 사는 상태" 뿐이다.

| 변수 | 타입 | 맥락 | 바뀌는 자리 | 읽는 자리 |
|---|---|---|---|---|
| `_phase` | atomic\<Phase\> IDLE / HANDSHAKING / READY / CLOSING | 수명 | initialize, onHandshake DONE, apply(CLOSE), reset | dispatch 첫 줄, pickWrite, settle, alive() (다른 thread) |
| `_deadline` | uint64_t, 0 이면 없음 | timeout | apply, stepLocalEvents (만료시 0) | stepLocalEvents, waitTimeout |
| `_work` | WorkContext IDLE / REQUESTED / WORKING / DONE | Work | apply(START_WORK) request, apply(CLOSE) abandon, onWorkReady start, closeStream (worker) finish, stepLocalEvents onFinished, reset | stepLocalEvents, pickWrite, settle, closeStream (worker) |
| `_read_event`, `_write_event` | IoEvent (backend 의 구체 타입) | 방향마다 걸린 요청 하나 | listener 만 | pickWrite, onTimeout, stepLocalEvents, settle |
| `_plain` | StageSet { receive, send, notify } | 저장 공간 셋 | send : adopt(WRITE) 와 worker 의 closeStream(WRITE) 가 `Sender::stage`, onSent(USER) 가 `Sender::release`. notify : host 의 notify / broadcast (아무 thread) 가 `stageNotification`, onSent(NOTIFY) 가 `Sender::release`. notify 를 만지는 자리는 전부 host 의 mutex 안 | pickWrite (notify 는 lock 안), onTimeout, ConnectionStream (WORKING 동안 receive 와 send) |
| `_tls` | TlsEngine (cipher receive / send, SSL) | TLS | onHandshake, refill 의 decode, pickWrite 의 encode, onSent 의 cipherSend consume | resolve, ConnectionStream (WORKING) |
| `_socket`, `_id`, `_context`, `_info`, `_state` | | 수명과 사용자 값 | initialize, reset | |

v2 9.2 와 비교해 없어진 것과 이유:

| v2 | v4 | 왜 |
|---|---|---|
| `_stream_event`, `_notify_event` | `_read_event`, `_write_event` | 용도가 아니라 방향으로 나눈다. socket 당 write 는 하나 |
| `_resume_read` | 없음 | control 답장은 cipherSend 에 쌓이고 write lane 이 보낸다. READ 는 미루지 않는다 |
| `_write_length`, `_notify_plain_length` | 없음 | 평문 길이는 `IoAction::as.write.length` 에 실려 완료가 가져온다 |
| `_peer_closed` | 없음 | WORKING 동안 IO Thread 는 socket 을 보지 않는다. worker 의 Stream 이 끊김을 직접 본다 |
| `_tls.cipher().notify` | 없음 | 송신 암호문은 cipherSend 하나 |
| `NotificationContext::queued` | 없음 | 적는 thread 가 늘 wake 하고, IO Thread 가 적은 것은 wait 전의 drain 이 건다 (10절) |

19절 (사이클 5) 에서 더 없어진 것:

| 있었던 것 | 어디로 | 왜 |
|---|---|---|
| `_notification` (NotificationContext 포인터) | host 의 `std::mutex` 를 dispatch / pickWrite 의 인자로. listener 는 Owner 가 wake. IO thread 판별은 `IoLoop::isIoThread()` | 풀 안의 객체는 바깥을 가리키지 않는다 |
| `_send`, `_notify` (Sender 둘) | `Sender::stage(_plain.send, …)` 같은 static 함수 (9.3) | 상태가 없는 것을 멤버로 둘 이유가 없다 |
| `_stream` (ConnectionStream) | `stream(generation)` 이 값으로 만들어 돌려준다 (9.7) | 같다 |
| `handler.app_context`, handler 포인터 | `Handler` (9.4) 를 콜백이 나가는 함수가 참조로 받는다 | run 동안만 있는 것 |
| `_limits`, `_tls_enabled`, `_handshake_timeout_ms` | `Stage::limit` 에 들어가고 버린다 / `_tls.active()` / 상수 `TLS_HANDSHAKE_TIMEOUT_MS` | 읽는 자리가 하나다 |

### 9.3 Sender : 보내는 두 걸음

```cpp
struct Sender                                   // connection/kiotty_sender.h. 상태 없음. "한 Stage 에 넣는다" 와 "치운다" 둘
{
    static KiottyNetCode stage(StageBase& stage, KiottyNetSpan data);   // 복사. writable() < data.length 면 BUFFER_FULL 이고 아무것도 넣지 않는다
    static void          release(StageBase& stage, size_t length);      // 완료 뒤. consume 하고 compact
};
```

- readable 과 peek 은 `stage.consumer()` 가, clear 는 `StageBase::clear()` 가 이미 한다. 어느 Stage 인지가 호출 줄 (`Sender::stage(_plain.send, data)`, `Sender::release(_plain.notify, plain)`) 에 보인다.
- 누가 어느 thread 에서 부르는지는 Connection 의 맥락이 정한다. send 는 "RUNNING 이면 IO Thread, WORKING 이면 worker" 라 동시에 둘인 순간이 없어 lock 이 없다. notify 는 아무 thread 가 stage 하므로 그것을 만지는 네 자리 (host 의 notify / broadcast 의 stage, pickWrite 의 readable + peek + encode, onSent(NOTIFY) 의 release, reset 의 clear) 가 host 의 mutex (`ConnectionHost::_notification`) 안이다. 앞의 둘은 host 가 lock 을 잡고 부르고, 뒤의 둘은 IoLoop 가 넘긴 `std::mutex&` 로 Connection 이 잡는다.
- release 는 늘 compact 한다. release 는 write event 가 비어 있을 때 (완료 직후) 불리므로 걸린 send 가 가리키는 구간을 옮기는 일이 없다. "비었을 때만 compact" 로 하면 송신 중 뒤에 붙는 notify 가 끊이지 않을 때 writable 이 0 으로 줄어 BUFFER_FULL 이 굳어지므로 늘 한다.
- stage 가 `BUFFER_FULL` 을 돌려주는 조건은 `writable() < data.length` 다. notify 는 호출자가 그 결과를 받고, user write 는 adopt 가 CLOSE(BUFFER_FULL) 로 바꾼다.

### 9.4 인터페이스

```cpp
enum class Phase : uint8_t { IDLE, HANDSHAKING, READY, CLOSING };

struct Handler                                  // run 의 stack 에 하나. 콜백이 받을 것 전부. 아래로 참조로 내려간다. 복사 없음
{
    Handler(const KiottyNetConnectionHandler& callbacks, KiottyNetAppContext app_context, Owner& owner);
    const KiottyNetConnectionHandler& callbacks;   // 사용자의 struct. run 이 돌아올 때까지 살아 있어야 한다 (포인터 때와 같다)
    KiottyNetAppContext               app_context;
    Owner&                            owner;       // handle 에 찍는다 (3절의 KiottyNetConnectionHandle::owner)
};

struct ConnectionLimits { size_t receive; size_t send; size_t notify; };
struct ConnectionConfiguration                  // accept / admit 마다 하나. 이동만 (socket). 참조 멤버가 있어 대입은 없고 그것으로 충분하다
{
    ActiveSocket               socket;          // Connection 이 든다
    KiottyNetConnectionContext context;         // Connection 이 든다
    const char*                hostname;        // client 는 Owner 의 _config.hostname, server 는 "". TlsEngine::open 에 쓰고 버린다 (SNI 와 SSL_set1_host 가 복사한다)
    ConnectionLimits           limits;          // host 의 _limits. Stage::limit(capacity) 셋에 들어가고 버린다
    TlsProfile&                profile;         // Owner 의 _profile. valid() 가 거짓이면 평문. TlsEngine::open 에 쓰고 버린다 (SSL_new 가 SSL_CTX 의 참조 수를 올린다)
};

class Connection : public IoSource              // configuration_type = ConnectionConfiguration. 기본 생성자만 (is_reusable, 19.2)
{
public:
    KiottyNetCode initialize(PoolHandle id, ConnectionConfiguration&& configuration);   // accept / admit 때 (v2 의 open). Stage::limit 셋, socket, profile.valid() 면 _tls.open(profile, hostname). HANDSHAKING 또는 READY
    void          reset() noexcept;             // host.release 가 lock 안에서. socket.close, _work.reset, Stage 비움, IDLE. generation 은 풀이 올린다
    PoolHandle    id() const;                   // handle 의 id 와 WorkContext::verify 의 generation
    int32_t       generation() const;
    bool          alive() const;                // HANDSHAKING 또는 READY
    Phase         phase() const;

    SocketHandle socket() const override;
    int32_t      slot() const override;
    IoEvent&     eventFor(const IoAction& action) override;
    IoPayload    resolve(const IoAction& action) override;
    SocketCode   attach(EventListener& listener) override;
    void         detach(EventListener& listener) override;
    void         shutdown() override;

    // step (IO Thread, 콜백 있음). handler 는 run 의 것, notification 은 host 의 mutex
    IoAction onOpened(const Handler& handler);                                                             // initialize 직후. 평문 : onReady. TLS : onHandshake
    IoAction dispatch(const IoCompletion& completion, const Handler& handler, std::mutex& notification);   // phase 라우팅 → kind 별 on* 하나 → apply

    // drain (IO Thread, 콜백 없음)
    IoAction pickWrite(std::mutex& notification);                                // 비어 있지 않으면 NONE. 9.6

    // stepLocalEvents / settle / waitTimeout 이 읽는 것 (IO Thread)
    uint64_t deadline() const;
    bool     expireDeadline(uint64_t now);      // _deadline != 0 && now >= _deadline 이면 0 으로 돌리고 true
    bool     workReady() const;                 // _work.requested() && 두 event 가 비었음
    bool     workFinished() const;              // _work.finished()
    IoAction takeWorkOutcome();                 // _work.onFinished(). DONE → IDLE
    bool     working() const;                   // _work.working()
    const IoEvent& readEvent() const;
    const IoEvent& writeEvent() const;

    // 아무 thread. notify 둘은 host 가 lock 을 잡고 부른다
    KiottyNetConnectionInfo    info() const;
    KiottyNetConnectionContext context() const;
    int32_t                    state() const;
    void                       setState(int32_t state);
    size_t                     notificationWritable() const;
    KiottyNetCode              stageNotification(KiottyNetSpan data);           // Sender::stage(_plain.notify, data)

    // worker (WORKING 동안만)
    Result<KiottyNetCode, ConnectionStream> stream(int32_t generation);         // _work.verify(generation) 이 SUCCESS 일 때만 값 (9.7)
    KiottyNetCode closeStream(int32_t generation, const KiottyNetConnectionAction& then);   // 9.7. wake 는 Owner 가

private:
    IoAction onReceived(const IoCompletion& completion, const Handler& handler);
    IoAction onSent(const IoCompletion& completion, const Handler& handler, std::mutex& notification);
    IoAction onTimeout(const IoCompletion& completion, const Handler& handler);
    IoAction onWorkReady(const IoCompletion& completion, const Handler& handler);
    IoAction onWorkFinished(const IoCompletion& completion, const Handler& handler);
    IoAction onHandshake(const Handler& handler);
    IoAction onPeerGone(SocketCode code);
    IoAction adopt(const KiottyNetConnectionAction& returned);                            // return → IoAction 변환 한 걸음. WRITE 는 여기서 Sender::stage 하고 HOLD 가 된다. 콜백 없음, 재귀 없음
    IoAction drive(IoAction action, const Handler& handler);                              // READ 계열이면 receive Stage 에 대 보고 찼으면 onRead. switch 하나의 loop. 깊이 상수
    IoAction extend(const KiottyNetConnectionAction& returned, const Handler& handler);   // onTimeout 에 사용자 action 이 진행 중일 때 (11절)
    Result<KiottyNetCode, size_t> refill();                                               // receive Stage 를 compact 하고 TLS 면 SSL 에 남은 평문을 꺼낸다. 늘어난 길이
    IoAction apply(IoAction action, const Handler& handler);                              // deadline, Work 요청, CLOSING + onDisconnected
    IoAction encoded(IoAction write, Span<char> plain);                                   // TLS 면 encode. pickWrite 의 마지막 한 걸음
    bool     userActionPending() const;                                                   // _read_event.pending() || _plain.send.consumer().readable() > 0
    StageBase& receiveStage(IoPurpose purpose); // HANDSHAKE / TLS USER : cipherReceive. 평문 USER : _plain.receive
    KiottyNetConnectionHandle handle(const Handler& handler) const;                       // { &handler.owner, _id 의 값 }
    KiottyNetConnectionStream streamHandle(const Handler& handler) const;                 // 같은 값. onWorkStarted 에
};
```

### 9.5 step 쪽 : dispatch → apply

```cpp
IoAction Connection::dispatch(const IoCompletion& completion, const Handler& handler, std::mutex& notification)
{
    if (_phase == Phase::CLOSING)
    {
        return IoAction::none();                // 늦은 완료 (CANCELLED 포함) 와 CLOSE 뒤에 온 local completion. kind 를 가리지 않는다
    }
    IoAction action = IoAction::none();
    switch (completion.kind)
    {
    case IoKind::RECEIVE:    action = onReceived(completion, handler);     break;
    case IoKind::SEND:       action = onSent(completion, handler, notification);         break;
    case IoKind::TIMEOUT:    action = onTimeout(completion, handler);      break;
    case IoKind::WORK_READY: action = onWorkReady(completion, handler);    break;
    case IoKind::WORK_DONE:  action = onWorkFinished(completion, handler); break;
    }
    return apply(action, handler);
}

IoAction Connection::apply(IoAction action, const Handler& handler)
{
    switch (action.kind)
    {
    case IoActionKind::NONE:
        return action;
    case IoActionKind::HOLD:                    // 사용자의 WRITE 도 여기로 온다. adopt 가 stage 뒤 HOLD(timeout) 로 바꿨다. 거는 것은 drain 이다
    case IoActionKind::READ:
    case IoActionKind::READ_UNTIL:
        setDeadline(action.timeout_ms);
        return action;
    case IoActionKind::START_WORK:
        _deadline = 0;
        _work.request();                        // REQUESTED. WORKING 으로 올리는 것은 stepLocalEvents 의 WORK_READY 다 (10절)
        return action;
    case IoActionKind::CLOSE:
        _deadline = 0;
        _phase = Phase::CLOSING;
        _work.abandon();                        // REQUESTED 였으면 IDLE. 시작하지 않은 Work 는 CLOSE 가 거둔다. WORKING / DONE 은 그대로 (worker 가 끝낸다)
        handler.callbacks.onDisconnected(handler.app_context, _context, handle(handler), action.as.close.reason);
        return action;
    }
}

IoAction Connection::onOpened(const Handler& handler)
{
    if (_tls.active())
    {
        setDeadline(_handshake_timeout_ms);     // 첫 걸음이 SEND 인 클라이언트를 위해. READ(HANDSHAKE) 는 걸음마다 같은 timeout 을 다시 건다
        return apply(onHandshake(handler), handler);
    }
    KiottyNetConnectionAction returned = handler.callbacks.onReady(_context, handle(handler));
    return apply(drive(adopt(returned), handler), handler);
}

IoAction Connection::onHandshake(const Handler& handler)
{
    switch (_tls.handshake())
    {
    case TlsStep::SEND:    return IoAction::none();                        // 바이트는 cipherSend 에. 다음 drain 이 WRITE(HANDSHAKE) 로 건다
    case TlsStep::RECEIVE: return IoAction::read(IoPurpose::HANDSHAKE, _handshake_timeout_ms, 0);
    case TlsStep::DONE:
    {
        _phase = Phase::READY;                  // handshake 와 함께 온 첫 application data 는 SSL 안에 있다. drive 의 refill 이 꺼낸다
        KiottyNetConnectionAction returned = handler.callbacks.onReady(_context, handle(handler));
        return drive(adopt(returned), handler);
    }
    case TlsStep::FAILED:
        handler.callbacks.onError(handler.app_context, _info, _tls.lastCode(), _tls.lastMessage());
        return IoAction::close(_tls.lastCode());
    }
}

IoAction Connection::onReceived(const IoCompletion& completion, const Handler& handler)
{
    if (completion.code != SocketCode::SUCCESS)
    {
        return onPeerGone(completion.code);
    }
    receiveStage(completion.action.purpose).provider().commit(completion.length);
    if (completion.action.purpose == IoPurpose::HANDSHAKE)
    {
        return onHandshake(handler);
    }
    return drive(completion.action, handler);   // 걸었던 READ 를 그대로. TLS 의 decode 는 drive 의 refill 이 한다. 평문과 같은 길이다
}

IoAction Connection::onSent(const IoCompletion& completion, const Handler& handler, std::mutex& notification)
{
    if (completion.code != SocketCode::SUCCESS)
    {
        return onPeerGone(completion.code);
    }
    if (_tls.active())
    {
        _tls.cipherSend().consumer().commit(completion.length);             // 암호문은 길이로. 평문은 아래 맥락이 자기 길이로
    }
    size_t plain = completion.action.as.write.length;
    switch (completion.action.purpose)                                      // 라우팅만. 각 맥락이 자기 뒷정리를 한다
    {
    case IoPurpose::HANDSHAKE:
        return onHandshake(handler);
    case IoPurpose::CONTROL:
        return IoAction::none();
    case IoPurpose::NOTIFY:
    {
        std::lock_guard<std::mutex> lock(notification);
        Sender::release(_plain.notify, plain);
        return IoAction::none();
    }
    case IoPurpose::USER:
    {
        Sender::release(_plain.send, plain);
        KiottyNetConnectionAction returned = handler.callbacks.onWritten(_context, handle(handler), plain);
        return drive(adopt(returned), handler);
    }
    }
}

IoAction Connection::onTimeout(const IoCompletion&, const Handler& handler)
{
    if (_phase == Phase::HANDSHAKING)
    {
        handler.callbacks.onError(handler.app_context, _info, KIOTTY_TLS_HANDSHAKE_TIMEOUT, "tls: handshake timeout");
        return IoAction::close(KIOTTY_TLS_HANDSHAKE_TIMEOUT);
    }
    KiottyNetConnectionAction returned = handler.callbacks.onTimeout(_context, handle(handler));
    if (userActionPending())                    // READ 가 걸려 있거나 (read event pending) WRITE 가 끝나지 않았다 (send Stage 에 바이트)
    {
        return extend(returned, handler);
    }
    return drive(adopt(returned), handler);
}

IoAction Connection::onWorkReady(const IoCompletion&, const Handler& handler)
{
    _work.start(generation());                  // REQUESTED → WORKING
    handler.callbacks.onWorkStarted(_context, streamHandle(handler));
    return IoAction::none();
}

IoAction Connection::onWorkFinished(const IoCompletion& completion, const Handler& handler)
{
    return drive(completion.action, handler);   // worker 의 outcome. stepLocalEvents 가 _work 에서 꺼내 실었다. closeStream 이 adopt 와 같은 규칙으로 만든 IoAction (WRITE 는 이미 stage 되어 HOLD) 이라 그대로 drive
}

IoAction Connection::onPeerGone(SocketCode code)
{
    if (code == SocketCode::CLOSED)
    {
        return IoAction::close(KIOTTY_PEER_CLOSED);
    }
    return IoAction::close(KIOTTY_SOCKET_ERROR);
}

IoAction Connection::adopt(const KiottyNetConnectionAction& returned)    // 콜백의 return → IoAction. 변환 한 걸음. 콜백도 재귀도 없다
{
    IoAction action = IoAction::from(returned);
    if (action.kind != IoActionKind::WRITE)
    {
        return action;
    }
    KiottyNetCode staged = Sender::stage(_plain.send, returned.as.write.data);             // Stage 에 넣는 것으로 WRITE 는 끝. 거는 것은 drain
    if (staged != KIOTTY_SUCCESS)
    {
        return IoAction::close(staged);         // BUFFER_FULL
    }
    return IoAction::hold(action.timeout_ms);   // read lane 에서 보면 "deadline 만 두고 기다린다". apply 가 deadline 을 건다
}

IoAction Connection::drive(IoAction action, const Handler& handler)       // 다음에 걸 action 하나를 만든다. 재귀 없음. 반복 상한은 recv_capacity (onRead 한 번이 1 바이트 이상 소비)
{
    for (;;)
    {
        switch (action.kind)
        {
        case IoActionKind::HOLD:
        case IoActionKind::START_WORK:
        case IoActionKind::CLOSE:
            return action;                      // apply 로. 그대로

        case IoActionKind::READ:
        case IoActionKind::READ_UNTIL:
        {
            Result<KiottyNetCode, KiottyNetSpan> ready = satisfied(action, _plain.receive);   // 조건 초과 : REQUEST_TOO_LARGE. max 안에 delimiter 없음 : PROTOCOL_ERROR. 안 찼으면 빈 span (scanned 전진)
            if (!ready)
            {
                return IoAction::close(ready.code());
            }
            if (ready.value().length == 0)
            {
                Result<KiottyNetCode, size_t> more = refill();              // 자리를 만들고 (compact), TLS 면 SSL 에 남은 평문을 꺼낸다
                if (!more)
                {
                    handler.callbacks.onError(handler.app_context, _info, more.code(), _tls.lastMessage());
                    return IoAction::close(more.code());
                }
                if (more.value() == 0)
                {
                    return action;              // 정말 더 받아야 한다. resolve 가 receive Stage 의 빈 자리를 payload 로
                }
                break;                          // 꺼낸 평문으로 같은 READ 를 다시 본다
            }
            KiottyNetConnectionAction next = handler.callbacks.onRead(_context, handle(handler), ready.value());
            _plain.receive.consumer().commit(ready.value().length);
            action = adopt(next);               // WRITE 면 여기서 stage 되어 HOLD 로. 다음 바퀴가 그 kind 로 간다
            break;
        }

        case IoActionKind::WRITE:               // adopt 를 지난 action 에는 없다. 걸었던 READ 를 넣는 onReceived 도 READ 만 준다
        case IoActionKind::NONE:
        case IoActionKind::ACCEPT:
            return IoAction::none();
        }
    }
}

Result<KiottyNetCode, size_t> Connection::refill()
{
    _plain.receive.compact();                   // 걸린 READ 도 살아 있는 span 도 없는 유일한 자리라 옮겨도 된다. scanned 는 begin 기준이라 그대로다
    size_t produced = 0;
    if (_tls.active())
    {
        Result<KiottyNetCode, size_t> decoded = _tls.decode(_plain.receive);   // cipherReceive 와 SSL 안의 것을 plain 의 자리만큼. 끝에 cipherReceive compact
        if (!decoded)
        {
            return error(decoded.code());       // RECORD_ERROR, PEER_CLOSED (close_notify)
        }
        produced = decoded.value();
    }
    if (produced == 0 && _plain.receive.provider().writable() == 0)
    {
        return error(KIOTTY_REQUEST_TOO_LARGE); // Stage 가 가득 찼는데 조건이 안 찼다. 더 받을 자리가 없다
    }
    return ok(produced);
}
```

- `receiveStage(HANDSHAKE)` 와 `receiveStage(USER, TLS)` 는 cipherReceive, 평문 USER 는 `_plain.receive`. `resolve(READ)` 는 그 Stage 의 `provider().prepare(writable)`. refill 이 compact 를 끝낸 뒤라 writable 은 0 이 아니다.
- decode 가 한 자리 (refill) 에만 있다. onReceived, onHandshake DONE, onSent(USER), onTimeout, onWorkFinished 가 전부 drive 를 지나므로 "SSL 안에 평문이 남은 채 READ 를 건다" 가 생기지 않는다. 평문 연결은 refill 이 compact 만 한다.
- call stack 의 깊이는 상수다: dispatch → on* → drive → handler 콜백. adopt 는 변환이고 drive 는 loop 하나라 서로 부르지 않는다. 사용자의 WRITE 는 adopt 에서 stage 되어 HOLD(timeout) 로 바뀌므로 **step 의 output 에 WRITE 가 나타나지 않는다.** WRITE 를 거는 쪽은 drain 하나다 (10절).

| Phase | RECEIVE | SEND | TIMEOUT | WORK_READY | WORK_DONE |
|---|---|---|---|---|---|
| IDLE | 오지 않는다 | | | | |
| HANDSHAKING | commit → onHandshake | HANDSHAKE : cipherSend consume → onHandshake | onError(HANDSHAKE_TIMEOUT) → CLOSE | 오지 않는다 | 오지 않는다 |
| READY | commit → drive (refill 이 decode) | purpose 로 라우팅 : NOTIFY release / CONTROL 없음 / USER release → onWritten → adopt → drive | 사용자 action 진행 중이면 extend, 아니면 adopt → drive | WORKING, onWorkStarted | outcome → drive |
| CLOSING | NONE (dispatch 첫 줄) | NONE | NONE | NONE | NONE. _work 는 stepLocalEvents 가 이미 IDLE 로 돌렸다. settle 이 반납 |

### 9.6 drain 쪽 : pickWrite → resolve

```cpp
IoAction Connection::pickWrite(std::mutex& notification)   // IO Thread. drain 이 slot 마다. 콜백 없음
{
    if (!alive())
    {
        return IoAction::none();                // IDLE, CLOSING
    }
    if (_work.working())
    {
        return IoAction::none();                // REQUESTED / WORKING / DONE. socket 은 worker 의 것 (또는 곧 worker 의 것)
    }
    if (_write_event.pending())
    {
        return IoAction::none();                // socket 당 write 하나
    }
    return select();
}

IoAction Connection::select()                   // 무엇을 보내나. phase 가 후보를 정한다 : HANDSHAKING 은 cipherSend 만, READY 는 cipherSend → notify → send
{
    if (_tls.active() && _tls.cipherSend().consumer().readable() > 0)
    {
        IoPurpose purpose = _phase == Phase::HANDSHAKING ? IoPurpose::HANDSHAKE : IoPurpose::CONTROL;
        return IoAction::write(purpose, 0, 0);  // OpenSSL 이 만든 것은 늘 먼저. 평문 길이 0
    }
    if (_phase == Phase::HANDSHAKING)
    {
        return IoAction::none();
    }
    {
        std::lock_guard<std::mutex> lock(notification);
        size_t plain = _plain.notify.consumer().readable();
        if (plain > 0)
        {
            return encoded(IoAction::write(IoPurpose::NOTIFY, 0, plain), _plain.notify.consumer().peek());
        }
    }
    size_t plain = _plain.send.consumer().readable();
    if (plain > 0)
    {
        return encoded(IoAction::write(IoPurpose::USER, 0, plain), _plain.send.consumer().peek());
    }
    return IoAction::none();
}

IoAction Connection::encoded(IoAction write, Span<char> plain)          // TLS 면 고른 평문을 지금 encode. 암호화 순서 = 송신 순서
{
    if (_tls.active())
    {
        _tls.encode(KiottyNetSpan{ plain.data, plain.length });     // cipherSend 로. 평문은 완료 때 release 한다 (pick 은 pending 이라 다시 보지 않는다)
    }
    return write;
}

IoPayload Connection::resolve(const IoAction& action)
{
    switch (action.kind)
    {
    case IoActionKind::READ:
    case IoActionKind::READ_UNTIL:
    {
        StageBase& in = receiveStage(action.purpose);
        size_t room = in.provider().writable();
        return IoPayload{ in.provider().prepare(room), room };
    }
    case IoActionKind::WRITE:
    {
        if (_tls.active())
        {
            Stage::Consumer out = _tls.cipherSend().consumer();
            return IoPayload{ out.peek(), out.readable() };
        }
        Span<char> plain = action.purpose == IoPurpose::NOTIFY ? _plain.notify.consumer().peek() : _plain.send.consumer().peek();   // NOTIFY 는 lock 안에서
        return IoPayload{ plain.data, action.as.write.length };     // 길이는 pick 이 정한 값. 그 뒤 뒤에 붙은 notify 는 다음 WRITE 다
    }
    default:
        return IoPayload{ nullptr, 0 };
    }
}
```

- pickWrite 는 두 프로세스다. "지금 이 socket 에 걸 수 있나" (alive → working → pending, input 은 상태 셋, output 은 bool) 와 "무엇을" (select, input 은 phase 와 Stage 셋, output 은 WRITE 하나 또는 NONE).
- 한 번의 WRITE 는 한 맥락의 readable 전체다. 두 맥락을 한 WRITE 에 섞지 않는다. 그래서 완료는 한 맥락만 치운다.
- notify 가 user 보다 먼저다 (v2 의 "broadcast 우선"). USER WRITE 의 timeout 은 notify 뒤에서 기다리는 시간을 포함하고, 그 시간은 push_capacity 분량으로 유계다.
- TLS 의 평문 길이 `as.write.length` 는 encode 한 평문 길이다. 완료의 `length` 는 암호문 길이이고 cipherSend 에서 그만큼 consume 한다. 둘을 섞어 쓰는 자리가 없다.
- 평문 notify 의 payload 는 "pick 이 본 readable 의 시작 주소 + pick 이 정한 길이" 다. pick 과 resolve 사이에 다른 thread 가 뒤에 붙여도 payload 는 그 길이만큼이다 (compact 는 release 에서만 하므로 시작 주소도 움직이지 않는다). 그래서 완료의 release(plain) 가 보낸 것과 정확히 같은 구간을 치운다.

### 9.7 WorkContext, ConnectionStream, closeStream

```cpp
class WorkContext                               // IDLE → REQUESTED (apply, IO) → WORKING (onWorkReady, IO) → DONE (closeStream, worker) → IDLE (stepLocalEvents, IO)
{
public:
    enum class State : uint8_t { IDLE, REQUESTED, WORKING, DONE };
    void          request();                    // IDLE → REQUESTED
    void          abandon();                    // REQUESTED → IDLE. apply(CLOSE). 다른 상태면 아무것도 안 한다
    void          start(int32_t generation);    // REQUESTED → WORKING
    bool          requested() const;            // stepLocalEvents 의 workReady
    bool          finished() const;             // stepLocalEvents. acquire load == DONE
    IoAction      onFinished();                 // DONE → IDLE. outcome. stepLocalEvents 가 completion 에 싣는다
    bool          working() const;              // REQUESTED, WORKING, DONE. IO Thread 가 socket 을 만지면 안 되는 구간
    void          reset();
    // worker
    KiottyNetCode verify(int32_t generation) const;   // WORKING 이고 generation 일치면 SUCCESS. generation 다르면 PEER_CLOSED. WORKING 아니면 BUSY
    void          finish(IoAction outcome);     // verify 뒤에만. _outcome 뒤 CAS WORKING → DONE (release)
private:
    std::atomic<State> _state;
    int32_t            _generation;
    IoAction           _outcome;
};

class ConnectionStream                          // stream/kiotty_connection_stream.h. 참조 넷을 드는 값. 쓰는 함수가 만들고 그 자리에서 버린다. 할당 없음. 복사 생성만 (대입 없음)
{
public:
    ConnectionStream(ActiveSocket& socket, TlsEngine& tls, StageBase& receive, StageBase& send);
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_ms);      // flush → receive Stage 에 남은 것 먼저 → 비면 poll + recv (+ decode) 반복
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_ms);              // flush → 평문 : 사용자 버퍼에서 바로 sendAll. TLS : send_capacity 씩 encode → cipherSend → sendAll
    KiottyNetCode   writeFile(const char* path, uint64_t offset, uint64_t length, uint32_t timeout_ms);   // 파일을 send_capacity 씩 읽어 write 와 같은 길. 플랫폼 공통. zero-copy 는 두지 않는다
    bool            connected() const;                                           // non-blocking recv(MSG_PEEK). 0 이면 false
    KiottyNetCode   handshake(uint32_t timeout_ms);                              // StreamClient 용. SEND 면 sendAll(cipherSend), RECEIVE 면 receiveSome(cipherReceive)
private:
    KiottyNetCode   flush(uint32_t timeout_ms);                                  // cipherSend 에 남은 것 (handshake 끝의 ticket, KeyUpdate 답장) 을 먼저 sendAll. 평문이면 아무것도 없다
    ActiveSocket& _socket;
    TlsEngine&    _tls;
    StageBase&    _receive;
    StageBase&    _send;
};

Result<KiottyNetCode, ConnectionStream> Connection::stream(int32_t generation)     // worker
{
    KiottyNetCode verified = _work.verify(generation);
    if (verified != KIOTTY_SUCCESS)
    {
        return error(verified);
    }
    return ok(ConnectionStream(_socket, _tls, _plain.receive, _plain.send));      // 포인터 넷을 적는 것뿐이라 비용이 없다
}

KiottyNetCode Connection::closeStream(int32_t generation, const KiottyNetConnectionAction& then)   // worker
{
    KiottyNetCode verified = _work.verify(generation);              // WORKING 이고 내 것인가. 한 번만 본다. WORKING → DONE 은 이 thread 만 하므로 경합이 없다
    if (verified != KIOTTY_SUCCESS)
    {
        return verified;
    }
    if (then.type == KIOTTY_ACTION_START_WORK)
    {
        return KIOTTY_INVALID_ARGUMENT;
    }
    IoAction outcome = IoAction::from(then);
    if (then.type == KIOTTY_ACTION_WRITE)
    {
        KiottyNetCode staged = Sender::stage(_plain.send, then.as.write.data);   // WORKING 동안 send Stage 는 worker 의 것. 데이터를 가진 thread 가 stage 한다 (adopt 와 같은 규칙)
        if (staged != KIOTTY_SUCCESS)
        {
            return staged;                      // BUFFER_FULL. Stream 은 유효하게 남는다
        }
        outcome = IoAction::hold(then.timeout_ms);                  // adopt 와 같다. stage 된 WRITE 는 read lane 에서 HOLD 다
    }
    _work.finish(outcome);                      // → DONE. stepLocalEvents 가 WORK_DONE 에 실어 drive 로 보낸다. wake 는 Owner::closeStream 이 (9.8)
    if (_phase == Phase::CLOSING)
    {
        return KIOTTY_CLOSED;                   // Owner 가 먼저 닫았다. Work 는 끝났고 settle 이 반납한다
    }
    return KIOTTY_SUCCESS;
}
```

- WORKING 동안 IO Thread 가 이 연결에서 만지는 것은 notify Stage (lock 안의 stage) 와 `_phase` (Owner close 의 shutdown) 뿐이다. deadline 은 0, drain 는 pickWrite 의 둘째 줄에서 돌아가고, 어떤 submit 도 없다.
- 그래서 WORKING 동안 두 event 는 비어 있다. REQUESTED 가 그것을 보장한다 (10절 WORK_READY 의 조건). IOCP 에서 동기 recv / send 와 overlapped 요청을 섞지 않는 조건이기도 하다.
- REQUESTED 동안 cipherSend 에 남을 수 있는 OpenSSL 출력 (handshake 끝의 NewSessionTicket, KeyUpdate 답장) 은 Stream 의 모든 동기 함수가 첫 걸음으로 flush 하므로 worker 의 첫 read / write 때 나간다. IO 를 하지 않는 Work 뒤에는 WORK_DONE 다음의 drain 이 보낸다.
- Owner close / IoLoop shutdown 이 WORKING 인 연결에 하는 것은 `apply(CLOSE)` (onDisconnected, CLOSING) 와 `shutdown()` (FIN) 이다. socket.close 는 settle 이 `!working()` 을 본 뒤에 한다. worker 의 Stream read 는 0 (CLOSED), write 는 오류를 받아 끝난다. `isConnectionStreamOpen` 은 CLOSING 이면 false.
- 평문의 Stream write 는 복사 0 이다. 큰 것은 writeFile 로 send_capacity 씩 읽어 보낸다. TLS 는 encode 한 번이 어느 설계에서도 피할 수 없는 복사다.
- `KIOTTY_BUSY` ("IO Thread 에서 부르면") 는 Owner 의 readStream 등이 `_loop.isIoThread()` 로 먼저 거른다. Connection 과 ConnectionStream 은 thread 를 모른다.

### 9.8 ConnectionHost, notify, broadcast

host 가 연결 풀과 notify 의 mutex 를 든다. wake 는 Owner 가 한다 (listener 는 Owner 의 멤버고 host 는 listener 를 모른다).

```cpp
template <size_t N>
class ConnectionHost                            // connection/kiotty_connection_host.h
{
public:
    void initialize(ConnectionLimits limits);   // Owner::initialize 가. 이 뒤로 상수
    Result<KiottyNetCode, Connection&> acquire(ConnectionConfiguration&& configuration);   // IO Thread. EMPTY 칸 → Connection::initialize. 실패면 reset 하고 code
    void          release(Connection& c);       // IO Thread. lock 안에서 c.reset() → EMPTY. notify Stage 를 비우는 줄이 lock 안이어야 해서 (아래)
    Connection&   at(int32_t slot);
    int32_t       capacity() const;
    bool          full() const;
    bool          empty() const;
    std::mutex&   notificationMutex();          // IoLoop 가 dispatch / pickWrite 에 넘긴다
    // 아무 thread
    KiottyNetCode notify(PoolHandle id, KiottyNetSpan data);      // lock → find (generation) → alive → stageNotification
    KiottyNetCode broadcast(KiottyNetSpan data);                  // lock 안에서 전부 검사 → 전부 stage. 하나라도 모자라면 아무 데도 안 쓴다
    KiottyNetConnectionInfo info(PoolHandle id) const;            // 없으면 빈 값
    // worker
    Result<KiottyNetCode, ConnectionStream> stream(PoolHandle id);                          // find → c.stream(generation). 없으면 PEER_CLOSED
    KiottyNetCode closeStream(PoolHandle id, const KiottyNetConnectionAction& then);
private:
    std::mutex                  _notification;  // 모든 연결의 notify Stage 를 한 mutex 가 지킨다
    ConnectionLimits            _limits;
    ReusablePool<Connection, N> _connections;
};

KiottyNetCode ConnectionHost::notify(PoolHandle id, KiottyNetSpan data)    // 아무 thread
{
    std::lock_guard<std::mutex> lock(_notification);
    Connection* c = _connections.find(id);
    if (c == nullptr || !c->alive())
    {
        return KIOTTY_PEER_CLOSED;
    }
    return c->stageNotification(data);          // 넘치면 BUFFER_FULL 이고 아무것도 넣지 않는다
}

KiottyNetCode Server::notify(int64_t id, KiottyNetSpan data)             // 아무 thread. base.cpp 의 notifyConnection 이 handle.owner 로 부른다 (15절)
{
    KiottyNetCode staged = _host.notify(PoolHandle::fromValue(id), data);
    if (staged != KIOTTY_SUCCESS)
    {
        return staged;
    }
    _listener.wake();                           // 늘 깨운다. IO Thread 안이면 WAKE 하나가 더 돌 뿐이다
    return KIOTTY_SUCCESS;
}

KiottyNetCode Server::closeStream(int64_t id, const KiottyNetConnectionAction& then)   // worker
{
    KiottyNetCode closed = _host.closeStream(PoolHandle::fromValue(id), then);
    _listener.wake();                           // DONE 이 됐으면 stepLocalEvents 가 집는다. 아니었어도 WAKE 하나뿐이다
    return closed;
}
```

- notify 는 "넣고 깨운다" 로 끝난다. 언제 나가는지는 drain 의 pickWrite 가, 다 나간 뒤 치우는 것은 onSent(NOTIFY) 가 한다. 세 자리가 각자 한 가지만 한다.
- WORKING 인 연결의 notify 는 쌓이기만 한다. pickWrite 가 working 에서 돌아가므로 특별한 코드가 없다. WORK_DONE 다음의 drain 이 집어 간다.
- 암호화는 IO Thread 의 pickWrite 에서만. `SSL*` 은 WORKING 이 아닌 동안 IO Thread 의 것, WORKING 동안 worker 의 것이다. 두 thread 가 동시에 만지는 순간이 없다.
- `ConnectionHost::release` 가 lock 을 잡고 `reset()` 을 부르는 이유: 다른 thread 가 `alive()` 를 통과한 뒤 CLOSING 이 될 수 있기 때문이다. 그 stage 는 lock 안에서 끝나고, reset 의 clear 가 그 뒤에 lock 을 잡는다.
- EventClient 도 같은 모양이다 (host 는 `<1>`). StreamClient 는 연결 handle 을 내지 않으므로 Owner 의 일곱 함수가 전부 `KIOTTY_INVALID_ARGUMENT` 다.

### 9.9 Acceptor

v2 9.8 그대로에 `listening()` 하나를 더한다. open 하지 않았거나 close 된 Acceptor 는 거짓이고, settle 과 step(ACCEPT) 가 그것을 본다. EventClient 는 open 하지 않는 Acceptor 를 하나 들고 IoLoop::run 에 넘긴다 (10절). 그래서 run 의 인자에 nullptr 가 없다.

## 10. io_loop

```cpp
class IoLoop                                    // 멤버 둘. 둘 다 다른 thread 가 읽거나 쓴다. 나머지는 run 의 인자로 받아 아래로 참조로 내려간다. TLS 메모리는 모른다 (19.5)
{
public:
    KiottyNetCode run(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler, bool stop_when_empty);
    void          stop();                       // 아무 thread. _stopped. 부른 쪽이 listener.wake()
    bool          isIoThread() const;           // 아무 thread. Stream 의 BUSY
    void          admit(ConnectionHost& host, EventListener& listener, TlsProfile& profile, ActiveSocket&& socket, const char* hostname, const Handler& handler);   // EventClient 가 run 전에
private:
    void     runOnce(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler, bool stop_when_empty);
    void     drain(ConnectionHost& host, EventListener& listener);                                   // slot 마다 pickWrite → submit (write lane)
    void     settle(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, bool stop_when_empty);   // slot 반납, acceptor 재무장, stop_when_empty
    Span<const IoCompletion> collectIoEvents(ConnectionHost& host, EventListener& listener);         // wait. OS 완료만. 배열은 listener 의 것
    void     step(const IoCompletion& completion, ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler);   // dispatch → apply → submit (read lane, lifecycle)
    bool     stepLocalEvents(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler);   // slot 마다 TIMEOUT / WORK_READY / WORK_DONE 을 만들어 바로 step. 하나라도 했으면 true
    void     shutdown(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler);
    IoAction access(const IoCompletion& completion, ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler);
    void     submit(IoSource& source, const IoAction& action, EventListener& listener);
    uint32_t waitTimeout(ConnectionHost& host, uint64_t now);

    std::atomic<bool>            _stopped;
    std::atomic<std::thread::id> _io_thread;   // run 의 첫 줄이 적고 끝에 비운다. thread::id 는 trivially copyable 이라 atomic 이 된다
};
```

step 의 인자가 여섯인 것은 그 함수가 그만큼 필요하다는 뜻이고, 그것을 보이는 것이 목적이라 struct 로 묶지 않는다. 참조 넷 (host, listener, acceptor, profile) 중 "없음" 이 있던 둘은 객체의 상태로 답한다: `acceptor.listening()` (EventClient 와 close 된 Server 는 거짓), `profile.valid()` (평문이면 거짓).

한 바퀴. 다섯 프로세스의 input 과 output:

| 프로세스 | input | output | listener | handler |
|---|---|---|---|---|
| drain | 연결마다의 Stage 와 write event | write lane 의 요청 (연결당 최대 하나) | submit (WRITE) | 없음 |
| settle | host | slot 반납, acceptor 요청, `_stopped` | submit (ACCEPT) | 없음 |
| collectIoEvents | 가장 가까운 deadline | OS 완료의 `Span<IoCompletion>` (listener 의 배열) | wait | 없음 |
| step | IoCompletion 하나 | read lane 의 요청 하나 또는 lifecycle 변화, Stage 에 쌓인 바이트 | submit (READ / ACCEPT), detach (CLOSE) | 여기만 |
| stepLocalEvents | step 뒤의 연결 상태 (deadline, Work) | slot 마다 local completion 을 만들어 step 한 결과. 하나라도 있었는가 (bool) | step 과 같다 | step 과 같다 |

```cpp
KiottyNetCode IoLoop::run(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler, bool stop_when_empty)
{
    _io_thread = std::this_thread::get_id();
    if (acceptor.listening())
    {
        submit(acceptor, IoAction::accept(), listener);
    }
    while (!_stopped)
    {
        runOnce(host, listener, acceptor, profile, handler, stop_when_empty);
    }
    shutdown(host, listener, acceptor, profile, handler);
    _io_thread = std::thread::id();
    return KIOTTY_SUCCESS;
}

void IoLoop::runOnce(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler, bool stop_when_empty)
{
    drain(host, listener);                      // Stage 에 있는 것을 건다. 앞 바퀴의 step 과 run 전의 admit 이 남긴 것
    settle(host, listener, acceptor, stop_when_empty);
    Span<const IoCompletion> arrived = collectIoEvents(host, listener);
    for (const IoCompletion& completion : arrived)
    {
        step(completion, host, listener, acceptor, profile, handler);
    }
    bool stepped = true;
    while (stepped)
    {
        stepped = stepLocalEvents(host, listener, acceptor, profile, handler);   // 아무것도 만들지 않을 때까지. 상한은 slot 당 셋 (아래)
    }
}

uint32_t IoLoop::waitTimeout(ConnectionHost& host, uint64_t now)   // 가장 가까운 deadline. 다른 thread 가 적는 것은 전부 wake 하므로 그것만 본다
{
    uint32_t timeout = EventListener::WAIT_FOREVER;
    for (int32_t i = 0; i < host.capacity(); ++i)
    {
        timeout = nearer(timeout, host.at(i).deadline(), now);
    }
    return timeout;
}

Span<const IoCompletion> IoLoop::collectIoEvents(ConnectionHost& host, EventListener& listener)
{
    uint32_t timeout = waitTimeout(host, monotonicMs());
    return listener.wait(timeout);              // 꺼낸 완료의 event 는 여기서 pending 이 내려간다. 배열은 listener 가 open 때 만든 것
}

bool IoLoop::stepLocalEvents(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler)
{                                               // OS 완료를 다 step 한 뒤의 상태로 만든다. 그래서 CLOSE 뒤의 연결은 deadline 0, REQUESTED 아님. 배열 없이 만들어 바로 step
    bool stepped = false;
    uint64_t now = monotonicMs();
    for (int32_t i = 0; i < host.capacity(); ++i)
    {
        Connection& c = host.at(i);
        if (c.expireDeadline(now))
        {
            step(IoCompletion::timeout(i), host, listener, acceptor, profile, handler);
            stepped = true;
        }
        if (c.workReady())                      // REQUESTED 이고 두 event 가 비었다. 이 바퀴의 SEND 완료가 비운 것도 보인다
        {
            step(IoCompletion::workReady(i), host, listener, acceptor, profile, handler);
            stepped = true;
        }
        if (c.workFinished())
        {
            IoAction outcome = c.takeWorkOutcome();                 // DONE → IDLE 은 여기서. CLOSING 이어도 outcome 은 꺼내고 dispatch 가 버린다
            step(IoCompletion::workDone(i, outcome), host, listener, acceptor, profile, handler);
            stepped = true;
        }
    }
    return stepped;
}

void IoLoop::step(const IoCompletion& completion, ConnectionHost& host, EventListener& listener, Acceptor& acceptor, TlsProfile& profile, const Handler& handler)
{
    IoSource* source = nullptr;
    IoAction  action = IoAction::none();
    switch (completion.kind)
    {
    case IoKind::WAKE:
        return;
    case IoKind::ACCEPT:
        source = &acceptor;
        action = access(completion, host, listener, acceptor, profile, handler);
        break;
    default:
    {
        Connection& c = host.at(completion.slot);
        source = &c;
        action = c.dispatch(completion, handler, host.notificationMutex());
        break;
    }
    }
    submit(*source, action, listener);
}

void IoLoop::submit(IoSource& source, const IoAction& action, EventListener& listener)
{
    switch (action.kind)
    {
    case IoActionKind::NONE:
    case IoActionKind::HOLD:
    case IoActionKind::START_WORK:
        return;
    case IoActionKind::CLOSE:
        source.detach(listener);                // 걸린 요청은 CANCELLED 완료로 돌아온다
        source.shutdown();                      // FIN. socket.close 는 settle 의 release
        return;
    case IoActionKind::READ:
    case IoActionKind::READ_UNTIL:
    case IoActionKind::WRITE:                   // WRITE 는 drain 의 pickWrite 에서만 온다. step 의 output 에는 WRITE 가 없다 (9.5)
    case IoActionKind::ACCEPT:
        listener.submit(source.eventFor(action), action, source.resolve(action));
        return;
    }
}

void IoLoop::drain(ConnectionHost& host, EventListener& listener)   // write lane. slot 마다 "걸 수 있고 보낼 것이 있으면" 하나
{
    for (int32_t i = 0; i < host.capacity(); ++i)
    {
        Connection& c = host.at(i);
        submit(c, c.pickWrite(host.notificationMutex()), listener);
    }
}

void IoLoop::settle(ConnectionHost& host, EventListener& listener, Acceptor& acceptor, bool stop_when_empty)
{
    for (int32_t i = 0; i < host.capacity(); ++i)
    {
        Connection& c = host.at(i);
        if (c.phase() != Phase::CLOSING)
        {
            continue;
        }
        if (c.readEvent().pending() || c.writeEvent().pending() || c.working())
        {
            continue;                           // 늦은 완료가 돌아오거나 worker 가 Stream 을 놓을 때까지
        }
        host.release(c);                        // lock 안의 reset : socket.close, Stage 비움, IDLE. generation 은 풀이 올린다
    }
    if (acceptor.listening() && !_stopped && !acceptor.event().pending() && !host.full())
    {
        submit(acceptor, IoAction::accept(), listener);
    }
    if (stop_when_empty && host.empty())
    {
        _stopped = true;
    }
}
```

- `access`, `admit`, `shutdown` 은 v2 10절 그대로에 인자가 붙은 것이다. `access` 는 `host.acquire({ socket, context, "", host.limits(), profile })` 뒤 `onOpened(handler)` 의 action 을 돌려준다. `admit` 은 hostname 을 넣고 `onOpened` 의 action 을 submit 한다 (TLS 서버는 READ(HANDSHAKE), TLS 클라이언트는 NONE 이고 ClientHello 는 cipherSend 에 남는다). EventClient 는 run 전에 admit 하므로 첫 runOnce 의 drain 이 그것을 건다. 바퀴의 첫 프로세스가 drain 인 이유가 이것이다: **wait 에 들어갈 때 Stage 에 걸리지 않은 바이트가 없다.**
- `shutdown` 은 alive 인 연결마다 `apply(CLOSE)` + `submit(CLOSE)` 뒤 `while (!host.empty()) runOnce(...)` 다. WORKING 인 연결은 worker 의 closeStream 이 올 때까지 그 while 이 돈다. IO 를 하지 않는 worker 가 closeStream 을 부르지 않으면 run 은 돌아오지 않는다. 이것은 Work 의 계약 ("받았으면 반드시 close 한다") 이고 Owner::shutdown 의 문서에 적는다.
- stepLocalEvents 가 step 뒤에 있는 이유: 같은 배치의 OS 완료가 연결을 닫거나 (CLOSE 뒤의 TIMEOUT 은 생기지 않는다. deadline 이 0 이다), 새 action 을 걸거나 (새 deadline 이 미래라 TIMEOUT 이 생기지 않는다), write event 를 비울 (REQUESTED 가 WORK_READY 가 된다) 수 있기 때문이다. local completion 은 "지금 상태" 에서 만들어야 거짓이 없다. 그래도 남는 늦은 것 (CANCELLED 완료) 은 dispatch 의 첫 줄이 받는다.
- local completion 을 모아 두지 않고 slot 마다 만들어 바로 step 하는 이유: 모을 이유가 "OS 완료를 다 step 한 뒤의 상태에서 만든다" 였고, 그것은 OS 완료 loop 가 끝난 뒤에 scan 하면 그대로 성립한다. slot i 의 step 이 slot j 의 deadline 이나 Work 를 바꾸는 길이 없으므로 (콜백이 다른 연결에 할 수 있는 것은 notify 뿐이고 그것은 Stage 에 넣는 것이다) 순서도 같다. 그래서 3 × N 배열이 멤버에도 stack 에도 없다.
- stepLocalEvents 를 "아무것도 만들지 않을 때까지" 반복하는 이유: local completion 의 step (TIMEOUT 의 onTimeout, WORK_DONE 뒤의 drive) 이 `thenWork` 를 돌려주면 REQUESTED 가 되는데, 한 번만 scan 하면 그것을 WORK_READY 로 만드는 자리가 이미 지나갔고 다음 바퀴의 wait 는 deadline 만 보므로 (REQUESTED 는 deadline 0) 다른 OS 완료가 올 때까지 Work 가 시작되지 않는다. 끝나는 근거: 한 바퀴 안에서 slot 의 상태는 한 방향으로만 간다 (deadline 만료 → 0, REQUESTED → WORKING, DONE → IDLE). WORK_DONE → drive → START_WORK → REQUESTED → WORK_READY → WORKING 이 가장 긴 사슬이고, WORKING 에서 나가는 것은 worker thread 다. 그래서 slot 당 step 은 최대 셋, 전체 3 × N 이다.
- 이것은 completion 사이의 우선순위가 아니다. 규칙은 하나다: **OS 가 만든 것 먼저, 우리가 만든 것은 그 뒤의 상태에서.** 한 scan 에서 같은 slot 의 local completion 셋이 함께 나오는 일은 없다 (Work 중에는 deadline 이 0, REQUESTED 와 DONE 은 다른 상태).
- WORK_READY 는 같은 runOnce 안에서 처리된다. thenWork 를 return 한 콜백이 끝나고 stepLocalEvents 가 바로 뒤에 돌므로 사이에 wait 가 없다. 기다리는 경우는 하나, write event 에 notify 가 걸려 있을 때 (W=1) 그 SEND 완료 하나다. 그 완료가 step 되면 같은 runOnce 의 stepLocalEvents 가 WORK_READY 를 만든다.
- step 에서 WRITE 를 걸지 않는 것은 예외가 아니라 규칙이다. **action 의 kind 가 어느 lane 인지 정한다.** READ 계열과 ACCEPT 는 step 이, WRITE 는 drain 이 건다. 콜백의 thenWrite, worker 의 closeStream(thenWrite), 다른 thread 의 notify, OpenSSL 의 handshake / control 이 전부 "Stage 에 넣는다" 로 끝나고, 거는 쪽은 drain 한 자리다.
- 콜백 안의 notify 와 thenWrite 는 다음 runOnce 의 첫 프로세스 (drain) 에서, wait 전에 나간다. 다른 thread 의 notify 는 wake → 그 다음 runOnce 의 drain 다.
- 상태를 읽어 판단하는 `pending()` 은 pickWrite (write event), onTimeout (read event), stepLocalEvents 의 workReady (둘), settle (둘) 넷이다.

TLS 서버 연결의 바퀴 (요청 하나, 응답 하나, 중간에 다른 thread 의 notify). TLS 1.3 이라 handshake 끝에 NewSessionTicket 이 한 번 더 나간다:

| 바퀴 | drain | collectIoEvents | step |
|---|---|---|---|
| 1 | 없음 | ACCEPT | admit → onConnected → open (HANDSHAKING) → attach → onHandshake RECEIVE → READ(HS) 를 read event 에 |
| 2 | 없음 | RECEIVE (HS, ClientHello) | commit → onHandshake SEND → NONE (ServerHello..Finished 가 cipherSend 에) |
| 3 | cipherSend 있음 → WRITE(HANDSHAKE) | SEND (HS) | cipherSend consume → onHandshake RECEIVE → READ(HS) |
| 4 | 없음 | RECEIVE (HS, client Finished) | onHandshake SEND → NONE (DONE 이지만 NewSessionTicket 이 cipherSend 에 있어 SEND 가 먼저) |
| 5 | WRITE(HANDSHAKE) | SEND (HS) | onHandshake DONE → READY → onReady → thenRead(4) → drive → refill (SSL 에 평문 없음) → READ(USER) |
| 6 | 없음 | RECEIVE (USER) | commit → drive → refill: decode 4 바이트 → 찼음 → onRead → thenWrite(응답) → adopt: `_send.stage` → HOLD (deadline) |
| 7 | `_send` 있음 → encode → WRITE(USER, plain 4) → submit | WAKE (다른 thread 의 notify. `_notify` 에 2 바이트) | 없음 |
| 8 | write pending → NONE | SEND (USER) | cipherSend consume → `Sender::release(_plain.send, 4)` → onWritten(4) → thenRead(4) → drive → refill 0 → READ(USER) |
| 9 | `_notify` 있음 → encode → WRITE(NOTIFY, plain 2) | (대기) | |
| 10 | 없음 | SEND (NOTIFY) | cipherSend consume → `Sender::release(_plain.notify, 2)` → NONE |

notify 가 나가는 중에 thenWrite 가 들어오는 경우 (7 과 9 의 순서가 바뀐 것): WRITE(NOTIFY) 가 걸린 채 onRead 가 thenWrite 를 돌려주면 adopt 의 `_send.stage` 와 deadline 까지만 일어나고, 다음 drain 은 pending 이라 NONE. SEND(NOTIFY) 가 `_notify.release`, 그 다음 drain 이 WRITE(USER). peer 는 `notify 전체, 응답 전체` 순서로 받는다. 분기가 없다. pending 이 아니었다면 그 drain 이 바로 걸었을 뿐이다.

## 11. TIMEOUT 과 CLOSE

TIMEOUT 뒤가 "진행 중인 사용자 action 위에 다른 action" 이 생길 수 있는 유일한 자리다. 보는 것은 `userActionPending()` 하나다: `_read_event.pending() || _plain.send.consumer().readable() > 0`. 앞은 READ 가 걸려 있다는 것, 뒤는 WRITE 가 (notify 뒤에서 기다리든 걸려 있든) 아직 onWritten 까지 가지 않았다는 것이다. 둘 다 그 맥락의 공개 인터페이스를 읽는다. notify 의 write event 는 사용자 action 과 무관하므로 보지 않는다.

| `userActionPending()` | handler 의 return | 일어나는 일 |
|---|---|---|
| false (HOLD 뒤) | 무엇이든 | adopt → drive → apply. WRITE 면 stage, START_WORK 면 REQUESTED |
| true | CLOSE | 닫는다. 걸린 요청은 CANCELLED 완료로 돌아오고 settle 이 반납 |
| true | HOLD, READ, READ_UNTIL | 연장. 걸린 요청 그대로, deadline 만 그 action 의 timeout_ms 뒤로 (0 이면 없음) |
| true | WRITE | onError(KIOTTY_BUSY, "action rejected: io outstanding. use notify") 한 번, 그리고 그 WRITE 의 timeout_ms 로 연장 |
| true | START_WORK | 같은 onError 한 번. START_WORK 에는 timeout 이 없으므로 deadline 은 없음 (걸린 READ / WRITE 는 완료로 끝난다) |

HANDSHAKING 의 TIMEOUT 은 onError(TLS_HANDSHAKE_TIMEOUT) → CLOSE. WORKING 중에는 deadline 이 0 이라 TIMEOUT 이 없다. extend 는 adopt 를 거치지 않으므로 거부된 WRITE 의 데이터가 Stage 에 남지 않는다.

CLOSE 의 공통 순서 (handler return, onPeerGone, handshake 실패, worker 의 closeStream(thenClose), Owner close 모두):

1. `apply(CLOSE)` : `_deadline = 0`, `_phase = CLOSING`, `_work.abandon()`, `onDisconnected` (accept 된 연결은 반드시 한 번)
2. `submit(CLOSE)` : `detach(listener)`, `shutdown()` (FIN)
3. 걸려 있던 요청마다 CANCELLED 완료 (IOCP 는 OS 가, epoll 은 detach 가) → dispatch 첫 줄이 CLOSING 을 보고 NONE. WORKING 이었으면 worker 의 Stream 이 끊김을 보고 closeStream → DONE → stepLocalEvents 가 IDLE 로 → WORK_DONE 은 NONE
4. `settle` : 두 event 가 비었고 working 이 아니면 `host.release` → socket.close, Stage 비움 (notify 는 lock 안), generation++, IDLE

## 12. server, client

v2 12절 그대로. 바뀌는 것:

- `StreamClient` 는 `sendAll / receiveSome` 대신 `ConnectionStream` 을 쓴다. 멤버로 두지 않고 connect / read / write / disconnect 마다 `ConnectionStream stream(_socket, _tls, _receive, _send)` 를 그 자리에서 만든다 (생성자는 참조 넷을 묶는 것뿐이다). `connect` 는 connectWithRetry 뒤 TLS 면 `stream.handshake(connect_timeout_ms)`. 그래서 Connection 의 WORKING 과 StreamClient 가 같은 코드다.
- `Owner::shutdown` 은 `close + run 의 return 대기` 그대로. WORKING 인 연결이 있으면 그 worker 가 closeStream 할 때까지 run 이 돌아오지 않는다. 이것을 공개 문서 (requirement 7절 close) 에 적는다.
- await 는 `Parker` 하나다 (core/kiotty_parker.h). mutex 와 condition_variable 이 있는 유일한 자리이고, lock 의 범위는 park / unpark 의 본문이다. Context 의 create / destroy mutex 와 host 의 notify mutex 는 기다리는 것이 아니라 잠그는 것이라 `std::mutex` 그대로다.

```cpp
class Parker
{
public:
    template <class Ready>
    bool park(Ready ready, uint64_t timeout_ms);        // lock → ready() 가 참이 될 때까지 wait (0 이면 무한) → unlock. 돌아올 때의 ready()
    template <class Change>
    void unpark(Change change);                         // lock → change() → unlock → notify_all. ready() 가 읽는 것은 change() 가 적는다
private:
    std::mutex              _mutex;
    std::condition_variable _cv;
};

KiottyNetCode Server::run(const KiottyNetConnectionHandler& callbacks, KiottyNetAppContext app_context)   // run 을 부른 thread 가 IO Thread
{
    Handler handler(callbacks, app_context, *this);
    KiottyNetCode code = KIOTTY_SUCCESS;
    {
        TlsMemory::Scope scope(_tls_memory);            // attachThread 포함 (19.5). 이 thread 의 OpenSSL 할당을 이 Owner 의 서브풀로
        code = _loop.run(_host, _listener, _acceptor, _profile, handler, false);
    }
    TlsMemory::detachThread();                          // IO Thread 가 끝나는 자리 둘 (Server, EventClient) 에만
    _parker.unpark([this, code]() { _code = code; _finished = true; });
    return code;
}

KiottyNetCode Server::await(uint64_t timeout_ms)
{
    bool finished = _parker.park([this]() { return _finished; }, timeout_ms);
    if (!finished)
    {
        return KIOTTY_TIMEOUT;
    }
    return _code;
}
```

- EventClient 의 run 은 `_loop.admit(...)` 뒤 `_loop.run(_host, _listener, _acceptor, _profile, handler, true)` 다. `_acceptor` 는 open 하지 않은 것이다 (9.9).
- `close` 는 `_loop.stop()` 뒤 `_listener.wake()`. `broadcast` 는 `_host.broadcast(data)` 뒤 `_listener.wake()`.

## 13. context, config

v2 13절 그대로. 바뀌는 것:

- Context 는 process 에 하나인 static 객체다. heap 을 쓰지 않고, DLL 사본마다 하나다. 그것에 닿는 함수는 `Context::open()` 하나이고 그것을 부르는 자리는 `KiottyNet_initializeContext` 하나다. 라이브러리 안의 어느 코드도 static 객체를 직접 찾지 않는다 (singleton 접근 없음). 그 뒤의 모든 C 함수는 받은 것 (context, Owner 포인터, handle 의 owner) 을 통해 간다 (15절).
- `initializeContext` 를 두 번 부르면 같은 객체와 `KIOTTY_SUCCESS` 다. release 는 한 번으로 끝난다. 두 모듈이 각자 release 하면 첫 release 가 둘 다 끝내는데, 그것이 "Context 둘은 없다" 의 뜻이다.
- OpenSSL 초기화 (`OPENSSL_INIT_NO_ATEXIT`, process 첫 번째만), hook (`TlsMemory::functions()`), 예열 handshake 가 `open` 에서 돈다 (19.5). Owner 는 `create*` 가 만들고, `KIOTTY_OUT_OF_MEMORY` 는 `create*` 가 돌려준다. `KiottyNet_initializeContext(void)` 는 인자를 받지 않는다. `open` 이 받는 `ContextConfiguration` 은 라이브러리 안의 struct 이고 base.cpp 가 기본값 (타입별 미리 만들 개수 0) 으로 넘긴다. 공개 config 는 필요해지면 그때 base.h 에 더한다.
- `config::configureServerMaxClients` 가 `[1, KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER]` 를 검사한다. 기본 128. buffer capacity 는 `[1, KIOTTY_NET_MAX_*_CAPACITY]`.
- base.h 의 불투명 struct 는 C++ 클래스 그 자체다 (cast 없음): `KiottyNetContextImpl` 은 Context, `KiottyNetConnectionOwner` 는 Owner, `KiottyNetServer` / `KiottyNetEventClient` / `KiottyNetStreamClient` 는 Owner 셋. 전역 namespace 의 그 이름으로 정의하고 `kiotty::net` 안에서는 typedef 로 부른다.

```cpp
// context/kiotty_owner.h
struct KiottyNetConnectionOwner                 // 데이터 없음. 연결 handle 로 오는 호출 일곱. Server 와 EventClient 가 구현하고 StreamClient 는 전부 INVALID_ARGUMENT
{
    virtual ~KiottyNetConnectionOwner();
    virtual KiottyNetCode           notify(int64_t id, KiottyNetSpan data) = 0;
    virtual KiottyNetConnectionInfo info(int64_t id) const = 0;
    virtual KiottyNetResult         readStream(int64_t id, void* buffer, size_t length, uint32_t timeout_ms) = 0;
    virtual KiottyNetResult         writeStream(int64_t id, KiottyNetSpan data, uint32_t timeout_ms) = 0;
    virtual KiottyNetCode           writeStreamFile(int64_t id, const char* path, uint64_t offset, uint64_t length, uint32_t timeout_ms) = 0;
    virtual bool                    isStreamOpen(int64_t id) = 0;
    virtual KiottyNetCode           closeStream(int64_t id, const KiottyNetConnectionAction& then) = 0;
};
namespace kiotty { namespace net { typedef ::KiottyNetConnectionOwner Owner; } }

// context/kiotty_context.h
struct KiottyNetContextImpl                     // = Context. 복사 / 이동 없음
{
    static Result<KiottyNetCode, Context&> open(const ContextConfiguration& configuration);   // 유일한 static 함수. 함수 안의 static 객체 하나를 initialize 하고 돌려준다 (C++11 의 thread-safe static init)
    void release();                             // 남은 Owner destroy → 세 풀 비움 → TlsMemory 의 등록표 지움. OPENSSL_cleanup 은 부르지 않는다

    Result<KiottyNetCode, Server&>       createServer(const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig* tls);
    Result<KiottyNetCode, StreamClient&> createStreamClient(const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig* tls);
    Result<KiottyNetCode, EventClient&>  createEventClient(const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig* tls);
    void destroy(Server& server);
    void destroy(StreamClient& client);
    void destroy(EventClient& client);
private:
    Context();
    LazyReusablePool<Server,       MAX_SERVERS>        _servers;
    LazyReusablePool<StreamClient, MAX_STREAM_CLIENTS> _stream_clients;
    LazyReusablePool<EventClient,  MAX_EVENT_CLIENTS>  _event_clients;
    std::mutex                                         _mutex;        // open / release / create / destroy
    bool                                               _initialized;
};
```

- handle: 3절의 struct `{ owner, id }`. `id` 는 `generation (32) << 32 | slot (16)` 이고 양수다. Owner 는 destroy 로 지워지지 않고 은퇴하므로 (19.1 #4) `owner` 포인터는 releaseContext 전까지 유효하고, 틀린 handle 은 connection generation 이 거른다 (`ReusablePool::find`). Owner 의 reset 을 지나도 Connection 의 generation 은 0 으로 돌아가지 않는다 (generation 은 풀의 slot 에 붙어 있다). releaseContext 뒤의 handle 은 context 포인터와 똑같이 dangling 이고 사용자 오류다. Stream handle 은 같은 값의 다른 struct 타입이라 Connection handle 을 Stream 함수에 넘기는 것은 compile 되지 않는다.

## 14. thread 규칙

| 클래스 | 부르는 thread |
|---|---|
| Context | 아무 thread (mutex). 소멸자 (process 종료) 는 run 을 부른 thread 가 아니어야 한다 |
| Server, EventClient | `run` 은 IO Thread 가 되는 thread. `close`, `await`, `broadcast`, `notify`, `info` 는 아무 thread. Stream 함수 다섯은 worker (IO Thread 면 `isIoThread()` 로 BUSY) |
| IoLoop | `run` IO Thread. `stop`, `isIoThread` 아무 thread |
| ConnectionHost | `notify`, `broadcast`, `info` 는 아무 thread (lock). `stream`, `closeStream` 은 worker. 나머지 IO Thread |
| Connection | `info`, `alive`, `context`, `state` 는 아무 thread. `stageNotification`, `notificationWritable` 은 host 의 lock 안. `stream`, `closeStream` 은 worker. 나머지 IO Thread |
| Sender::stage / release (send Stage) | RUNNING 이면 IO Thread, WORKING 이면 worker. 동시에 둘인 순간은 없다 |
| Sender::stage / release (notify Stage) | stage 는 아무 thread, release 는 IO Thread. 전부 host 의 mutex 안 |
| TlsEngine, socket | WORKING 이 아닌 동안 IO Thread, WORKING 동안 worker. `SSL*` 도 같다 |
| ConnectionStream | Connection 의 것은 worker 하나, StreamClient 의 것은 한 번에 한 thread. 쓰는 함수가 만든다 |
| TlsMemory | `Scope` 는 그 thread 의 할당 경로. attach 는 Scope 가, `detachThread` 는 IO Thread 가 끝나는 run 의 끝이 (19.5) |
| Parker | `park` 는 await 를 부른 thread, `unpark` 는 run 의 끝 |
| EventListener | `wake` 만 아무 thread |

다른 thread 의 호출: `closeStream` 은 verify → (WRITE 면 send Stage 복사) → CAS → Owner 의 wake. `notify` / `broadcast` 는 lock 안에서 notify Stage 복사 → Owner 의 wake. Owner `close` 는 `_stopped` → wake. 콜백 안에서 handle 로 할 수 있는 것은 `getConnectionInfo` 와 `notifyConnection` 둘이라 재진입이 없다.

## 15. base.cpp 번역 표

static 에 닿는 줄은 `initializeContext` 하나다. 나머지는 받은 포인터 (context, Owner, `handle.owner`) 의 메서드 하나다.

| C | 번역 |
|---|---|
| `KiottyNet_initialize*Config`, `configure*` | `config::` 같은 이름. NULL 이면 INVALID_ARGUMENT |
| `thenHold`, `thenRead`, `thenReadUntil`, `thenWrite`, `thenWork`, `thenClose`, `acceptConnection`, `ignoreConnection` | `action::` 같은 이름 |
| `initializeContext()` | `Context::open(config)`. code 와 `&value()` |
| `releaseContext(ctx)` | `ctx->release()` |
| `createServer(ctx, config, tls)` 등 create 셋 | `ctx->createServer(...)`. NULL 이면 INVALID_ARGUMENT |
| `destroy*(ctx, owner)` | `ctx->destroy(*owner)` |
| `run / close / await / broadcastServer`, EventClient 의 `run / close / await / notify`, StreamClient 의 `connect / disconnect / read / write` | 같은 이름의 메서드. `run(*handler, app_context)` 은 handler NULL 을 먼저 거른다 |
| `getEventClientConnection(client)` | `client->connection()` (struct 값. 없으면 `{ NULL, 0 }`) |
| `getConnectionInfo(h)` | `h.owner == NULL` 이면 빈 값. 아니면 `h.owner->info(h.id)` |
| `notifyConnection(h, data)` | `h.owner == NULL` 이면 INVALID_ARGUMENT. 아니면 `h.owner->notify(h.id, data)` |
| `readConnectionStream(s, ...)`, `writeConnectionStream`, `writeConnectionStreamFile`, `isConnectionStreamOpen` | `s.owner == NULL` 이면 INVALID_ARGUMENT. 아니면 `s.owner->readStream(s.id, ...)` 등. Owner 가 IO Thread 면 BUSY, 아니면 `_host.stream(id)` 의 code (PEER_CLOSED / BUSY) 또는 그 Stream 의 메서드 |
| `closeConnectionStream(s, action)` | `s.owner->closeStream(s.id, action)` |

StateMachine 은 `Connection::state() / setState() / context()` 를 쓴다. C++ 의 `KiottyNet_Connection` 은 `operator==` 와 `operator<` (owner 는 `std::less`, 그 다음 id) 를 가지므로 map 의 key 가 된다. C 에서는 `owner` 와 `id` 둘을 비교한다.

## 16. 메모리 (128 client, 4 KiB 기준. 예약과 상한은 19절)

| 객체 | 잡는 것 | 크기 |
|---|---|---|
| Connection (평문) | 객체 + StageSet (recv + send + push) + IoEvent 둘 | 약 12 KiB |
| Connection (TLS) | 위 + cipherReceive (recv) + cipherSend (max(send, push) + 레코드 오버헤드 + 512) + SSL (Owner 의 TLS 서브풀, SSL_clear 재사용) | 약 21 KiB + SSL 대기 약 30 ~ 50 KiB, handshake 중 최대 약 68 KiB |
| ConnectionHost | Connection[128] + mutex | 평문 약 1.5 MiB, TLS 약 4.3 MiB |
| IoLoop | 멤버 둘. local completion 배열 없음 (OS 완료는 listener 의 배열) | 수십 B |
| EventClient | capacity 1 + open 하지 않은 Acceptor | 평문 약 13 KiB |
| Context | static 객체 하나. 타입별 LazyReusablePool 셋 (19.3). Owner 는 `create*` 가 만든다 | 수 KB. Owner 는 19.3 의 표 |

## 17. v2 에서 바뀐 것 (코드가 있는 층)

| 층 | 바꿀 것 |
|---|---|
| base.h (사이클 1) | 3절. action 함수 이름 여섯, `KiottyNetWorkHandle` → `KiottyNetConnectionStream`, onWorkStarted / onError 시그니처, Stream 함수 다섯, `initializeContextWithTlsPool`. statemachine.h 의 `KiottyNet_holdCommunication(0)` → `thenHold(0)`. C++ `KiottyNet_Work` → `KiottyNet_ConnectionStream`. requirement 5 ~ 7절, user_guide_http 1.5 |
| core (사이클 2) | 없음 |
| error, socket (사이클 3a) | 없음 |
| event (사이클 3b) | `IoAction::as.write.length`, `IoAction::write(purpose, timeout, length)`, `IoKind::WORK_READY`, `IoCompletion::workReady`, `IoCompletion::workDone(slot, outcome)`. 테스트 셋의 모양 (7절). listener 구현은 그대로 |
| 테스트 계획 (test_base_v0) | 9절 Connection 의 케이스를 9.5 / 9.6 의 표로, 10절 IoLoop 를 여섯 프로세스로. 사이클 5 시작 전에 고친다 |

## 18. 구현 순서

① base.h 를 3절대로 고치고 statemachine.h / requirement 를 맞춘다 (사이클 1 재방문) ② core/pool (ReusablePool, LazyReusablePool), Parker, event 의 세 줄 (사이클 3b 재방문) ③ epoll backend (사이클 3c) ④ tls (stub 먼저, TlsMemory) ⑤ stream (ConnectionStream, 파일 backend) ⑥ connection (IoSource, Sender, WorkContext, Connection, Acceptor, ConnectionHost) ⑦ io_loop ⑧ server / client / context / config ⑨ base.cpp 번역.

## 19. 메모리 할당

2026-11-02 ~ 03 에 정했다. 사이클 4 의 계측 (`docs/progress/261102_progress_base_cycle4.md`) 이 근거다. 코드는 아직 사이클 4 의 구조이고 사이클 5 에서 이 절로 바꾼다. 19.9 가 이 절 때문에 고치는 다른 절이다.

### 19.1 원칙

1. **객체는 한 번 만들고, 실행 중에는 켜고 끈다.** Context 는 static 객체 하나, Owner 는 `create*` 의 `unique_ptr` 하나, 그 아래 (Connection, IoEvent, Stage, TLS 서브풀, Acceptor, listener 의 완료 배열) 는 전부 Owner 의 멤버거나 Owner 의 create 때 잡은 것이다. run 중에는 생성자도 malloc 도 free 도 돌지 않는다. OpenSSL 의 할당은 19.5 의 arena 로 간다.
2. **메모리가 보장되는 시점은 `create*` 다.** Owner (Server / StreamClient / EventClient) 하나를 `new (std::nothrow)` 로 한 번 만든다. 못 만들면 `KIOTTY_OUT_OF_MEMORY`, 만들었으면 그 Owner 의 run 중에 메모리 부족은 없다. `initializeContext` 의 config 로 개수를 주면 그때 미리 만든다. Context 자체는 heap 을 쓰지 않는다.
3. **상한 곱셈을 하지 않는다.** Context 는 타입별 포인터 표만 가진다. 메모리는 실제로 만든 Owner 에 비례한다. Owner 하나의 크기는 compile time 상한 (19.4) 으로 정해지고 config 와 무관하다.
4. **은퇴한 Owner 는 OS 에 돌려주지 않는다.** destroy 는 은퇴 상태로 두고, 같은 타입의 다음 create 가 재사용하며, `releaseContext` 가 한 번에 해제한다. 표가 타입별이라 다른 타입에 자리를 내주는 free 가 없다. 그래서 handle 의 `owner` 포인터는 releaseContext 전까지 늘 유효하다 (13절).
5. **잡아 둔 메모리는 쓰기 전까지 RAM 에 올라가지 않는다.** 큰 배열은 default-init 으로 두고 생성자에서 0 으로 채우지 않는다. Linux, macOS, Android 는 처음 쓰는 page 만 RAM 에 올리고, Windows 는 commit 을 전부 센다 (19.8).
6. **Pool 은 소비의 상한이고 손상의 방어가 아니다.** 문서와 인허가 자료에서 둘을 구분해 적는다.
7. **MISRA C++:2023.** 할당은 `std::unique_ptr` 가 관리한다. raw storage 위의 placement new, `union`, 포인터 ↔ 정수 변환, 소유하는 raw 포인터를 쓰지 않는다. 소유하지 않는 raw 포인터는 쓴다. C API 의 handle 은 포인터와 정수를 나란히 둔 struct 라 변환이 없다. 동적 메모리의 advisory 규칙은 "`create*` 는 초기화 단계" 로, static 객체 하나 (Context) 와 .bss 의 전역 TLS arena 는 deviation 하나로 재분류한다. 코드는 C++11 부분집합으로 쓰고 정적 분석 job 은 C++17 모드로 돈다.
8. **멤버는 run 보다 오래 사는 상태뿐이다.** 한 함수가 쓰고 버리는 것은 그 함수의 참조 인자다. 풀 안의 객체 (Connection, Owner 셋) 는 자기 바깥을 가리키는 멤버를 갖지 않는다 (예외는 backend 의 IoEvent 가 attach 때 적는 listener 포인터 하나). 생성자에서 받는 소유하지 않는 의존은 포인터가 아니라 참조 멤버다 (ConnectionStream, Handler, TlsMemory::Scope). 포인터로 남는 것은 생성자 없이 바뀌는 값 (thread 의 current arena) 과 C 문자열이다.

### 19.2 풀 계약

풀에 들어가는 타입 `T` 는 base 를 상속하지 않는다. 계약은 멤버 typedef 와 함수 둘이고, 풀이 compile time 에 검사한다. 풀은 전부 `core/pool/` 에 있다.

```cpp
// T 가 갖출 것
typedef KiottyNetCode    code_type;
typedef XxxConfiguration configuration_type;    // initialize 에 필요한 것 전부를 담은 struct 하나
code_type initialize(PoolHandle id, configuration_type configuration);   // 풀이 id 를 준다 (core/pool/kiotty_pool_handle.h 의 { generation, index })
void      reset() noexcept;                     // 멱등. 한 번도 initialize 되지 않았거나 반쯤 켜진 상태에서도 안전
// 기본 생성자만. 복사 / 이동 없음

// core/pool/kiotty_reusable.h : 검출 idiom (C++11). ReusablePool 과 LazyReusablePool 이 static_assert 로 쓴다
template <class T> struct is_reusable
{
    static const bool value =
        std::is_default_constructible<T>::value &&
        !std::is_copy_constructible<T>::value && !std::is_copy_assignable<T>::value &&
        !std::is_move_constructible<T>::value && !std::is_move_assignable<T>::value &&
        has_initialize<T>::value &&             // decltype(declval<T&>().initialize(declval<PoolHandle>(), declval<typename T::configuration_type>())) 이 code_type
        has_reset<T>::value;                    // decltype(declval<T&>().reset()) 이 void 이고 noexcept
};

enum class SlotState : uint8_t { EMPTY, ACTIVE, RETIRED };

template <class T, size_t N>
class ReusablePool                              // core/pool/kiotty_reusable_pool.h. static_assert(is_reusable<T>::value). 객체는 풀과 같이 생성된다 (멤버 배열). 잠금은 호출자가
{                                               // acquire(configuration) → Result<code_type, T&> : EMPTY 칸 → generation++ → initialize. 실패면 reset 하고 code
    std::array<T, N>         _objects;          // release(T&) : reset, EMPTY.  find(PoolHandle) : generation 불일치면 nullptr.  all() : Span<T>
    std::array<SlotState, N> _states;           // EMPTY / ACTIVE
    std::array<int32_t, N>   _generations;      // slot 에 붙어 있다. 풀이 사는 동안 0 으로 돌아가지 않는다
    size_t                   _count;
};

template <class T, size_t N>
class LazyReusablePool                          // core/pool/kiotty_lazy_reusable_pool.h. static_assert(is_reusable<T>::value). 객체는 lazy 로 한 번 생성되고 RETIRED 로 남는다
{                                               // acquire : RETIRED → initialize. 없으면 EMPTY 에 new (std::nothrow) T → initialize. 없으면 TABLE_FULL
    std::array<std::unique_ptr<T>, N> _objects; //           새로 만든 객체의 initialize 가 실패해도 지우지 않고 RETIRED 로 둔다
    std::array<SlotState, N>          _states;  // release : reset, RETIRED.  reserve(n) : n 개를 만들어 RETIRED 로 (ContextConfiguration. 기본 0)
};
```

이름의 뜻: `StaticPool` 은 "멤버가 전부 static (.bss)" 이고, `ReusablePool` 은 "객체가 멤버 배열에 있고 켜고 끈다", `LazyReusablePool` 은 "객체를 heap 에 늦게 하나씩 만들고 은퇴시킨다" 다.

### 19.3 멤버 (Context 부터 아래로. 기능은 각 절)

멤버의 선언 순서가 생성 순서다. 멤버는 run 보다 오래 사는 상태뿐이고 (19.1 #8), 한 함수가 쓰고 버리는 것은 그 함수의 인자다. "받는 것" 열이 그 함수가 필요한 인자다.

```cpp
// context/kiotty_context.h. process 에 하나인 static 객체 (13절)
class Context
{
    LazyReusablePool<Server,       MAX_SERVERS>         _servers;
    LazyReusablePool<StreamClient, MAX_STREAM_CLIENTS>  _stream_clients;
    LazyReusablePool<EventClient,  MAX_EVENT_CLIENTS>   _event_clients;
    std::mutex                                          _mutex;
    bool                                                _initialized;
};

// server / client. Owner (KiottyNetConnectionOwner) 는 데이터가 없다 (13절). 형제를 가리키는 멤버는 없다
class Server : public Owner                     // configuration_type = ServerConfiguration { KiottyNetServerConfig config; const KiottyNetTlsServerConfig* tls; }
{
    KiottyNetServerConfig              _config;
    OwnerTlsMemory<ServerTlsLayout>    _tls_memory;   // 19.5. profile 1 + 연결 MAX_CONNECTIONS 몫. TLS OFF 면 0 바이트
    TlsProfile                         _profile;      // valid() 가 거짓이면 평문
    ConnectionHost<MAX_CONNECTIONS>    _host;
    EventListener                      _listener;     // open(LISTENER_CAPACITY) 이 완료 배열을 heap 에 (create 때)
    Acceptor                           _acceptor;
    IoLoop                             _loop;
    Parker                             _parker;
    KiottyNetCode                      _code;
    bool                               _finished;
    PoolHandle                         _id;
};

class EventClient : public Owner                // configuration_type = ClientConfiguration { KiottyNetClientConfig config; const KiottyNetTlsClientConfig* tls; }
{
    KiottyNetClientConfig              _config;       // hostname 은 여기 (char 배열). admit 이 빌려 준다
    OwnerTlsMemory<ClientTlsLayout>    _tls_memory;   // profile 1 + 연결 1 몫
    TlsProfile                         _profile;
    ConnectionHost<1>                  _host;
    EventListener                      _listener;
    Acceptor                           _acceptor;     // open 하지 않는다. listening() 이 거짓 (9.9)
    IoLoop                             _loop;
    std::atomic<bool>                  _stop;
    Parker                             _parker;
    KiottyNetCode                      _code;
    bool                               _finished;
    PoolHandle                         _id;
};

class StreamClient : public Owner               // configuration_type = ClientConfiguration. 연결 handle 을 내지 않으므로 Owner 의 일곱은 전부 INVALID_ARGUMENT
{
    KiottyNetClientConfig              _config;
    OwnerTlsMemory<ClientTlsLayout>    _tls_memory;
    TlsProfile                         _profile;
    ActiveSocket                       _socket;
    TlsEngine                          _tls;
    Stage<MAX_RECV_CAPACITY>           _receive;
    Stage<MAX_SEND_CAPACITY>           _send;
    std::atomic<bool>                  _stop;
    PoolHandle                         _id;
};                                              // ConnectionStream 은 함수마다 그 자리에서 만든다 (12절)

// connection
template <size_t N>
class ConnectionHost                            // 9.8. initialize(limits). acquire(configuration). release(c) 가 lock 안에서 c.reset()
{
    std::mutex                         _notification;  // 모든 연결의 notify Stage 를 지킨다. notificationMutex() 로 IoLoop 에
    ConnectionLimits                   _limits;
    ReusablePool<Connection, N>        _connections;
};

class Connection : public IoSource              // configuration_type = ConnectionConfiguration (9.4). 자기 바깥을 가리키지 않는다
{
    std::atomic<Phase>          _phase;
    uint64_t                    _deadline;
    WorkContext                 _work;
    IoEvent                     _read_event;    // backend 의 listener 포인터는 19.1 #8 의 예외
    IoEvent                     _write_event;
    StageSet                    _plain;         // Stage<N> 셋. limit 은 initialize 가
    TlsEngine                   _tls;           // cipher Stage 둘 + SSL
    ActiveSocket                _socket;
    PoolHandle                  _id;
    KiottyNetConnectionContext  _context;
    KiottyNetConnectionInfo     _info;
    int32_t                     _state;
};

struct Handler                                  // 9.4. run 의 stack 에 하나. 멤버가 아니다
{
    const KiottyNetConnectionHandler& callbacks;
    KiottyNetAppContext               app_context;
    Owner&                            owner;
};

// io_loop
class IoLoop                                    // 10절. 나머지는 run 의 인자
{
    std::atomic<bool>            _stopped;
    std::atomic<std::thread::id> _io_thread;
};
static const size_t LISTENER_CAPACITY = MAX_CONNECTIONS + 2;    // 연결 + acceptor + wake

// event. backend 는 OS 매크로로 하나만 compile 되고 구체 타입을 그대로 내보낸다. Holder 없음. 완료 배열은 open(capacity) 이 heap 에 만든다 (create 때, 19.1 #1)
class EventListener                             // backend/kiotty_iocp_event_listener.cpp
{
    HANDLE            _port;
    OVERLAPPED_ENTRY* _entries;                 // open 이 LISTENER_CAPACITY 개. unique_ptr<[]>
    IoCompletion*     _completions;
    size_t            _capacity;
    uint32_t          _generation;
};
class EventListener                             // backend/kiotty_epoll_event_listener.cpp
{
    int               _epoll;
    int               _wake;
    epoll_event*      _entries;
    IoCompletion*     _completions;
    size_t            _capacity;
    uint32_t          _generation;
    IoEvent*          _ready_head;              // 소유하지 않음
    IoEvent*          _ready_tail;
};

class IoEventBase { bool _pending; IoAction _action; };         // IoEvent 는 기본 생성자로 만들어지고 EventListener::attach(event, socket, slot) 이 제자리에서 채운다
class IoEvent : public IoEventBase              // iocp
{
    const EventListener* _listener;             // 소유하지 않음
    uint32_t             _generation;
    SOCKET               _socket;
    int32_t              _slot;
    SocketCode           _failure;
    Request              _request;              // { OVERLAPPED overlapped; IoEvent* owner; }  owner = this 는 생성자에서. 멤버라 이사하지 않는다
    LPFN_ACCEPTEX        _accept_ex;
};
class IoEvent : public IoEventBase              // epoll
{
    EventListener*       _listener;
    uint32_t             _generation;
    int                  _fd;
    int32_t              _slot;
    bool                 _registered;
    bool                 _queued;
    SocketCode           _result;
    IoPayload            _payload;
    size_t               _done;
    IoEvent*             _next;
};

class Acceptor : public IoSource
{
    PassiveSocket   _socket;
    IoEvent         _event;
    AcceptBuffer    _buffer;                    // { SocketHandle socket; char addresses[2 * ADDRESS_LENGTH]; }
    bool            _no_delay;
};

class WorkContext      { std::atomic<State> _state; int32_t _generation; IoAction _outcome; };
class ConnectionStream { ActiveSocket& _socket; TlsEngine& _tls; StageBase& _receive; StageBase& _send; };   // 값. 쓰는 함수가 만든다
class Parker           { std::mutex _mutex; std::condition_variable _cv; };

// core/kiotty_stage.h. 로직은 StageBase, 저장 공간은 Stage<N>. 받는 쪽은 전부 StageBase&
class StageBase
{                                               // Stage<N> 의 생성자 본문이 bind(_storage) 로 묶는다. limit(capacity) ≤ N 은 Connection::initialize 가
    Span<unsigned char> _buffer;
    size_t              _capacity;
    size_t              _begin_pos;
    size_t              _end_pos;
};
template <size_t N>
class Stage : public StageBase { std::array<unsigned char, N> _storage; };   // N = 0 도 적법 (TLS OFF 의 cipher)

class StageSet
{
    Stage<MAX_RECV_CAPACITY> receive;
    Stage<MAX_SEND_CAPACITY> send;
    Stage<MAX_PUSH_CAPACITY> notify;
};

// tls
class TlsProfile { ssl_ctx_st* _context; bool _server; bool _verify_hostname; };
class TlsEngine
{
    Stage<whenTls(MAX_RECV_CAPACITY)>  _cipher_receive;         // TLS OFF 면 0
    Stage<whenTls(CIPHER_SEND_BYTES)>  _cipher_send;            // max(send, push) + 레코드 오버헤드 + 512
    ssl_st*       _ssl;
    bio_st*       _network;
    bool          _active;
    bool          _failed;
    KiottyNetCode _last_code;
};

// core/pool/kiotty_pool.h : 등급 풀. Layout 이 크기를 정하므로 저장 공간이 멤버다
class PoolBase { public: virtual unsigned char* allocate(size_t) = 0; virtual unsigned char* reallocate(unsigned char*, size_t) = 0; virtual bool release(unsigned char*) = 0; virtual bool contains(const unsigned char*) const = 0; };
template <class Layout>
class Pool : public PoolBase
{
    struct ClassState { size_t untouched; uint32_t free_head; size_t failures; };
    alignas(16) std::array<unsigned char, Layout::ARENA_BYTES> _arena;    // default-init. 쓰기 전까지 RAM 에 없다
    std::array<uint32_t, Layout::BLOCK_COUNT>                  _next;     // free list. 블록 안에 쓰지 않는다
    std::array<ClassState, Layout::CLASS_COUNT>                _classes;
    std::atomic_flag                                           _lock = ATOMIC_FLAG_INIT;
};
template <class Tag, class Layout>
class StaticPool { /* 같은 알고리즘. 멤버가 전부 static (.bss, 상수 초기화, 파괴자 없음) */ };

// tls/kiotty_tls_memory.h : 19.5. 전역은 static, thread 는 thread() 하나, Owner 는 멤버
class TlsMemory
{
    PoolBase&               _pool;              // 파생의 서브풀. 생성자에서 묶는다
    static GlobalArena      s_global;           // StaticPool<GlobalTlsTag, GlobalTlsLayout> 을 PoolBase 로 감싼 객체 하나
    static Range            s_ranges[MAX_SERVERS + MAX_STREAM_CLIENTS + MAX_EVENT_CLIENTS];   // 등록표. Context::release 가 지운다
    static std::atomic_flag s_ranges_lock;
    struct Thread { PoolBase* current; PoolBase* last_hit; bool attached; };   // thread_local 하나. thread() 만 돌려준다
};
template <class Layout>
class OwnerTlsMemory : public TlsMemory { Pool<Layout> _pool; };
```

Connection 의 함수가 받는 것 (10번째 규칙의 "이 함수 호출 때 이 인자들이 필요하다"):

| Connection 의 함수 | thread | 받는 것 | 왜 그것만 |
|---|---|---|---|
| `initialize(id, ConnectionConfiguration&&)` | IO | socket, context, hostname, limits, profile | 셋은 쓰고 버린다 (9.4) |
| `onOpened(handler)`, `dispatch(completion, handler, notification)` | IO | Handler (콜백 + app_context + owner), host 의 mutex | 콜백은 여기서만 나간다. mutex 는 onSent(NOTIFY) 의 release |
| `pickWrite(notification)` | IO | host 의 mutex | notify Stage 를 읽고 encode 한다 |
| `stageNotification(data)`, `notificationWritable()` | 아무 | 없음 | host 가 lock 을 잡고 부른다 |
| `reset()` | IO | 없음 | 풀 계약. host.release 가 lock 을 잡고 부른다 |
| `stream(generation)`, `closeStream(generation, then)` | worker | generation, then | wake 는 Owner 가 한다 |
| `expireDeadline(now)`, `workReady()`, `workFinished()`, `takeWorkOutcome()`, `deadline()`, `phase()`, `working()` | IO | now 또는 없음 | 상태 읽기 |

켜고 끄는 순서.

- `initializeContext()`: `Context::open(ContextConfiguration())` (기본값) → static 객체의 `initialize` 가 OpenSSL 을 `OPENSSL_INIT_NO_ATEXIT` 로 초기화하고 (process 첫 번째만) `TlsMemory::functions()` 로 hook 을 걸고 예열한다 → configuration 의 개수만큼 `LazyReusablePool::reserve` (기본 0 이라 아무것도 만들지 않는다). 두 번째 호출은 같은 객체와 SUCCESS.
- `create*`: `_mutex` 안에서 `LazyReusablePool::acquire({ config, tls })` → Owner 의 `initialize` 가 `TlsMemory::Scope` 안에서 `_profile` 을 만들고 `_host.initialize(limits)`, `_listener.open(LISTENER_CAPACITY)` 를 한다. 서브풀의 등록표 발행은 `OwnerTlsMemory` 의 생성자가 한 번 (19.5).
- accept / admit: `host.acquire({ socket, context, hostname, host.limits(), profile })` → `Connection::initialize(id, configuration)` → `onOpened(handler)`.
- `run`: `Handler handler(callbacks, app_context, *this)` → `TlsMemory::Scope` 안에서 `_loop.run(_host, _listener, _acceptor, _profile, handler, stop_when_empty)` → `TlsMemory::detachThread()` → `_parker.unpark` (12절).
- `destroy*`: shutdown (close + run 의 return 대기) 이 끝난 뒤 `_mutex` 안에서 `LazyReusablePool::release` → Owner 의 `reset` (`SSL_CTX_free` 는 Scope 안). 등록표는 그대로다.
- `releaseContext`: 남은 Owner 를 전부 destroy 한 뒤 세 풀을 비우고 (`unique_ptr` 해제) 등록표를 지운다. `OPENSSL_cleanup` 은 부르지 않는다. 전역 arena 는 .bss 라 돌려줄 것이 없고, 정리 뒤에는 OpenSSL 을 다시 초기화할 수 없기 때문이다. static Context 의 소멸자는 process 종료 때 빈 풀을 지우고 끝난다. releaseContext 없이 종료하면 그때 Owner 들이 지워진다 (run 중인 thread 가 있으면 사용자 오류다. 19.9 의 테스트).

| Owner (TLS ON, 상한 128 연결, 16 KiB) | `sizeof` |
|---|---|
| Server | 약 30 MB (Stage 10 MB + TLS 서브풀 19 MB + 객체) |
| Server, TLS OFF | 약 7.5 MB |
| StreamClient / EventClient | 약 0.25 MB |
| Context | 수 KB. static 객체 하나 |

`KiottyNet_serverMemorySize()`, `KiottyNet_clientMemorySize()` 를 C API 로 공개한다. `sizeof` 상수다.

### 19.4 compile time parameter 와 handle

CMake cache 변수이고 `kiotty_net` 의 PUBLIC 정의다. `base/pool/kiotty_memory_limits.h` 의 `MemoryLimits` 가 모으고 static_assert 로 범위를 검사한다.

| 변수 | 기본 | 범위 | 뜻 |
|---|---|---|---|
| `KIOTTY_NET_MAX_SERVERS` | 4 | 1 ~ 16 | Server 표의 칸 수 |
| `KIOTTY_NET_MAX_STREAM_CLIENTS` | 8 | 1 ~ 16 | StreamClient 표의 칸 수 |
| `KIOTTY_NET_MAX_EVENT_CLIENTS` | 4 | 1 ~ 16 | EventClient 표의 칸 수 |
| `KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER` | 128 | 1 ~ 1024 | Server 의 Connection 수이자 `max_clients` 의 상한 |
| `KIOTTY_NET_MAX_RECV_CAPACITY` | 16384 | 1 ~ | recv Stage 와 cipherReceive 의 N. `recv_capacity` 의 상한 |
| `KIOTTY_NET_MAX_SEND_CAPACITY` | 16384 | 1 ~ | send Stage 의 N |
| `KIOTTY_NET_MAX_PUSH_CAPACITY` | 16384 | 1 ~ | push Stage 의 N |
| `KIOTTY_NET_USE_TLS` | ON | | cipher Stage 의 N, TLS 서브풀, 전역 TLS arena 의 유무 |
| `KIOTTY_NET_TLS_STATIC_POOL` | ON | | OpenSSL 사본을 kiotty 가 혼자 쓸 때 (정적 링크 + 심볼 숨김). 19.5 의 hook 을 건다. 시스템 OpenSSL 이면 OFF 이고 OpenSSL 의 malloc 그대로 (`TlsMemory::installed()` false) |

handle: 3절의 struct `{ owner, id }`. `id` 는 `generation (32) << 32 | slot (16)` 으로 unsigned 로 만들어 int64 양수에 넣는다. type, owner slot, owner generation 비트는 없다 (owner 는 포인터다). Owner handle 은 base.h 그대로 포인터다. 비트를 아는 자리는 `core/pool/kiotty_pool_handle.h` 의 `PoolHandle::value()` / `fromValue()` 둘이다.

### 19.5 TLS arena 와 라우팅

OpenSSL 의 allocator 는 process 에 하나 (`CRYPTO_set_mem_functions`) 다. 할당은 thread 의 current arena 로, 해제는 주소로 arena 를 찾는다. 19.5 의 전부가 클래스 하나 (`TlsMemory`) 에 있다: 전역은 static 멤버, thread 는 private `thread()` 하나가 돌려주는 thread_local, Owner 는 멤버. IoLoop 는 TLS 메모리를 모르고 Owner 의 코드만 Scope 를 만든다.

| | 전역 arena (`TlsMemory::s_global`) | Owner 서브풀 (`OwnerTlsMemory<*TlsLayout>::_pool`) |
|---|---|---|
| 담는 것 | OpenSSL 전역 상태 (약 1 MB), thread 별 상태 (error queue, DRBG), 예열 때 생기는 lazy 캐시 | TlsProfile (SSL_CTX, 인증서) 과 그 Owner 의 연결 N 개의 SSL |
| 저장 공간 | .bss (`StaticPool`). process 가 끝날 때까지 산다. `GlobalArena : PoolBase` 가 감싼 객체 하나 | Owner 의 멤버 |
| 수명 | process | Owner 객체 (releaseContext 까지) |
| 등급 | 계측의 "전역 + thread" 열 | 계측의 "profile × 1 + 연결 × N" 열. Server 는 N = `MAX_CONNECTIONS_PER_SERVER`, client 는 1 |
| 잠금 | 등급마다 `atomic_flag` | 같다 |

```cpp
// tls/kiotty_tls_memory.h. OpenSSL 헤더를 모른다. 함수 포인터의 시그니처만 OpenSSL 의 것 (size, file, line) 이다
struct TlsMemoryFunctions                       // CRYPTO_set_mem_functions 에 그대로 넘기는 셋. backend 의 installTlsMemory() 가 받는다
{
    void* (*allocate)(size_t bytes, const char* file, int line);
    void* (*reallocate)(void* block, size_t bytes, const char* file, int line);
    void  (*release)(void* block, const char* file, int line);
};

class TlsMemory
{
public:
    // 전역 (process 에 하나). 전부 static
    static TlsMemoryFunctions functions();      // Context::open → backend 가 CRYPTO_set_mem_functions(f.allocate, f.reallocate, f.release)
    static bool               installed();      // hook 이 걸렸는가 (정적 링크 build)
    static void               detachThread();   // IO Thread 끝. current 를 전역으로 되돌리고 backend 의 OPENSSL_thread_stop. Server::run 과 EventClient::run 의 끝 한 줄
    static size_t             lateFrees();      // 어느 arena 에도 없는 주소의 free. 역참조하지 않고 센다
    static StaticPoolUsage    globalUsage(size_t index);

    // Owner 별
    class Scope                                 // RAII. 이 thread 의 할당을 이 Owner 의 서브풀로. 생성자가 attachThread 도 한다. 소멸자가 이전 arena 를 복원한다
    {
    public:
        explicit Scope(TlsMemory& owner);       // attachThread() → _previous 에 지금 것 → current = &owner._pool
        ~Scope();                               // current = &_previous
    private:
        PoolBase& _previous;                    // 늘 있다. 전역 arena 도 PoolBase 객체라 nullptr 인 때가 없다
    };
    size_t failures() const;                    // 서브풀이 못 준 횟수. 그 handshake 만 실패했다

protected:
    explicit TlsMemory(PoolBase& pool);         // 묶기만 한다. pool 은 아직 만들어지지 않았을 수 있다 (파생의 멤버). 여기서 읽지 않는다
    void publish();                             // 파생 생성자의 본문에서. 등록표에 (begin, end, &_pool). release 순서로 발행

private:
    struct Thread { PoolBase* current; PoolBase* last_hit; bool attached; };   // 값이 바뀌므로 포인터. 처음은 &s_global, nullptr, false
    struct Range  { const unsigned char* begin; const unsigned char* end; std::atomic<PoolBase*> pool; };
    class GlobalArena : public PoolBase { /* StaticPool<GlobalTlsTag, GlobalTlsLayout> 의 static 함수로 forward */ };

    static Thread&  thread();                   // thread_local 을 돌려주는 유일한 자리
    static void     attachThread();             // thread().attached 가 false 일 때만. current 가 전역인 채로 backend 의 ERR_clear_error + RAND_bytes (이 thread 의 OpenSSL 상태를 전역 arena 에)
    static void*    allocate(size_t bytes, const char* file, int line);        // thread().current. 못 주면 nullptr 이고 failures++
    static void*    reallocate(void* block, size_t bytes, const char* file, int line);   // block 의 arena 안에서 새로 꺼내 복사. nullptr 면 allocate
    static void     release(void* block, const char* file, int line);          // arenaOf → release. 없으면 lateFrees++
    static PoolBase* arenaOf(const void* block); // last_hit → 등록표 → 전역. 비교는 std::less<const unsigned char*>

    PoolBase&               _pool;
    static GlobalArena      s_global;
    static Range            s_ranges[MAX_SERVERS + MAX_STREAM_CLIENTS + MAX_EVENT_CLIENTS];   // 등록표. 칸 = Owner. Context::release 가 지운다
    static std::atomic_flag s_ranges_lock;
};

template <class Layout>
class OwnerTlsMemory : public TlsMemory         // Server 는 ServerTlsLayout, client 둘은 ClientTlsLayout. TLS OFF 면 Layout 의 ARENA_BYTES 가 0
{
public:
    OwnerTlsMemory() : TlsMemory(_pool), _pool() { publish(); }
private:
    Pool<Layout> _pool;
};
```

- 클래스가 둘인 이유: class template 의 static 멤버는 instantiation 마다 따로 생긴다. Server 와 client 의 Layout 이 다르므로 서브풀은 template 이어야 하고, 전역 arena 와 thread_local 은 하나여야 한다. 그래서 static 전부를 template 이 아닌 `TlsMemory` 에 두고, 서브풀 멤버만 `OwnerTlsMemory<Layout>` 이 든다.
- `TlsMemory(_pool)` 은 아직 만들어지지 않은 멤버에 참조를 묶는 것인데, 묶기만 하고 읽지 않으면 적법하다. 정적 분석기가 "초기화 전 사용" 으로 볼 수 있으니 base 생성자가 `_pool` 을 건드리지 않는다는 것을 그 자리에 적는다.
- detach 를 Scope 의 소멸자에 넣지 않는 이유: StreamClient 는 호출자 thread 에서 함수마다 Scope 를 만드는데, 소멸자가 `OPENSSL_thread_stop` 을 부르면 호출마다 이 thread 의 오류 큐와 DRBG 를 지우고 다음 호출의 attach 가 OS 의 entropy 를 다시 읽어 다시 만든다. IO Thread 의 콜백 안에서 StreamClient 를 부르면 (Scope 가 겹친다) 안쪽 소멸자가 Server 쪽이 쓰던 thread 상태를 지운다. 틀리지는 않지만 매번 낸다. 그래서 detach 는 "이 thread 의 OpenSSL 이 끝났다" 를 아는 자리, IO Thread 가 끝나는 run 의 끝 둘에만 있다. 사용자 thread 는 언제 끝나는지 라이브러리가 모르므로 detach 할 자리가 없다 (Windows 정적 링크에서는 그 thread 의 상태가 남는다. 사이클 6).

| 자리 | Scope |
|---|---|
| Owner 의 `initialize` (TlsProfile 생성) 와 `reset` (`SSL_CTX_free`) | 그 Owner |
| IO Thread (run 을 부른 thread) | `Server::run` / `EventClient::run` 의 Scope 하나가 run 전체를 감싼다. 끝에 `detachThread()` |
| 호출자 thread 에서 OpenSSL 을 부르는 API (StreamClient 의 connect / read / write / disconnect) | 함수마다 그 Owner 의 Scope |
| `Context::open` | 전역 (Scope 없음). memory BIO 쌍으로 loopback handshake 를 한 번 돌려 lazy 전역 캐시를 전역 arena 에 만든다 |

예열 (attachThread) 은 `ERR_clear_error()` 와 `RAND_bytes()` 한 번이다. Owner 서브풀에 전역 꼬리표 할당이 들어오지 않는지 계측 도구로 테스트한다. 지금 코드의 free 함수 셋 (`useTlsStaticPool`, `tlsPoolClassCount`, `tlsPoolUsage`) 은 이 클래스의 static 이 된다.

OpenSSL 설정: `OPENSSL_INIT_NO_ATEXIT`, `SSL_MODE_RELEASE_BUFFERS`, `SSL_SESS_CACHE_OFF`, `SSL_OP_NO_RENEGOTIATION`, `SSL_OP_NO_COMPRESSION` (있음), `SSL_CTX_set_num_tickets(0)`, `SSL_CTX_set_max_cert_list(16 KiB)`, `SSL_CTX_set_verify_depth` (config). 상대가 보내는 양의 상한이 계측의 전제다.

### 19.6 OpenSSL 의 사용량 (BIO pair, OpenSSL 3.6.1, 실제 byte)

| Stage | server 최대 / 대기 | client 최대 / 대기 |
|---|---|---|
| 4 KiB | 66 K / 28 ~ 47 K | 67 ~ 96 K / 33 ~ 50 K |
| 16 KiB | 66 K / 28 ~ 47 K | 67 K / 33 ~ 50 K |
| 64 KiB | 68 K / 29 ~ 48 K | 68 K / 33 ~ 50 K |

전역 상태는 처음 한 번 약 1 MB 다. profile 하나는 인증서를 읽는 동안 수십 KB, 열려 있는 동안 수백 B 다. 등급 합산으로 서브풀의 연결당 예약은 약 150 KiB 다.

등급 목록은 `tests/measure/kiotty_measure_tls_memory.cpp` 의 계측이다. 할당마다 "전역 / profile / 연결" 꼬리표와 크기 등급 (32, 48, 64, 96, … 1.5 배 간격) 을 기록하고, 연결 N 개를 handshake 와 최대 길이 왕복까지 같이 진행시켜 동시 최대를 만든다. 인증서 (RSA 2048 / 4096, ECDSA P-256) × TLS 1.2 / 1.3 × client 인증 여덟 경우와 Stage 4 / 16 / 64 KiB 의 최악이 `GlobalTlsLayout`, `ServerTlsLayout`, `ClientTlsLayout` 이 된다. OpenSSL 을 올리거나 설정 범위를 넓히면 도구를 다시 돌린다. 인증서는 `tests/measure/generate_certs.sh` 가 만든다.

### 19.7 OpenSSL 사본

기본 build 는 OpenSSL 정적 링크 + 심볼 숨김 (`-fvisibility=hidden`, `--exclude-libs`) 이다. 시스템 OpenSSL 로 build 하면 `KIOTTY_NET_TLS_STATIC_POOL` OFF 이고 OpenSSL 의 malloc 이며 상한 약속이 빠진다. OpenSSL 을 올릴 때는 "계측 도구 재실행 → 등급 표 갱신 → 풀 바닥 테스트 → build" 가 CI 한 번에 돈다.

### 19.8 보이는 메모리

작업 관리자의 "메모리" 열, Linux `RES`, Android PSS, macOS footprint 는 쓴 page 만 센다. Windows "커밋 크기", Process Explorer Private Bytes, Linux `VIRT` 는 잡은 크기를 센다. EventClient 하나짜리 application 은 어느 도구로 보아도 수 MB 다. Server 는 `max_clients` 와 무관하게 Windows commit 에 약 30 MB 로 보인다. 줄이려면 `KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER` 를 낮춘다. 전역 TLS arena (약 1 ~ 2 MB) 가 고정 비용이다.

### 19.9 이 절이 고치는 다른 절 (사이클 5)

| 절 | 고칠 것 |
|---|---|
| 1 (구조도), 2 (파일) | `Holder`, Context 의 heap Pool, `ContextPoolLayout`, `TlsPoolLayout`, `MAX_TLS_CONTEXTS`, `kiotty_holder.h`, `kiotty_notification_context.h` 를 지우고 `core/pool/` (`is_reusable`, `ReusablePool`, `LazyReusablePool`), `StageBase` / `Stage<N>`, `Parker`, `TlsMemory` 를 적는다 |
| 3 (base.h) | handle 둘을 struct `{ owner, id }` 로, `KiottyNetConnectionOwner` tag, `KiottyNet_*MemorySize`. `initializeContext(void)` 는 그대로 (Context 설정은 안의 기본값). C++ `KiottyNet_Connection` 의 `operator==` / `operator<` |
| 4 (core) | `Stage(Pool&, capacity)` → `StageBase` + `Stage<N>`, `limit(capacity)`, `clear()`. StageSet 은 `Stage<N>` 셋 |
| 7 (event) | `IoEvent` → `IoEvent` 구체 타입, 기본 생성자. `attach(event, socket, slot)` 이 제자리에서 채운다. `eventFor` 의 반환형 |
| 8 (tls) | `TlsEngine(Pool&, ...)` → `Stage<N>` 둘. `decode(StageBase&)`. `open(TlsProfile&, hostname)`. `OPENSSL_INIT_NO_ATEXIT`, `SSL_CTX_set_num_tickets(0)`, `SSL_CTX_set_max_cert_list`. `kiotty_tls_memory.h` 의 free 함수 셋 → `TlsMemory` |
| 9 (Connection) | 생성자 인자 제거. `open` → `initialize(id, ConnectionConfiguration&&)`, `release` → `reset`. `NotificationContext` 삭제 (mutex 는 host, wake 는 Owner, IO thread 는 IoLoop). `Sender` 는 static 함수 둘. `stream()` 은 값. `handle(handler)`. ConnectionHost 를 template 으로, `initialize(limits)`, `release` 가 lock 안에서 `reset` |
| 10 (io_loop) | 멤버 둘. run 의 참조 인자 다섯. `stepLocalEvents` + 배열 → `stepLocalEvents` 반복. `isIoThread` |
| 12, 13 (server, client, context, config) | Owner 와 Context 가 `is_reusable` 계약 (`configuration_type`, `initialize`, `reset`). static Context 하나와 `Context::open`. 타입별 `LazyReusablePool`. Context config. `Parker`. EventClient 의 Acceptor. 켜고 끄는 순서 (19.3) |
| 14 (thread) | IO thread 의 Scope / `detachThread`, 호출자 thread 의 Scope, Parker |
| 15 (base.cpp) | `handle.owner->` 번역. `Context::open` |
| 16 (메모리 표) | Owner 별 `sizeof` 로 다시 쓴다 |
| 테스트 | 서브풀 바닥 (그 Owner 만 실패), 전역 꼬리표 격리, destroy 뒤 늦은 free 0, 은퇴 Owner 의 `initialize` 재사용, `releaseContext` 없이 종료, `initializeContext` 두 번, local completion 의 thenWork 가 같은 바퀴에 시작되는가, 틀린 handle (은퇴한 Owner, 지난 generation) 이 INVALID_ARGUMENT / PEER_CLOSED 인가 |

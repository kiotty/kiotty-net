# kiotty_net_base.h 구현 설계 v2 (확정)

2026-10-29. v1 의 모든 결정을 반영한 구현용 요약이다. 역사와 근거는 `design_base_v1.html` 과 피드백 문서에 있다. 이 문서는 클래스의 인터페이스와 한 바퀴의 순서만 적는다. 두 문서가 다르면 requirement 가, 흐름이 다르면 implementation_guide 가 우선한다.

원칙

1. `src/kiotty_net_base.cpp` 는 번역만 한다. handle 과 struct 를 구현 객체로 바꿔 메서드 하나를 부르고 결과를 C 타입으로 돌려준다. 검사, 분기, 상태 변경, 복사를 두지 않는다 (14절).
2. 클래스는 최소 함수와 최소 변수만 가진다. 상태를 읽어 판단하는 줄은 `pending()` 셋 (onTimeout, settle, onNotified) 밖에 두지 않는다.
3. 의존은 아래로만 간다. OS 가 다른 부분은 그 폴더의 `backend/` 안에만, OpenSSL 은 `tls/` 안에만 있다.
4. 한 줄에 문장 하나. 중첩 호출은 지역 변수로 푼다. cancel 은 없다. 요청은 completion 으로만 비워진다.

```
kiotty_net_base.cpp ── 번역
    ├── Context ───── Holder<Owner> slot 16, TLS Pool, g_contexts[16]
    ├── Server ──────┐
    ├── EventClient ─┼── Owner ── ConnectionHost + EventListener + IoLoop (+ Acceptor) + TlsProfile
    └── StreamClient ┘               IoLoop ── collect → broadcast → step (dispatch → submit) → settle
                                     Connection ── StageSet (평문), TlsEngine, WorkContext, IoEvent 둘   (IoSource)
                                     Acceptor ──── PassiveSocket, IoEvent 하나                           (IoSource)
                                     EventListener + IoEvent ── IOCP / epoll / kqueue / io_uring
                                     ActiveSocket / PassiveSocket, TlsProfile / TlsEngine
core/ : Stage, StageSet, Span, Holder, Result, PoolHandle, Pool, Clock
```

## 1. 폴더

```
src/
    kiotty_net_base.cpp
    implementation/
        core/                       namespace kiotty
            kiotty_stage.h          Stage (있음)
            kiotty_stage_set.h      StageSet { receive, send, notify }
            kiotty_holder.h         Holder<Base> (있음)
            kiotty_result.h         Result<Code, Value> (있음)
            kiotty_span.h           Span<T>
            kiotty_pool_handle.h    PoolHandle, toRaw / fromRaw, ownerOf / slotOf
            kiotty_pool.h           Pool (reference 의 BlockPool)
            kiotty_clock.h          monotonicMs
        base/                       namespace kiotty::net
            error/      kiotty_socket_error.h (SocketCode, toNetCode), backend/{windows,posix}_error.cpp
            socket/     kiotty_socket.h (ActiveSocket, PassiveSocket, connectWithRetry), backend/{windows,posix}_socket.cpp
            event/      kiotty_io_action.h, kiotty_event.h (IoEvent), kiotty_event_listener.h, backend/{iocp,epoll,kqueue,io_uring}_*.cpp
            tls/        kiotty_tls_profile.h, kiotty_tls_engine.h, backend/kiotty_openssl_tls.cpp | kiotty_stub_tls.cpp (KIOTTY_NET_USE_TLS 가 고른다)
            connection/ kiotty_io_source.h, kiotty_work_context.h, kiotty_notification_context.h,
                        kiotty_connection.h, kiotty_acceptor.h, kiotty_connection_host.h
            io_loop/    kiotty_io_loop.h
            server/     kiotty_server.h
            client/     kiotty_event_client.h, kiotty_stream_client.h
            context/    kiotty_owner.h, kiotty_context.h
            config/     kiotty_config.h (initialize* / configure*), kiotty_action.h (action struct 조립)
```

- backend 는 전부 `.cpp`. OS struct (OVERLAPPED 등) 는 `.cpp` 안의 struct 에 두고 `Holder<IoEvent>` 로 값처럼 든다.
- `KIOTTY_NET_USE_TLS` 가 보이는 자리는 tls/backend 의 파일 선택 하나. stub 의 `TlsProfile::open` 은 `KIOTTY_TLS_NOT_SUPPORTED`. 평문 통과 stub 은 없다.

## 2. 규약

- `core/` 는 `namespace kiotty`, `base/` 는 `namespace kiotty { namespace net { } }`. 접두어 없음.
- 공개 struct (`KiottyNetSpan`, `KiottyNetEndpoint`, `KiottyNetTransport`, 설정, `KiottyNetConnectionHandler`, `KiottyNetConnectionInfo`, `KiottyNetConnectionAction`, `KiottyNetConnectionDecision`) 는 내부에서도 그대로 쓴다.
- 내부 에러는 `Result<Code, Value>` 와 enum. `SocketCode → KiottyNetCode` 는 Connection 과 StreamClient 에서만. output 인자 없음.
- 모든 메모리는 create 의 생성자에서. 실패면 `valid() == false` 이고 create 의 Result.code 가 이유. OpenSSL 할당은 Context 의 Pool.
- 복사 없음. slot 안의 것 (Connection, Owner) 은 이동도 없음.
- 콜백은 IO Thread 의 `onOpened` 와 `dispatch` 안에서만. resolve, submit, settle, onNotified 는 handler 를 받지 않는다.
- IO Thread 밖에서 EventListener 에 들어가는 호출은 `wake()` 하나.
- 들어온 것을 처리하는 함수는 전부 `on*` 이고 IoAction 을 돌려준다.

## 3. base.h 변경

```c
typedef enum KiottyNetCode { ..., KIOTTY_TLS_NOT_SUPPORTED, KIOTTY_TLS_CERTIFICATE_NOT_FOUND, KIOTTY_TLS_CERTIFICATE_INVALID,
    KIOTTY_TLS_VERIFY_FAILED, KIOTTY_TLS_HOSTNAME_MISMATCH, KIOTTY_TLS_HANDSHAKE_FAILED, KIOTTY_TLS_HANDSHAKE_TIMEOUT, KIOTTY_TLS_RECORD_ERROR } KiottyNetCode;

/* Server / Client config 에 bool no_delay (기본 true, KIOTTY_PIPE 는 무시) */
KIOTTY_NET_API KiottyNetCode KiottyNet_configureServerNoDelay(KiottyNetServerConfig* config, bool no_delay);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientNoDelay(KiottyNetClientConfig* config, bool no_delay);

typedef enum KiottyNetActionType { KIOTTY_ACTION_HOLD, KIOTTY_ACTION_READ, KIOTTY_ACTION_READ_UNTIL, KIOTTY_ACTION_WRITE, KIOTTY_ACTION_START_WORK, KIOTTY_ACTION_CLOSE } KiottyNetActionType;

typedef struct KiottyNetConnectionAction        /* statemachine.h 의 KiottyNetEntryAction 과 같은 모양. 값 */
{
    KiottyNetActionType type;
    uint32_t            timeout_ms;             /* 0 이면 없음 */
    union
    {
        struct { size_t exact; } read;
        struct { KiottyNetSpan delimiter; size_t max; } read_until;
        struct { KiottyNetSpan data; } write;
        struct { KiottyNetCode reason; } close;
    } as;
} KiottyNetConnectionAction;

KiottyNetConnectionAction KiottyNet_holdCommunication(uint32_t timeout_ms);
KiottyNetConnectionAction KiottyNet_readConnection(size_t exact, uint32_t timeout_ms);
KiottyNetConnectionAction KiottyNet_readConnectionUntil(KiottyNetSpan delimiter, size_t max, uint32_t timeout_ms);
KiottyNetConnectionAction KiottyNet_writeConnection(KiottyNetSpan data, uint32_t timeout_ms);
KiottyNetConnectionAction KiottyNet_startWork(void);
KiottyNetConnectionAction KiottyNet_closeConnection(KiottyNetCode reason);

typedef struct KiottyNetConnectionDecision { bool accepted; KiottyNetConnectionContext connection_context; } KiottyNetConnectionDecision;
KiottyNetConnectionDecision KiottyNet_acceptConnection(KiottyNetConnectionContext connection_context);
KiottyNetConnectionDecision KiottyNet_ignoreConnection(void);

typedef struct KiottyNetConnectionHandler
{
    KiottyNetConnectionDecision (*onConnected)   (KiottyNetAppContext app, KiottyNetConnectionInfo info);                      /* handle 은 아직 없다 */
    KiottyNetConnectionAction   (*onReady)       (KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection);       /* handshake 뒤. 첫 action */
    KiottyNetConnectionAction   (*onRead)        (KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection, KiottyNetSpan data);
    KiottyNetConnectionAction   (*onWritten)     (KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection, size_t length);
    KiottyNetConnectionAction   (*onTimeout)     (KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection);
    void                        (*onWorkStarted) (KiottyNetConnectionContext ctx, KiottyNetWorkHandle work);
    void                        (*onDisconnected)(KiottyNetAppContext app, KiottyNetConnectionContext ctx, KiottyNetConnectionHandle connection, KiottyNetCode reason);
    void                        (*onError)       (KiottyNetAppContext app, KiottyNetConnectionHandle connection, KiottyNetCode code, const char* message);   /* connection 0 이면 없음. message 는 static */
} KiottyNetConnectionHandler;

KiottyNetConnectionInfo KiottyNet_getConnectionInfo(KiottyNetConnectionHandle connection);
KiottyNetCode           KiottyNet_notifyConnection(KiottyNetConnectionHandle connection, KiottyNetSpan data);   /* 아무 thread. return 하지 않는 유일한 길 */
KiottyNetCode           KiottyNet_finishWork(KiottyNetWorkHandle work, KiottyNetConnectionAction action);     /* START_WORK 는 INVALID_ARGUMENT */
bool                    KiottyNet_isWorkConnected(KiottyNetWorkHandle work);

/* create 는 { KiottyNetCode code; Handle handle; } 를 돌려준다 */
KiottyNetContextResult      KiottyNet_initializeContext(void);
KiottyNetServerResult       KiottyNet_createServer(KiottyNetContext, const KiottyNetServerConfig*, const KiottyNetTlsServerConfig*);
KiottyNetEventClientResult  KiottyNet_createEventClient(KiottyNetContext, const KiottyNetClientConfig*, const KiottyNetTlsClientConfig*);
KiottyNetStreamClientResult KiottyNet_createStreamClient(KiottyNetContext, const KiottyNetClientConfig*, const KiottyNetTlsClientConfig*);
```

지우는 것: `setConnectionTimeout`, handle 받는 action 함수들, `finishWorkWith*` 넷, `KiottyNetConnectionHost`, `onConnected` 의 handle 인자. statemachine.h 는 `KiottyNetEntryAction` 을 `KiottyNetConnectionAction` 으로 바꾸고 `KiottyNet_make*EntryAction` 여섯 개를 지운다. `KiottyNetTransport` 는 그대로다.

| action | timeout_ms | 지나면 |
|---|---|---|
| READ, READ_UNTIL | 조건이 찰 때까지 | onTimeout (10절) |
| WRITE | onWritten 까지 | onTimeout |
| HOLD | 아무것도 걸지 않고 기다리는 한도 | onTimeout |
| START_WORK | 없음. finishWork 의 action 이 다음 deadline | — |
| CLOSE | 없음 | — |

## 4. core

```cpp
class Stage;                                    // 있음. Provider (writable / prepare / commit), Consumer (readable / peek / commit), compact
struct StageSet                                 // 0 인 것은 잡지 않는다
{
    StageSet(size_t receive_capacity, size_t send_capacity, size_t notify_capacity);
    bool  valid() const;
    Stage receive;
    Stage send;                                 // 순서가 있는 한 줄 : USER, HANDSHAKE, CONTROL
    Stage notify;                               // 아무 때나 옆에서 : NOTIFY
};
template <typename T> struct Span { T* data; size_t length; T* begin() const; T* end() const; bool empty() const; T& operator[](size_t) const; };
struct PoolHandle { int32_t generation; int32_t index; };     // 0 은 무효
int64_t toRaw(PoolHandle); PoolHandle fromRaw(int64_t);
class Pool                                      // 크기 등급별 고정 블록. 가득 차면 heap fallback + 카운터
{
public:
    explicit Pool(size_t bytes);
    void*  allocate(size_t size);
    void*  reallocate(void* block, size_t size);
    void   release(void* block);
    size_t fallbackCount() const;
};
uint64_t monotonicMs();                         // deadline 의 유일한 시계
```

- 오용 계약 (테스트 v0 14절 3, 사이클 2 에서 확정): `Stage` 의 Provider / Consumer `commit` 이 writable / readable 을 넘기면 debug 는 `assert`, release 는 그 값으로 clamp. `Pool::release` / `reallocate` 에 Pool 이 준 적 없는 포인터가 오면 debug 는 `assert`, release 는 무시 (`reallocate` 는 nullptr). 테스트는 `EXPECT_DEBUG_DEATH`.
- Pool 의 등급은 64, 128, 256, 512, 1024, 4096, 16384, 65536 바이트 여덟이고 `bytes / 8` 씩 나눠 가진다. 맞는 등급이 가득 차면 다음 등급으로 가지 않고 heap fallback. fallback 블록은 앞에 16 바이트 header (magic + 크기) 를 둔다. 남의 포인터 판별과 `reallocate` 의 옛 크기가 이것으로 된다. `allocate(0)` 은 1 바이트로 본다. 모든 블록은 16 정렬.
- PoolHandle 의 index 는 `makeIndex(OwnerIndex, slot)` 로 만들고 `ownerOf(index)` / `slotOf(index)` 로 푼다. `OwnerIndex { context_slot, owner_slot, owner_generation }`.

## 5. IoAction, IoPayload, IoCompletion

dispatch (completion → action) → resolve (action → payload) → submit (action + payload → listener) → OS → completion (걸었던 action 을 싣고).

```cpp
enum class IoActionKind : uint8_t { NONE, HOLD, READ, READ_UNTIL, WRITE, START_WORK, CLOSE, ACCEPT };
enum class IoPurpose    : uint8_t { USER, HANDSHAKE, CONTROL, NOTIFY };    // 완료를 어느 Stage 로 돌려주나

struct IoAction
{
    IoActionKind kind;
    IoPurpose    purpose;
    uint32_t     timeout_ms;
    union
    {
        struct { size_t exact; } read;
        struct { char delimiter[4]; uint8_t length; size_t max; size_t scanned; } read_until;   // scanned : 진행 위치
        struct { KiottyNetCode reason; } close;
    } as;
    static IoAction none();
    static IoAction hold(uint32_t timeout_ms);
    static IoAction read(IoPurpose purpose, uint32_t timeout_ms, size_t exact);   // HANDSHAKE 는 exact 0
    static IoAction write(IoPurpose purpose, uint32_t timeout_ms);                 // 바이트는 이미 Stage 에
    static IoAction startWork();
    static IoAction close(KiottyNetCode reason);
    static IoAction accept();
    static IoAction from(const KiottyNetConnectionAction& action);                 // purpose USER. WRITE 의 data 는 adopt 가 Stage 로
};
struct IoPayload { void* buffer; size_t length; };
enum class IoKind : uint8_t { ACCEPT, RECEIVE, SEND, WAKE, TIMEOUT, WORK_DONE };   // 뒤 둘은 collect 가 만든다
struct IoCompletion
{
    IoKind     kind;
    int32_t    slot;                            // ACCEPT / WAKE 는 -1
    SocketCode code;                            // SUCCESS, CLOSED (0 바이트), CANCELLED (닫힌 소켓의 늦은 완료), 오류
    size_t     length;
    IoAction   action;                          // 걸었던 action (값). TIMEOUT / WORK_DONE / WAKE 는 NONE
    static IoCompletion timeout(int32_t slot);
    static IoCompletion workDone(int32_t slot);
};
```

IoAction 은 사용자 메모리를 가리키지 않는다. Connection 은 "걸린 read 조건" 을 들지 않는다. 조건은 완료가 가져온다.

## 6. socket

```cpp
typedef intptr_t SocketHandle;
class ActiveSocket                              // 이동만
{
public:
    static Result<SocketCode, ActiveSocket> open(KiottyNetTransport transport);   // non-blocking. TCP → AF_INET, PIPE → AF_UNIX
    SocketCode connect(const KiottyNetEndpoint& endpoint, uint32_t timeout_ms);    // 동기. non-blocking connect + poll
    SocketCode setNoDelay(bool on);                                                // PIPE 면 SUCCESS 로 무시
    Result<SocketCode, size_t> send(const void* data, size_t length);              // 즉시. StreamClient, reactor backend
    Result<SocketCode, size_t> receive(void* data, size_t length);
    SocketCode shutdown();
    void       close();
    bool valid() const; SocketHandle handle() const; KiottyNetConnectionInfo peer() const;
};
class PassiveSocket
{
public:
    SocketCode open(const KiottyNetEndpoint& endpoint, int backlog);               // Unix domain 은 unlink 뒤 bind
    Result<SocketCode, ActiveSocket> accept();
    void close(); bool valid() const; SocketHandle handle() const;
};
Result<KiottyNetCode, ActiveSocket> connectWithRetry(const KiottyNetClientConfig& config, const std::atomic<bool>& stop);
// max_trial 번, connect_timeout_ms 씩, 사이에 trial_interval_ms (stop 을 본다). 성공하면 no_delay 적용
```

사이클 3a 에서 정한 세부:

- `SocketCode` 는 SUCCESS, WOULD_BLOCK, PENDING, CLOSED, CANCELLED, TIMEOUT, REFUSED, RESET, UNREACHABLE, ADDRESS_IN_USE, INVALID_ARGUMENT, OUT_OF_RESOURCE, NOT_SUPPORTED, UNKNOWN. OS 코드는 backend 의 표 하나 (`socketErrorTable()`) 로만 바꾼다. 표에 없으면 UNKNOWN. `toNetCode` 는 SUCCESS → SUCCESS, CLOSED → PEER_CLOSED, CANCELLED → CLOSED, TIMEOUT → TIMEOUT, 나머지 → SOCKET_ERROR.
- `PassiveSocket::port()` 를 더했다 (port 0 bind 뒤의 실제 포트).
- `connect` 의 timeout 0 은 한도 없음. 대기는 POSIX 가 poll, Windows 는 select (WSAPoll 이 실패한 connect 를 알리지 않는 버그 때문).
- TCP listening 소켓은 POSIX 에서 SO_REUSEADDR, Windows 에서 SO_EXCLUSIVEADDRUSE. 둘 다 같은 포트의 두 번째 listen 은 ADDRESS_IN_USE.
- SIGPIPE 는 Linux 는 MSG_NOSIGNAL, Apple 은 SO_NOSIGPIPE 로 소켓마다 막는다. 프로세스 signal 은 건드리지 않는다.
- `connectWithRetry` 의 `max_trial == 0` 은 1 로 본다. interval 은 10 ms 씩 쪼개 stop 을 본다. 실패 코드는 마지막 시도의 `toNetCode`.

## 7. event

proactor 흉내. submit 에 payload 를 주면 IOCP 는 kernel 이, epoll 은 readable 뒤 listener 가 거기에 쓴다. epoll 은 `EPOLLONESHOT` 이라 걸린 요청이 곧 관심이다. 레벨 트리거 + 영구 등록은 쓰지 않는다.

```cpp
struct IoEvent                                  // 걸린 요청 하나. HOLDER_SIZE 128, ALIGN 8
{
    bool            pending() const;            // submit 이 true, 완료를 꺼낼 때 false. listener 만 바꾼다
    const IoAction& action() const;
};
class EventListener                             // STORAGE_SIZE 256, ALIGN 16
{
public:
    SocketCode open(size_t completion_capacity);
    void       close();
    SocketCode attach(SocketHandle socket, int32_t slot);        // 연결당 한 번. IOCP: CreateIoCompletionPort. epoll: ADD (관심 0)
    SocketCode submit(Holder<IoEvent>& event, SocketHandle socket, const IoAction& action, IoPayload payload);
    //   READ / READ_UNTIL → WSARecv | MOD EPOLLIN|ONESHOT 뒤 recv → payload
    //   WRITE             → WSASend | 바로 send, 남으면 MOD EPOLLOUT|ONESHOT
    //   ACCEPT            → AcceptEx | MOD EPOLLIN|ONESHOT
    //   즉시 실패는 자기 완료 배열에 오류 완료로
    void       detach(SocketHandle socket, int32_t slot);        // socket.close 직전. 걸린 요청은 CANCELLED 완료로 (epoll 은 listener 가 만든다)
    Span<const IoCompletion> wait(uint32_t timeout_ms);          // 꺼낼 때 pending 을 내린다
    SocketCode               wake();                             // 아무 thread. { WAKE } 하나
};
```

## 8. tls

socket → cipher StageSet → TlsEngine → plain StageSet. memory BIO 로 completion 마다 한 걸음. `SSL_set_fd` 는 쓰지 않는다. `SSL*` 은 IO Thread 만 만진다.

```cpp
class TlsProfile                                // SSL_CTX 하나. Owner 마다
{
public:
    KiottyNetCode openServer(const KiottyNetTlsServerConfig& config);   // 파일 없음 : CERTIFICATE_NOT_FOUND. 파싱 / 불일치 : CERTIFICATE_INVALID. stub : NOT_SUPPORTED
    KiottyNetCode openClient(const KiottyNetTlsClientConfig& config);
    void close(); bool valid() const; uint32_t handshakeTimeoutMs() const;
};
enum class TlsStep : uint8_t { SEND, RECEIVE, DONE, FAILED };
class TlsEngine                                 // 연결 하나의 SSL + BIO 둘 + cipher StageSet
{
public:
    TlsEngine(Pool& pool, size_t receive_capacity, size_t send_capacity, size_t notify_capacity);   // 셋 다 0 이면 평문 (active 늘 false)
    KiottyNetCode open(TlsProfile& profile, const char* hostname);   // SSL_new 또는 SSL_clear. 서버 accept state, 클라이언트 connect state + SNI
    void          close();
    bool          active() const;
    StageSet&     cipher();
    TlsStep       handshake();                  // SSL_do_handshake. cipher.send 에 바이트가 있으면 SEND 가 먼저, 1 이면 DONE, WANT_READ 면 RECEIVE, 아니면 FAILED
    KiottyNetCode decode(Stage& plain);         // cipher.receive → SSL_read → plain. 레코드 미완이면 0 바이트
    KiottyNetCode encode(KiottyNetSpan plain, Stage& cipher);   // SSL_write → cipher().send 또는 cipher().notify
    bool          hasControlOutput() const;     // decode 뒤 cipher.send 에 바이트 (KeyUpdate 답장)
    KiottyNetCode lastCode() const;
    const char*   lastMessage() const;          // OpenSSL static 문자열
};
```

| 어디서 | code | 나가는 곳 |
|---|---|---|
| profile open 파일 없음 / 파싱 실패 | CERTIFICATE_NOT_FOUND / CERTIFICATE_INVALID | create 의 code |
| handshake 검증 / hostname / 협상 / 시간 초과 | VERIFY_FAILED / HOSTNAME_MISMATCH / HANDSHAKE_FAILED / HANDSHAKE_TIMEOUT | onError → onDisconnected |
| handshake 중 peer 끊음, READY 의 close_notify | PEER_CLOSED | onDisconnected 만 |
| READY 복호화 실패 | RECORD_ERROR | onError → onDisconnected |

## 9. connection

### 9.1 IoSource

```cpp
class IoSource                                  // Connection, Acceptor 의 base. submit 이 보는 전부
{
public:
    virtual ~IoSource();
    virtual SocketHandle     socket() const = 0;
    virtual int32_t          slot() const = 0;                       // Acceptor 는 -1
    virtual Holder<IoEvent>& eventFor(const IoAction& action) = 0;   // Connection : NOTIFY 면 _notify_event, 아니면 _stream_event
    virtual IoPayload        resolve(const IoAction& action) = 0;    // Connection : Stage 의 자리. Acceptor : AcceptEx 버퍼 또는 빈 payload
    virtual void             close() = 0;
};
```

### 9.2 Connection 의 상태

| 상태 | 타입 | 역할 | 바뀌는 자리 | 읽는 자리 |
|---|---|---|---|---|
| `_phase` | atomic\<Phase\> IDLE / HANDSHAKING / READY / CLOSING | slot 이 비었나, 누가 처리하나, 늦은 완료를 버릴지 | open, onHandshake DONE, apply(CLOSE), release | dispatch, settle, acquire, alive() (다른 thread) |
| `_deadline` | uint64_t, 0 이면 없음 | TIMEOUT 을 만들 시각 | apply, collect (만료시 0), extend | collect |
| `_work` | WorkContext | worker 와의 CAS 경계 | apply(START_WORK), finishWork, onWorkFinished | collect, onPeerGone |
| `_peer_closed` | atomic bool | Work 중 알게 된 끊김을 WORK_DONE 까지 | onPeerGone | onWorkFinished, isWorkConnected |
| `_stream_event`, `_notify_event` | Holder\<IoEvent\> | 걸린 요청 하나씩 | listener 만 | onTimeout, settle, onNotified |
| `_plain` | StageSet | 평문 | dispatch, 다른 thread (notify, finishWork) | resolve, pump, onNotified |
| `_tls` | TlsEngine | cipher StageSet + SSL | open, close, dispatch | resolve |
| `_resume_read` | IoAction | CONTROL 답장 중 미뤄 둔 READ(USER) | onReceived 가 WRITE(CONTROL) 낼 때 | onSent(CONTROL) → pump |
| `_write_length` | size_t | 보내는 사용자 WRITE 의 평문 길이 (onWritten 값) | stageWrite, finishWork | onSent(USER) |
| `_notify_plain_length` | size_t | 걸린 WRITE(NOTIFY) 의 평문 길이 (TLS) | onNotified | consumeNotification |
| `_handshake_timeout_ms`, `_app_context` | | | open | onHandshake, onError |
| `_socket`, `_slot`, `_generation` (atomic), `_context`, `_info`, `_state` | | 수명과 사용자 값 | open, release | |

### 9.3 인터페이스

```cpp
enum class Phase : uint8_t { IDLE, HANDSHAKING, READY, CLOSING };
typedef KiottyNetConnectionHandler Handler;

class Connection : public IoSource
{
public:
    Connection(int32_t slot, Pool& tls_pool, NotificationContext& notification, size_t recv_capacity, size_t send_capacity, size_t push_capacity, bool tls);
    bool valid() const;

    void       open(ActiveSocket&& socket, KiottyNetConnectionContext context, KiottyNetAppContext app_context, TlsProfile* profile, const char* hostname);
    void       release();                       // settle 이. _work.reset, Stage 비움, IDLE
    PoolHandle handle() const;
    int32_t    generation() const;
    bool       alive() const;                   // HANDSHAKING 또는 READY
    Phase      phase() const;

    SocketHandle     socket() const override;
    int32_t          slot() const override;
    Holder<IoEvent>& eventFor(const IoAction& action) override;
    IoPayload        resolve(const IoAction& action) override;
    void             close() override;          // shutdown + socket.close

    IoAction onOpened(const Handler& handler);                                   // open 직후. 평문 : onReady. TLS : onHandshake
    IoAction dispatch(const IoCompletion& completion, const Handler& handler);
    IoAction onNotified();                                                       // broadcast 가 slot 마다. 콜백 없음

    bool expireDeadline(uint64_t now);                                           // _deadline != 0 && now >= _deadline 이면 0 으로 돌리고 true
    bool workFinished() const;
    const Holder<IoEvent>& streamEvent() const;
    const Holder<IoEvent>& notifyEvent() const;

    KiottyNetConnectionInfo    info() const;                                     // 아무 thread
    KiottyNetConnectionContext context() const;
    int32_t                    state() const;
    void                       setState(int32_t state);
    KiottyNetCode              notify(KiottyNetSpan data);                       // 아무 thread
    KiottyNetCode              finishWork(int32_t generation, const KiottyNetConnectionAction& action);   // 아무 thread
    bool                       isWorkConnected(int32_t generation) const;

    size_t notificationWritable() const;        // ConnectionHost::broadcast 가 lock 안에서
    void   appendNotification(KiottyNetSpan data);

private:
    IoAction onReceived(const IoCompletion& completion, const Handler& handler);
    IoAction onSent(const IoCompletion& completion, const Handler& handler);
    IoAction onTimeout(const IoCompletion& completion, const Handler& handler);
    IoAction onWorkFinished(const IoCompletion& completion, const Handler& handler);
    IoAction onHandshake(const Handler& handler);
    IoAction onPeerGone(SocketCode code);
    IoAction adopt(const KiottyNetConnectionAction& returned, const Handler& handler);    // return → IoAction. READ 면 pump, WRITE 면 stageWrite
    IoAction extend(const KiottyNetConnectionAction& returned, const Handler& handler);   // onTimeout 에 요청이 걸려 있을 때 (10절)
    IoAction pump(IoAction read, const Handler& handler);                                 // Stage 에 찼으면 onRead 바로. loop
    IoAction stageWrite(KiottyNetSpan data);                                              // → _plain.send 또는 encode → cipher().send. 넘치면 CLOSE(BUFFER_FULL)
    IoAction apply(IoAction action, const Handler& handler);                              // deadline, Work 시작, CLOSING + onDisconnected
    void     consumeNotification(Stage& out, size_t sent);
    void     encodeSendStage();                                                           // worker 의 평문 → cipher().send
    Stage&   receiveStage(IoPurpose purpose);   // HANDSHAKE / TLS USER : cipher().receive. 평문 USER : _plain.receive
    Stage&   sendStage(IoPurpose purpose);      // HANDSHAKE / CONTROL / TLS USER : cipher().send. NOTIFY : cipher().notify 또는 _plain.notify. 평문 USER : _plain.send
};
```

### 9.4 dispatch

```cpp
IoAction Connection::dispatch(const IoCompletion& completion, const Handler& handler)
{
    IoAction action = IoAction::none();
    switch (completion.kind)
    {
    case IoKind::RECEIVE:   action = onReceived(completion, handler);     break;
    case IoKind::SEND:      action = onSent(completion, handler);         break;
    case IoKind::TIMEOUT:   action = onTimeout(completion, handler);      break;
    case IoKind::WORK_DONE: action = onWorkFinished(completion, handler); break;
    }
    return apply(action, handler);
}

IoAction Connection::apply(IoAction action, const Handler& handler)
{
    switch (action.kind)
    {
    case IoActionKind::NONE:
        return action;
    case IoActionKind::HOLD:
    case IoActionKind::READ:
    case IoActionKind::READ_UNTIL:
    case IoActionKind::WRITE:
        setDeadline(action.timeout_ms);
        return action;
    case IoActionKind::START_WORK:
        _deadline = 0;
        _work.start(generation());
        handler.onWorkStarted(_context, workHandle());
        return action;
    case IoActionKind::CLOSE:
        _deadline = 0;
        _phase = Phase::CLOSING;
        handler.onDisconnected(_app_context, _context, handle(), action.as.close.reason);
        return action;
    }
}

IoAction Connection::onOpened(const Handler& handler)
{
    IoAction action = IoAction::none();
    if (_tls.active())
    {
        action = onHandshake(handler);
    }
    else
    {
        KiottyNetConnectionAction returned = handler.onReady(_context, handle());
        action = adopt(returned, handler);
    }
    return apply(action, handler);
}

IoAction Connection::onHandshake(const Handler& handler)
{
    switch (_tls.handshake())
    {
    case TlsStep::SEND:    return IoAction::write(IoPurpose::HANDSHAKE, _handshake_timeout_ms);
    case TlsStep::RECEIVE: return IoAction::read(IoPurpose::HANDSHAKE, _handshake_timeout_ms, 0);
    case TlsStep::DONE:
    {
        _phase = Phase::READY;
        KiottyNetConnectionAction returned = handler.onReady(_context, handle());
        return adopt(returned, handler);
    }
    case TlsStep::FAILED:
        handler.onError(_app_context, handle(), _tls.lastCode(), _tls.lastMessage());
        return IoAction::close(_tls.lastCode());
    }
}

IoAction Connection::onReceived(const IoCompletion& completion, const Handler& handler)
{
    if (_phase == Phase::CLOSING)
    {
        return IoAction::none();                // 늦은 완료 (CANCELLED 포함)
    }
    if (completion.code != SocketCode::SUCCESS)
    {
        return onPeerGone(completion.code);
    }
    receiveStage(completion.action.purpose).provider().commit(completion.length);
    if (completion.action.purpose == IoPurpose::HANDSHAKE)
    {
        return onHandshake(handler);
    }
    if (_tls.active())
    {
        KiottyNetCode decoded = _tls.decode(_plain.receive);
        if (decoded != KIOTTY_SUCCESS)
        {
            handler.onError(_app_context, handle(), decoded, _tls.lastMessage());
            return IoAction::close(decoded);
        }
        if (_tls.hasControlOutput())
        {
            _resume_read = completion.action;
            return IoAction::write(IoPurpose::CONTROL, 0);
        }
    }
    return pump(completion.action, handler);
}

IoAction Connection::onSent(const IoCompletion& completion, const Handler& handler)
{
    if (_phase == Phase::CLOSING)
    {
        return IoAction::none();
    }
    if (completion.code != SocketCode::SUCCESS)
    {
        return onPeerGone(completion.code);
    }
    Stage& out = sendStage(completion.action.purpose);
    switch (completion.action.purpose)
    {
    case IoPurpose::NOTIFY:
        consumeNotification(out, completion.length);
        return onNotified();
    case IoPurpose::HANDSHAKE:
        out.consumer().commit(completion.length);
        return onHandshake(handler);
    case IoPurpose::CONTROL:
        out.consumer().commit(completion.length);
        return pump(_resume_read, handler);
    case IoPurpose::USER:
    {
        out.consumer().commit(completion.length);
        if (out.consumer().readable() > 0)
        {
            return IoAction::write(IoPurpose::USER, completion.action.timeout_ms);   // 남은 조각
        }
        out.compact();
        KiottyNetConnectionAction returned = handler.onWritten(_context, handle(), _write_length);
        return adopt(returned, handler);
    }
    }
}

void Connection::consumeNotification(Stage& out, size_t sent)
{
    std::lock_guard<std::mutex> lock(_notification.mutex);
    out.consumer().commit(sent);
    if (_tls.active())
    {
        _plain.notify.consumer().commit(_notify_plain_length);
    }
    if (out.consumer().readable() == 0)
    {
        out.compact();
    }
}

IoAction Connection::onTimeout(const IoCompletion&, const Handler& handler)
{
    if (_phase == Phase::HANDSHAKING)
    {
        handler.onError(_app_context, handle(), KIOTTY_TLS_HANDSHAKE_TIMEOUT, "tls: handshake timeout");
        return IoAction::close(KIOTTY_TLS_HANDSHAKE_TIMEOUT);
    }
    KiottyNetConnectionAction returned = handler.onTimeout(_context, handle());
    if (_stream_event.pending())
    {
        return extend(returned, handler);
    }
    return adopt(returned, handler);
}

IoAction Connection::extend(const KiottyNetConnectionAction& returned, const Handler& handler)
{
    if (returned.type == KIOTTY_ACTION_CLOSE)
    {
        return IoAction::from(returned);
    }
    if (returned.type == KIOTTY_ACTION_WRITE || returned.type == KIOTTY_ACTION_START_WORK)
    {
        handler.onError(_app_context, handle(), KIOTTY_BUSY, "action rejected: io outstanding. use notify");
    }
    setDeadline(returned.timeout_ms);
    return IoAction::none();
}

IoAction Connection::onWorkFinished(const IoCompletion&, const Handler& handler)
{
    IoAction outcome = _work.onFinished();
    if (_peer_closed.load())
    {
        return IoAction::close(KIOTTY_PEER_CLOSED);
    }
    if (outcome.kind == IoActionKind::WRITE && _tls.active())
    {
        encodeSendStage();
    }
    if (outcome.kind == IoActionKind::READ || outcome.kind == IoActionKind::READ_UNTIL)
    {
        return pump(outcome, handler);
    }
    return outcome;
}

IoAction Connection::onPeerGone(SocketCode code)
{
    if (_work.working())
    {
        _peer_closed.store(true);               // notify 완료로만 올 수 있다. onWorkFinished 가 닫는다
        return IoAction::none();
    }
    if (code == SocketCode::CLOSED)
    {
        return IoAction::close(KIOTTY_PEER_CLOSED);
    }
    return IoAction::close(KIOTTY_SOCKET_ERROR);
}

IoAction Connection::adopt(const KiottyNetConnectionAction& returned, const Handler& handler)
{
    IoAction action = IoAction::from(returned);
    if (action.kind == IoActionKind::WRITE)
    {
        return stageWrite(returned.as.write.data);
    }
    if (action.kind == IoActionKind::READ || action.kind == IoActionKind::READ_UNTIL)
    {
        return pump(action, handler);
    }
    return action;
}

IoAction Connection::pump(IoAction read, const Handler& handler)
{
    for (;;)
    {
        Result<KiottyNetCode, KiottyNetSpan> ready = satisfied(read, _plain.receive);   // 조건 초과 : REQUEST_TOO_LARGE. 안 찼으면 빈 span (scanned 전진)
        if (!ready)
        {
            return IoAction::close(ready.code());
        }
        if (ready.value().length == 0)
        {
            return read;                        // 더 받아야 한다
        }
        KiottyNetConnectionAction next = handler.onRead(_context, handle(), ready.value());
        _plain.receive.consumer().commit(ready.value().length);
        IoAction action = IoAction::from(next);
        if (action.kind == IoActionKind::WRITE)
        {
            return stageWrite(next.as.write.data);
        }
        if (action.kind != IoActionKind::READ && action.kind != IoActionKind::READ_UNTIL)
        {
            return action;
        }
        read = action;
    }
}

IoPayload Connection::resolve(const IoAction& action)
{
    switch (action.kind)
    {
    case IoActionKind::READ:
    case IoActionKind::READ_UNTIL:
    {
        Stage& in = receiveStage(action.purpose);
        size_t room = in.provider().writable();
        return IoPayload{ in.provider().prepare(room), room };
    }
    case IoActionKind::WRITE:
    {
        Stage& out = sendStage(action.purpose);
        size_t ready = out.consumer().readable();
        return IoPayload{ out.consumer().peek(), ready };
    }
    default:
        return IoPayload{ nullptr, 0 };
    }
}
```

| Phase | RECEIVE | SEND | TIMEOUT | WORK_DONE |
|---|---|---|---|---|
| IDLE | 오지 않는다 | | | |
| HANDSHAKING | commit → onHandshake | commit → onHandshake | onError(HANDSHAKE_TIMEOUT) → CLOSE | 오지 않는다 |
| READY | USER : (decode →) pump. CONTROL 답장이 생기면 READ 를 미루고 WRITE(CONTROL) | USER : 남은 조각 또는 onWritten. CONTROL : pump(미뤄 둔 READ). NOTIFY : onNotified | 걸린 것 없으면 adopt, 있으면 extend | outcome 또는 CLOSE(PEER_CLOSED) |
| CLOSING | NONE | NONE | 오지 않는다 | 오지 않는다 |

### 9.5 WorkContext

```cpp
class WorkContext                               // IDLE → WORKING (start, IO) → DONE (finish, worker, CAS release) → IDLE (onFinished, IO)
{
public:
    enum class State : uint8_t { IDLE, WORKING, DONE };
    void          start(int32_t generation);
    bool          finished() const;             // collect. acquire load == DONE
    IoAction      onFinished();                 // DONE → IDLE
    bool          working() const;              // WORKING 또는 DONE
    void          reset();
    KiottyNetCode finish(int32_t generation, IoAction outcome);   // generation 다르면 PEER_CLOSED. WORKING 아니면 BUSY
    bool          matches(int32_t generation) const;
private:
    std::atomic<State> _state;
    int32_t            _generation;
    IoAction           _outcome;
};

KiottyNetCode Connection::finishWork(int32_t generation, const KiottyNetConnectionAction& action)   // 아무 thread
{
    if (!_work.matches(generation))
    {
        return KIOTTY_PEER_CLOSED;
    }
    if (action.type == KIOTTY_ACTION_START_WORK)
    {
        return KIOTTY_INVALID_ARGUMENT;
    }
    IoAction outcome = IoAction::from(action);
    if (outcome.kind == IoActionKind::WRITE)    // worker 가 _plain.send 에 평문을 직접 쓴다. encode 는 IO Thread (onWorkFinished)
    {
        KiottyNetSpan data = action.as.write.data;
        if (_plain.send.provider().writable() < data.length)
        {
            return KIOTTY_BUFFER_FULL;
        }
        copy data into _plain.send;
        _write_length = data.length;
    }
    KiottyNetCode code = _work.finish(generation, outcome);
    if (code == KIOTTY_SUCCESS)
    {
        _notification.listener->wake();
    }
    return code;
}
```

Owner 의 close 는 WORKING 인 연결도 바로 닫고 generation 을 올린다. 늦은 finishWork 는 PEER_CLOSED.

### 9.6 NotificationContext, notify, onNotified, broadcast

```cpp
struct NotificationContext                      // host 에 하나. 모든 연결의 _plain.notify 를 한 mutex 가 지킨다
{
    std::mutex        mutex;
    EventListener*    listener;
    std::thread::id   io_thread;
    std::atomic<bool> queued;                   // notify 가 쌓였다. collect 가 기다리지 않게 한다
    bool isIoThread() const;
};

KiottyNetCode Connection::notify(KiottyNetSpan data)                 // 아무 thread
{
    {
        std::lock_guard<std::mutex> lock(_notification.mutex);
        if (!alive())
        {
            return KIOTTY_PEER_CLOSED;
        }
        if (_plain.notify.provider().writable() < data.length)
        {
            return KIOTTY_BUFFER_FULL;
        }
        copy data into _plain.notify;           // 평문. 경계 헤더 없음
    }
    _notification.queued.store(true);
    if (!_notification.isIoThread())
    {
        _notification.listener->wake();
    }
    return KIOTTY_SUCCESS;
}

IoAction Connection::onNotified()                                    // IO Thread. broadcast 와 onSent(NOTIFY)
{
    if (_notify_event.pending())
    {
        return IoAction::none();
    }
    if (!alive())
    {
        return IoAction::none();
    }
    std::lock_guard<std::mutex> lock(_notification.mutex);
    size_t plain = _plain.notify.consumer().readable();
    if (plain == 0)
    {
        return IoAction::none();
    }
    if (_tls.active())
    {
        KiottyNetSpan span = { _plain.notify.consumer().peek(), plain };
        _tls.encode(span, _tls.cipher().notify);
        _notify_plain_length = plain;
    }
    return IoAction::write(IoPurpose::NOTIFY, 0);
}

KiottyNetCode ConnectionHost::broadcast(KiottyNetSpan data)          // 아무 thread. 하나라도 모자라면 아무 데도 안 쓴다
{
    std::lock_guard<std::mutex> lock(_notification.mutex);
    for (size_t i = 0; i < _capacity; ++i)
    {
        Connection& c = _slots[i];
        if (c.alive() && c.notificationWritable() < data.length)
        {
            return KIOTTY_BUFFER_FULL;
        }
    }
    for (size_t i = 0; i < _capacity; ++i)
    {
        Connection& c = _slots[i];
        if (c.alive())
        {
            c.appendNotification(data);
        }
    }
    _notification.queued.store(true);
    if (!_notification.isIoThread())
    {
        _notification.listener->wake();
    }
    return KIOTTY_SUCCESS;
}
```

- 암호화는 IO Thread 만. `SSL*` 은 thread-safe 가 아니고 레코드 순서 = submit 순서여야 한다.
- 같은 socket 의 send 둘 (USER, NOTIFY) 은 건 순서대로 나간다. notify Stage 의 compact 는 걸린 send 가 없을 때만.
- instant (동기 송신) 는 없다. 실시간성은 broadcast 우선 + `no_delay`.

### 9.7 ConnectionHost

```cpp
class ConnectionHost                            // loop 도 listener 도 모른다
{
public:
    ConnectionHost(size_t capacity, Pool& tls_pool, size_t recv_capacity, size_t send_capacity, size_t push_capacity, bool tls);
    bool        valid() const;
    size_t      capacity() const;
    Connection& at(int32_t slot);               // IDLE 도 돌려준다. loop 는 전부 돈다
    Connection* acquire();                      // IDLE 하나. 없으면 nullptr. _count++
    void        release(Connection& connection);// settle 이. → IDLE, _count--
    bool        full() const;                   // _count == _capacity
    bool        empty() const;                  // _count == 0
    Connection* find(PoolHandle handle);        // generation 다르거나 IDLE 이면 nullptr
    NotificationContext& notification();
    KiottyNetCode notify(PoolHandle handle, KiottyNetSpan data);
    KiottyNetCode broadcast(KiottyNetSpan data);
private:
    Connection*         _slots;                 // placement new 한 번. 이동 없음
    size_t              _capacity;
    size_t              _count;
    NotificationContext _notification;
};
```

IDLE 인 Connection 은 deadline 0, Work IDLE, event 둘 비어 있음, `alive()` 거짓, CLOSING 아님. 그래서 collect / broadcast / settle 은 slot 을 전부 돌고 "비었나" 를 묻지 않는다.

### 9.8 Acceptor

```cpp
class Acceptor : public IoSource                // Server 만. slot -1
{
public:
    KiottyNetCode                    open(const KiottyNetEndpoint& endpoint, int backlog, bool no_delay);
    Result<SocketCode, ActiveSocket> accept();  // ACCEPT 완료 뒤. IOCP : AcceptEx 뒤처리 + SO_UPDATE_ACCEPT_CONTEXT. epoll : accept. no_delay 적용
    const Holder<IoEvent>&           event() const;
    SocketHandle     socket() const override;
    int32_t          slot() const override;
    Holder<IoEvent>& eventFor(const IoAction&) override;
    IoPayload        resolve(const IoAction&) override;   // IOCP : 미리 만든 소켓 + 주소 버퍼 2 × (sizeof(sockaddr_storage) + 16). epoll : { nullptr, 0 }
    void             close() override;
private:
    PassiveSocket   _socket;
    Holder<IoEvent> _event;
    SocketHandle    _next;                      // IOCP 전용
    char            _addresses[ADDRESS_BUFFER];
    bool            _no_delay;
};
```

## 10. io_loop

```cpp
class IoLoop
{
public:
    IoLoop(ConnectionHost& host, EventListener& listener, Acceptor* acceptor, TlsProfile* profile, bool stop_when_empty);
    KiottyNetCode run(const Handler& handler, KiottyNetAppContext app_context);   // 부른 thread 가 IO Thread
    void          stop();                                                         // 아무 thread. _stopped = true, wake
    void          admit(ActiveSocket&& socket, const char* hostname);             // EventClient 가 run 직전에
private:
    void                     runOnce();
    Span<const IoCompletion> collect();
    void                     broadcast();
    void                     step(const IoCompletion& completion);
    IoAction                 access(const IoCompletion& completion);
    void                     submit(IoSource& source, const IoAction& action);
    void                     settle();
    void                     shutdown();
    ConnectionHost&     _host;
    EventListener&      _listener;
    Acceptor*           _acceptor;
    TlsProfile*         _profile;
    const Handler*      _handler;
    KiottyNetAppContext _app_context;
    IoCompletion*       _completions;           // listener capacity + 2 × host capacity
    std::atomic<bool>   _stopped;
    bool                _stop_when_empty;
};
```

한 바퀴: collect → broadcast → step (completion 마다 dispatch → submit 한 번) → settle. 콜백은 step 에서만, listener 호출은 collect 와 submit 에서만.

```cpp
KiottyNetCode IoLoop::run(const Handler& handler, KiottyNetAppContext app_context)
{
    _handler = &handler;
    _app_context = app_context;
    _host.notification().io_thread = std::this_thread::get_id();
    _host.notification().listener = &_listener;
    if (_acceptor)
    {
        submit(*_acceptor, IoAction::accept());
    }
    while (!_stopped)
    {
        runOnce();
    }
    shutdown();
    return KIOTTY_SUCCESS;
}

void IoLoop::runOnce()
{
    Span<const IoCompletion> completions = collect();
    broadcast();
    for (const IoCompletion& completion : completions)
    {
        step(completion);
    }
    settle();
}

Span<const IoCompletion> IoLoop::collect()
{
    uint32_t timeout = nearestDeadline(monotonicMs());
    if (_host.notification().queued.load())
    {
        timeout = 0;
    }
    Span<const IoCompletion> waited = _listener.wait(timeout);
    size_t count = copy(waited, _completions);
    uint64_t now = monotonicMs();
    for (int32_t i = 0; i < _host.capacity(); ++i)
    {
        Connection& c = _host.at(i);
        if (c.expireDeadline(now))
        {
            _completions[count++] = IoCompletion::timeout(i);
        }
        if (c.workFinished())
        {
            _completions[count++] = IoCompletion::workDone(i);
        }
    }
    return Span<const IoCompletion>{ _completions, count };
}

void IoLoop::broadcast()
{
    _host.notification().queued.store(false);
    for (int32_t i = 0; i < _host.capacity(); ++i)
    {
        Connection& c = _host.at(i);
        IoAction action = c.onNotified();
        submit(c, action);
    }
}

void IoLoop::step(const IoCompletion& completion)
{
    IoSource* source = nullptr;
    IoAction  action = IoAction::none();
    switch (completion.kind)
    {
    case IoKind::WAKE:
        return;
    case IoKind::ACCEPT:
        source = _acceptor;
        action = access(completion);
        break;
    case IoKind::RECEIVE:
    case IoKind::SEND:
    case IoKind::TIMEOUT:
    case IoKind::WORK_DONE:
    {
        Connection& c = _host.at(completion.slot);
        source = &c;
        action = c.dispatch(completion, *_handler);
        break;
    }
    }
    submit(*source, action);
}

IoAction IoLoop::access(const IoCompletion& completion)
{
    if (completion.code == SocketCode::CANCELLED)
    {
        return IoAction::none();                // shutdown 이 acceptor 를 닫았다
    }
    if (completion.code != SocketCode::SUCCESS)
    {
        _handler->onError(_app_context, 0, toNetCode(completion.code), "accept failed");
        return IoAction::accept();
    }
    Result<SocketCode, ActiveSocket> accepted = _acceptor->accept();
    if (!accepted)
    {
        _handler->onError(_app_context, 0, toNetCode(accepted.code()), "accept failed");
        return IoAction::accept();
    }
    admit(std::move(accepted.value()), nullptr);
    if (_host.full())
    {
        return IoAction::none();                // backlog 가 받는다. settle 이 다시 건다
    }
    return IoAction::accept();
}

void IoLoop::admit(ActiveSocket&& socket, const char* hostname)
{
    KiottyNetConnectionDecision decision = _handler->onConnected(_app_context, socket.peer());
    if (!decision.accepted)
    {
        socket.close();
        return;
    }
    Connection& c = *_host.acquire();           // accept 는 full 이 아닐 때만 걸었다
    c.open(std::move(socket), decision.connection_context, _app_context, _profile, hostname);
    _listener.attach(c.socket(), c.slot());
    IoAction first = c.onOpened(*_handler);
    submit(c, first);
}

void IoLoop::submit(IoSource& source, const IoAction& action)
{
    switch (action.kind)
    {
    case IoActionKind::NONE:
    case IoActionKind::HOLD:
    case IoActionKind::START_WORK:
        return;
    case IoActionKind::CLOSE:
        _listener.detach(source.socket(), source.slot());
        source.close();
        return;
    case IoActionKind::READ:
    case IoActionKind::READ_UNTIL:
    case IoActionKind::WRITE:
    case IoActionKind::ACCEPT:
        _listener.submit(source.eventFor(action), source.socket(), action, source.resolve(action));
        return;
    }
}

void IoLoop::settle()
{
    for (int32_t i = 0; i < _host.capacity(); ++i)
    {
        Connection& c = _host.at(i);
        if (c.phase() != Phase::CLOSING)
        {
            continue;
        }
        if (c.streamEvent().pending() || c.notifyEvent().pending())
        {
            continue;
        }
        _host.release(c);
    }
    if (_acceptor && !_stopped && !_acceptor->event().pending() && !_host.full())
    {
        submit(*_acceptor, IoAction::accept());
    }
    if (_stop_when_empty && _host.empty())
    {
        _stopped = true;
    }
}

void IoLoop::shutdown()
{
    if (_acceptor)
    {
        submit(*_acceptor, IoAction::close(KIOTTY_CLOSED));
    }
    for (int32_t i = 0; i < _host.capacity(); ++i)
    {
        Connection& c = _host.at(i);
        if (!c.alive())
        {
            continue;
        }
        IoAction closing = c.apply(IoAction::close(KIOTTY_CLOSED), *_handler);
        submit(c, closing);
    }
    while (!_host.empty())
    {
        runOnce();                              // CANCELLED 완료가 다 돌아올 때까지
    }
}
```

handler 안의 notify 는 다음 바퀴 첫 broadcast 에서 나간다 (collect 는 queued 면 기다리지 않는다). 다른 thread 의 notify 는 wake → broadcast 로 콜백을 기다리지 않는다.

TLS 서버 연결의 바퀴: ① ACCEPT → access → admit → onHandshake → READ(HS) ② RECEIVE(HS) → WRITE(HS) ③ SEND(HS) → READ(HS) ④ RECEIVE(HS) → DONE → onReady → READ(USER) ⑤ RECEIVE(USER) → decode → pump → onRead → WRITE(USER) ⑥ WAKE → broadcast → onNotified → WRITE(NOTIFY) ⑦ SEND(USER) → onWritten → READ(USER), SEND(NOTIFY) → onNotified → NONE.

## 11. TIMEOUT 과 CLOSE

TIMEOUT 뒤가 "걸린 요청 위에 다른 action" 이 생길 수 있는 유일한 자리다.

| `_stream_event.pending()` | handler 의 return | 일어나는 일 |
|---|---|---|
| false (HOLD 뒤, notify 만 걸림) | 무엇이든 | adopt → apply → submit |
| true | CLOSE | 닫는다. 걸린 요청은 CANCELLED 완료로 돌아오고 settle 이 반납 |
| true | HOLD, READ, READ_UNTIL | 연장. 걸린 요청 그대로, deadline 만 timeout_ms 뒤로 |
| true | WRITE, START_WORK | onError(KIOTTY_BUSY, "action rejected: io outstanding. use notify") 한 번, 그리고 연장 |

HANDSHAKING 의 TIMEOUT 은 onError(TLS_HANDSHAKE_TIMEOUT) → CLOSE. Work 중에는 deadline 이 0 이라 TIMEOUT 이 없다. extend 는 adopt 를 거치지 않으므로 거부된 WRITE 의 데이터가 Stage 에 남지 않는다.

CLOSE 의 공통 순서 (handler return, onPeerGone, handshake 실패, worker outcome, Owner close 모두):

1. `apply(CLOSE)` : `_deadline = 0`, `_phase = CLOSING`, `onDisconnected` (accept 된 연결은 반드시 한 번)
2. `submit(CLOSE)` : `listener.detach`, `source.close()` (shutdown + closesocket)
3. 걸려 있던 요청마다 CANCELLED 완료 (IOCP 는 OS 가, epoll 은 detach 가) → onReceived / onSent 가 CLOSING 을 보고 NONE
4. `settle` : 두 event 가 비면 `host.release` → IDLE. slot 재사용 전에 늦은 완료가 다 돌아오므로 completion 에 generation 을 싣지 않는다

## 12. server, client

```cpp
class Owner                                     // HOLDER_SIZE 1024, ALIGN 16
{
public:
    virtual ~Owner();
    virtual void shutdown() = 0;                // close + run 의 return 대기
};

class Server : public Owner
{
public:
    Server(const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig* tls, Pool& tls_pool);   // _code 에 생성 결과
    KiottyNetCode code() const;
    KiottyNetCode run(const Handler& handler, KiottyNetAppContext app_context);   // acceptor.open → _code + notify_all → _loop.run → _code + notify_all
    void          close();                                                      // _loop.stop()
    KiottyNetCode await(uint64_t timeout_ms);                                   // wait_for(_code != BUSY). 돌고 있으면 SUCCESS, 끝났으면 run 의 값, 지나면 TIMEOUT
    KiottyNetCode broadcast(KiottyNetSpan data);
    ConnectionHost& host();
    void shutdown() override;
private:
    KiottyNetServerConfig _config;  TlsProfile _tls;  ConnectionHost _host;  EventListener _listener;  Acceptor _acceptor;
    IoLoop _loop;                               // acceptor 있음, stop_when_empty false
    std::mutex _mutex;  std::condition_variable _cv;  KiottyNetCode _code;
};

class EventClient : public Owner                // capacity 1
{
public:
    EventClient(const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig* tls, Pool& tls_pool);
    KiottyNetCode run(const Handler& handler, KiottyNetAppContext app_context);   // connectWithRetry → _loop.admit(socket, host) → _loop.run
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);
    PoolHandle    connection() const;            // onConnected 전이면 { 0, 0 }
    KiottyNetCode notify(KiottyNetSpan data);
    ConnectionHost& host();
    void shutdown() override;
private:
    KiottyNetClientConfig _config;  TlsProfile _tls;  ConnectionHost _host;  EventListener _listener;
    IoLoop _loop;                               // acceptor nullptr, stop_when_empty true
    std::atomic<bool> _stop;  std::mutex _mutex;  std::condition_variable _cv;  KiottyNetCode _code;
};

class StreamClient : public Owner               // 동기. loop 없음. 한 번에 한 thread
{
public:
    StreamClient(const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig* tls, Pool& tls_pool);
    KiottyNetCode   connect();                  // connectWithRetry → TLS 면 handshake 를 for 로 : SEND 면 sendAll(cipher().send), RECEIVE 면 receiveSome(cipher().receive)
    void            disconnect();
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_ms);    // _receive_stage 가 비어 있으면 receiveSome (+ decode) 반복
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_ms);           // 평문 sendAll. TLS 는 send_capacity 씩 encode → sendAll
    void shutdown() override;
private:
    KiottyNetCode sendAll(Stage& out, uint32_t timeout_ms);       // non-blocking send + poll 반복
    KiottyNetCode receiveSome(Stage& in, uint32_t timeout_ms);    // poll 한 번 + recv 한 번
    KiottyNetClientConfig _config;  TlsProfile _tls_profile;  ActiveSocket _socket;  TlsEngine _tls;  Stage _receive_stage;  std::atomic<bool> _stop;
};
```

## 13. context, config

```cpp
class Context
{
public:
    static const size_t MAX_OWNERS = 16;
    Context(size_t tls_pool_bytes);             // WSAStartup. 프로세스 첫 Context 면 CRYPTO_set_mem_functions 로 Pool 을 OpenSSL 에. g_contexts 에 등록
    ~Context();                                 // 남은 소유자 shutdown + 파괴. run 을 부른 thread 에서 부수면 assert
    KiottyNetCode code() const;
    template <typename T, typename... Args> Result<KiottyNetCode, T*> create(Args&&... args);   // 빈 slot 에 Holder<Owner>::make<T>. 없으면 TABLE_FULL. T::code() 실패면 반납
    void     destroy(Owner* owner);
    Owner*   find(uint32_t owner_slot, uint32_t owner_generation);
    uint32_t slot() const;
private:
    Holder<Owner> _owners[MAX_OWNERS];
    uint32_t      _generations[MAX_OWNERS];
    Pool          _tls_pool;
    std::mutex    _mutex;
};
static const size_t MAX_CONTEXTS     = 16;
static const size_t DEFAULT_TLS_POOL = 32 * 64 * 1024;   // initializeContext 는 인자 없음. 모자라면 fallbackCount 가 센다
static Context* g_contexts[MAX_CONTEXTS];
```

handle (int64): `generation (32) | index (32)`, index = `context_slot (4) | owner_slot (4) | owner_generation (8) | connection_slot (16)`. `conn(handle)` = `g_contexts[ctx] → Context::find(owner, gen) → host().find(PoolHandle)`. 하나라도 nullptr 이면 아무 일도 하지 않고 PEER_CLOSED.

config: `initialize* / configure*` 와 1:1 인 자유 함수. 길이 검사, 복사, 기본값 (`no_delay = true`), `initialization_mark` (0x4B4E4554 "KNET"), Unix domain 경로 길이 (< 108). `isInitialized` 는 4 종류 오버로드. `config/kiotty_action.h` 는 action struct 조립만.

## 14. thread 규칙

| 클래스 | 부르는 thread |
|---|---|
| Context | 아무 thread (mutex). 소멸자는 run 을 부른 thread 가 아니어야 한다 |
| Server, EventClient | `run` 은 IO Thread 가 되는 thread. `close`, `await`, `broadcast`, `notify` 는 아무 thread |
| IoLoop | `run` IO Thread. `stop` 아무 thread |
| Connection | `notify`, `finishWork`, `isWorkConnected`, `info`, `alive` 는 아무 thread. 나머지 IO Thread |
| EventListener | `wake` 만 아무 thread |
| TlsEngine, socket | 소유 객체의 thread. `SSL*` 은 IO Thread 만 |
| StreamClient | 한 번에 한 thread |

다른 thread 의 호출: `finishWork` 는 `_plain.send` 복사 → CAS → wake. `notify` / `broadcast` 는 lock 안에서 `_plain.notify` 복사 → `queued` → (IO Thread 아니면) wake. Owner `close` 는 `_stopped` → wake. 콜백 안에서 handle 로 할 수 있는 것은 `getConnectionInfo` 와 `notifyConnection` 둘이라 재진입이 없다.

## 15. base.cpp 번역 표

| C | 번역 |
|---|---|
| `KiottyNet_initialize*Config`, `configure*` | `config::` 같은 이름. NULL 이면 INVALID_ARGUMENT |
| `holdCommunication`, `readConnection`, `readConnectionUntil`, `writeConnection`, `startWork`, `closeConnection`, `acceptConnection`, `ignoreConnection` | `action::` 같은 이름 |
| `initializeContext` | `new (nothrow) Context(DEFAULT_TLS_POOL)`. code() 실패면 delete |
| `releaseContext` | `delete` |
| `createServer(ctx, config, tls)` | `ctx->create<Server>(*config, tls, ctx->tlsPool())`. NULL / `!isInitialized` 면 INVALID_ARGUMENT |
| `destroyServer` | `ctx->destroy(server)` |
| `run / close / await / broadcastServer`, EventClient 의 `run / close / await / notify`, StreamClient 의 `connect / disconnect / read / write` | 같은 이름의 메서드 |
| `getEventClientConnection` | `toRaw(client->connection())` |
| `getConnectionInfo(h)` | `conn(h) ? conn(h)->info() : {}` |
| `notifyConnection(h, data)` | `host(h)->notify(fromRaw(h), data)` |
| `finishWork(w, action)` | `conn(w)->finishWork(fromRaw(w).generation, action)` |
| `isWorkConnected(w)` | `conn(w) ? conn(w)->isWorkConnected(gen) : false` |

StateMachine 은 `Connection::state() / setState() / context()` 를 쓴다.

## 16. 메모리 (32 client, 4 KiB 기준)

| 객체 | 잡는 것 | 크기 |
|---|---|---|
| Connection (평문) | 객체 + StageSet (recv + send + push) + IoEvent 둘 | 약 12 KiB |
| Connection (TLS) | 위 + cipher StageSet (receive 16 KiB + 헤더, send / notify 는 capacity + 레코드 오버헤드) + SSL (Pool, SSL_clear 재사용) | 약 42 KiB + SSL |
| ConnectionHost | Connection[max_clients] + NotificationContext | 평문 약 400 KiB, TLS 약 1.4 MiB |
| Server | 위 + listener storage + 완료 배열 + Acceptor + IoLoop | |
| EventClient | capacity 1 | 평문 약 13 KiB |
| Context | Holder\<Owner\>[16] + TLS Pool | 2 MiB |

## 17. 구현 순서

① base.h 를 3절대로 고치고 statemachine.h 를 맞춘다 ② core (StageSet, Span, PoolHandle, Pool, Clock) ③ error / socket / event 헤더 + IOCP backend ④ tls (stub 먼저) ⑤ connection (IoSource, WorkContext, NotificationContext, Connection, Acceptor, ConnectionHost) ⑥ io_loop ⑦ server / client / context / config ⑧ base.cpp 번역.

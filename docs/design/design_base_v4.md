# kiotty_net_base.h 구현 설계 v4

2026-10-31. `design_base_v2.md` 위에 v6 / v7 피드백과 그 뒤의 논의 (송신 맥락의 분리, write lane 하나, Stream) 를 반영한 구현용 요약이다. 역사와 근거는 `design_base_v6_feedback.html`, `design_base_v7_feedback.html` 에 있다. 이 문서는 클래스의 인터페이스와 한 바퀴의 순서만 적는다. 두 문서가 다르면 requirement 가, 흐름이 다르면 implementation_guide 가 우선한다. 사이클 0 ~ 3b 에서 이미 코드가 된 부분 (core, error, socket, event, IOCP) 은 바뀐 줄만 적고 나머지는 v2 를 가리킨다.

설계 방침 (이 문서가 지키는 세 가지)

1. **맥락은 맥락끼리.** 받는 맥락 (receive), 보내는 맥락 셋 (user send, notify, TLS 의 cipher 출력), Work 맥락, 수명 맥락 (phase) 은 각자 자기 Stage 와 자기 완료 처리를 가진다. 한 맥락의 코드가 다른 맥락의 변수를 읽거나 바꾸지 않는다. 맥락 사이를 잇는 것은 IoAction 과 IoCompletion 두 값 타입뿐이다.
2. **엣지 케이스는 프로세스가 삼킨다.** "W 가 1 이면 ...", "둘이 동시에 끝나면 ..." 같은 분기를 두지 않는다. 그런 상황이 생기지 않도록 프로세스를 놓는다. socket 당 WRITE 는 하나이므로 "동시에 끝남" 이 없고, 송신은 Stage 에 넣는 것과 Stage 에서 꺼내 거는 것이 다른 프로세스이므로 넣는 쪽이 "지금 걸 수 있나" 를 묻지 않는다. 늦은 완료와 닫힌 뒤의 local completion 은 dispatch 의 첫 줄 하나 (phase 라우팅) 가 전부 받는다.
3. **프로세스는 input → output 이다.** 한 바퀴는 여섯 프로세스 (drain, settle, collectIoEvents, step, collectLocalEvents, step) 이고 각 프로세스는 더 작은 프로세스 (dispatch → apply → submit, pick → submit) 로 쪼개진다. 각 단계의 input 과 output 은 값이고, side effect 는 그 단계가 소유한 Stage / event / 상태에만 생긴다. 다른 thread 가 쓰는 것은 세 자리 (notify Stage, WorkContext 의 CAS, listener.wake) 뿐이고 전부 "적고 깨운다" 이다.

원칙 (v2 그대로)

1. `src/kiotty_net_base.cpp` 는 번역만 한다. 검사, 분기, 상태 변경, 복사를 두지 않는다 (15절).
2. 클래스는 최소 함수와 최소 변수만 가진다. 상태를 읽어 판단하는 줄은 `pending()` 넷 (pickWrite 의 write event, onTimeout 의 userActionPending, collectLocalEvents 의 workReady, settle) 밖에 두지 않는다.
3. 의존은 아래로만 간다. OS 가 다른 부분은 그 폴더의 `backend/` 안에만, OpenSSL 은 `tls/` 안에만 있다. listener 는 바이트와 방향만 안다.
4. 한 줄에 문장 하나. 중첩 호출은 지역 변수로 푼다. cancel 은 없다. 요청은 completion 으로만 비워진다.

```
kiotty_net_base.cpp ── 번역
    ├── Context ───── Holder<Owner> slot 16, TLS Pool, g_contexts[16]
    ├── Server ──────┐
    ├── EventClient ─┼── Owner ── ConnectionHost + EventListener + IoLoop (+ Acceptor) + TlsProfile
    └── StreamClient ┘               IoLoop ── drain (pick → submit) → settle → collectIoEvents → step (dispatch → apply → submit) → collectLocalEvents → step
                                     Connection ── receive Stage, Sender 둘 (send, notify), TlsEngine, WorkContext, ConnectionStream, IoEvent 둘 (read, write)
                                     Acceptor ──── PassiveSocket, IoEvent 하나
                                     EventListener + IoEvent ── IOCP (있음) / epoll / kqueue / io_uring
                                     ActiveSocket / PassiveSocket (있음), TlsProfile / TlsEngine
core/ : Stage, StageSet, Span, Holder, Result, PoolHandle, Pool, Clock (있음)
```

## 1. 폴더

```
src/
    kiotty_net_base.cpp
    implementation/
        core/                       namespace kiotty                        (있음. 바뀌지 않는다)
        base/                       namespace kiotty::net
            error/      (있음)
            socket/     (있음)
            event/      kiotty_io_action.h (as.write.length 추가), kiotty_event.h (IoKind::WORK_READY 추가), kiotty_event_listener.h (그대로),
                        backend/kiotty_iocp_event_listener.cpp (있음), kiotty_epoll_event_listener.cpp, kiotty_kqueue_*, kiotty_io_uring_*
            tls/        kiotty_tls_profile.h, kiotty_tls_engine.h, backend/kiotty_openssl_tls.cpp | kiotty_stub_tls.cpp
            stream/     kiotty_connection_stream.h/.cpp                 동기 통로. Connection (WORKING) 과 StreamClient 가 같이 쓴다
                        backend/kiotty_posix_file.cpp, kiotty_windows_file.cpp   writeFile 의 파일 열기 / 읽기
            connection/ kiotty_io_source.h, kiotty_sender.h, kiotty_work_context.h, kiotty_notification_context.h,
                        kiotty_connection.h/.cpp, kiotty_acceptor.h/.cpp, kiotty_connection_host.h/.cpp
            io_loop/    kiotty_io_loop.h/.cpp
            server/     kiotty_server.h/.cpp
            client/     kiotty_event_client.h/.cpp, kiotty_stream_client.h/.cpp
            context/    kiotty_owner.h, kiotty_context.h/.cpp
            config/     kiotty_config.h/.cpp, kiotty_action.h
```

- backend 는 전부 `.cpp`. OS struct 는 `.cpp` 안의 struct 에 두고 `Holder<IoEvent>` 로 값처럼 든다.
- `KIOTTY_NET_USE_TLS` 가 보이는 자리는 tls/backend 의 파일 선택 하나.

## 2. 규약

v2 2절 그대로. 더하는 것:

- Connection 안에서 Stage 를 직접 만지는 클래스는 그 Stage 의 소유자 하나다. receive Stage 는 Connection 의 수신 코드 (drive / refill) 와 ConnectionStream (WORKING) 만, send Stage 는 `Sender _send` 만, notify Stage 는 `Sender _notify` 만, cipher Stage 둘은 TlsEngine 과 ConnectionStream (WORKING) 만 만진다.
- 콜백은 IO Thread 의 step 안 (`onOpened`, `dispatch`) 에서만 나간다. drain, settle, collectIoEvents, collectLocalEvents, resolve, pick 은 handler 를 받지 않는다.
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

typedef int64_t KiottyNetConnectionHandle;      /* 비동기. 모든 콜백에. getConnectionInfo / notifyConnection 만 */
typedef int64_t KiottyNetConnectionStream;      /* 동기. onWorkStarted 가 준다. closeConnectionStream 까지 유효. KiottyNetWorkHandle 은 없다 */

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

KiottyNetContextResult KiottyNet_initializeContext(void);                          /* TLS Pool 8 MiB */
KiottyNetContextResult KiottyNet_initializeContextWithTlsPool(size_t bytes);
```

| 항목 | 값 |
|---|---|
| `max_clients` 기본 | 128 |
| `max_clients` 상한 | 1024. `configureServerMaxClients` 가 0 또는 1024 초과면 `KIOTTY_INVALID_ARGUMENT` |
| TLS Pool 기본 | 8 MiB (128 × 64 KiB) |

지우는 것: `KiottyNetWorkHandle`, `KiottyNet_finishWork`, `KiottyNet_isWorkConnected`, 옛 이름의 action 함수 여섯 (`holdCommunication`, `readConnection`, `readConnectionUntil`, `writeConnection`, `startWork`, `closeConnection`). C++ 의 `KiottyNet_Work` 는 `KiottyNet_ConnectionStream` 으로 이름을 바꾸고 `read / write / writeFile / open / close(action)` 을 가진다. 소멸자는 close 하지 않은 Stream 을 `thenClose(KIOTTY_SUCCESS)` 로 닫는다.

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
| IO Thread 에서 read / write / writeFile | `KIOTTY_BUSY` (`NotificationContext::isIoThread()`) |
| WORKING 이 아닌 연결의 Stream (close 한 뒤, 또는 Work 가 끝난 뒤) | `KIOTTY_BUSY` |
| 연결이 닫히고 slot 이 재사용됨 (generation 불일치) | `KIOTTY_PEER_CLOSED` |
| Owner 가 WORKING 중에 닫음 | read / write 는 socket 의 0 / 오류를 그대로 (`PEER_CLOSED` / `SOCKET_ERROR`). `isConnectionStreamOpen` 은 false. `closeConnectionStream` 은 `KIOTTY_CLOSED` 를 돌려주되 Work 는 끝난다 |
| Stream 이 아닌 값 (0, Connection handle 을 넘김) | handle 의 타입 비트로 구분. `KIOTTY_INVALID_ARGUMENT` |
| `closeConnectionStream(stream, thenWork())` | `KIOTTY_INVALID_ARGUMENT`. Stream 은 유효하게 남는다 |
| `closeConnectionStream(stream, thenWrite(data))` 의 data 가 `send_capacity` 초과 | `KIOTTY_BUFFER_FULL`. Stream 은 유효하게 남는다 |

## 4. core

v2 4절 그대로. 코드가 있다. `StageSet { receive, send, notify }` 는 평문 Stage 셋의 저장 공간으로 그대로 쓰고, 9.3 의 `Sender` 둘이 send / notify 를 참조로 든다.

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
enum class IoKind : uint8_t { ACCEPT, RECEIVE, SEND, WAKE, TIMEOUT, WORK_READY, WORK_DONE };   // 뒤 셋은 collectLocalEvents 가 만든다 (local completion)
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

3b 테스트에서 바뀌는 것: `WriteLargerThanSocketBufferCompletesPossiblyPartiallyAndTheRestIsResubmittable` 을 `WriteLargerThanSocketBufferCompletesOnceWithFullLength` 로. `TwoSendsSubmittedInOrderArriveAtPeerInThatOrder` 와 `DetachWithPendingReadAndWriteDeliversTwoCancelled` 는 event 둘을 read / write 모양 (`IoAction::read` 하나 + `IoAction::write` 하나) 으로 다시 쓴다. listener 구현은 바뀌지 않는다.

## 8. tls

v2 8절에서 cipher Stage 가 셋에서 둘로 준다. 송신 암호문은 하나다.

```cpp
class TlsProfile { /* v2 그대로. SSL_OP_NO_RENEGOTIATION 을 켠다 */ };
enum class TlsStep : uint8_t { SEND, RECEIVE, DONE, FAILED };
class TlsEngine                                 // 연결 하나의 SSL + BIO 둘 + cipher Stage 둘
{
public:
    TlsEngine(Pool& pool, size_t receive_capacity, size_t send_capacity);   // 둘 다 0 이면 평문 (active 늘 false)
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

## 9. connection

### 9.1 IoSource

```cpp
class IoSource                                  // Connection, Acceptor 의 base. submit 이 보는 전부
{
public:
    virtual ~IoSource();
    virtual SocketHandle     socket() const = 0;
    virtual int32_t          slot() const = 0;
    virtual Holder<IoEvent>& eventFor(const IoAction& action) = 0;   // 방향으로 고른다. WRITE → write event, 그 외 → read event. Acceptor 는 하나
    virtual IoPayload        resolve(const IoAction& action) = 0;    // action 의 purpose 가 가리키는 Stage 의 자리 (9.6)
    virtual SocketCode       attach(EventListener& listener) = 0;
    virtual void             detach(EventListener& listener) = 0;
    virtual void             shutdown() = 0;    // FIN. submit(CLOSE). socket.close 는 release 가 (9.5)
};
```

### 9.2 Connection 이 드는 것

| 변수 | 타입 | 맥락 | 바뀌는 자리 | 읽는 자리 |
|---|---|---|---|---|
| `_phase` | atomic\<Phase\> IDLE / HANDSHAKING / READY / CLOSING | 수명 | open, onHandshake DONE, apply(CLOSE), release | dispatch 첫 줄, pickWrite, settle, alive() (다른 thread) |
| `_deadline` | uint64_t, 0 이면 없음 | timeout | apply, collectLocalEvents (만료시 0) | collectLocalEvents, waitTimeout |
| `_work` | WorkContext IDLE / REQUESTED / WORKING / DONE | Work | apply(START_WORK) request, apply(CLOSE) abandon, onWorkReady start, closeStream (worker) finish, collectLocalEvents onFinished, release reset | collectLocalEvents, pickWrite, settle, closeStream (worker) |
| `_read_event`, `_write_event` | Holder\<IoEvent\> | 방향마다 걸린 요청 하나 | listener 만 | pickWrite, onTimeout, collectLocalEvents, settle |
| `_plain` | StageSet { receive, send, notify } | 저장 공간 | — | 아래 셋이 참조 |
| `_send` | Sender (send Stage) | user send | adopt(WRITE) / worker 의 closeStream(WRITE) 가 stage, onSent(USER) 가 release | pickWrite, onTimeout |
| `_notify` | Sender (notify Stage) | notify | notify / broadcast (아무 thread) 가 stage, onSent(NOTIFY) 가 release. 전부 `NotificationContext::mutex` 안 | pickWrite (lock 안) |
| `_tls` | TlsEngine (cipher receive / send, SSL) | TLS | onHandshake, refill 의 decode, pickWrite 의 encode, onSent 의 cipherSend consume | resolve |
| `_stream` | ConnectionStream (참조 넷: socket, tls, receive Stage, send Stage) | Work 의 동기 통로 | — | worker (WORKING 동안만) |
| `_handshake_timeout_ms`, `_app_context` | | | open | onHandshake, onError |
| `_socket`, `_slot`, `_generation` (atomic), `_context`, `_info`, `_state` | | 수명과 사용자 값 | open, release | |

v2 9.2 와 비교해 없어진 것과 이유:

| v2 | v4 | 왜 |
|---|---|---|
| `_stream_event`, `_notify_event` | `_read_event`, `_write_event` | 용도가 아니라 방향으로 나눈다. socket 당 write 는 하나 |
| `_resume_read` | 없음 | control 답장은 cipherSend 에 쌓이고 write lane 이 보낸다. READ 는 미루지 않는다 |
| `_write_length`, `_notify_plain_length` | 없음 | 평문 길이는 `IoAction::as.write.length` 에 실려 완료가 가져온다 |
| `_peer_closed` | 없음 | WORKING 동안 IO Thread 는 socket 을 보지 않는다. worker 의 Stream 이 끊김을 직접 본다 |
| `_tls.cipher().notify` | 없음 | 송신 암호문은 cipherSend 하나 |
| `NotificationContext::queued` | 없음 | 적는 thread 가 늘 wake 하고, IO Thread 가 적은 것은 wait 전의 drain 이 건다 (10절) |

### 9.3 Sender : 보내는 맥락 하나

```cpp
class Sender                                    // connection/kiotty_sender.h. send 와 notify 가 각각 하나씩. Stage 를 참조로 든다. lock 은 모른다
{
public:
    explicit Sender(Stage& stage);
    KiottyNetCode stage(KiottyNetSpan data);    // 복사. 자리가 모자라면 BUFFER_FULL 이고 아무것도 넣지 않는다
    size_t        readable() const;             // 지금 보낼 수 있는 평문 길이
    Span<char>    peek();                       // readable 전체. pick 이 payload 또는 encode 입력으로 쓴다
    void          release(size_t length);       // 완료 뒤. consume 하고 compact
    void          clear();                      // release 가. 전부 버린다
};
```

- Sender 는 "한 Stage 에 넣고, 보고, 치운다" 셋만 한다. 누가 어느 thread 에서 부르는지는 Connection 의 맥락이 정한다. send 는 "RUNNING 이면 IO Thread, WORKING 이면 worker" 라 동시에 둘인 순간이 없어 lock 이 없다. notify 는 아무 thread 가 stage 하므로 그것을 만지는 네 자리 (Connection::notify / ConnectionHost::broadcast 의 stage, pickWrite 의 readable + peek + encode, onSent(NOTIFY) 의 release, Connection::release 의 clear) 가 `NotificationContext::mutex` 를 잡는다.
- release 는 늘 compact 한다. release 는 write event 가 비어 있을 때 (완료 직후) 불리므로 걸린 send 가 가리키는 구간을 옮기는 일이 없다. "비었을 때만 compact" 로 하면 송신 중 뒤에 붙는 notify 가 끊이지 않을 때 writable 이 0 으로 줄어 BUFFER_FULL 이 굳어지므로 늘 한다.
- stage 가 `BUFFER_FULL` 을 돌려주는 조건은 `writable() < data.length` 다. notify 는 호출자가 그 결과를 받고, user write 는 adopt 가 CLOSE(BUFFER_FULL) 로 바꾼다.

### 9.4 인터페이스

```cpp
enum class Phase : uint8_t { IDLE, HANDSHAKING, READY, CLOSING };
typedef KiottyNetConnectionHandler Handler;

class Connection : public IoSource
{
public:
    Connection(int32_t slot, Pool& tls_pool, NotificationContext& notification, size_t recv_capacity, size_t send_capacity, size_t push_capacity, bool tls);
    bool valid() const;

    void       open(ActiveSocket&& socket, KiottyNetConnectionContext context, KiottyNetAppContext app_context, TlsProfile* profile, const char* hostname);
    void       release();                       // settle 이. socket.close, _work.reset, Stage 비움 (notify 는 lock 안), generation++, IDLE
    PoolHandle handle() const;
    PoolHandle streamHandle() const;            // handle 과 같은 값 + 타입 비트
    int32_t    generation() const;
    bool       alive() const;                   // HANDSHAKING 또는 READY
    Phase      phase() const;

    SocketHandle     socket() const override;
    int32_t          slot() const override;
    Holder<IoEvent>& eventFor(const IoAction& action) override;
    IoPayload        resolve(const IoAction& action) override;
    SocketCode       attach(EventListener& listener) override;
    void             detach(EventListener& listener) override;
    void             shutdown() override;

    // step (IO Thread, 콜백 있음)
    IoAction onOpened(const Handler& handler);                                   // open 직후. 평문 : onReady. TLS : onHandshake
    IoAction dispatch(const IoCompletion& completion, const Handler& handler);   // phase 라우팅 → kind 별 on* 하나 → apply

    // drain (IO Thread, 콜백 없음)
    IoAction pickWrite();                                                        // 비어 있지 않으면 NONE. 9.6

    // collectLocalEvents / settle / waitTimeout 이 읽는 것 (IO Thread)
    uint64_t deadline() const;
    bool     expireDeadline(uint64_t now);      // _deadline != 0 && now >= _deadline 이면 0 으로 돌리고 true
    bool     workReady() const;                 // _work.requested() && 두 event 가 비었음
    bool     workFinished() const;              // _work.finished()
    IoAction takeWorkOutcome();                 // _work.onFinished(). DONE → IDLE
    bool     working() const;                   // _work.working()
    const Holder<IoEvent>& readEvent() const;
    const Holder<IoEvent>& writeEvent() const;

    // 아무 thread
    KiottyNetConnectionInfo    info() const;
    KiottyNetConnectionContext context() const;
    int32_t                    state() const;
    void                       setState(int32_t state);
    KiottyNetCode              notify(KiottyNetSpan data);                       // lock 안에서 _notify.stage → wake
    size_t                     notificationWritable() const;                    // ConnectionHost::broadcast 가 lock 안에서
    void                       appendNotification(KiottyNetSpan data);

    // worker (WORKING 동안만)
    Result<KiottyNetCode, ConnectionStream*> stream(int32_t generation);       // _work.verify(generation) 이 SUCCESS 일 때만 값
    KiottyNetCode                            closeStream(int32_t generation, const KiottyNetConnectionAction& then);   // 9.7

private:
    IoAction onReceived(const IoCompletion& completion, const Handler& handler);
    IoAction onSent(const IoCompletion& completion, const Handler& handler);
    IoAction onTimeout(const IoCompletion& completion, const Handler& handler);
    IoAction onWorkReady(const IoCompletion& completion, const Handler& handler);
    IoAction onWorkFinished(const IoCompletion& completion, const Handler& handler);
    IoAction onHandshake(const Handler& handler);
    IoAction onPeerGone(SocketCode code);
    IoAction adopt(const KiottyNetConnectionAction& returned);                            // return → IoAction 변환 한 걸음. WRITE 는 여기서 _send.stage 하고 HOLD 가 된다. 콜백 없음, 재귀 없음
    IoAction drive(IoAction action, const Handler& handler);                              // READ 계열이면 receive Stage 에 대 보고 찼으면 onRead. switch 하나의 loop. 깊이 상수
    IoAction extend(const KiottyNetConnectionAction& returned, const Handler& handler);   // onTimeout 에 사용자 action 이 진행 중일 때 (11절)
    Result<KiottyNetCode, size_t> refill();                                               // receive Stage 를 compact 하고 TLS 면 SSL 에 남은 평문을 꺼낸다. 늘어난 길이
    IoAction apply(IoAction action, const Handler& handler);                              // deadline, Work 요청, CLOSING + onDisconnected
    IoAction encoded(IoAction write, Span<char> plain);                                   // TLS 면 encode. pickWrite 의 마지막 한 걸음
    bool     userActionPending() const;                                                   // _read_event.pending() || _send.readable() > 0
    Stage&   receiveStage(IoPurpose purpose);   // HANDSHAKE / TLS USER : cipherReceive. 평문 USER : _plain.receive
};
```

### 9.5 step 쪽 : dispatch → apply

```cpp
IoAction Connection::dispatch(const IoCompletion& completion, const Handler& handler)
{
    if (_phase == Phase::CLOSING)
    {
        return IoAction::none();                // 늦은 완료 (CANCELLED 포함) 와 CLOSE 뒤에 온 local completion. kind 를 가리지 않는다
    }
    IoAction action = IoAction::none();
    switch (completion.kind)
    {
    case IoKind::RECEIVE:    action = onReceived(completion, handler);     break;
    case IoKind::SEND:       action = onSent(completion, handler);         break;
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
        _work.request();                        // REQUESTED. WORKING 으로 올리는 것은 collectLocalEvents 의 WORK_READY 다 (10절)
        return action;
    case IoActionKind::CLOSE:
        _deadline = 0;
        _phase = Phase::CLOSING;
        _work.abandon();                        // REQUESTED 였으면 IDLE. 시작하지 않은 Work 는 CLOSE 가 거둔다. WORKING / DONE 은 그대로 (worker 가 끝낸다)
        handler.onDisconnected(_app_context, _context, handle(), action.as.close.reason);
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
    KiottyNetConnectionAction returned = handler.onReady(_context, handle());
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
        KiottyNetConnectionAction returned = handler.onReady(_context, handle());
        return drive(adopt(returned), handler);
    }
    case TlsStep::FAILED:
        handler.onError(_app_context, _info, _tls.lastCode(), _tls.lastMessage());
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

IoAction Connection::onSent(const IoCompletion& completion, const Handler& handler)
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
        std::lock_guard<std::mutex> lock(_notification.mutex);
        _notify.release(plain);
        return IoAction::none();
    }
    case IoPurpose::USER:
    {
        _send.release(plain);
        KiottyNetConnectionAction returned = handler.onWritten(_context, handle(), plain);
        return drive(adopt(returned), handler);
    }
    }
}

IoAction Connection::onTimeout(const IoCompletion&, const Handler& handler)
{
    if (_phase == Phase::HANDSHAKING)
    {
        handler.onError(_app_context, _info, KIOTTY_TLS_HANDSHAKE_TIMEOUT, "tls: handshake timeout");
        return IoAction::close(KIOTTY_TLS_HANDSHAKE_TIMEOUT);
    }
    KiottyNetConnectionAction returned = handler.onTimeout(_context, handle());
    if (userActionPending())                    // READ 가 걸려 있거나 (read event pending) WRITE 가 끝나지 않았다 (send Stage 에 바이트)
    {
        return extend(returned, handler);
    }
    return drive(adopt(returned), handler);
}

IoAction Connection::onWorkReady(const IoCompletion&, const Handler& handler)
{
    _work.start(generation());                  // REQUESTED → WORKING
    handler.onWorkStarted(_context, streamHandle());
    return IoAction::none();
}

IoAction Connection::onWorkFinished(const IoCompletion& completion, const Handler& handler)
{
    return drive(completion.action, handler);   // worker 의 outcome. collectLocalEvents 가 _work 에서 꺼내 실었다. closeStream 이 adopt 와 같은 규칙으로 만든 IoAction (WRITE 는 이미 stage 되어 HOLD) 이라 그대로 drive
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
    KiottyNetCode staged = _send.stage(returned.as.write.data);             // Stage 에 넣는 것으로 WRITE 는 끝. 거는 것은 drain
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
                    handler.onError(_app_context, _info, more.code(), _tls.lastMessage());
                    return IoAction::close(more.code());
                }
                if (more.value() == 0)
                {
                    return action;              // 정말 더 받아야 한다. resolve 가 receive Stage 의 빈 자리를 payload 로
                }
                break;                          // 꺼낸 평문으로 같은 READ 를 다시 본다
            }
            KiottyNetConnectionAction next = handler.onRead(_context, handle(), ready.value());
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
| CLOSING | NONE (dispatch 첫 줄) | NONE | NONE | NONE | NONE. _work 는 collectLocalEvents 가 이미 IDLE 로 돌렸다. settle 이 반납 |

### 9.6 drain 쪽 : pickWrite → resolve

```cpp
IoAction Connection::pickWrite()                // IO Thread. drain 이 slot 마다. 콜백 없음
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
        std::lock_guard<std::mutex> lock(_notification.mutex);
        size_t plain = _notify.readable();
        if (plain > 0)
        {
            return encoded(IoAction::write(IoPurpose::NOTIFY, 0, plain), _notify.peek());
        }
    }
    size_t plain = _send.readable();
    if (plain > 0)
    {
        return encoded(IoAction::write(IoPurpose::USER, 0, plain), _send.peek());
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
        Stage& in = receiveStage(action.purpose);
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
        Span<char> plain = action.purpose == IoPurpose::NOTIFY ? _notify.peek() : _send.peek();   // NOTIFY 는 lock 안에서
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
class WorkContext                               // IDLE → REQUESTED (apply, IO) → WORKING (onWorkReady, IO) → DONE (closeStream, worker) → IDLE (collectLocalEvents, IO)
{
public:
    enum class State : uint8_t { IDLE, REQUESTED, WORKING, DONE };
    void          request();                    // IDLE → REQUESTED
    void          abandon();                    // REQUESTED → IDLE. apply(CLOSE). 다른 상태면 아무것도 안 한다
    void          start(int32_t generation);    // REQUESTED → WORKING
    bool          requested() const;            // collectLocalEvents 의 workReady
    bool          finished() const;             // collectLocalEvents. acquire load == DONE
    IoAction      onFinished();                 // DONE → IDLE. outcome. collectLocalEvents 가 completion 에 싣는다
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

class ConnectionStream                          // stream/kiotty_connection_stream.h. Connection (WORKING) 과 StreamClient 가 같이 쓴다. 할당 없음
{
public:
    ConnectionStream(ActiveSocket& socket, TlsEngine& tls, Stage& receive, Stage& send);
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_ms);      // flush → receive Stage 에 남은 것 먼저 → 비면 poll + recv (+ decode) 반복
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_ms);              // flush → 평문 : 사용자 버퍼에서 바로 sendAll. TLS : send_capacity 씩 encode → cipherSend → sendAll
    KiottyNetCode   writeFile(const char* path, uint64_t offset, uint64_t length, uint32_t timeout_ms);   // 파일을 send_capacity 씩 읽어 write 와 같은 길. 플랫폼 공통. zero-copy 는 두지 않는다
    bool            connected() const;                                           // non-blocking recv(MSG_PEEK). 0 이면 false
    KiottyNetCode   handshake(uint32_t timeout_ms);                              // StreamClient 용. SEND 면 sendAll(cipherSend), RECEIVE 면 receiveSome(cipherReceive)
private:
    KiottyNetCode   flush(uint32_t timeout_ms);                                  // cipherSend 에 남은 것 (handshake 끝의 ticket, KeyUpdate 답장) 을 먼저 sendAll. 평문이면 아무것도 없다
};

Result<KiottyNetCode, ConnectionStream*> Connection::stream(int32_t generation)    // worker
{
    KiottyNetCode verified = _work.verify(generation);
    if (verified != KIOTTY_SUCCESS)
    {
        return error(verified);
    }
    return ok(&_stream);
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
        KiottyNetCode staged = _send.stage(then.as.write.data);     // WORKING 동안 send Stage 는 worker 의 것. 데이터를 가진 thread 가 stage 한다 (adopt 와 같은 규칙)
        if (staged != KIOTTY_SUCCESS)
        {
            return staged;                      // BUFFER_FULL. Stream 은 유효하게 남는다
        }
        outcome = IoAction::hold(then.timeout_ms);                  // adopt 와 같다. stage 된 WRITE 는 read lane 에서 HOLD 다
    }
    _work.finish(outcome);                      // → DONE. collectLocalEvents 가 WORK_DONE 에 실어 drive 로 보낸다
    _notification.listener->wake();
    if (_phase == Phase::CLOSING)
    {
        return KIOTTY_CLOSED;                   // Owner 가 먼저 닫았다. Work 는 끝났고 settle 이 반납한다
    }
    return KIOTTY_SUCCESS;
}
```

- WORKING 동안 IO Thread 가 이 연결에서 만지는 것은 `_notify` (lock 안의 stage) 와 `_phase` (Owner close 의 shutdown) 뿐이다. deadline 은 0, drain 는 pickWrite 의 둘째 줄에서 돌아가고, 어떤 submit 도 없다.
- 그래서 WORKING 동안 두 event 는 비어 있다. REQUESTED 가 그것을 보장한다 (10절 WORK_READY 의 조건). IOCP 에서 동기 recv / send 와 overlapped 요청을 섞지 않는 조건이기도 하다.
- REQUESTED 동안 cipherSend 에 남을 수 있는 OpenSSL 출력 (handshake 끝의 NewSessionTicket, KeyUpdate 답장) 은 Stream 의 모든 동기 함수가 첫 걸음으로 flush 하므로 worker 의 첫 read / write 때 나간다. IO 를 하지 않는 Work 뒤에는 WORK_DONE 다음의 drain 이 보낸다.
- Owner close / IoLoop shutdown 이 WORKING 인 연결에 하는 것은 `apply(CLOSE)` (onDisconnected, CLOSING) 와 `shutdown()` (FIN) 이다. socket.close 는 settle 이 `!working()` 을 본 뒤에 한다. worker 의 Stream read 는 0 (CLOSED), write 는 오류를 받아 끝난다. `isConnectionStreamOpen` 은 CLOSING 이면 false.
- 평문의 Stream write 는 복사 0 이다. 큰 것은 writeFile 로 send_capacity 씩 읽어 보낸다. TLS 는 encode 한 번이 어느 설계에서도 피할 수 없는 복사다.

### 9.8 NotificationContext, notify, broadcast

```cpp
struct NotificationContext                      // host 에 하나. 모든 연결의 notify Stage 를 한 mutex 가 지킨다
{
    std::mutex        mutex;
    EventListener*    listener;
    std::thread::id   io_thread;                // Stream 의 "IO Thread 에서는 BUSY" 에 쓴다. wake 경로는 보지 않는다
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
        KiottyNetCode staged = _notify.stage(data);                 // 넘치면 BUFFER_FULL 이고 아무것도 넣지 않는다
        if (staged != KIOTTY_SUCCESS)
        {
            return staged;
        }
    }
    _notification.listener->wake();             // 늘 깨운다. IO Thread 안이면 WAKE 하나가 더 돌 뿐이다
    return KIOTTY_SUCCESS;
}

KiottyNetCode ConnectionHost::broadcast(KiottyNetSpan data)          // 아무 thread. lock 안에서 전부 검사 → 전부 stage. 하나라도 모자라면 아무 데도 안 쓴다. 끝에 wake 한 번
```

- notify 는 "넣고 깨운다" 로 끝난다. 언제 나가는지는 drain 의 pickWrite 가, 다 나간 뒤 치우는 것은 onSent(NOTIFY) 가 한다. 세 자리가 각자 한 가지만 한다.
- WORKING 인 연결의 notify 는 쌓이기만 한다. pickWrite 가 working 에서 돌아가므로 특별한 코드가 없다. WORK_DONE 다음의 drain 이 집어 간다.
- 암호화는 IO Thread 의 pickWrite 에서만. `SSL*` 은 WORKING 이 아닌 동안 IO Thread 의 것, WORKING 동안 worker 의 것이다. 두 thread 가 동시에 만지는 순간이 없다.
- `Connection::release` 가 notify Stage 를 비우는 줄은 lock 안이다. 다른 thread 가 `alive()` 를 통과한 뒤 CLOSING 이 될 수 있기 때문이다 (그 stage 는 lock 안에서 끝나고, release 의 clear 가 그 뒤에 lock 을 잡는다).

### 9.9 ConnectionHost, Acceptor

v2 9.7, 9.8 그대로. `ConnectionHost::release(connection)` 이 `connection.release()` 를 부르고 그 안에서 socket.close 가 일어난다.

## 10. io_loop

```cpp
class IoLoop
{
public:
    IoLoop(ConnectionHost& host, EventListener& listener, Acceptor* acceptor, TlsProfile* profile, bool stop_when_empty);
    KiottyNetCode run(const Handler& handler, KiottyNetAppContext app_context);
    void          stop();
    void          admit(ActiveSocket&& socket, const char* hostname);
private:
    void                     runOnce();
    void                     drain();       // slot 마다 pickWrite → submit (write lane)
    void                     settle();          // slot 반납, acceptor 재무장, stop_when_empty
    Span<const IoCompletion> collectIoEvents();         // wait. OS 완료만
    void                     step(const IoCompletion& completion);   // dispatch → apply → submit (read lane, lifecycle)
    Span<const IoCompletion> collectLocalEvents();          // step 뒤의 상태에서 TIMEOUT / WORK_READY / WORK_DONE
    void                     shutdown();
    IoAction                 access(const IoCompletion& completion);
    void                     submit(IoSource& source, const IoAction& action);
    uint32_t                 waitTimeout(uint64_t now);
    ...
};
```

한 바퀴. 여섯 프로세스의 input 과 output:

| 프로세스 | input | output | listener | handler |
|---|---|---|---|---|
| drain | 연결마다의 Stage 와 write event | write lane 의 요청 (연결당 최대 하나) | submit (WRITE) | 없음 |
| settle | host | slot 반납, acceptor 요청, `_stopped` | submit (ACCEPT) | 없음 |
| collectIoEvents | 가장 가까운 deadline | OS 완료의 `Span<IoCompletion>` | wait | 없음 |
| step | IoCompletion 하나 | read lane 의 요청 하나 또는 lifecycle 변화, Stage 에 쌓인 바이트 | submit (READ / ACCEPT), detach (CLOSE) | 여기만 |
| collectLocalEvents | step 뒤의 연결 상태 (deadline, Work) | local completion 의 `Span<IoCompletion>` | 없음 | 없음 |
| step | local completion 하나 | 위와 같다 | 위와 같다 | 여기만 |

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
    drain();                                // Stage 에 있는 것을 건다. 앞 바퀴의 step 과 run 전의 admit 이 남긴 것
    settle();
    Span<const IoCompletion> arrived = collectIoEvents();
    for (const IoCompletion& completion : arrived)
    {
        step(completion);
    }
    Span<const IoCompletion> local = collectLocalEvents();
    for (const IoCompletion& completion : local)
    {
        step(completion);
    }
}

uint32_t IoLoop::waitTimeout(uint64_t now)      // 가장 가까운 deadline. 다른 thread 가 적는 것은 전부 wake 하므로 그것만 본다
{
    uint32_t timeout = EventListener::WAIT_FOREVER;
    for (int32_t i = 0; i < _host.capacity(); ++i)
    {
        timeout = nearer(timeout, _host.at(i).deadline(), now);
    }
    return timeout;
}

Span<const IoCompletion> IoLoop::collectIoEvents()
{
    uint32_t timeout = waitTimeout(monotonicMs());
    return _listener.wait(timeout);             // 꺼낸 완료의 event 는 여기서 pending 이 내려간다
}

Span<const IoCompletion> IoLoop::collectLocalEvents()       // OS 완료를 다 step 한 뒤의 상태로 만든다. 그래서 CLOSE 뒤의 연결은 deadline 0, REQUESTED 아님
{
    size_t count = 0;
    uint64_t now = monotonicMs();
    for (int32_t i = 0; i < _host.capacity(); ++i)
    {
        Connection& c = _host.at(i);
        if (c.expireDeadline(now))
        {
            _local[count++] = IoCompletion::timeout(i);
        }
        if (c.workReady())                      // REQUESTED 이고 두 event 가 비었다. 이 바퀴의 SEND 완료가 비운 것도 보인다
        {
            _local[count++] = IoCompletion::workReady(i);
        }
        if (c.workFinished())
        {
            _local[count++] = IoCompletion::workDone(i, c.takeWorkOutcome());   // DONE → IDLE 은 여기서. CLOSING 이어도 outcome 은 꺼내고 dispatch 가 버린다
        }
    }
    return Span<const IoCompletion>{ _local, count };
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
    default:
    {
        Connection& c = _host.at(completion.slot);
        source = &c;
        action = c.dispatch(completion, *_handler);
        break;
    }
    }
    submit(*source, action);
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
        source.detach(_listener);               // 걸린 요청은 CANCELLED 완료로 돌아온다
        source.shutdown();                      // FIN. socket.close 는 settle 의 release
        return;
    case IoActionKind::READ:
    case IoActionKind::READ_UNTIL:
    case IoActionKind::WRITE:                   // WRITE 는 drain 의 pickWrite 에서만 온다. step 의 output 에는 WRITE 가 없다 (9.5)
    case IoActionKind::ACCEPT:
        _listener.submit(source.eventFor(action), action, source.resolve(action));
        return;
    }
}

void IoLoop::drain()                            // write lane. slot 마다 "걸 수 있고 보낼 것이 있으면" 하나
{
    for (int32_t i = 0; i < _host.capacity(); ++i)
    {
        Connection& c = _host.at(i);
        submit(c, c.pickWrite());
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
        if (c.readEvent().pending() || c.writeEvent().pending() || c.working())
        {
            continue;                           // 늦은 완료가 돌아오거나 worker 가 Stream 을 놓을 때까지
        }
        _host.release(c);                       // socket.close, generation++, IDLE
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
```

- `access`, `admit`, `shutdown` 은 v2 10절 그대로. `admit` 은 `onOpened` 의 action 을 submit 한다 (TLS 서버는 READ(HANDSHAKE), TLS 클라이언트는 NONE 이고 ClientHello 는 cipherSend 에 남는다). EventClient 는 run 전에 admit 하므로 첫 runOnce 의 drain 이 그것을 건다. 바퀴의 첫 프로세스가 drain 인 이유가 이것이다: **wait 에 들어갈 때 Stage 에 걸리지 않은 바이트가 없다.**
- `shutdown` 은 alive 인 연결마다 `apply(CLOSE)` + `submit(CLOSE)` 뒤 `while (!_host.empty()) runOnce()` 다. WORKING 인 연결은 worker 의 closeStream 이 올 때까지 그 while 이 돈다. IO 를 하지 않는 worker 가 closeStream 을 부르지 않으면 run 은 돌아오지 않는다. 이것은 Work 의 계약 ("받았으면 반드시 close 한다") 이고 Owner::shutdown 의 문서에 적는다.
- collectLocalEvents 가 step 뒤에 있는 이유: 같은 배치의 OS 완료가 연결을 닫거나 (CLOSE 뒤의 TIMEOUT 은 생기지 않는다. deadline 이 0 이다), 새 action 을 걸거나 (새 deadline 이 미래라 TIMEOUT 이 생기지 않는다), write event 를 비울 (REQUESTED 가 WORK_READY 가 된다) 수 있기 때문이다. local completion 은 "지금 상태" 에서 만들어야 거짓이 없다. 그래도 남는 늦은 것 (CANCELLED 완료) 은 dispatch 의 첫 줄이 받는다.
- 이것은 completion 사이의 우선순위가 아니다. 규칙은 하나다: **OS 가 만든 것 먼저, 우리가 만든 것은 그 뒤의 상태에서.** 같은 slot 에서 local completion 셋이 함께 나오는 일은 없다 (Work 중에는 deadline 이 0, REQUESTED 와 DONE 은 다른 상태).
- WORK_READY 는 같은 runOnce 안에서 처리된다. thenWork 를 return 한 콜백이 끝나고 collectLocalEvents 가 바로 뒤에 돌므로 사이에 wait 가 없다. 기다리는 경우는 하나, write event 에 notify 가 걸려 있을 때 (W=1) 그 SEND 완료 하나다. 그 완료가 step 되면 같은 runOnce 의 collectLocalEvents 가 WORK_READY 를 만든다.
- step 에서 WRITE 를 걸지 않는 것은 예외가 아니라 규칙이다. **action 의 kind 가 어느 lane 인지 정한다.** READ 계열과 ACCEPT 는 step 이, WRITE 는 drain 이 건다. 콜백의 thenWrite, worker 의 closeStream(thenWrite), 다른 thread 의 notify, OpenSSL 의 handshake / control 이 전부 "Stage 에 넣는다" 로 끝나고, 거는 쪽은 drain 한 자리다.
- 콜백 안의 notify 와 thenWrite 는 다음 runOnce 의 첫 프로세스 (drain) 에서, wait 전에 나간다. 다른 thread 의 notify 는 wake → 그 다음 runOnce 의 drain 다.
- 상태를 읽어 판단하는 `pending()` 은 pickWrite (write event), onTimeout (read event), collectLocalEvents 의 workReady (둘), settle (둘) 넷이다.

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
| 8 | write pending → NONE | SEND (USER) | cipherSend consume → `_send.release(4)` → onWritten(4) → thenRead(4) → drive → refill 0 → READ(USER) |
| 9 | `_notify` 있음 → encode → WRITE(NOTIFY, plain 2) | (대기) | |
| 10 | 없음 | SEND (NOTIFY) | cipherSend consume → `_notify.release(2)` → NONE |

notify 가 나가는 중에 thenWrite 가 들어오는 경우 (7 과 9 의 순서가 바뀐 것): WRITE(NOTIFY) 가 걸린 채 onRead 가 thenWrite 를 돌려주면 adopt 의 `_send.stage` 와 deadline 까지만 일어나고, 다음 drain 은 pending 이라 NONE. SEND(NOTIFY) 가 `_notify.release`, 그 다음 drain 이 WRITE(USER). peer 는 `notify 전체, 응답 전체` 순서로 받는다. 분기가 없다. pending 이 아니었다면 그 drain 이 바로 걸었을 뿐이다.

## 11. TIMEOUT 과 CLOSE

TIMEOUT 뒤가 "진행 중인 사용자 action 위에 다른 action" 이 생길 수 있는 유일한 자리다. 보는 것은 `userActionPending()` 하나다: `_read_event.pending() || _send.readable() > 0`. 앞은 READ 가 걸려 있다는 것, 뒤는 WRITE 가 (notify 뒤에서 기다리든 걸려 있든) 아직 onWritten 까지 가지 않았다는 것이다. 둘 다 그 맥락의 공개 인터페이스를 읽는다. notify 의 write event 는 사용자 action 과 무관하므로 보지 않는다.

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
3. 걸려 있던 요청마다 CANCELLED 완료 (IOCP 는 OS 가, epoll 은 detach 가) → dispatch 첫 줄이 CLOSING 을 보고 NONE. WORKING 이었으면 worker 의 Stream 이 끊김을 보고 closeStream → DONE → collectLocalEvents 가 IDLE 로 → WORK_DONE 은 NONE
4. `settle` : 두 event 가 비었고 working 이 아니면 `host.release` → socket.close, Stage 비움 (notify 는 lock 안), generation++, IDLE

## 12. server, client

v2 12절 그대로. 바뀌는 것:

- `StreamClient` 는 `sendAll / receiveSome` 대신 `ConnectionStream` 하나를 값으로 든다. `connect` 는 connectWithRetry 뒤 TLS 면 `_stream.handshake(connect_timeout_ms)`. `read / write` 는 `_stream.read / write`. 그래서 Connection 의 WORKING 과 StreamClient 가 같은 코드다.
- `Owner::shutdown` 은 `close + run 의 return 대기` 그대로. WORKING 인 연결이 있으면 그 worker 가 closeStream 할 때까지 run 이 돌아오지 않는다. 이것을 공개 문서 (requirement 7절 close) 에 적는다.

## 13. context, config

v2 13절 그대로. 바뀌는 것:

- `Context(size_t tls_pool_bytes)`. `initializeContext` 는 8 MiB, `initializeContextWithTlsPool(bytes)` 는 인자.
- `config::configureServerMaxClients` 가 `[1, 1024]` 를 검사한다. 기본 128.
- handle (int64): `generation (32) | index (32)`. Stream handle 은 같은 값에 최상위 비트 (bit 63) 를 세운다. `conn(h)` 는 그 비트를 벗기고 찾고, `KiottyNet_*ConnectionStream` 은 그 비트가 없으면 `KIOTTY_INVALID_ARGUMENT`.

## 14. thread 규칙

| 클래스 | 부르는 thread |
|---|---|
| Context | 아무 thread (mutex). 소멸자는 run 을 부른 thread 가 아니어야 한다 |
| Server, EventClient | `run` 은 IO Thread 가 되는 thread. `close`, `await`, `broadcast`, `notify` 는 아무 thread |
| IoLoop | `run` IO Thread. `stop` 아무 thread |
| Connection | `notify`, `info`, `alive`, `context`, `state` 는 아무 thread. `stream`, `closeStream` 은 worker. 나머지 IO Thread |
| Sender (send) | RUNNING 이면 IO Thread, WORKING 이면 worker. 동시에 둘인 순간은 없다 |
| Sender (notify) | stage 는 아무 thread, readable / peek / release / clear 는 IO Thread. 전부 `NotificationContext::mutex` 안 |
| TlsEngine, socket | WORKING 이 아닌 동안 IO Thread, WORKING 동안 worker. `SSL*` 도 같다 |
| ConnectionStream | Connection 의 것은 worker 하나, StreamClient 의 것은 한 번에 한 thread |
| EventListener | `wake` 만 아무 thread |

다른 thread 의 호출: `closeStream` 은 verify → (WRITE 면 send Stage 복사) → CAS → wake. `notify` / `broadcast` 는 lock 안에서 notify Stage 복사 → wake. Owner `close` 는 `_stopped` → wake. 콜백 안에서 handle 로 할 수 있는 것은 `getConnectionInfo` 와 `notifyConnection` 둘이라 재진입이 없다.

## 15. base.cpp 번역 표

| C | 번역 |
|---|---|
| `KiottyNet_initialize*Config`, `configure*` | `config::` 같은 이름. NULL 이면 INVALID_ARGUMENT |
| `thenHold`, `thenRead`, `thenReadUntil`, `thenWrite`, `thenWork`, `thenClose`, `acceptConnection`, `ignoreConnection` | `action::` 같은 이름 |
| `initializeContext` / `initializeContextWithTlsPool(bytes)` | `new (nothrow) Context(DEFAULT_TLS_POOL)` / `Context(bytes)` |
| `releaseContext` | `delete` |
| `createServer(ctx, config, tls)` 등 create 셋 | `ctx->create<T>(...)`. NULL / `!isInitialized` 면 INVALID_ARGUMENT |
| `destroy*` | `ctx->destroy(owner)` |
| `run / close / await / broadcastServer`, EventClient 의 `run / close / await / notify`, StreamClient 의 `connect / disconnect / read / write` | 같은 이름의 메서드 |
| `getEventClientConnection` | `toRaw(client->connection())` |
| `getConnectionInfo(h)` | `conn(h) ? conn(h)->info() : {}` |
| `notifyConnection(h, data)` | `host(h)->notify(fromRaw(h), data)` |
| `readConnectionStream(s, ...)`, `writeConnectionStream`, `writeConnectionStreamFile`, `isConnectionStreamOpen` | 타입 비트 없으면 INVALID_ARGUMENT. IO Thread 면 BUSY. `conn(s)->stream(gen)` 의 code (PEER_CLOSED / BUSY) 또는 그 Stream 의 메서드 |
| `closeConnectionStream(s, action)` | `conn(s)->closeStream(gen, action)` |

StateMachine 은 `Connection::state() / setState() / context()` 를 쓴다.

## 16. 메모리 (128 client, 4 KiB 기준)

| 객체 | 잡는 것 | 크기 |
|---|---|---|
| Connection (평문) | 객체 + StageSet (recv + send + push) + IoEvent 둘 | 약 12 KiB |
| Connection (TLS) | 위 + cipherReceive (16 KiB + 헤더) + cipherSend (max(send, push) + 레코드 오버헤드 + 512) + SSL (Pool, SSL_clear 재사용) | 약 34 KiB + SSL |
| ConnectionHost | Connection[128] + NotificationContext | 평문 약 1.5 MiB, TLS 약 4.3 MiB |
| IoLoop | local completion 배열 3 × 128 (OS 완료는 listener 의 배열) | |
| EventClient | capacity 1 | 평문 약 13 KiB |
| Context | Holder\<Owner\>[16] + TLS Pool | 8 MiB |

## 17. v2 에서 바뀐 것 (코드가 있는 층)

| 층 | 바꿀 것 |
|---|---|
| base.h (사이클 1) | 3절. action 함수 이름 여섯, `KiottyNetWorkHandle` → `KiottyNetConnectionStream`, onWorkStarted / onError 시그니처, Stream 함수 다섯, `initializeContextWithTlsPool`. statemachine.h 의 `KiottyNet_holdCommunication(0)` → `thenHold(0)`. C++ `KiottyNet_Work` → `KiottyNet_ConnectionStream`. requirement 5 ~ 7절, user_guide_http 1.5 |
| core (사이클 2) | 없음 |
| error, socket (사이클 3a) | 없음 |
| event (사이클 3b) | `IoAction::as.write.length`, `IoAction::write(purpose, timeout, length)`, `IoKind::WORK_READY`, `IoCompletion::workReady`, `IoCompletion::workDone(slot, outcome)`. 테스트 셋의 모양 (7절). listener 구현은 그대로 |
| 테스트 계획 (test_base_v0) | 9절 Connection 의 케이스를 9.5 / 9.6 의 표로, 10절 IoLoop 를 여섯 프로세스로. 사이클 5 시작 전에 고친다 |

## 18. 구현 순서

① base.h 를 3절대로 고치고 statemachine.h / requirement 를 맞춘다 (사이클 1 재방문) ② event 의 세 줄 (사이클 3b 재방문) ③ epoll backend (사이클 3c) ④ tls (stub 먼저) ⑤ stream (ConnectionStream, 파일 backend) ⑥ connection (IoSource, Sender, WorkContext, NotificationContext, Connection, Acceptor, ConnectionHost) ⑦ io_loop ⑧ server / client / context / config ⑨ base.cpp 번역.

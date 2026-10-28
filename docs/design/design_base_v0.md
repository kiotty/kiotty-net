# kiotty_net_base.h 구현 설계 v0

`include/kiotty_net_base.h` (requirement_v0.md 1~7절) 를 `src/implementation/base` 의 클래스들로 구현하기 위한 설계다. 이 문서는 **클래스의 인터페이스만** 정한다. 각 클래스의 안 (알고리즘, 자료구조, OS 호출) 은 구현하면서 따로 논의한다. 두 문서가 다르면 requirement_v0.md 가, 흐름이 다르면 implementation_guide_v0.md 가 우선한다.

원칙 세 가지.

1. `src/kiotty_net_base.cpp` 는 **번역만** 한다. C 인터페이스의 handle 과 struct 를 구현 객체로 바꿔 메서드 하나를 부르고, 결과를 다시 C 타입으로 돌려준다. 검사, 분기, 상태 변경, 복사 같은 로직을 두지 않는다. 함수 하나가 메서드 하나에 대응한다 (6절 표).
2. 클래스는 목적에 필요한 **최소 함수와 최소 변수** 만 가진다. "나중에 쓸지 모르는" 메서드, getter, 추상화는 두지 않는다. 필요해지면 그때 더한다.
3. 의존 방향은 아래로만 간다. 위 계층은 아래 계층을 알고, 아래 계층은 위를 모른다. OS 가 다른 부분은 그 폴더의 `backend/` 안에만 있다.

```
kiotty_net_base.cpp ─────── 번역
    │
    ├── Context ──────────── 플랫폼 초기화, 소유자 표
    ├── Server / EventClient ── ConnectionHost ── IoLoop      (loop 를 가진 소유자)
    │                                 │
    │                             Connection ── ReadRequest, WriteLane, PushLane, Work 상태
    │                                 │
    └── StreamClient ────────┬── TlsSession / TlsContext (선택)
                             ├── EventListener + IoEvent    (IOCP / epoll / kqueue / io_uring)
                             └── Socket / ListenSocket      (TCP / Unix domain)
                                      │
                                 SocketCode ← OS 에러        (error/backend)
core/ : Stage, PoolHandle, Holder, Result, 시계
```

목차

1. 폴더 구조
2. 공용 규약 (namespace, 코드, handle, 메모리)
3. core
4. base 의 클래스 인터페이스 (error, socket, event, tls, connection, connection_host, io_loop, client, context, config)
5. thread 규칙
6. kiotty_net_base.cpp 번역 표
7. 생성 시점에 잡는 메모리
8. 논의해서 결정할 것

---

## 1. 폴더 구조

지금 있는 빈 골격을 기준으로 하고, 없는 것은 **(추가)** 로 표시했다. 파일은 `kiotty_` 접두어, include guard 는 파일 경로 대문자다.

```
src/
    kiotty_net_base.cpp                          # 6절 번역 표 그대로. 로직 없음
    implementation/
        core/                                    # namespace kiotty. 네트워크를 모른다
            kiotty_holder.h                      # 있음. Derived 를 Base 크기 안에 보관 (backend IoEvent 에 쓴다)
            kiotty_result.h                      # 있음. Result<Code, Value>. 내부 전용
            kiotty_stage.h                       # 비어 있음. requirement 4절의 Stage
            kiotty_pool_handle.h                 # (추가) PoolHandle { generation, index } <-> int64_t
            kiotty_clock.h                       # (추가) 단조 시계 ms
        base/                                    # namespace kiotty::net
            error/
                kiotty_socket_error.h            # SocketCode, OS 에러 -> SocketCode, SocketCode -> KiottyNetCode
                backend/
                    kiotty_windows_socket_error.h
                    kiotty_linux_socket_error.h  # (darwin 은 linux 와 같은 errno 표를 쓴다. 다르면 파일을 나눈다)
            socket/
                kiotty_socket.h                  # Socket, ListenSocket
                backend/                         # (추가) kiotty_windows_socket.cpp, kiotty_posix_socket.cpp
            event/
                kiotty_event.h                   # IoEvent (backend 가 채우는 완료 저장소), IoCompletion
                kiotty_event_listener.h          # EventListener : wait / submit / wake
                backend/                         # (추가) kiotty_iocp_*.cpp, kiotty_epoll_*.cpp, kiotty_kqueue_*.cpp, kiotty_io_uring_*.cpp
            tls/                                 # (추가) OpenSSL 은 여기만 포함한다
                kiotty_tls_context.h             # TlsContext : SSL_CTX 하나. 설정 struct 에서 만든다
                kiotty_tls_session.h             # TlsSession : 연결 하나의 SSL + memory BIO
            connection/
                kiotty_connection.h              # Connection, ReadRequest, WriteLane, PushLane, FinishRequest
                kiotty_connection_host.h         # ConnectionHost : pool + EventListener + IoLoop + done ring + push slot
            io_loop/
                kiotty_io_loop.h                 # IoLoop : 한 바퀴의 순서 (a~e)
            server/                              # (추가)
                kiotty_server.h                  # Server : ConnectionHost + ListenSocket
            client/                              # (추가)
                kiotty_event_client.h            # EventClient : ConnectionHost (capacity 1) + connect 재시도
                kiotty_stream_client.h           # StreamClient : 동기. IoLoop 없음
            context/                             # (추가)
                kiotty_context.h                 # Context, OwnerTable
            config/                              # (추가)
                kiotty_config.h                  # initialize* / configure* 의 검사와 복사. 순수 함수
```

- `event/` 와 `socket/` 의 backend 는 `.cpp` 다. 헤더는 하나이고 OS 마다 다른 `.cpp` 가 같은 선언을 구현한다. backend 전용 struct (OVERLAPPED 를 담는 IoEvent 의 실제 모양) 는 그 `.cpp` 안에 둔다. 헤더가 OS 헤더를 포함하지 않게 하기 위해서다.
- `tls/` 가 OpenSSL 을 포함하는 유일한 곳이다. `Connection` 은 `TlsSession` 의 인터페이스만 본다.

## 2. 공용 규약

- `core/` 는 `namespace kiotty`, `base/` 는 `namespace kiotty { namespace net { } }` 다. 내부 타입에 접두어는 없다.
- 공개 struct (`KiottyNetSpan`, `KiottyNetEndpoint`, 설정 struct, `KiottyNetConnectionHandler`, `KiottyNetConnectionInfo`) 는 내부에서도 **그대로** 쓴다. 같은 뜻의 내부 struct 를 다시 만들지 않는다.
- 내부 에러는 `Result<Code, Value>` 와 코드 enum 으로 돌려준다. 공개 코드 `KiottyNetCode` 로 바꾸는 자리는 **Connection 과 StreamClient** 다. 그 아래 (socket, event, tls) 는 `SocketCode` 를 쓴다. 그 아래가 `KIOTTY_PEER_CLOSED` 같은 공개 의미를 알 필요가 없기 때문이다.
- pool handle 은 `PoolHandle { int32_t generation; int32_t index; }` 다 (규약 8). 0 은 무효. `index` 가 소유자를 찾는 열쇠다 (8절 결정 1).
- 모든 메모리는 `create*` 시점의 생성자 안에서 잡는다. 생성자 뒤에 `new` / `malloc` 을 부르는 메서드는 없다. 잡지 못하면 객체는 `valid() == false` 이고 base.cpp 가 NULL 을 돌려준다.
- 클래스는 복사하지 않는다. pool 안에 사는 것 (Connection) 은 이동도 하지 않는다. 소유자 (Server, EventClient, StreamClient, Context) 는 포인터로만 다룬다.

---

## 3. core

### 3.1 Stage (`core/kiotty_stage.h`)

requirement 4절의 선언 그대로다. 수신 Stage 와 push lane 이 이것이다.

```cpp
class Stage
{
public:
    explicit Stage(size_t capacity);
    ~Stage();

    class Provider
    {
    public:
        size_t writable() const;
        void*  prepare(size_t requested);
        void   commit(size_t written);
    };

    class Consumer
    {
    public:
        size_t readable() const;
        void*  prepare(size_t requested);
        void   commit(size_t consumed);
    };

    bool      valid() const;
    Provider& provider();
    Consumer& consumer();
    size_t    capacity() const;
    void      compact();

private:
    char*  _buffer;
    size_t _capacity;
    size_t _begin_pos;
    size_t _end_pos;
};
```

### 3.2 PoolHandle (`core/kiotty_pool_handle.h`)

```cpp
struct PoolHandle
{
    int32_t generation;
    int32_t index;
};

int64_t    toRaw(PoolHandle handle);
PoolHandle fromRaw(int64_t raw);
```

`index` 안의 비트 배치 (소유자 번호 / slot) 는 8절 결정 1 이다. 배치가 정해지면 `ownerOf(index)` / `slotOf(index)` 둘이 여기에 들어간다.

### 3.3 Clock (`core/kiotty_clock.h`)

```cpp
uint64_t monotonicMs();
```

deadline 과 timeout 계산에 쓰는 유일한 시계다. `std::chrono::steady_clock` 으로 충분하면 그것을 감싼다.

---

## 4. base 의 클래스 인터페이스

### 4.1 error

```cpp
enum class SocketCode : int32_t
{
    SUCCESS = 0,
    PENDING,            // 비동기 요청이 걸렸다. 완료는 EventListener::wait 로 온다
    WOULD_BLOCK,        // reactor backend 에서 지금은 못 한다
    INTERRUPTED,
    CLOSED,             // peer 가 FIN
    TIMEOUT,
    REFUSED,
    UNREACHABLE,
    ADDRESS_IN_USE,
    INVALID_HANDLE,
    INVALID_ARGUMENT,
    OUT_OF_RESOURCE,
    NOT_SUPPORTED,
    FAILED
};

SocketCode    lastSocketError();                 // backend : WSAGetLastError() / errno 를 SocketCode 로
SocketCode    toSocketCode(int os_error);        // backend
KiottyNetCode toNetCode(SocketCode code);        // CLOSED -> KIOTTY_PEER_CLOSED, TIMEOUT -> KIOTTY_TIMEOUT, 나머지 -> KIOTTY_SOCKET_ERROR
```

### 4.2 socket (`socket/kiotty_socket.h`)

연결된 소켓 하나와 listening 소켓 하나. transport 는 `KiottyNetEndpoint::transport` 그대로 받는다. TCP 와 Unix domain 둘 다 socket 이므로 클래스는 하나다 (8절 결정 2).

```cpp
typedef intptr_t SocketHandle;
static const SocketHandle INVALID_SOCKET_HANDLE = -1;

class Socket
{
public:
    Socket();
    ~Socket();
    Socket(Socket&& other);
    Socket& operator=(Socket&& other);

    static Result<SocketCode, Socket> open(KiottyNetTransport transport);   // socket(), non-blocking 으로 만든다

    SocketCode connect(const KiottyNetEndpoint& endpoint);   // non-blocking connect 시작. SUCCESS 또는 PENDING
    SocketCode finishConnect();                              // 쓰기 가능 통지 뒤. SO_ERROR 를 읽는다

    Result<SocketCode, size_t> send(const void* data, size_t length);      // 즉시. StreamClient 와 reactor backend 가 쓴다
    Result<SocketCode, size_t> receive(void* data, size_t length);

    SocketCode shutdown();
    void       close();

    bool                    valid() const;
    SocketHandle            handle() const;
    KiottyNetConnectionInfo peer() const;

private:
    SocketHandle       _handle;
    KiottyNetTransport _transport;
};

class ListenSocket
{
public:
    ListenSocket();
    ~ListenSocket();

    SocketCode                 open(const KiottyNetEndpoint& endpoint, int backlog);   // socket + bind + listen
    Result<SocketCode, Socket> accept();                                               // reactor backend 와 IOCP 의 AcceptEx 뒤처리가 쓴다
    void                       close();

    bool         valid() const;
    SocketHandle handle() const;

private:
    SocketHandle       _handle;
    KiottyNetTransport _transport;
};
```

- `Socket` 은 OS 핸들의 소유자다. 소멸자가 닫는다. 이동만 된다.
- `Socket` 에 비동기 메서드가 없다. 비동기 요청은 `EventListener::submit*` 가 핸들을 받아 건다. 그래서 IOCP 의 `WSASend` 와 epoll 의 `send` 가 한 클래스에 섞이지 않는다.

### 4.3 event

`EventListener` 는 OS 의 다중화 객체 (IOCP / epoll / kqueue / io_uring) 하나다. **어느 backend 에서도 proactor 처럼 보이게 한다.** `submitReceive` 에 버퍼를 넘기면, IOCP 는 kernel 이 거기에 쓰고, epoll 은 readable 통지 뒤 listener 가 `recv` 해서 거기에 쓴다. 호출자 (Connection) 는 완료에 실린 길이만 본다. 구현 가이드 1.3 의 `onRecvComplete(connection, read_length)` 가 이 통일의 결과다.

```cpp
enum class IoKind : uint8_t
{
    NONE,
    ACCEPT,
    CONNECT,
    RECEIVE,
    SEND,
    WAKE
};

struct IoEvent                                 // 걸린 요청 하나의 저장소. backend 가 실제 모양 (OVERLAPPED 등) 을 정한다
{
    static const size_t HOLDER_SIZE  = 128;    // backend 중 가장 큰 것. static_assert 로 검사
    static const size_t HOLDER_ALIGN = 8;
};

struct IoCompletion                            // wait 가 돌려주는 완료 하나
{
    IoKind     kind;
    SocketCode code;
    size_t     length;                         // RECEIVE / SEND 가 옮긴 바이트
    void*      owner;                          // attach 에 넘긴 포인터 (Connection* 또는 Server*)
};

class EventListener
{
public:
    static const size_t STORAGE_SIZE  = 256;   // backend 상태 (IOCP 핸들, epoll fd + eventfd 등)
    static const size_t STORAGE_ALIGN = 16;

    EventListener();
    ~EventListener();

    SocketCode open();
    void       close();

    SocketCode attach(SocketHandle handle, void* owner);                                           // IOCP: CreateIoCompletionPort, epoll: EPOLL_CTL_ADD
    void       detach(SocketHandle handle);

    SocketCode submitAccept (Holder<IoEvent>& event, ListenSocket& listener);
    SocketCode submitConnect(Holder<IoEvent>& event, Socket& socket, const KiottyNetEndpoint& endpoint);
    SocketCode submitReceive(Holder<IoEvent>& event, SocketHandle handle, void* buffer, size_t length);
    SocketCode submitSend   (Holder<IoEvent>& event, SocketHandle handle, const void* buffer, size_t length);

    Result<SocketCode, size_t> wait(uint32_t timeout_ms, IoCompletion* completions, size_t capacity);   // 꺼낸 개수. TIMEOUT 이면 0

    SocketCode wake();                          // 아무 thread. IoCompletion { WAKE } 하나를 만든다 (PQCS / eventfd / pipe / msg ring)

private:
    alignas(STORAGE_ALIGN) char _storage[STORAGE_SIZE];
};

Holder<IoEvent> makeIoEvent();                  // backend 가 자기 struct 를 Holder 에 넣어 돌려준다
```

- `Holder<IoEvent>` 를 쓰는 이유 : IOCP 의 OVERLAPPED 가 들어간 struct 를 Connection 안에 **값으로** 두되, 헤더에는 OS 타입이 보이지 않게 하려는 것이다 (reference 의 `IOTransferEvent` 와 같은 방법).
- 한 소켓에는 receive 와 send 요청이 각각 하나만 걸린다. 그래서 Connection 은 `Holder<IoEvent>` 둘을 가진다. Server 는 accept 하나, EventClient 는 connect 하나를 더 가진다.
- `wait` 의 완료 배열은 호출자 (IoLoop) 의 stack 에 있다. 32 client 면 64 칸으로 충분하다.

### 4.4 tls

TLS 는 transport 위의 선택 계층이다 (requirement 3절 2번). OpenSSL memory BIO 로 **바이트만** 다루고 socket 을 모른다. 그래서 TCP / Unix domain 어느 쪽에도 얹히고 StreamClient 도 같은 클래스를 쓴다.

```cpp
class TlsContext                                // SSL_CTX 하나. 소유자 (Server, EventClient, StreamClient) 가 하나씩 가진다
{
public:
    TlsContext();
    ~TlsContext();

    KiottyNetCode openServer(const KiottyNetTlsServerConfig& config);   // 인증서 / key 로드 실패 -> KIOTTY_NOT_FOUND, 그 외 -> KIOTTY_INVALID_ARGUMENT
    KiottyNetCode openClient(const KiottyNetTlsClientConfig& config);
    void          close();

    bool valid() const;

private:
    void* _ctx;                                 // SSL_CTX*. 헤더에 OpenSSL 타입을 두지 않는다
};

class TlsSession                                // 연결 하나의 SSL + memory BIO 둘
{
public:
    TlsSession();
    ~TlsSession();

    KiottyNetCode open(TlsContext& context, const char* hostname);   // hostname 은 클라이언트의 SNI / 검증용. 서버와 Unix domain 은 NULL
    void          close();

    Result<KiottyNetCode, size_t> encrypt(KiottyNetSpan plain, Stage& cipher_out);          // plain -> cipher_out. 옮긴 plain 길이
    Result<KiottyNetCode, size_t> decrypt(Stage& cipher_in, Stage& plain_out);              // cipher_in -> plain_out. 옮긴 plain 길이
    KiottyNetCode                 handshake(Stage& cipher_in, Stage& cipher_out);           // SUCCESS 면 끝. KIOTTY_BUSY 면 더 주고받아야 한다

    bool handshakeDone() const;

private:
    void* _ssl;                                 // SSL*
};
```

- Stage 를 cipher 쪽 버퍼로 그대로 쓴다. Connection 은 TLS 일 때 `_cipher_recv`, `_cipher_send` Stage 둘을 더 가진다 (7절). 평문 연결은 이 둘을 잡지 않는다.
- 인증서 검증 실패는 `handshake` 가 `KIOTTY_PROTOCOL_ERROR` 로 돌려주고 Connection 이 그 코드로 닫는다.

### 4.5 connection (`connection/kiotty_connection.h`)

구현 가이드 1절의 `Connection` 이다. 필드 이름은 가이드 (`_recv_stage`, `_read_request`, `_work_state`, `_saved_request`, `_finish_request`, `_peer_closed`, `_written`, `_sending`) 를 그대로 쓴다.

```cpp
struct ReadRequest                              // 구현 가이드 1.1
{
    enum Kind { NONE, EXACT, UNTIL };
    Kind    kind;
    size_t  length;
    char    delimiter[4];
    uint8_t delimiter_length;
    size_t  scanned;
};

struct WriteLane                                // 구현 가이드 1.6
{
    char*             bytes;
    size_t            capacity;
    size_t            length;
    size_t            sent;
    std::atomic<bool> pending;
};

struct PushLane
{
    Stage  ring;
    size_t head_sent;
};

struct FinishRequest                            // worker 가 쓰고 IO Thread 가 적용한다 (구현 가이드 1.5)
{
    enum Kind { READ, READ_UNTIL, WRITE, CLOSE };
    Kind          kind;
    size_t        length;                       // READ: exact, READ_UNTIL: max
    char          delimiter[4];
    uint8_t       delimiter_length;
    uint32_t      timeout_ms;
    KiottyNetCode reason;                       // CLOSE
};

enum class WorkState : uint8_t { RUNNING, DRAINING, WORKING, DONE };
enum class Lane      : uint8_t { NONE, WRITE, PUSH };

class ConnectionHost;

class Connection
{
public:
    Connection(ConnectionHost& host, int32_t index, size_t recv_capacity, size_t send_capacity, size_t push_capacity, bool tls);
    ~Connection();

    bool valid() const;

    // 수명. IO Thread. ConnectionHost 가 부른다
    void       open(Socket&& socket, TlsContext* tls, const char* hostname);   // generation 증가, RUNNING, TLS 면 handshake 시작
    void       close(KiottyNetCode reason);                                    // WORKING 이면 미룬다 (소유자 close 는 예외). 마지막에 host 가 onDisconnected 를 낸다
    bool       connected() const;                                              // onConnected 가 나갔고 아직 닫히지 않았다
    PoolHandle handle() const;
    int32_t    generation() const;

    // 5절 Connection 함수. IO Thread. base.cpp 가 host.find(handle) 로 찾아 부른다
    KiottyNetConnectionInfo info() const;
    void                    read(size_t exact);
    void                    readUntil(KiottyNetSpan delimiter, size_t max);
    void                    write(KiottyNetSpan data);
    void                    setTimeout(uint32_t ms);
    KiottyNetConnectionContext connectionContext() const;
    int32_t                    state() const;                                   // Stateful 계층 전용 (구현 가이드 2.2)
    void                       setState(int32_t state);

    // 아무 thread
    KiottyNetCode startWork();                                                  // CAS RUNNING -> DRAINING, host.wake()
    KiottyNetCode finishWork(int32_t generation, const FinishRequest& request); // generation 검사, WRITE 면 write lane 복사, CAS WORKING -> DONE, host.enqueueDone()
    bool          workConnected(int32_t generation) const;

    // IO Thread. host / IoLoop 가 부른다
    bool     push(KiottyNetSpan data);                                          // push lane 복사. 가득 차면 false
    void     onReceiveComplete(SocketCode code, size_t length);                 // 구현 가이드 1.3
    void     onSendComplete(SocketCode code, size_t length);                    // 구현 가이드 1.6 (S)
    void     onConnectComplete(SocketCode code);                                // EventClient 전용
    void     applyFinish();                                                     // DONE -> RUNNING. 구현 가이드 1.5
    void     completeWritten();                                                 // loop 끝 (e). _written 이면 onWritten + pump
    uint64_t deadline() const;                                                  // 0 이면 없음. IoLoop 가 가장 가까운 것을 고른다
    void     onDeadline();                                                      // RUNNING: onTimeout, DRAINING: close(TIMEOUT), WORKING: 무시

private:
    void pump();                                                                // 구현 가이드 1.3
    void drain();                                                               // 구현 가이드 1.6. backend 차이는 EventListener::submitSend 안에 있다
    void beginDraining();                                                       // DRAINING -> WORKING 조건 검사와 onWorkStarted
    void submitReceive();

    ConnectionHost&            _host;
    int32_t                    _index;
    std::atomic<int32_t>       _generation;
    Socket                     _socket;
    TlsSession                 _tls;                                            // 평문이면 open 하지 않는다
    Holder<IoEvent>            _receive_event;
    Holder<IoEvent>            _send_event;
    Stage                      _recv_stage;
    Stage                      _cipher_recv;                                    // TLS 전용. 평문이면 capacity 0
    Stage                      _cipher_send;
    WriteLane                  _write;
    PushLane                   _push;
    ReadRequest                _read_request;
    ReadRequest                _saved_request;
    uint64_t                   _deadline;
    uint64_t                   _saved_deadline;
    FinishRequest              _finish_request;
    std::atomic<WorkState>     _work_state;
    std::atomic<bool>          _peer_closed;
    Lane                       _sending;
    size_t                     _written;
    bool                       _recv_pending;
    bool                       _send_pending;
    bool                       _closing;
    KiottyNetCode              _close_reason;
    KiottyNetConnectionContext _connection_context;
    int32_t                    _state;
};
```

- public 메서드는 세 묶음이다. **5절 C 함수와 1:1 인 것** (`info` ~ `setTimeout`, `startWork`, `finishWork`, `workConnected`), **host / IoLoop 가 부르는 hook**, **수명**. 이 밖의 메서드는 private 이다.
- `finishWork` 하나가 `finishWorkWith*` 넷을 받는다. 넷의 차이는 `FinishRequest::kind` 이고, base.cpp 는 struct 를 채워 넘기기만 한다. `KIOTTY_BUFFER_FULL` (WRITE 가 큼) 이면 상태를 바꾸지 않는다.
- handler 는 Connection 이 들고 있지 않다. `_host.handler()` 로 지금 run 의 표를 읽는다 (7절: run 마다 바뀔 수 있다).
- TLS 가 켜진 연결의 `onReceiveComplete` 는 `_cipher_recv` 에 commit 하고 `_tls.decrypt` 로 `_recv_stage` 를 채운 뒤 `pump` 한다. `drain` 은 lane 의 구간을 `_tls.encrypt` 로 `_cipher_send` 에 넣고 그것을 `submitSend` 한다. 평문이면 Stage 와 lane 을 바로 쓴다. 이 분기는 Connection 안에만 있다.

### 4.6 connection_host (`connection/kiotty_connection_host.h`)

Server 와 EventClient 의 공통 부분이다. pool, EventListener, IoLoop, done ring, push slot, 지금 run 의 handler 를 가진다. 둘의 차이 (listen + accept 인가, connect + 재시도인가) 는 가상 함수 둘이다.

```cpp
struct PushSlot                                 // 구현 가이드 1.6. notify / broadcast 의 대기 slot
{
    std::atomic<int32_t>    state;              // IDLE, FILLED, DONE
    PoolHandle              target;             // index 0 이면 broadcast
    KiottyNetSpan           data;
    KiottyNetCode           code;
    std::mutex              mutex;
    std::condition_variable cv;
};

class DoneRing                                  // MPSC. 칸 수 == max_clients (구현 가이드 1.5)
{
public:
    explicit DoneRing(size_t capacity);
    ~DoneRing();

    bool valid() const;
    void push(int32_t index);                   // worker. 칸은 모자라지 않는다 (연결당 DONE 은 하나)
    bool pop(int32_t& index);                   // IO Thread

private:
    struct Cell
    {
        std::atomic<bool> ready;
        int32_t           index;
    };
    Cell*               _cells;
    size_t              _capacity;
    std::atomic<size_t> _tail;
    size_t              _head;
};

enum class RunState : uint8_t { IDLE, OPENING, RUNNING, CLOSING, STOPPED };

class ConnectionHost
{
public:
    static const size_t PUSH_SLOT_COUNT = 4;

    ConnectionHost(Context& context, size_t capacity, size_t recv_capacity, size_t send_capacity, size_t push_capacity, bool tls);
    virtual ~ConnectionHost();

    bool valid() const;

    // 7절 수명. run 은 blocking, close 와 await 는 아무 thread
    KiottyNetCode run(const KiottyNetConnectionHandler* handler, KiottyNetAppContext app_context);
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);

    // handle -> Connection. generation 이 다르거나 닫혔으면 nullptr
    Connection* find(PoolHandle handle);

    // notify / broadcast. 아무 thread. 구현 가이드 1.6 의 post
    KiottyNetCode post(PoolHandle target, KiottyNetSpan data);

    // Connection 과 IoLoop 가 부른다
    const KiottyNetConnectionHandler& handler() const;
    KiottyNetAppContext               appContext() const;
    EventListener&                    listener();
    bool                              isIoThread() const;
    void                              wake();
    void                              enqueueDone(int32_t index);

    // IoLoop 의 한 바퀴가 부른다 (구현 가이드 1.6 끝의 a~e)
    void processDone();                         // (b) done ring
    void processPushSlots();                    // (b) slot 분배
    void processCompletion(const IoCompletion& completion);   // (c)
    void processDeadlines(uint64_t now_ms);     // (d)
    void completeWritten();                     // (e)
    uint64_t nearestDeadline() const;

    // Connection 이 닫힐 때 부른다
    void onConnectionClosed(Connection& connection, KiottyNetCode reason);   // onDisconnected 전달, slot 반납, EventClient 면 loop 종료

protected:
    virtual KiottyNetCode open() = 0;                                         // Server: bind + listen + submitAccept. EventClient: connect 시작
    virtual void          onOpenCompletion(const IoCompletion& completion) = 0;   // ACCEPT / CONNECT 완료
    virtual void          onAllClosed() = 0;                                  // close 뒤 마지막 연결까지 닫혔다

    Connection* acquire();                      // 빈 slot. 없으면 nullptr (SESSION_EXHAUSTED)
    KiottyNetCode deliver(PoolHandle target, KiottyNetSpan data);              // IO Thread. 구현 가이드 1.6

    Context&                          _context;
    uint32_t                          _owner_id;
    Connection*                       _pool;
    size_t                            _capacity;
    EventListener                     _listener;
    IoLoop                            _loop;
    DoneRing                          _done;
    PushSlot                          _slots[PUSH_SLOT_COUNT];
    TlsContext                        _tls;
    const KiottyNetConnectionHandler* _handler;
    KiottyNetAppContext               _app_context;
    std::atomic<RunState>             _run_state;
    KiottyNetCode                     _run_result;
    std::mutex                        _run_mutex;
    std::condition_variable           _run_cv;
    std::thread::id                   _io_thread;
};
```

- `_pool` 은 `Connection[capacity]` 의 storage 다. Connection 은 이동하지 않으므로 placement new 로 한 번 만들고 소멸자에서 한 번 부순다. 연결이 닫혀도 객체는 그대로이고 `open` 이 generation 을 올린다.
- `find` 가 유일한 "handle → 객체" 길이다. base.cpp 는 `Context::findOwner(handle)->find(handle)` 두 단계로 Connection 을 얻는다. 둘 중 하나가 nullptr 이면 그 함수는 아무 일도 하지 않고 `KIOTTY_PEER_CLOSED` 다.

### 4.7 io_loop (`io_loop/kiotty_io_loop.h`)

한 바퀴의 **순서** 만 책임진다. 바퀴의 각 단계는 host 의 `process*` 를 부르는 한 줄이다. 그래서 IoLoop 은 Connection 을 모른다.

```cpp
class IoLoop
{
public:
    static const size_t COMPLETION_CAPACITY = 64;

    explicit IoLoop(ConnectionHost& host);

    KiottyNetCode run();                        // host.open() 성공 뒤 stop 까지. a -> b -> c -> d -> e 반복
    void          stop();                       // 아무 thread. 플래그 + host.wake()

private:
    void runOnce();

    ConnectionHost&   _host;
    std::atomic<bool> _stopped;
};
```

```
a. _host.listener().wait(nearestDeadline 까지, completions)
b. WAKE 가 있으면 _host.processDone(), _host.processPushSlots()
c. 나머지 completion 마다 _host.processCompletion()
d. _host.processDeadlines(monotonicMs())
e. _host.completeWritten()
```

### 4.8 server (`server/kiotty_server.h`)

```cpp
class Server : public ConnectionHost
{
public:
    Server(Context& context, const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig* tls);

    KiottyNetCode broadcast(KiottyNetSpan data);   // post(PoolHandle{0, 0}, data)

protected:
    KiottyNetCode open() override;                                  // _listen.open + attach + submitAccept
    void          onOpenCompletion(const IoCompletion& completion) override;   // accept 완료: acquire -> Connection::open -> 다시 submitAccept. 가득 차면 바로 close
    void          onAllClosed() override;                           // _listen.close

private:
    KiottyNetServerConfig _config;
    ListenSocket          _listen;
    Holder<IoEvent>       _accept_event;
};
```

### 4.9 client (`client/kiotty_event_client.h`, `client/kiotty_stream_client.h`)

```cpp
class EventClient : public ConnectionHost       // capacity 1
{
public:
    EventClient(Context& context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig* tls);

    PoolHandle    connection() const;           // onConnected 전이면 { 0, 0 }
    KiottyNetCode notify(KiottyNetSpan data);   // post(connection(), data)

protected:
    KiottyNetCode open() override;                                  // Socket::open + connect + submitConnect. 재시도는 onOpenCompletion 이 trial 을 세며 다시 open
    void          onOpenCompletion(const IoCompletion& completion) override;   // finishConnect -> Connection::open. 실패면 trial_interval 뒤 다시, max_trial 넘으면 run 종료
    void          onAllClosed() override;                           // 연결 하나가 닫히면 loop 종료 (run 이 return)

private:
    KiottyNetClientConfig _config;
    Holder<IoEvent>       _connect_event;
    uint32_t              _trial;
};
```

```cpp
class StreamClient                              // 동기. IoLoop 도 EventListener 도 없다
{
public:
    StreamClient(Context& context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig* tls);
    ~StreamClient();

    bool valid() const;

    KiottyNetCode   connect();                                              // max_trial 만큼, connect_timeout_ms 씩. TLS 면 handshake 까지
    void            disconnect();
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_ms); // _recv_stage 에 남은 것이 있으면 그것부터. 없으면 timeout 안에 한 번 recv
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_ms);

private:
    KiottyNetCode waitReadable(uint32_t timeout_ms);                        // poll / select 한 번 (8절 결정 5)
    KiottyNetCode waitWritable(uint32_t timeout_ms);

    Context&              _context;
    KiottyNetClientConfig _config;
    Socket                _socket;
    TlsContext            _tls_context;
    TlsSession            _tls;
    Stage                 _recv_stage;          // TLS 가 레코드 단위로 풀어 주는 평문 중 아직 안 돌려준 것
    Stage                 _cipher_recv;         // TLS 전용
    Stage                 _cipher_send;
};
```

### 4.10 context (`context/kiotty_context.h`)

```cpp
class OwnerTable                                // 프로세스에 하나. pool handle 의 index 로 소유자를 찾는다 (8절 결정 1)
{
public:
    static const size_t MAX_OWNERS = 64;

    Result<KiottyNetCode, uint32_t> add(ConnectionHost& owner);     // owner_id. 가득 차면 KIOTTY_TABLE_FULL
    void                            remove(uint32_t owner_id);
    ConnectionHost*                 find(PoolHandle handle);        // owner_id 와 owner generation 이 맞으면

private:
    struct Entry
    {
        ConnectionHost* owner;
        uint32_t        generation;
    };
    Entry      _entries[MAX_OWNERS];
    std::mutex _mutex;
};

OwnerTable& owners();                           // 전역 인스턴스

class Context
{
public:
    Context();                                  // WSAStartup, OpenSSL 초기화 (프로세스에서 처음이면)
    ~Context();                                 // 아직 살아 있는 소유자가 있으면 assert

    bool valid() const;

    void onOwnerCreated();                      // 소유자 수. destroy 순서 검사용
    void onOwnerDestroyed();

private:
    bool                 _platform_ready;
    std::atomic<int32_t> _owner_count;
};
```

- C 의 Connection 함수들은 context 인자를 받지 않으므로 소유자 표는 프로세스 전역이어야 한다. `Context` 는 플랫폼 초기화와 destroy 순서 검사만 맡는다. 이 둘의 역할 나눔은 8절 결정 1 과 함께 확정한다.

### 4.11 config (`config/kiotty_config.h`)

base.h 의 `KiottyNet_initialize*` / `KiottyNet_configure*` 21 개와 1:1 인 자유 함수다. 길이 검사, 복사, 기본값, `initialization_mark` 가 전부 여기에 있고 base.cpp 는 이름만 바꿔 부른다. 클래스가 아니라 함수인 이유는 상태가 없기 때문이다.

```cpp
static const uint32_t INITIALIZATION_MARK = 0x4B4E4554;   // "KNET"

KiottyNetCode initializeTcpServerConfig(KiottyNetServerConfig& config, const char* host, uint16_t port);
KiottyNetCode initializePipeServerConfig(KiottyNetServerConfig& config, const char* pipe_name);
KiottyNetCode configureServerMaxClients(KiottyNetServerConfig& config, size_t max_clients);
KiottyNetCode configureServerBufferCapacity(KiottyNetServerConfig& config, size_t recv_capacity, size_t send_capacity, size_t push_capacity);
// ... Client, TlsServer, TlsClient 도 같은 이름 규칙

bool isInitialized(const KiottyNetServerConfig& config);   // create 와 configure 의 첫 검사. 4 종류 오버로드
```

---

## 5. thread 규칙

| 클래스 | 만드는 / 부수는 thread | 메서드를 부르는 thread |
|---|---|---|
| `Context`, `OwnerTable` | 앱 | 아무 thread (내부 mutex) |
| `Server`, `EventClient`, `ConnectionHost` | 앱. 소멸자는 `close` 뒤 `run` 의 return 을 기다린다. IO Thread 안에서 부수면 assert | `run` 은 IO Thread 가 되는 thread. `close`, `await`, `post`, `wake`, `enqueueDone` 은 아무 thread. `process*`, `find`, `acquire` 는 IO Thread |
| `IoLoop` | host 와 같다 | `run` 은 IO Thread. `stop` 은 아무 thread |
| `Connection` | host 생성자 / 소멸자 | `startWork`, `finishWork`, `workConnected` 는 아무 thread (atomic 과 generation 으로 보호). 나머지 모두 IO Thread |
| `EventListener` | host | `wake` 만 아무 thread. 나머지 IO Thread |
| `Socket`, `ListenSocket`, `TlsSession` | 소유 객체 | 소유 객체의 thread 하나 |
| `StreamClient` | 앱 | 한 번에 한 thread. 동시 호출은 지원하지 않는다 |

---

## 6. kiotty_net_base.cpp 번역 표

왼쪽 C 함수 하나는 오른쪽 한 줄이다. `host(handle)` 은 `owners().find(fromRaw(handle))`, `conn(handle)` 은 `host(handle) ? host(handle)->find(fromRaw(handle)) : nullptr` 이다. nullptr 이면 "아무 일도 하지 않고 PEER_CLOSED" (규약 8) 다. 그 nullptr 검사와 C ↔ 내부 타입 변환이 base.cpp 에 허용된 전부다.

| C 함수 | 번역 |
|---|---|
| `KiottyNet_initialize*Config`, `KiottyNet_configure*` (21 개) | `config::` 의 같은 이름 함수. `config == NULL` 이면 `KIOTTY_INVALID_ARGUMENT` |
| `KiottyNet_initializeContext` | `new (nothrow) Context`. `!valid()` 면 delete 하고 NULL |
| `KiottyNet_releaseContext` | `delete` |
| `KiottyNet_createServer(ctx, config, tls)` | `new (nothrow) Server(*ctx, *config, tls)`. `!valid()` 면 NULL. `ctx` / `config` NULL 이나 `!isInitialized` 면 NULL |
| `KiottyNet_destroyServer` | `delete` |
| `KiottyNet_runServer` | `server->run(handler, app_context)` |
| `KiottyNet_closeServer` / `awaitServer` | `server->close()` / `server->await(ms)` |
| `KiottyNet_broadcastServer` | `server->broadcast(data)` |
| `KiottyNet_createEventClient` / `destroy` / `run` / `close` / `await` | Server 와 같다 |
| `KiottyNet_getEventClientConnection` | `toRaw(client->connection())` |
| `KiottyNet_notifyEventClient` | `client->notify(data)` |
| `KiottyNet_createStreamClient` / `destroy` | `new (nothrow) StreamClient(...)` / `delete` |
| `KiottyNet_connectStreamClient` / `disconnect` / `read` / `write` | 같은 이름의 메서드 |
| `KiottyNet_getConnectionInfo(h)` | `conn(h) ? conn(h)->info() : KiottyNetConnectionInfo{}` |
| `KiottyNet_readConnection(h, exact)` | `conn(h)->read(exact)` |
| `KiottyNet_readConnectionUntil(h, delim, max)` | `conn(h)->readUntil(delim, max)` |
| `KiottyNet_writeConnection(h, data)` | `conn(h)->write(data)` |
| `KiottyNet_closeConnection(h, reason)` | `conn(h)->close(reason)` |
| `KiottyNet_setConnectionTimeout(h, ms)` | `conn(h)->setTimeout(ms)` |
| `KiottyNet_startWork(h)` | `conn(h)->startWork()` |
| `KiottyNet_notifyConnection(h, data)` | `host(h)->post(fromRaw(h), data)` |
| `KiottyNet_finishWorkWithRead(w, exact, ms)` | `conn(w)->finishWork(gen(w), FinishRequest{ READ, exact, ms })` |
| `KiottyNet_finishWorkWithReadUntil(w, delim, max, ms)` | `conn(w)->finishWork(gen(w), FinishRequest{ READ_UNTIL, delim, max, ms })` |
| `KiottyNet_finishWorkWithWrite(w, data)` | `conn(w)->finishWork(gen(w), FinishRequest{ WRITE, data })` |
| `KiottyNet_finishWorkWithClose(w, reason)` | `conn(w)->finishWork(gen(w), FinishRequest{ CLOSE, reason })` |
| `KiottyNet_isWorkConnected(w)` | `conn(w) ? conn(w)->workConnected(gen(w)) : false` |

- `finishWork` 의 `FinishRequest` 를 채우는 것은 "struct 조립" 이라 번역에 속한다. 단 `delimiter` 길이 검사 (1..4) 는 Connection 안에서 한다.
- `KiottyNet_finishWorkWithWrite` 의 `data` 복사도 base.cpp 가 아니라 `Connection::finishWork` 가 한다. base.cpp 는 span 을 넘길 뿐이다.
- 8절 StateMachine (`KiottyNet_runStatefulServer`) 은 `kiotty_net_statemachine.cpp` 의 일이며, `Connection::state()` / `setState()` / `connectionContext()` 와 `ConnectionHost::appContext()` 를 쓴다.

---

## 7. 생성 시점에 잡는 메모리

`create*` 하나가 잡는 것 전부다. 이 뒤에 잡는 것은 없다.

| 객체 | 잡는 것 | 32 client, 기본 4 KiB 일 때 |
|---|---|---|
| `Connection` (평문) | `sizeof(Connection)` + `recv_capacity` + `send_capacity` + `push_capacity` | 약 12 KiB + 객체 |
| `Connection` (TLS) | 위 + `_cipher_recv`, `_cipher_send` (각 TLS 레코드 최대 16 KiB + 헤더) + OpenSSL 의 SSL 객체 | 약 46 KiB + SSL |
| `ConnectionHost` | `Connection[max_clients]`, `DoneRing(max_clients)`, `PushSlot[4]`, `EventListener` storage | 평문 약 400 KiB, TLS 약 1.5 MiB |
| `Server` | 위 + `ListenSocket` + accept `IoEvent` | |
| `EventClient` | capacity 1 의 host + connect `IoEvent` | 평문 약 13 KiB |
| `StreamClient` | `Socket` + `Stage` 셋 (평문이면 하나) | |

- OpenSSL 의 SSL 객체는 라이브러리 안에서 `SSL_new` 로 잡힌다. "런타임 할당 없음" 의 예외이며, 연결이 열릴 때가 아니라 `TlsSession::open` 을 **Connection 생성자에서 미리** 부를지 (SSL 객체를 slot 마다 재사용, `SSL_clear`) 는 8절 결정 4 다.

---

## 8. 논의해서 결정할 것

인터페이스는 위대로 두고, 아래가 정해지면 해당 클래스의 **안** 을 쓴다. 제안을 적었다.

1. **pool handle 의 index 비트 배치와 소유자 찾기.** C 의 Connection 함수는 context 를 받지 않으므로 전역 `OwnerTable` 이 필요하다. 소유자가 destroy 된 뒤 늦게 온 `finish*` 가 새 소유자의 같은 slot 을 건드리지 않으려면 소유자에도 generation 이 있어야 한다.
   제안: `index = owner_slot(6 bit, 64 개) | owner_generation(10 bit) | connection_slot(16 bit)`. connection generation 은 1 부터 시작해 handle 이 0 이 되지 않는다. `OwnerTable` 은 전역 하나, `Context` 는 플랫폼 초기화와 destroy 순서 검사만.
2. **Pipe transport 의 실체.** Windows named pipe 는 socket 이 아니라 HANDLE 이라 accept 모델이 다르다 (`ConnectNamedPipe`, 클라이언트마다 instance). Windows 10 1803 부터 `AF_UNIX` 를 winsock 이 지원한다.
   제안: `KIOTTY_PIPE` = Unix domain socket 으로 양쪽 모두 구현한다. `host` 는 경로다. 그러면 `Socket` 하나로 끝나고 requirement 13 의 "Windows, Linux 모두에서 Unix Domain" 이 바로 충족된다. named pipe 는 필요해질 때 `backend/` 에 더한다.
3. **reactor backend 의 proactor 흉내.** epoll / kqueue 에서 `submitReceive` 가 받은 버퍼에 listener 가 `recv` 해서 완료를 만든다. level-triggered 로 가고 `submitSend` 는 EAGAIN 때만 EPOLLOUT 을 켠다 (구현 가이드 1.6). io_uring 은 진짜 proactor 라 IOCP 와 같은 길이다.
   제안: 그대로. 이 결정이 `Connection` 에서 backend 분기를 없앤다.
4. **TLS 메모리.** `SSL_new` 를 연결이 열릴 때 부르면 런타임 할당이다.
   제안: `Connection` 생성자에서 `TlsSession::open` 까지 하고, 연결이 닫히면 `SSL_clear` 로 재사용한다. OpenSSL 의 내부 할당은 `CRYPTO_set_mem_functions` 로 pool 에 붙이는 것을 v1 으로 미룬다.
5. **StreamClient 의 대기.** `SO_RCVTIMEO` 는 TLS 와 섞이면 레코드 중간에 끊길 수 있다.
   제안: non-blocking socket + `poll` 한 번 (`waitReadable` / `waitWritable`) 으로 timeout 을 잰다. Windows 는 `WSAPoll`.
6. **deadline scan.** 연결 배열을 매 바퀴 훑는다 (구현 가이드 1.5). 32 개면 충분하다.
   제안: 그대로. `nearestDeadline` 도 scan 이다.
7. **소유자 `close()` 와 worker 의 Session.** `close` 는 WORKING 연결에도 바로 CLOSED 를 낸다. worker 가 아직 Session 을 쓰고 있을 수 있는 것은 사용자 책임으로 둔다 (progress 문서 열린 질문 2).
   제안: 라이브러리 규칙은 두지 않고 사용 지침으로 적는다.
8. **`Holder<IoEvent>::HOLDER_SIZE`.** IOCP 의 OVERLAPPED (32 바이트) + kind + owner 면 64 로 충분하고, AcceptEx 의 주소 버퍼 (2 × (sizeof(sockaddr_storage) + 16)) 를 event 안에 두면 더 커진다.
   제안: accept 전용 버퍼는 `Server` 가 따로 들고 `IoEvent` 는 128 로 시작한다. backend 가 `static_assert` 로 검사한다.

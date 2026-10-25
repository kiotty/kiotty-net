# Kiotty-Net

## 목표

1. Client, Server를 모두 지원하는 C++11 이하에서 작동 가능한 Network Library
2. Cross-platform 지원
   - Windows
   - Linux
   - Android
   - MacOS
   - IOS
3. OS별 Backend는 아래와 가음
    - Windows
      - IOCP
    - Linux
      - io_uring
      - epoll (io_uring 지원 안 하면)
    - MacOS (Darwin)
      - kqueue
4. TLS 지원을 위한 openssl을 제외하고 zero-dependency
5. Request 모델(요청, 응답)과 Event 모델(서버에서 발생하는 이벤트 보내기)를 모두 지원함 
6. Major한 통신 Protocol 지원
   - HTTP
   - WebSocket
7. Custom 통신 Protocol을 지원하여, 서버 개발자가 프로토콜에 대한 정의만 되면 개발 가능함.
    - 추후, DICOM 라이브러리 개발에 소스 코드를 그대로 활용할 예정
8. AI 또는 사람이 읽기 좋은 단순한 문서화
9.  HTTP는 Rest API 뿐만 아니라 정적 파일 서빙 기능도 제공함. 정적파일 서빙은 파일 redirecting 뿐만 아니라 메모리에 올라와있는 바이너리 파일도 적절한 redirecting을 통해 제공해줄 수 있음
10. 서버의 성능은 고성능이 아니라 일반 PC 또는 모바일 디바이스에서 최대 32 Client까지 지원하는 것으로 가정하고 있음.
11. 서버가 완전 고성능을 지원하는 게 아닌데신 의료기기 소프트웨어에서 사용할 확률이 높기 때문에 매우 높은 수준의 메모리 안정성을 지원해야 함.

- Runtime 동적 할당을 최대한 제약하기
  - 이미 할당된 Pool을 통해서 할당하기
  - Stack을 최대한 활용하기
- 메모리는 어플리케이션 시작 시점에서 최대한 모두 할당되고 사용한 메모리를 재활용하는 방식으로 사용하기
- Dangling Pointer를 방지해야 함

11. 예외 처리 모델은 Result<V, Code> 방식으로 사용함.
12. 빌드에 포함하는 방법은 아래와 같다.

- Github Action을 통해서 배포된 이미 빌드된 결과물 가져오기
  - C 버전 : pre-built된 static/dynamic library와 single c interface include 파일
  - C++ 버전 : pre-built된 static/dynamic library와 c++ class 형식으로 된 single interface include 파일
- FetchContent로 손쉽게 가져오기
  - PREBUILT option 키면, CMake가 알아서 architecture랑 os, compiler 파악해서 Github Action에 있는 pre-built된 거 가져오기
  - PREBUILT option 안 키면, 그냥 소스코드 포함 빌드
- 간단한 script로 아래의 조건만 넣으면 소스 코드를 특정 위치로 간단히 import 시켜주기.
  - 특정 tag
  - 원하는 위치
  - amalgam 여부

13. 가능하다면 Windows, Linux 모두에서 Unix Domain도 제공..


## 폴더 구조

README.md // 빌드에 포함하는 법, 사용법, 버전 관리 있음
CMakeLists.txt  // 최상위 빌드 스크립트
include/
    kiotty-net.h // C interface. C++ interface는 C++로 build 시 포함됨.
src/ // 
    kiotty-net.cpp // kiotty-net.h의 구현
    impl/
        core/
            kiotty-result.hpp // KiottyResult<Value, Code>
            kiotty-holder.hpp // Derived class의 최대 크기가 정해진 Base class를 별도로 동적할당하지 않고 보관하기 위한 container.
        
tests/ // 
docs/
    requirement/
        requirement_v0.md           // 이 문서. 외부 인터페이스와 규칙만
        implementation_guide_v0.md  // 구현 예시와 설계 근거



## 외부 인터페이스

이 장은 인터페이스 선언과 그 의미를 주석으로만 적는다. 구현 예시, 흐름 의사코드, 프로토콜 예제는 [implementation_guide_v0.md](implementation_guide_v0.md) 에 있다.

인터페이스는 C 와 C++ 두 벌이다. 규약은 다음과 같다.

- `include/kiotty-net.h` 하나에 C 인터페이스가 있고, C++ 로 빌드하면 같은 헤더가 C++ 인터페이스를 함께 노출한다. C++ 인터페이스는 C 구조체 위의 얇은 typed wrapper 다.
- C 는 `kiotty_` 접두사, 불투명 핸들 (`kiotty_server_t*`), 콜백은 `void* user_context` 를 첫 인자로 받는 함수 포인터 구조체다.
- 두 벌 모두 런타임 동적 할당이 없다. 객체 생성 시점(서버 생성, Runner 생성)에 `max_clients` 분량을 한 번 잡고 재사용한다.
- **thread 규칙.** 모든 콜백은 IO Thread 에서 순서대로 호출되고, `Connection` / `WebSocket` / `HttpResponse` 는 IO Thread 에서만 쓴다.
  다른 thread 가 할 수 있는 일은 소유자(Server, EventClient, HttpServer)의 `resume(id, payload)` 와 `stop()` 뿐이다. `resume` 은 IO Thread 에서 `onResumed` 콜백 한 번으로 돌아온다.
- 각 절은 C++ 선언을 먼저 적고 C 선언을 뒤에 적는다. 이름은 1:1 로 대응한다.

공통 타입

```cpp
struct Span {                     // 읽기 전용 바이트 범위. 텍스트와 바이너리 모두 이것으로 받는다. 소유하지 않는다
    const char* data;
    size_t      length;
};

typedef uint64_t ConnectionId;    // 상위 32비트 generation + 하위 32비트 slot index. 0 은 무효. 연결이 닫히면 같은 slot 이라도 값이 달라진다
```

```c
typedef struct kiotty_span { const char* data; size_t length; } kiotty_span_t;
typedef uint64_t kiotty_connection_id_t;
```

### 1. Error 처리

```cpp
enum NetCode {
    SUCCESS,
    PEER_CLOSED,          // 상대가 FIN 을 보냄. resume: 그 id 의 연결이 이미 닫힘
    SOCKET_ERROR,         // OS 소켓 에러
    PROTOCOL_ERROR,       // readUntil 의 max 안에 delimiter 가 없음, 프로토콜 파서 실패
    REQUEST_TOO_LARGE,    // read 요구 길이가 수신 Stage capacity 보다 큼
    UNHANDLED_EVENT,      // StateMachine 표에 (state, event) 행이 없음
    ILLEGAL_TRANSITION,   // StateMachine 핸들러가 .to() 목록에 없는 상태로 감 (debug)
    SESSION_EXHAUSTED,    // max_clients 초과
    DUPLICATE_RULE,       // C StateMachine 표에 같은 (state, event) 가 두 번
    TIMEOUT,              // 동기 API 의 timeout 만료
    TABLE_FULL,           // 라우트 / 정적 경로 / 승격 경로 등록 표가 가득 참
    BUFFER_FULL,          // 송신 Stage, 응답 버퍼, resume 큐에 여유가 없음
    NOT_FOUND,            // 파일 없음
    INVALID_ARGUMENT
};
```

```c
typedef enum kiotty_code {
    KIOTTY_SUCCESS, KIOTTY_PEER_CLOSED, KIOTTY_SOCKET_ERROR, KIOTTY_PROTOCOL_ERROR, KIOTTY_REQUEST_TOO_LARGE,
    KIOTTY_UNHANDLED_EVENT, KIOTTY_ILLEGAL_TRANSITION, KIOTTY_SESSION_EXHAUSTED, KIOTTY_DUPLICATE_RULE,
    KIOTTY_TIMEOUT, KIOTTY_TABLE_FULL, KIOTTY_BUFFER_FULL, KIOTTY_NOT_FOUND, KIOTTY_INVALID_ARGUMENT
} kiotty_code_t;
```

### 2. 예외 처리

무조건, Result<V, ErrorCode>로. 예외를 던지지 않는다.
길이와 코드를 같이 돌려주는 동기 API (StreamClient) 는 아래 타입을 쓴다.

```cpp
struct NetResult {
    int32_t bytes;   // 전달 또는 송신한 길이. 에러면 0
    NetCode code;
};
```

```c
typedef struct kiotty_result { int32_t bytes; kiotty_code_t code; } kiotty_result_t;
```

### 3. Configuration 구성 방식

1. 서버 Configuration과 클라이언트 Configuration은 구분되어야 한다.
    - TLS가 다르기 때문에
    - 동기 Connect, Read, Write 때문에 클라이언트 Configuration은 max_trial과 time_interval_between_trival이 있는 것이 좋다. (아이디어의 유효성 검증 필요)

2. Pipe, TCP, TLS 3가지 연결 방식이 지원된다.

3. TLS는 TLS V1.2, TLS V1.3을 지원한다.

4. TLS CipherSuite는 아래와 같이 지원한다.

```
enum class TlsV12CipherSuite {
    ECDHE_ECDSA_AES256_GCM_SHA384 = 1 << 0,
    ECDHE_RSA_AES256_GCM_SHA384 = 1 << 1,
    ECDHE_ECDSA_CHACHA20_POLY1305 = 1 << 2, 
    ECDHE_RSA_CHACHA20_POLY1305 = 1 << 3,
};
enum class TlsV13CipherSuite {
    TLS_AES_128_GCM_SHA256 = 1 << 0,
    TLS_AES_256_GCM_SHA384 = 1 << 1,
    TLS_CHACHA20_POLY1305_SHA256 = 1 << 2,
    TLS_AES_128_CCM_SHA256 = 1 << 3,
    TLS_AES_128_CCM8_SHA256 = 1 << 4
};
```

5. TLS Configuration은 알아야될 게 많다. C에서 가능한지는 모르겠지만 TLS Configuration은 Builder 형식으로 제공됐으면 좋겠다. 사용자는 Configuraiton의 빌더만 보고도 TLS 연결에서 무엇이 필요로 하고 무슨 순서로 이루어지고 어떤 게 필수고 어떤 게 필수가 아닌지 확인할 수 있을 것 같음.

- Client
   1. 어떤 TLS를 사용할지
      1. TLS V1.2면 어떤 CipherSuite를 쓸지
      2. TLS V1.3이면 어떤 Cipher SUite를 쓸지
   2. 서버의 신원을 검증할지 말지
      1. hostname 검증을 위해 필요한 CA 인증서 파일 및 디렉토리 경로 지정 (client_trust_store_path)
      2. 서버 검증한다면 hostname 검증할지
   3. 서버가 클라이언트 신원을 인증할지 말지
      1. cert_file 경로
      2. key file 경로
      3. key file이 암호화되있을 떄 비밀번호

- Server
   1. 어떤 TLS를 사용할지
      1. TLS V1.2면 어떤 CipherSuite를 쓸지
      2. TLS V1.3이면 어떤 Cipher SUite를 쓸지 
   2. 서버의 신원을 어떻게 증빙할지
      1. cert_file 경로
      2. key file 경로
      3. key file이 암호화되있을 떄 비밀번호 
   3. 클라이언트가 서버 신원을 인증할지 말지
        1. 신원 인증 방식 (인증할 수 있으면 인증함 (Optional), 반드시 인증해야 함(REQUIRED))
        2. 인증을 한다면, (server_trust_store_path)

6. Endpoint 와 공통 설정. TLS 설정(5번 Builder)은 미결이므로 포인터로만 둔다.

```cpp
enum class Transport { Tcp, Tls, Pipe };

struct Endpoint {
    Transport   transport;
    const char* host;        // Tcp / Tls: ip 또는 hostname. Pipe: pipe 이름 (Windows: \\.\pipe\name, Unix: 경로)
    uint16_t    port;        // Pipe 면 무시
};

struct ServerConfig {
    Endpoint          endpoint;
    size_t            max_clients;        // 동시 연결 수. 이만큼의 Connection, Stage, Session, resume 큐가 생성 시점에 잡힌다
    size_t            recv_capacity;      // Connection 당 수신 Stage 크기
    size_t            send_capacity;      // Connection 당 송신 Stage 크기
    const TlsServerConfig* tls;           // Transport::Tls 일 때만. 미결 (5번)
};

struct ClientConfig {
    Endpoint          endpoint;
    size_t            recv_capacity;
    size_t            send_capacity;
    uint32_t          connect_timeout_ms;
    uint32_t          max_trial;          // 연결 재시도 횟수
    uint32_t          trial_interval_ms;  // 재시도 간격
    const TlsClientConfig* tls;           // 미결 (5번)
};
```

```c
typedef enum kiotty_transport { KIOTTY_TCP, KIOTTY_TLS, KIOTTY_PIPE } kiotty_transport_t;
typedef struct kiotty_endpoint { kiotty_transport_t transport; const char* host; uint16_t port; } kiotty_endpoint_t;
typedef struct kiotty_server_config { kiotty_endpoint_t endpoint; size_t max_clients; size_t recv_capacity; size_t send_capacity; const kiotty_tls_server_config_t* tls; } kiotty_server_config_t;
typedef struct kiotty_client_config { kiotty_endpoint_t endpoint; size_t recv_capacity; size_t send_capacity; uint32_t connect_timeout_ms; uint32_t max_trial; uint32_t trial_interval_ms; const kiotty_tls_client_config_t* tls; } kiotty_client_config_t;
```

### 4. Stage

Connection 하나가 수신용, 송신용으로 두 개 가지는 고정 크기 버퍼. 라이브러리 내부 객체이며 C 인터페이스로 노출하지 않는다.
IO Thread 는 read 요청이 있든 없든 계속 recv 해서 수신 Stage 에 쌓고, `Connection::read*` 는 "이 조건이 되면 onRead 한 번" 이라는 조건 등록일 뿐이다.
수신 흐름(ReadRequest, pump)은 구현 가이드 1절에 있다.

```cpp
class Stage
{
public:
    Stage(size_t capacity);
    ~Stage();

    class Provider {                          // 쓰는 쪽 (recv 완료, write 요청)
        size_t writable() const;              // 현재 쓸 수 있는 길이
        void*  prepare(size_t requested);     // requested 만큼 쓸 수 있으면 버퍼, 아니면 nullptr
        void   commit(size_t written);        // 실제 쓴 길이
    };

    class Consumer {                          // 읽는 쪽 (onRead 전달, send 제출)
        size_t readable() const;              // 현재 읽을 수 있는 길이
        void*  prepare(size_t requested);     // requested 만큼 읽을 수 있으면 버퍼, 아니면 nullptr. commit 전까지는 peek
        void   commit(size_t consumed);       // 실제 소비한 길이
    };

    Provider& provider();
    Consumer& consumer();
    size_t    capacity() const;
    void      compact();                      // 남은 데이터를 버퍼 앞으로 memmove 해서 provider 공간 확보

private:
    char*  _buffer;
    size_t _capacity;
    size_t _begin_pos;                        // 데이터가 시작하는 index
    size_t _end_pos;                          // 데이터가 끝나는 index
};
```

### 5. Connection

- 비동기 Socket IO 모델에서 연결을 추상화하는 객체. EventClient, Server 에서 사용된다.
- Pipe, TLS, TCP 에 따라 read / write 의 내부가 달라지지만 인터페이스는 같다.
- **IO Thread 에서만 사용한다.** 다른 thread 는 `id()` 값을 들고 있다가 소유자의 `resume(id, payload)` 를 부른다 (6, 7절).
- read 요청은 두 종류뿐이고 한 번에 하나만 걸린다. 새 요청은 이전 요청을 대체한다. write 는 read 와 독립이다.
- `suspend()` 는 콜백을 멈추고 slot 을 붙잡아 둔다. peer 가 끊어도 slot 은 재사용되지 않고, `resume` 뒤 `onResumed` 다음에 `onDisconnected` 가 온다. 재개는 `resume` 으로만 된다.

```cpp
struct ConnectionInfo {
    char    hostname[128];   // ip 또는 pipe 이름
    int32_t port;
};

struct Connection {
    ConnectionInfo& info();
    ConnectionId    id() const;                      // 다른 thread 에 넘길 값. Session 에 저장해 둔다

    void* context() const;                           // per-connection slot. 라이브러리는 읽지도 쓰지도 않는다. (StateMachine Runner 는 여기에 Session 을 둔다)
    void  setContext(void* context);

    // 수신 Stage 에 정확히 exact 바이트가 모이면 onRead(data) 한 번. data.length == exact.
    // exact 가 recv_capacity 보다 크면 즉시 close(REQUEST_TOO_LARGE).
    void read(size_t exact);

    // delimiter 가 나타나면 onRead(data) 한 번. data 는 delimiter 를 포함한다.
    // max 바이트 안에 delimiter 가 없으면 close(PROTOCOL_ERROR). delimiter 는 1..4 바이트이며 등록 시 Connection 안으로 복사된다.
    void readUntil(Span delim, size_t max);

    // 송신 Stage 로 복사하고 제출. 완료되면 onWritten(length) 한 번. 여유가 없으면 즉시 close(BUFFER_FULL).
    void write(Span data);

    // Long term 작업을 위해 IO Thread 의 polling 을 멈춘다. 콜백이 나가지 않으며 들어온 데이터는 수신 Stage 에 쌓인다.
    // Server::resume(id, payload) 가 올 때까지 유지된다. 그동안 slot 은 재사용되지 않는다.
    void suspend();

    // 종료. 마지막으로 onDisconnected(reason) 한 번이 오고, 그 뒤 이 Connection 에 콜백은 없다.
    void close(NetCode reason = SUCCESS);

    // deadline 하나. 만료되면 onTimeout 한 번이 오고 deadline 은 해제된다. 0 이면 해제.
    void setTimeout(uint32_t ms);
};

// 모든 콜백의 첫 인자는 ConnectionHandler::user_context (handler 소유자) 다. per-connection 데이터는 Connection::context() 에 둔다.
struct ConnectionHandler {
    void* user_context;
    void (*onConnected)   (void* user_context, Connection* connection);                   // accept / connect 완료
    void (*onDisconnected)(void* user_context, Connection* connection, NetCode reason);   // 마지막 콜백
    void (*onRead)        (void* user_context, Connection* connection, Span data);        // data 는 수신 Stage. return 하면 무효
    void (*onWritten)     (void* user_context, Connection* connection, size_t length);    // Connection::write 하나의 완료
    void (*onResumed)     (void* user_context, Connection* connection, void* payload);    // Server::resume(id, payload). 이 안에서 다시 suspend() 하면 유지
    void (*onTimeout)     (void* user_context, Connection* connection);                   // setTimeout 만료
};
```

```c
typedef struct kiotty_connection kiotty_connection_t;                                   /* 불투명 */
typedef struct kiotty_connection_info { char hostname[128]; int32_t port; } kiotty_connection_info_t;

const kiotty_connection_info_t* kiotty_connection_info(kiotty_connection_t* c);
kiotty_connection_id_t kiotty_connection_id(kiotty_connection_t* c);
void*  kiotty_connection_context(kiotty_connection_t* c);
void   kiotty_connection_set_context(kiotty_connection_t* c, void* context);
void   kiotty_connection_read(kiotty_connection_t* c, size_t exact);
void   kiotty_connection_read_until(kiotty_connection_t* c, kiotty_span_t delim, size_t max);
void   kiotty_connection_write(kiotty_connection_t* c, kiotty_span_t data);
void   kiotty_connection_suspend(kiotty_connection_t* c);
void   kiotty_connection_close(kiotty_connection_t* c, kiotty_code_t reason);
void   kiotty_connection_set_timeout(kiotty_connection_t* c, uint32_t ms);

typedef struct kiotty_connection_handler {
    void* user_context;
    void (*on_connected)   (void* user_context, kiotty_connection_t* c);
    void (*on_disconnected)(void* user_context, kiotty_connection_t* c, kiotty_code_t reason);
    void (*on_read)        (void* user_context, kiotty_connection_t* c, kiotty_span_t data);
    void (*on_written)     (void* user_context, kiotty_connection_t* c, size_t length);
    void (*on_resumed)     (void* user_context, kiotty_connection_t* c, void* payload);
    void (*on_timeout)     (void* user_context, kiotty_connection_t* c);
} kiotty_connection_handler_t;
```

### 6. Client

1. Client는 Configuration과 상관 없이 동일한 기능을 제공해야 한다.

2. StreamClient와 EventClient를 구분한다.
    2.1 StreamClient의 목적은 User-space의 데이터를 direct하게 바로 network로 보내주기 위함이다. 동기 API 이며 IO Thread 가 없다.
    2.2 EventClient의 목적은 서버에서 먼저 날아오는 이벤트를 효과적으로 handle하면서도 User-space의 데이터를 direct하게 network로 보내는 것이 목적이다. Connection 하나와 IO Thread 를 가지며 ConnectionHandler (또는 StateMachine Runner) 를 주입받는다.

```cpp
class StreamClient {
public:
    StreamClient(const ClientConfig& config);
    NetCode   connect();                                            // max_trial / trial_interval_ms 만큼 재시도
    void      disconnect();
    NetResult read(void* buffer, size_t length, uint32_t timeout_ms);   // 최대 length. 0 바이트면 PEER_CLOSED
    NetResult write(Span data, uint32_t timeout_ms);
};

class EventClient {
public:
    EventClient(const ClientConfig& config, ConnectionHandler handler);
    NetCode connect();                                              // 성공하면 run() 안에서 onConnected 가 호출된다
    void    disconnect();
    NetCode run();                                                  // 호출한 thread 를 점유하고 event 를 polling. stop() 까지 return 하지 않는다
    void    stop();                                                 // 연결은 끊지 않고 polling 만 중단. 다른 thread 에서 호출 가능
    NetCode resume(ConnectionId id, void* payload);                 // 7절 Server::resume 과 동일. 다른 thread 에서 호출 가능
};
```

```c
typedef struct kiotty_stream_client kiotty_stream_client_t;
kiotty_stream_client_t* kiotty_stream_client_create(const kiotty_client_config_t* config);
void                    kiotty_stream_client_destroy(kiotty_stream_client_t* c);
kiotty_code_t           kiotty_stream_client_connect(kiotty_stream_client_t* c);
void                    kiotty_stream_client_disconnect(kiotty_stream_client_t* c);
kiotty_result_t         kiotty_stream_client_read(kiotty_stream_client_t* c, void* buffer, size_t length, uint32_t timeout_ms);
kiotty_result_t         kiotty_stream_client_write(kiotty_stream_client_t* c, kiotty_span_t data, uint32_t timeout_ms);

typedef struct kiotty_event_client kiotty_event_client_t;
kiotty_event_client_t*  kiotty_event_client_create(const kiotty_client_config_t* config, kiotty_connection_handler_t handler);
void                    kiotty_event_client_destroy(kiotty_event_client_t* c);
kiotty_code_t           kiotty_event_client_connect(kiotty_event_client_t* c);
void                    kiotty_event_client_disconnect(kiotty_event_client_t* c);
kiotty_code_t           kiotty_event_client_run(kiotty_event_client_t* c);
void                    kiotty_event_client_stop(kiotty_event_client_t* c);
kiotty_code_t           kiotty_event_client_resume(kiotty_event_client_t* c, kiotty_connection_id_t id, void* payload);
```

> 고민되는 부분. 생각보다 network 환경이 열악할 때도 있기 때문에 timeout만 걸어두는 게 아니라,
> 재시도 횟수와 시도와 시도 사이의 time interval도 걸어두는 게 좋다. (ClientConfig 의 max_trial / trial_interval_ms 로 반영)

### 7. Server

1. Server는 Configuration과 상관 없이 동일한 기능으로 돌아간다.
2. 생성 시점에 `max_clients` 분량의 Connection, Stage, resume 큐를 모두 잡는다. 이후 런타임 할당은 없다.
3. 프로토콜은 `ConnectionHandler` 로 주입한다. StateMachine 을 쓰면 `Runner::handler()` 가 그것을 돌려준다 (8절). HTTP / WebSocket 은 9, 10절의 전용 서버가 내부에서 이것을 쓴다.
4. `resume(id, payload)` 가 다른 thread 에서 IO Thread 로 들어오는 유일한 길이다. 긴 작업의 완료와 Event 모델의 push 가 모두 이것이다.

```cpp
class Server {
public:
    Server(const ServerConfig& config, ConnectionHandler handler);
    NetCode open();      // bind + listen 까지
    void    close();     // listen 소켓과 모든 Connection 종료. 각 Connection 에 onDisconnected(SUCCESS)
    NetCode run();       // 호출한 thread 를 점유하고 accept / IO event 를 polling. stop() 까지 return 하지 않는다
    void    stop();      // listen 은 유지한 채 polling 만 중단. 다른 thread 에서 호출 가능

    // 아무 thread 에서나. IO Thread 에서 onResumed(user_context, connection, payload) 가 한 번 호출된다.
    //   suspend 된 연결 : 살아 있음이 보장된다. onResumed 가 return 하면 재개된다. 그 안에서 다시 suspend() 하면 유지
    //   그 외 (push)    : 호출 시점에 살아 있으면 큐에 넣고, 전달 전에 닫히면 조용히 버린다. payload 의 수명은 호출자 책임
    // 반환값은 호출 시점 기준. id 의 연결이 이미 닫혔으면 PEER_CLOSED, 큐(max_clients * 4)가 가득 차면 BUFFER_FULL
    NetCode resume(ConnectionId id, void* payload);
};
```

```c
typedef struct kiotty_server kiotty_server_t;
kiotty_server_t* kiotty_server_create(const kiotty_server_config_t* config, kiotty_connection_handler_t handler);
void             kiotty_server_destroy(kiotty_server_t* s);
kiotty_code_t    kiotty_server_open(kiotty_server_t* s);
void             kiotty_server_close(kiotty_server_t* s);
kiotty_code_t    kiotty_server_run(kiotty_server_t* s);
void             kiotty_server_stop(kiotty_server_t* s);
kiotty_code_t    kiotty_server_resume(kiotty_server_t* s, kiotty_connection_id_t id, void* payload);
```

### 8. StateMachine

HTTP, WebSocket, Custom Protocol 작성을 돕는 **선택적 계층**이다. Connection 과 ConnectionHandler (5절) 위에 얹히며 그 둘은 이 계층을 모른다.
프로토콜 작성자는 **(1) State enum, (2) Session struct, (3) Handler 함수들, (4) rules(...) 표** 네 가지만 쓴다.
콜백을 직접 구현하지 않고, Handler 안에서 Connection 의 IO 함수를 직접 부르지도 않는다. Handler 는 `(Session, Event) -> Transition` 인 순수 함수이고, Transition 의 Request 를 Runner 가 Connection 호출로 바꾼다.

규칙

- `Event` 는 IO Thread 가 알려주는 "무슨 일이 일어났는가", `Request` 는 표가 돌려주는 "다음에 무엇을 해 달라" 다. 한 Transition 은 Request 하나만 가진다.
- `RESUMED` 는 `Server::resume(id, payload)` 가 도착한 것이다. 긴 작업의 완료와 다른 thread 의 push 가 모두 이 이벤트로 들어온다. 핸들러가 return 한 Request 가 곧 재개다.
- `Transition::stay(request)` 는 상태를 바꾸지 않는다. ANY 행(push, WRITTEN 흡수)에서 현재 상태를 모른 채 요청만 낼 때 쓴다.
- `WRITTEN` 은 `Request::write` 의 완료에 온다. push 로 낸 write 의 완료도 온다. 수신 흐름과 무관한 WRITTEN 은 `when(WRITTEN).then(ignore)` 한 행이 `stay()` 로 흡수한다.
- `CLOSED` 는 마지막 Event 다. 그 핸들러의 Transition 은 무시된다.
- 표에 (state, event) 행이 없으면 `close(UNHANDLED_EVENT)`. 조용히 무시하지 않는다. 정확 일치 행이 ANY 행보다 우선한다.
- `.to(...)` 는 핸들러가 갈 수 있는 다음 상태 목록이다. debug 빌드의 Runner 는 목록 밖으로 가면 `close(ILLEGAL_TRANSITION)`. `.to()` 가 없는 행은 검사하지 않는다. `stay` 는 검사 대상이 아니다.
- `Event::data` 는 수신 Stage 를 가리키며 핸들러가 return 하면 무효다. 보관하려면 Session 으로 복사한다.
- state 는 Runner 가 소유한다. Session 에 state 멤버를 두지 않는다.
- 표는 컴파일 타임 상수다. C++ 의 `rules()` 는 (state, event) 로 정렬하고 중복 행을 컴파일 에러로 만든다. C 의 표는 `static const` 배열이며 Runner init 이 중복을 검사한다.

```cpp
namespace fsm {

struct Event {
    enum Kind { CONNECTED, READ, WRITTEN, RESUMED, TIMEOUT, CLOSED };
    Kind    kind;
    Span    data;         // READ: 수신 데이터 (return 하면 무효). WRITTEN: data.length = 보낸 길이, data.data = nullptr
    void*   payload;      // RESUMED 전용. Server::resume(id, payload) 의 payload
    NetCode code;         // CLOSED 전용. 그 외 SUCCESS
};
enum { ANY = -1, MAX_NEXT = 4 };

struct Request {
    Request();                                               // 기본값 = 새 요청 없음
    static Request read(size_t exact);                       // 정확히 exact 바이트가 모이면 READ 한 번
    static Request readUntil(Span delim, size_t max);        // delimiter(1..4 바이트, 등록 시 복사) 까지 포함해서 READ 한 번. max 안에 없으면 CLOSED(PROTOCOL_ERROR)
    static Request write(Span data);                         // 송신 Stage 로 복사. 완료되면 WRITTEN 한 번
    static Request suspend();                                // 콜백 중단 + slot hold. Server::resume 이 RESUMED 로 온다
    static Request close();                                  // 정상 종료. 마지막으로 CLOSED(SUCCESS) 한 번
    Request& timeout(uint32_t ms);                           // 이 요청에 deadline. 만료되면 TIMEOUT. 핸들러가 순수 함수인 모델에서 시간이 들어오는 유일한 입구
};

template <typename State>
struct Transition {
    Transition(State next);                                  // 상태 전이, 요청 없음 (CLOSED 핸들러 등)
    Transition(State next, Request request);                 // 상태 전이 + 요청
    static Transition stay(Request request = Request());     // 상태 유지 + 요청 (push, WRITTEN 흡수)
};

template <typename State, typename Session>
struct Rule {                                                // 표의 한 행. 사용자가 직접 만들지 않고 from / when 으로 만든다
    typedef Transition<State> (*Handler)(Session& session, Connection& connection, const Event& event);
    template <typename... S> constexpr Rule to(S... next) const;   // .to(A, B)    : 갈 수 있는 다음 상태 1..MAX_NEXT 개
    template <State... next> constexpr Rule to() const;            // .to<A, B>()  : 같은 뜻
};

// 표 작성. State 와 Session 은 .then(handler) 의 함수 타입에서 추론되므로 접두사가 필요 없다.
//   from(S).when(E).then(handler).to(S1, S2)   이 (상태, 이벤트) 에서 handler 가 돌고 S1 또는 S2 로 간다
//   when(E).then(handler)                      모든 상태에서 (ANY). 다음 상태 검사 없음
template <typename State> constexpr From<State> from(State state);
constexpr When when(Event::Kind event);

// 행들을 (state, event) 순으로 컴파일 타임 정렬한 표. 같은 (state, event) 가 두 번이면 컴파일 에러.
template <typename R, typename... Rs> constexpr Table<R, 1 + sizeof...(Rs)> rules(R first, Rs... rest);

template <typename State, typename Session>
struct Machine {                                             // 표 + 초기 상태. 포인터 + 개수라서 C 의 kiotty_state_machine_t 와 같은 모양
    const Rule<State, Session>* rule; size_t count; State initial;
    template <size_t N> constexpr Machine(const Table<Rule<State, Session>, N>& table, State initial);
};

template <typename State, typename Session, size_t N>
struct Slots;                                                // Connection N 개 분의 { state, in_use, Session }. 사용자가 한 번 잡아 두는 저장소

template <typename State, typename Session>
class Runner {                                               // ConnectionHandler 를 구현하는 어댑터. Connection 을 만지는 코드는 여기뿐
public:
    template <size_t N> Runner(const Machine<State, Session>& machine, Slots<State, Session, N>& storage);
    ConnectionHandler handler();                             // Server / EventClient 에 주입
};

} // namespace fsm
```

사용 예

```cpp
using namespace fsm;
enum class HttpState { Idle, ReadingHeader, ReadingBody, Processing, Responding };
struct HttpSession { ConnectionId id; /* per-connection 데이터. state 멤버 없음 */ };
typedef Transition<HttpState> Next;

static Next onHeader(HttpSession& s, Connection& c, const Event& e);   // ... 핸들러들 (구현 가이드 3.1)

static constexpr auto HTTP_RULES = rules(
    when(Event::CONNECTED)                              .then(onConnected).to(HttpState::ReadingHeader),
    from(HttpState::ReadingHeader).when(Event::READ)    .then(onHeader)   .to(HttpState::ReadingBody, HttpState::Processing, HttpState::Responding),
    from(HttpState::ReadingBody)  .when(Event::READ)    .then(onBody)     .to(HttpState::Processing, HttpState::Responding),
    from(HttpState::Processing)   .when(Event::RESUMED) .then(onResumed)  .to(HttpState::Responding),
    from(HttpState::Responding)   .when(Event::WRITTEN) .then(onResponded).to(HttpState::ReadingHeader, HttpState::Idle),
    when(Event::TIMEOUT)                                .then(onTimeout)  .to(HttpState::Idle),
    when(Event::CLOSED)                                 .then(onClosed)
);
static constexpr Machine<HttpState, HttpSession> HTTP_MACHINE(HTTP_RULES, HttpState::Idle);

Slots<HttpState, HttpSession, 32> slots;
Runner<HttpState, HttpSession>    runner(HTTP_MACHINE, slots);
Server                            server(config, runner.handler());

// worker thread : 긴 작업이 끝나면
server.resume(session->id, &session->response);              // IO Thread 에서 Event::RESUMED 로 표에 들어간다
```

C 인터페이스. 표는 `static const kiotty_rule_t[]` 이고 `KIOTTY_RULE` 매크로가 next 개수를 컴파일 타임에 센다 (C99 이상).

```c
typedef enum kiotty_event_kind { KIOTTY_EVENT_CONNECTED, KIOTTY_EVENT_READ, KIOTTY_EVENT_WRITTEN, KIOTTY_EVENT_RESUMED, KIOTTY_EVENT_TIMEOUT, KIOTTY_EVENT_CLOSED } kiotty_event_kind_t;

typedef struct kiotty_event {
    kiotty_event_kind_t kind;
    kiotty_span_t       data;      /* READ: 수신 데이터 (return 하면 무효). WRITTEN: length = 보낸 길이 */
    void*               payload;   /* RESUMED 전용 */
    kiotty_code_t       code;      /* CLOSED 전용 */
} kiotty_event_t;

typedef enum kiotty_request_op { KIOTTY_REQUEST_NONE, KIOTTY_REQUEST_READ, KIOTTY_REQUEST_READ_UNTIL, KIOTTY_REQUEST_WRITE, KIOTTY_REQUEST_SUSPEND, KIOTTY_REQUEST_CLOSE } kiotty_request_op_t;

typedef struct kiotty_request {
    kiotty_request_op_t op;
    size_t              n;         /* READ: exact, READ_UNTIL: max */
    kiotty_span_t       data;      /* WRITE: 보낼 데이터, READ_UNTIL: delimiter (등록 시 복사됨) */
    uint32_t            timeout_ms;/* 0 == 없음 */
} kiotty_request_t;

typedef struct kiotty_transition {
    int32_t          next;         /* keep == 0 일 때 다음 상태 */
    int32_t          keep;         /* 1 이면 상태 유지 (stay) */
    kiotty_request_t request;
} kiotty_transition_t;

typedef kiotty_transition_t (*kiotty_handler_t)(void* session, kiotty_connection_t* c, const kiotty_event_t* event);

#define KIOTTY_ANY_STATE (-1)
#define KIOTTY_MAX_NEXT  4

typedef struct kiotty_rule {
    int32_t          state;                    /* 사용자 enum 값 또는 KIOTTY_ANY_STATE */
    int32_t          event;                    /* kiotty_event_kind_t */
    kiotty_handler_t handler;
    int32_t          next[KIOTTY_MAX_NEXT];    /* 갈 수 있는 다음 상태. debug 에서 검사 */
    int32_t          next_count;               /* 0 이면 검사 안 함 */
} kiotty_rule_t;

typedef struct kiotty_state_machine {
    const kiotty_rule_t* rules;
    size_t               count;
    int32_t              initial;
    size_t               session_size;         /* Runner 가 Connection 마다 잡아 두는 사용자 session 크기 */
} kiotty_state_machine_t;

/* 행 작성 */
#define KIOTTY_RULE(state, event, handler, ...)  { (state), (event), (handler), { __VA_ARGS__ }, (int32_t)(sizeof((int32_t[]){ __VA_ARGS__ }) / sizeof(int32_t)) }
#define KIOTTY_RULE_UNCHECKED(state, event, handler) { (state), (event), (handler), { 0 }, 0 }
#define KIOTTY_RULE_COUNT(table) (sizeof(table) / sizeof((table)[0]))

/* Transition / Request 생성 (C 에는 메서드가 없으므로 함수) */
kiotty_transition_t kiotty_go(int32_t next, kiotty_request_t request);      /* 상태 전이 + 요청 */
kiotty_transition_t kiotty_stay(kiotty_request_t request);                  /* 상태 유지 + 요청 */
kiotty_request_t    kiotty_none(void);                                      /* 요청 없음 */
kiotty_request_t    kiotty_read(size_t exact);
kiotty_request_t    kiotty_read_until(kiotty_span_t delim, size_t max);
kiotty_request_t    kiotty_write(kiotty_span_t data);
kiotty_request_t    kiotty_suspend(void);
kiotty_request_t    kiotty_close(void);
kiotty_request_t    kiotty_timeout(kiotty_request_t request, uint32_t ms);  /* 체이닝 대신 */

/* Runner. pool 은 max_clients * (16 + session_size, 16 정렬) 바이트. 사용자가 한 번 잡아 둔다 */
typedef struct kiotty_fsm_runner kiotty_fsm_runner_t;
#define KIOTTY_FSM_POOL_SIZE(session_size, max_clients) ((max_clients) * (16 + (((session_size) + 15) & ~(size_t)15)))
kiotty_code_t               kiotty_fsm_runner_init(kiotty_fsm_runner_t* runner, const kiotty_state_machine_t* machine, void* pool, size_t max_clients);   /* DUPLICATE_RULE 검사 */
kiotty_connection_handler_t kiotty_fsm_handler(kiotty_fsm_runner_t* runner);
```

```c
/* 사용 예 */
static const kiotty_rule_t HTTP_RULES[] = {
    KIOTTY_RULE(KIOTTY_ANY_STATE,    KIOTTY_EVENT_CONNECTED, on_connected, HTTP_READING_HEADER),
    KIOTTY_RULE(HTTP_READING_HEADER, KIOTTY_EVENT_READ,      on_header,    HTTP_READING_BODY, HTTP_PROCESSING, HTTP_RESPONDING),
    KIOTTY_RULE(HTTP_PROCESSING,     KIOTTY_EVENT_RESUMED,   on_resumed,   HTTP_RESPONDING),
    KIOTTY_RULE(HTTP_RESPONDING,     KIOTTY_EVENT_WRITTEN,   on_responded, HTTP_READING_HEADER, HTTP_IDLE),
    KIOTTY_RULE_UNCHECKED(KIOTTY_ANY_STATE, KIOTTY_EVENT_CLOSED, on_closed),
};
static const kiotty_state_machine_t HTTP_MACHINE = { HTTP_RULES, KIOTTY_RULE_COUNT(HTTP_RULES), HTTP_IDLE, sizeof(http_session_t) };

static char         pool[KIOTTY_FSM_POOL_SIZE(sizeof(http_session_t), 32)];
kiotty_fsm_runner_t runner;
kiotty_fsm_runner_init(&runner, &HTTP_MACHINE, pool, 32);
kiotty_server_t*    server = kiotty_server_create(&config, kiotty_fsm_handler(&runner));

/* worker thread */
kiotty_server_resume(server, session->id, &session->response);
```

### 9. HttpServer

HTTP/1.1 서버. 7절 Server 위에 라이브러리가 구현한 HTTP StateMachine (구현 가이드 3.1) 을 얹은 것이다. 사용자는 라우트를 등록하고 응답을 채운다.

- 라우트는 `route(method, path, HttpRoute)` 로 등록하며 `get / post / put / del / patch / options` 는 그 별칭이다. 콜백을 등록하는 방식이지 하나의 handler 구조체를 넘기는 방식이 아니다. 등록은 `open()` 전에만 되고 표는 고정 크기다 (`HttpLimits::max_routes`).
- path 매칭은 정확 일치와 `/*` 접미 wildcard 두 가지다. 등록 순서대로 검사한다. `:id` 같은 파라미터 파싱은 하지 않으며 `request.path` 를 사용자가 파싱한다.
- 정적 서빙은 세 가지다. 디렉토리 매핑, 파일 하나 매핑, 메모리 바이너리 매핑. 라우트와 같은 표에서 등록 순서대로 매칭하며 GET / HEAD 에만 적용된다.
- 아무것도 매칭되지 않으면 `notFound` 라우트, 그것도 없으면 404.
- 요청과 응답은 Connection 마다 고정 크기 버퍼(`HttpLimits`)에 있다. body 가 `max_body_bytes` 를 넘으면 413 으로 라이브러리가 거절한다. 응답 버퍼가 넘치면 setter 가 `BUFFER_FULL` 을 돌려준다.
- 긴 작업은 `HttpResponse::defer()` 로 Connection 을 suspend 하고, worker 가 결과를 만든 뒤 `HttpServer::resume(request.id, payload)` 를 부르면 IO Thread 에서 같은 라우트의 `resumed` 콜백이 호출된다. 응답 객체는 IO Thread 에서만 만지므로 worker 는 결과를 자기 메모리에 만들어 payload 로 넘긴다. `defer()` 한 뒤 `resumed` 까지 request 는 유효하다.
- keep-alive 는 라이브러리가 처리한다. `Connection: close` 또는 HTTP/1.0 이면 응답 후 닫는다.

```cpp
enum class HttpMethod { Get, Head, Post, Put, Delete, Patch, Options };

struct HttpHeader { Span name; Span value; };

struct HttpRequest {
    ConnectionId      id;              // resume 에 쓴다
    HttpMethod        method;
    Span              path;            // query 제외. percent-decoding 하지 않음
    Span              query;           // '?' 뒤. 없으면 길이 0
    const HttpHeader* headers;
    size_t            header_count;    // 최대 HttpLimits::max_headers
    Span              body;            // Content-Length 만큼. 없으면 길이 0
    bool              keep_alive;
    Span              header(const char* name) const;   // 대소문자 무시. 없으면 { nullptr, 0 }
    ConnectionInfo&   peer() const;
};

struct HttpResponse {                                                              // IO Thread 전용
    NetCode status(int code);                                                      // 기본 200. 호출하지 않으면 404
    NetCode header(const char* name, const char* value);                           // Content-Length, Connection, Date 는 라이브러리가 채운다
    NetCode body(Span data, const char* content_type);                             // 응답 버퍼로 복사
    NetCode bodyRef(Span data, const char* content_type);                          // 복사하지 않음. data 는 송신 완료까지 유효해야 한다 (정적 데이터, 메모리 바이너리)
    NetCode file(const char* path, const char* content_type);                      // 파일을 chunk 로 스트리밍. 가능하면 sendfile / TransmitFile. 없으면 NOT_FOUND
    void    defer();                                                               // 긴 작업. handle 이 return 하면 Connection 은 suspend. HttpServer::resume 으로 돌아온다
};

struct HttpRoute {                                                                 // C 의 kiotty_http_route_t 와 같은 모양
    void* user_context;
    void (*handle) (void* user_context, const HttpRequest& request, HttpResponse& response);
    void (*resumed)(void* user_context, const HttpRequest& request, HttpResponse& response, void* payload);   // defer 한 뒤 resume 이 오면. defer 하지 않는 라우트는 nullptr
};

struct HttpLimits {
    size_t   max_routes;            // 라우트 + 정적 경로 + 승격 경로 합계. 기본 64
    size_t   max_header_bytes;      // request line + headers. 기본 8 KiB
    size_t   max_headers;           // 기본 32
    size_t   max_body_bytes;        // 기본 64 KiB. 초과시 413
    size_t   response_bytes;        // 응답 버퍼 (status + headers + 복사된 body). 기본 64 KiB
    uint32_t header_timeout_ms;     // 헤더 수신 완료까지. 기본 10000
    uint32_t keep_alive_timeout_ms; // 다음 요청 대기. 기본 15000
};

class HttpServer {
public:
    HttpServer(const ServerConfig& config, const HttpLimits& limits);

    // 라우트 등록. 세 가지 오버로드가 있다.
    //   HttpRoute               : user_context + 함수 포인터. C 와 같은 모양
    //   함수 포인터             : capture 없는 람다가 여기로 온다.   server.get("/health", [](const HttpRequest&, HttpResponse& r) { r.status(200); });
    //   F&                      : capture 있는 람다나 functor 의 lvalue. 포인터만 보관하므로 서버보다 오래 살아야 한다. 할당 없음
    NetCode route(HttpMethod method, const char* path, HttpRoute route);
    NetCode route(HttpMethod method, const char* path, void (*fn)(const HttpRequest&, HttpResponse&));
    template <typename F> NetCode route(HttpMethod method, const char* path, F& fn);
    // get / post / put / del / patch / options : route(Method, ...) 의 별칭. 같은 세 오버로드
    NetCode get (const char* path, HttpRoute route);
    NetCode post(const char* path, HttpRoute route);
    NetCode notFound(HttpRoute route);                                                           // 아무 라우트도 안 맞을 때

    // 정적 서빙 등록
    NetCode serveDirectory(const char* url_prefix, const char* directory);                       // "/static/" -> "./www/". ".." 포함 경로는 403. content type 은 확장자로
    NetCode serveFile(const char* url_path, const char* file_path);                              // "/favicon.ico" -> "./www/favicon.ico"
    NetCode serveMemory(const char* url_path, Span data, const char* content_type);              // 메모리 바이너리. 복사하지 않으며 서버 생명 동안 유효해야 한다

    // WebSocket 승격 경로 등록 (10절)
    NetCode upgrade(const char* url_path, WebSocketHandler handler, const WebSocketLimits& limits);

    NetCode open();
    void    close();
    NetCode run();
    void    stop();

    // 아무 thread 에서나. 7절 Server::resume 과 같은 규칙.
    //   defer 된 HTTP 요청 : 그 라우트의 resumed(ctx, request, response, payload)
    //   WebSocket 연결     : WebSocketHandler::onResumed(ctx, ws, payload)
    NetCode resume(ConnectionId id, void* payload);
};
```

사용 예

```cpp
HttpServer server(config, limits);

server.get("/health", [](const HttpRequest&, HttpResponse& r) { r.status(200); });

auto report = [&](const HttpRequest& q, HttpResponse& r) {   // capture 있는 람다. lvalue 로 등록
    r.defer();
    workerPool.post(new_job(q.id));                            // worker: 결과를 만들고 server.resume(id, result)
};
server.get("/report", report);
HttpRoute reportRoute = { &app, nullptr, [](void*, const HttpRequest&, HttpResponse& r, void* result) {
    r.body(static_cast<Result*>(result)->span(), "application/json");
}};
server.route(HttpMethod::Get, "/report", reportRoute);        // resumed 콜백은 HttpRoute 형태로

server.serveDirectory("/", "./www/");
server.serveMemory("/app.wasm", wasm, "application/wasm");
server.upgrade("/ws", wsHandler, wsLimits);
server.open();
server.run();
```

```c
typedef enum kiotty_http_method { KIOTTY_HTTP_GET, KIOTTY_HTTP_HEAD, KIOTTY_HTTP_POST, KIOTTY_HTTP_PUT, KIOTTY_HTTP_DELETE, KIOTTY_HTTP_PATCH, KIOTTY_HTTP_OPTIONS } kiotty_http_method_t;
typedef struct kiotty_http_header { kiotty_span_t name; kiotty_span_t value; } kiotty_http_header_t;

typedef struct kiotty_http_request {
    kiotty_connection_id_t      id;
    kiotty_http_method_t        method;
    kiotty_span_t               path;
    kiotty_span_t               query;
    const kiotty_http_header_t* headers;
    size_t                      header_count;
    kiotty_span_t               body;
    int                         keep_alive;
    kiotty_connection_t*        connection;        /* peer 정보 조회용 */
} kiotty_http_request_t;
kiotty_span_t kiotty_http_request_header(const kiotty_http_request_t* request, const char* name);

typedef struct kiotty_http_response kiotty_http_response_t;                      /* 불투명. IO Thread 전용 */
kiotty_code_t kiotty_http_response_status(kiotty_http_response_t* r, int code);
kiotty_code_t kiotty_http_response_header(kiotty_http_response_t* r, const char* name, const char* value);
kiotty_code_t kiotty_http_response_body(kiotty_http_response_t* r, kiotty_span_t data, const char* content_type);
kiotty_code_t kiotty_http_response_body_ref(kiotty_http_response_t* r, kiotty_span_t data, const char* content_type);
kiotty_code_t kiotty_http_response_file(kiotty_http_response_t* r, const char* path, const char* content_type);
void          kiotty_http_response_defer(kiotty_http_response_t* r);

typedef struct kiotty_http_route {
    void* user_context;
    void (*handle) (void* user_context, const kiotty_http_request_t* request, kiotty_http_response_t* response);
    void (*resumed)(void* user_context, const kiotty_http_request_t* request, kiotty_http_response_t* response, void* payload);   /* NULL 가능 */
} kiotty_http_route_t;

typedef struct kiotty_http_limits { size_t max_routes; size_t max_header_bytes; size_t max_headers; size_t max_body_bytes; size_t response_bytes; uint32_t header_timeout_ms; uint32_t keep_alive_timeout_ms; } kiotty_http_limits_t;

typedef struct kiotty_http_server kiotty_http_server_t;
kiotty_http_server_t* kiotty_http_server_create(const kiotty_server_config_t* config, const kiotty_http_limits_t* limits);
void                  kiotty_http_server_destroy(kiotty_http_server_t* s);
kiotty_code_t         kiotty_http_server_route(kiotty_http_server_t* s, kiotty_http_method_t method, const char* path, kiotty_http_route_t route);
kiotty_code_t         kiotty_http_server_not_found(kiotty_http_server_t* s, kiotty_http_route_t route);
kiotty_code_t         kiotty_http_server_serve_directory(kiotty_http_server_t* s, const char* url_prefix, const char* directory);
kiotty_code_t         kiotty_http_server_serve_file(kiotty_http_server_t* s, const char* url_path, const char* file_path);
kiotty_code_t         kiotty_http_server_serve_memory(kiotty_http_server_t* s, const char* url_path, kiotty_span_t data, const char* content_type);
kiotty_code_t         kiotty_http_server_upgrade(kiotty_http_server_t* s, const char* url_path, kiotty_websocket_handler_t handler, const kiotty_websocket_limits_t* limits);
kiotty_code_t         kiotty_http_server_open(kiotty_http_server_t* s);
void                  kiotty_http_server_close(kiotty_http_server_t* s);
kiotty_code_t         kiotty_http_server_run(kiotty_http_server_t* s);
void                  kiotty_http_server_stop(kiotty_http_server_t* s);
kiotty_code_t         kiotty_http_server_resume(kiotty_http_server_t* s, kiotty_connection_id_t id, void* payload);
```

### 10. WebSocket / WebSocketHandler

WebSocket (RFC 6455) 은 HTTP Upgrade 로만 시작되므로 서버 쪽 진입점은 `HttpServer::upgrade(url_path, handler, limits)` 다. HttpServer 하나가 HTTP 라우트와 WebSocketHandler 를 동시에 가진다.
101 응답이 송신 완료되면 그 Connection 은 HTTP StateMachine 에서 WebSocket StateMachine (구현 가이드 3.2) 으로 넘어가고, 이후 HTTP 콜백은 오지 않는다.

- 수신 frame 의 조각(fragment)은 라이브러리가 합쳐서 `onMessage` 에 메시지 하나로 준다. `max_message_bytes` 를 넘으면 1009 로 close 한다.
- ping / pong 은 라이브러리가 처리한다. 들어온 ping 에는 pong 을 보내고, `ping_interval_ms` 마다 ping 을 보내며 `pong_timeout_ms` 안에 pong 이 없으면 1001 로 close 한다.
- `send` 는 frame 으로 감싸 송신 Stage 로 복사한다. 여유가 없으면 `BUFFER_FULL` 이며 아무것도 보내지 않는다. 송신 frame 은 masking 하지 않는다 (서버).
- `close` 는 close handshake 를 시작한다. 상대의 close frame 이 오거나 `close_timeout_ms` 가 지나면 `onClose`.
- **다른 thread 의 push** 는 `HttpServer::resume(ws.id(), payload)` 다. IO Thread 에서 `onResumed(ctx, ws, payload)` 가 호출되고 거기서 `ws.send()` 한다. 다른 thread 에서 `WebSocket` 을 직접 만지지 않는다.

```cpp
enum class WebSocketOpcode { Text = 1, Binary = 2 };

struct WebSocketMessage {
    WebSocketOpcode opcode;
    Span            data;      // 수신 버퍼. onMessage 가 return 하면 무효
};

struct WebSocket {                                               // Connection 위의 per-connection 객체. IO Thread 에서만 사용
    ConnectionInfo& info();
    ConnectionId    id() const;                                  // HttpServer::resume 에 쓴다
    void*  context() const;                                      // per-connection slot. 라이브러리는 건드리지 않는다
    void   setContext(void* context);
    NetCode send(WebSocketOpcode opcode, Span data);
    NetCode close(uint16_t code, const char* reason);            // reason 은 123 바이트 이하
};

struct WebSocketAccept {                                         // onUpgrade 에서 응답 헤더를 고른다
    NetCode protocol(const char* subprotocol);                   // Sec-WebSocket-Protocol. 요청에 없던 값이면 INVALID_ARGUMENT
};

struct WebSocketHandler {
    void* user_context;
    bool (*onUpgrade)(void* user_context, const HttpRequest& request, WebSocketAccept& accept);   // false 면 403 으로 거절하고 HTTP 로 남는다. nullptr 이면 모두 허용
    void (*onOpen)   (void* user_context, WebSocket& ws);                                         // 101 송신 완료
    void (*onMessage)(void* user_context, WebSocket& ws, const WebSocketMessage& message);
    void (*onResumed)(void* user_context, WebSocket& ws, void* payload);                          // HttpServer::resume(ws.id(), payload). push 는 여기서 ws.send()
    void (*onClose)  (void* user_context, WebSocket& ws, uint16_t code, NetCode reason);          // code: close frame 의 status. 없으면 1006. 마지막 콜백
};

struct WebSocketLimits {
    size_t   max_message_bytes;     // 조각을 합친 메시지 최대 크기. 기본 64 KiB. 초과시 1009
    uint32_t ping_interval_ms;      // 0 이면 ping 을 보내지 않음. 기본 30000
    uint32_t pong_timeout_ms;       // 기본 10000
    uint32_t close_timeout_ms;      // close handshake 대기. 기본 5000
};
```

사용 예

```cpp
// IO Thread
void onOpen(void* ctx, WebSocket& ws)                     { app.clients.add(ws.id()); }
void onResumed(void* ctx, WebSocket& ws, void* payload)   { ws.send(WebSocketOpcode::Text, static_cast<Message*>(payload)->span()); }
void onClose(void* ctx, WebSocket& ws, uint16_t, NetCode) { app.clients.remove(ws.id()); }

// 앱 thread : 센서 값이 바뀌면 모든 클라이언트에게 push
for (ConnectionId id : app.clients.snapshot()) server.resume(id, &message);   // 이미 닫힌 id 는 PEER_CLOSED 를 돌려준다
```

```c
typedef enum kiotty_websocket_opcode { KIOTTY_WS_TEXT = 1, KIOTTY_WS_BINARY = 2 } kiotty_websocket_opcode_t;
typedef struct kiotty_websocket_message { kiotty_websocket_opcode_t opcode; kiotty_span_t data; } kiotty_websocket_message_t;

typedef struct kiotty_websocket kiotty_websocket_t;                                /* 불투명. IO Thread 전용 */
const kiotty_connection_info_t* kiotty_websocket_info(kiotty_websocket_t* ws);
kiotty_connection_id_t kiotty_websocket_id(kiotty_websocket_t* ws);
void*         kiotty_websocket_context(kiotty_websocket_t* ws);
void          kiotty_websocket_set_context(kiotty_websocket_t* ws, void* context);
kiotty_code_t kiotty_websocket_send(kiotty_websocket_t* ws, kiotty_websocket_opcode_t opcode, kiotty_span_t data);
kiotty_code_t kiotty_websocket_close(kiotty_websocket_t* ws, uint16_t code, const char* reason);

typedef struct kiotty_websocket_accept kiotty_websocket_accept_t;                  /* 불투명 */
kiotty_code_t kiotty_websocket_accept_protocol(kiotty_websocket_accept_t* accept, const char* subprotocol);

typedef struct kiotty_websocket_handler {
    void* user_context;
    int  (*on_upgrade)(void* user_context, const kiotty_http_request_t* request, kiotty_websocket_accept_t* accept);   /* 0 이면 거절. NULL 이면 모두 허용 */
    void (*on_open)   (void* user_context, kiotty_websocket_t* ws);
    void (*on_message)(void* user_context, kiotty_websocket_t* ws, const kiotty_websocket_message_t* message);
    void (*on_resumed)(void* user_context, kiotty_websocket_t* ws, void* payload);
    void (*on_close)  (void* user_context, kiotty_websocket_t* ws, uint16_t code, kiotty_code_t reason);
} kiotty_websocket_handler_t;

typedef struct kiotty_websocket_limits { size_t max_message_bytes; uint32_t ping_interval_ms; uint32_t pong_timeout_ms; uint32_t close_timeout_ms; } kiotty_websocket_limits_t;
```

미결

- WebSocket 클라이언트. EventClient 위에 같은 `WebSocketHandler` 를 올리는 `WebSocketClient(config, url_path, handler, limits)` 로 두되 인터페이스는 다음 버전에서.
- 조각을 합치지 않고 그대로 전달하는 streaming 모드 (`max_message_bytes` 보다 큰 메시지).
- permessage-deflate 확장은 지원하지 않는다.

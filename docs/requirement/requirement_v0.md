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



## 외부 인터페이스

### 1. Error 처리

```cpp
enum NetCode {
    SUCCESS
};
```

### 2. 예외 처리

무조건, Result<V, ErrorCode>로.
외부 인터페이스는 아래 방식으로 처리

```cpp
struct NetResult {
    int32_t bytes;
    NetCode code;
}
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

### 4. Stage

```cpp
class Stage
{
public:
    Stage(size_t capacity);
    ~Stage();

    class Provider {
        // 현재 쓸 수 있는 길이를 준다.
        size_t writable() const;
        // 외부에서 요청한 길이만큼 쓸 수 있으면 버퍼를 준다.
        void* prepare(size_t requested_length);
        // 외부에서 실제 쓴 길이만큼 알려준다.
        void commit(size_t written_length);
    }


    class Consumer {
        // 현재  읽을 수 있는 길이만큼 준다.
        size_t readalbe() const;
        // 외부에서 요청한 길이만큼 읽을 수 있으면 버퍼를 준다.
        void* prepare(size_t requested_length);
        // 외부에서 실제 읽은 길이만큼 알려준다.
        void commit(size_t consumed_length);
    };    

    Provider& provider();
    Consumer& consumer();
    
    // _begin_pos를 0으로 남아있는 데이터의 길이만큼을 _end_pos로 돌리고, 데이터를 memmove.
    void compact();

private:
    char* _buffer;
    size_t _capacity;
    size_t _begin_pos; // 데어터가 시작하는 index
    size_t _end_pos; // 데이터가 끝나는 index
};

/*
IOThread
    // Read의 완료 루틴 (IOCP 또는 io_uring)
    ... (int32_t read_length // 실제 읽은 길이)
    auto& provider = stage.provider();
    auto& consumer = stage.consumer();
    
    provider.commit(read_length);

    int64_t length {0};
    while ((length = connection->latest_read_request_length) > 0) {
        auto* buffer = consumer.prepare(length);
        if (buffer) {
            handler.onRead(user_context, connection, buffer, Ok(length));
            consumer.commit(length);
        }
    }
    
    stage.compact();
    socket.read(); // provider.prepare(), provider.remained()를 인자로 넘기기    
*/

```

### 5. Connection

- 비동기 Socket IO 모델에서 연결을 추상화하는 객체
- EventClient, Server에서 사용됨
  - EventClient : 비동기 read, 동기 write
  - Server : 비동기 read, 비동기 write
- read, write
  - Pipe, TLS, TCP에 따라서 달라짐

```cpp

struct ConnectionInfo
{
    char hostname[128]; // ip 또는 pipe
    int32_t port;
}

struct ConnectionBlock {
    ConnectionInfo& info();
    // Connection이 다시 IO Thread로부터 event를 받도록 붙이기.
    // 내부적으로는 eventfd나 PostIO 뭐시기를 통해서 IOThread가 ConnectionHandler의 onAttached를 호출시키도록 함
    void resume(void* user_context);
};

struct Connection {
    ConnectionInfo& info();
    
    // 읽기 요청을 하고, onRead에서 읽은 값을 받는다. 
    // onRead는 Connection을 관리하는 주체가 내부적으로 가지고 있는 Stage에 값을 담아서 준다.
    void read(void* user_context, size_t size);

    // 쓰기 요청
    // Connection에서 송신 Stage로 데이터를 복사하고 제출함
    void write(void* user_context, const void* data, size_t size);

    // Long term 작업을 하기 위해 IO Thread에서 polling을 하지 않음.
    // peer가 끊거나 더 이상 polling을 하지 않(stop)거나 내가 disconnect해도 다시 attach할 때까지 connection은 유지됨.
    // detach와 동시에 Connection이 내부적으로 보유하고 있는 Stage의 lifecycle도 connection에 옮겨가면 좋을듯.
    ConnectionBlock suspend();

    /* 라이브러리 내부에서 HTTP, WebSocket을 위한 확장 함수 */
    // 요구한 길이만큼 버퍼 여유가 되면 pointer를 돌려줌
    // 요구한 길이만큼 버퍼가 없다면 nullptr
    // 어떻게 이 함수를 라이브러리 사용자한테는 공개 안하고 내부적으로만 쓸지는 고민
    void* beginWrite(size_t requested_length);
    void endWrite(void* user_context, size_t written_length);

};

struct ConnectionHandler {
    void* user_context;
    void (*onConnected)(void* user_context, Connection* connection);
    void (*onDisconnected)(void* user_context, Connection* connection, NetResult result);
    // 상대방에서 종료 / 내가 종료 (shutdown) / Routine 멈춤 / 에러 
    void (*onRead)(void* user_context, Connection* connection, const void* buf, NetResult result); // read에서 주입한 user_context
    void (*onWritten)(void* user_context, Connection* connection, NetResult result); // write에서 주입한 user_context

    // Connection 이벤트를 받아오지 않다가(외부 thread에서 긴 작업을 수행하기 위해서) 다시 받아오게 붙었을 때
    void (*onResumed)(void* user_context, Connection* connection);
}
```


### 6. Client

1. Client는 Configuration과 상관 없이 동일한 기능을 제공해야 한다.

2. StreamClient와 EventClient를 구분한다.
    2.1 StreamClient의 목적은 User-space의 데이터를 direct하게 바로 network로 보내주기 위함이다.
    2.2 EventClient의 목적은 서버에서 먼저 날아오는 이벤트를 효과적으로 handle하면서도 User-space의 데이터를 direct하게 network로 보내는 것이 목적이다.

- StreamClient

    1. connect
    2. disconnect
    3. read
    4. write
    
-  EventClient

    1. 생성자 (송신 버퍼 크기, 수신 버퍼 크기, Endpoint 주입) 
    2. connect
    3. disconnect
    4. run // 해당 thread를 점유하여 event를 계속 폴링함.
    5. stop // 연결은 끊지 않고, polling을 중간에 취소함.

> 고민되는 부분. 생각보다 network 환경이 열악할 때도 있기 때문에 timeout만 걸어두는 게 아니라,
> 재시도 횟수와 시도와 시도 사이의 time interval도 걸어두는 게 좋다.

### 7. Server

1. Server는 Configuration과 상관 없이 동일한 기능으로 돌아간다.
2. Server 인터페이스는 아래와 같다.

- Server

    1. 생성자 (송신 버퍼 크기, 수신 버퍼 크기, Endpoint 주입)
    2. open(); // listen까지는 해줌
    3. close(); // 
    4. run(); // 해당 thread를 점유하여 event를 계속 폴링함
    5. stop(); // listen은 유지한 상태로, polling을 중간에 취소함.

### StateMachine

State Machine이 없으면 이렇게 표현될 것이다.

```cpp
onResumed(Connection* connection, void* user_context)
{
    auto& next_state = user_context->state;
    when(next_state) 
    {
        State::V1 -> connection->read(user_context->buffer, length);
        State::V2 -> connection->write(user_context->buffer, length);
        State::V3 -> connection->read(user_context->buffer, length);
    }
}

onReadComplete, onWriteComplete(Connection* connection, void* user_context, const char* buffer, size_t length)
{
    // 1. 현재 상태
    auto& current_state = user_context->state;
    auto& next_state = when(current_state)
    {
        State::V1 -> doSomething(buffer, length);
        State::V2 -> doSomething(buffer, length);
        State::V3 -> doSomething(buffer, length);
        State::V4 -> {
            auto block = connection->suspend(); 
            doLongtermSomething(block, user_context, buffer, length);
            // 여기서 작업이 끝나면 block.resume(user_context);
            를 통해 작업이 끝났음을 알린다.
        }
    }
    when(next_state) 
    {
        State::V1 -> connection->read(user_context->buffer, length);
        State::V2 -> connection->write(user_context->buffer, length);
        State::V3 -> connection->read(user_context->buffer, length);
    }
}
```

(Old State, Event) { Action } -> (NewState, Next Event Request)

Old State, Event Result((WRITE, READ, RESUMED), data, length)
{ ACTION }
New State, Event Request((WRITE, READ, RESUMED), data, length)

#### 제안: StateMachine Interface

위 3줄을 그대로 코드로 옮긴다.

- `Event`  = "Event Result"  : IO Thread가 StateMachine에게 알려주는 **무슨 일이 일어났는가**
- `Request` = "Event Request" : StateMachine이 IO Thread에게 돌려주는 **다음에 무엇을 해 달라**
- `Transition` = (New State, Request)
- `Handler` = `(Session&, Connection&, const Event&) -> Transition` 인 **순수 함수 하나**
- `Rule` = (Old State, Event Kind) -> Handler 를 묶은 한 줄. `Rule` 배열이 곧 상태 전이표다.

프로토콜 작성자는 **(1) State enum, (2) Session struct, (3) Handler 함수들, (4) Rule 테이블** 4가지만 쓴다.
onRead / onWritten / onResumed / onConnected / onDisconnected 를 직접 구현하지 않으며, 그 안에서 state를 switch 하지도 않는다.
라이브러리가 ConnectionHandler 5개 콜백을 전부 `Event`로 바꿔 Rule 테이블로 dispatch 한다.

```cpp
// ------------------------------------------------------------------
// 1. Event : IO Thread -> StateMachine. "Event Result"
// ------------------------------------------------------------------
struct Event {
    enum Kind {
        CONNECTED,  // 연결 완료 (Server: accept 완료, EventClient: connect 완료)
        READ,       // Request::read* 가 요구한 조건이 충족됨. data/length 유효
        WRITTEN,    // Request::write 가 송신 완료됨. length = 보낸 바이트 수
        RESUMED,    // ConnectionBlock::resume(payload) 가 IO Thread에 도착함. payload 유효
        TIMEOUT,    // Request 에 걸어둔 timeout 만료
        CLOSED      // 상대방 종료 / 내 close / stop / 에러. code 에 원인. 이 다음 Event 는 없다
    };

    Kind        kind;
    const void* data;     // READ 전용. 수신 Stage 내부 버퍼. Handler 가 return 하면 무효가 된다
    size_t      length;   // READ: data 길이, WRITTEN: 송신 길이
    void*       payload;  // RESUMED 전용. resume(payload) 로 넘긴 포인터
    NetCode     code;     // CLOSED 전용. 그 외에는 SUCCESS
};

// ------------------------------------------------------------------
// 2. Request : StateMachine -> IO Thread. "Event Request"
//    한 Transition 은 read 또는 write 중 하나를 "주 요청" 으로 가진다.
// ------------------------------------------------------------------
struct Request {
    // --- read 계열 : 조건이 충족되면 READ 이벤트 한 번 ---
    static Request read(size_t exact);                          // 정확히 exact 바이트
    static Request readSome(size_t max);                        // 1 ~ max 바이트, 들어오는 즉시
    static Request readUntil(const char* delim, size_t max);    // delimiter 까지(포함). max 초과시 CLOSED(PROTOCOL_ERROR)

    // --- write : 송신 Stage 로 복사. 완료되면 WRITTEN 이벤트 한 번 ---
    static Request write(const void* data, size_t length);

    // --- 제어 ---
    static Request suspend();   // 이 Connection 의 polling 중단. ConnectionBlock::resume() 이 RESUMED 로 들어온다
    static Request close();     // 정상 종료. 마지막으로 CLOSED(SUCCESS) 가 한 번 온다
    static Request none();      // 새 요청 없음. 이미 걸려 있는 요청을 유지 (full-duplex 상황에서 사용)

    // --- modifier (chaining) ---
    Request& timeout(uint32_t ms);                               // 이 요청에 timeout. 만료시 TIMEOUT 이벤트
    Request& andWrite(const void* data, size_t length);         // read 요청과 동시에 write 하나를 같이 던진다 (예: pong 보내고 다음 frame 읽기)
};

// ------------------------------------------------------------------
// 3. Transition : (New State, Request)
// ------------------------------------------------------------------
template <typename State>
struct Transition {
    State   next;
    Request request;
};

// ------------------------------------------------------------------
// 4. StateMachine : Rule 테이블 + 초기 상태.  동적 할당 없음, 전부 static const.
// ------------------------------------------------------------------
template <typename State, typename Session>
struct StateMachine {
    typedef Transition<State> (*Handler)(Session& session, Connection& connection, const Event& event);

    enum { ANY_STATE = -1 };
    static const Event::Kind ANY_EVENT = (Event::Kind)-1;

    struct Rule {
        int         state;    // State 값 또는 ANY_STATE
        Event::Kind event;    // Event::Kind 또는 ANY_EVENT
        Handler     handler;
    };

    const Rule* rules;
    size_t      rule_count;
    State       initial;
};

// Session 은 사용자가 정의하는 per-connection struct. 라이브러리의 Pool 에 고정 크기로 미리 할당된다.
// 라이브러리는 Session 안에 state 를 보관할 자리만 요구한다 (예: State state; 멤버 또는 CRTP base).
```

라이브러리 내부 dispatch 는 다음 한 가지 루틴으로 끝난다. 5개 ConnectionHandler 콜백은 전부 이 루틴을 호출한다.

```cpp
// IO Thread 내부. 모든 ConnectionHandler 콜백은 Event 로 변환되어 여기로 들어온다.
void dispatch(Session& session, Connection& connection, const Event& event)
{
    const Rule* rule = findRule(machine, session.state, event.kind);
    //   매칭 우선순위 : (state, event) 정확 일치 > (ANY_STATE, event) > (state, ANY_EVENT) > (ANY_STATE, ANY_EVENT)
    //   매칭 없음     : 프로토콜 정의 오류. CLOSED(UNHANDLED_EVENT) 로 종료시킨다. 조용히 무시하지 않는다.

    Transition<State> t = rule->handler(session, connection, event);

    session.state = t.next;
    apply(connection, t.request);   // read -> Stage 에 "요구 길이/delimiter" 등록, write -> 송신 Stage 복사 후 제출,
                                    // suspend -> polling 해제, close -> shutdown, none -> 아무것도 안 함
}
```

##### 예시 1. HTTP/1.1 Server (keep-alive, long-term 작업 포함)

```cpp
enum class HttpState { Idle, ReadingHeader, ReadingBody, Processing, Responding };

struct HttpSession {
    HttpState       state;
    HttpRequest     request;       // 파싱 결과 (고정 크기)
    char            body[64 * 1024];
    HttpResponse    response;      // 고정 크기 응답 버퍼
    ConnectionBlock block;         // suspend 중 다른 thread 가 resume 할 때 사용
};

typedef StateMachine<HttpState, HttpSession> Http;
typedef Transition<HttpState>               Next;

static Next readHeader() { return { HttpState::ReadingHeader, Request::readUntil("\r\n\r\n", 8 * 1024).timeout(10000) }; }

static Next onConnected(HttpSession&, Connection&, const Event&) { return readHeader(); }

static Next onHeader(HttpSession& s, Connection& c, const Event& e)
{
    if (!parseRequestLineAndHeaders(s.request, e.data, e.length))
        return { HttpState::Responding, Request::write(BAD_REQUEST, sizeof(BAD_REQUEST) - 1) };

    if (s.request.content_length > 0)
        return { HttpState::ReadingBody, Request::read(s.request.content_length) };

    return process(s, c);
}

static Next onBody(HttpSession& s, Connection& c, const Event& e)
{
    memcpy(s.body, e.data, e.length);        // e.data 는 Handler 가 return 하면 무효. 필요하면 Session 으로 복사
    return process(s, c);
}

static Next process(HttpSession& s, Connection& c)
{
    if (isLongJob(s.request)) {
        s.block = c.block();                 // resume 용 handle (아래 "Connection 변경점" 참고)
        workerPool.post(&s);                 // 다른 thread 에서 작업 후 s.block.resume(&s.response)
        return { HttpState::Processing, Request::suspend() };
    }
    buildResponse(s.response, s.request);
    return { HttpState::Responding, Request::write(s.response.data, s.response.length) };
}

static Next onResumed(HttpSession& s, Connection&, const Event& e)
{
    HttpResponse* r = static_cast<HttpResponse*>(e.payload);
    return { HttpState::Responding, Request::write(r->data, r->length) };
}

static Next onResponded(HttpSession& s, Connection&, const Event&)
{
    if (s.request.keep_alive) return readHeader();
    return { HttpState::Idle, Request::close() };
}

static Next onTimeout(HttpSession&, Connection&, const Event&) { return { HttpState::Idle, Request::close() }; }
static Next onClosed (HttpSession& s, Connection&, const Event&) { releaseResources(s); return { HttpState::Idle, Request::none() }; }

// 이 표가 곧 상태 전이 다이어그램이다. 이 표만 보면 프로토콜의 흐름이 보여야 한다.
static const Http::Rule HTTP_RULES[] = {
//    Old State                 Event              Handler
    { Http::ANY_STATE,          Event::CONNECTED,  onConnected },
    { (int)HttpState::ReadingHeader, Event::READ,  onHeader    },
    { (int)HttpState::ReadingBody,   Event::READ,  onBody      },
    { (int)HttpState::Processing,    Event::RESUMED, onResumed },
    { (int)HttpState::Responding,    Event::WRITTEN, onResponded },
    { Http::ANY_STATE,          Event::TIMEOUT,    onTimeout   },
    { Http::ANY_STATE,          Event::CLOSED,     onClosed    },
};

static const Http HTTP_MACHINE = { HTTP_RULES, sizeof(HTTP_RULES) / sizeof(HTTP_RULES[0]), HttpState::Idle };
```

##### 예시 2. WebSocket frame 수신 (full-duplex, 가변 길이 header)

```cpp
enum class WsState { Header, ExtLength, Mask, Payload };

static Next onHeader(WsSession& s, Connection&, const Event& e)
{
    s.frame.parseFixedHeader(e.data);                       // 2 bytes
    if (s.frame.len7 == 126) return { WsState::ExtLength, Request::read(2) };
    if (s.frame.len7 == 127) return { WsState::ExtLength, Request::read(8) };
    return { WsState::Mask, Request::read(4) };             // 서버 수신 frame 은 항상 masked
}
static Next onExtLength(WsSession& s, Connection&, const Event& e) { s.frame.parseExtLength(e.data, e.length); return { WsState::Mask, Request::read(4) }; }
static Next onMask(WsSession& s, Connection&, const Event& e)      { s.frame.parseMask(e.data);                return { WsState::Payload, Request::read(s.frame.payload_length) }; }

static Next onPayload(WsSession& s, Connection& c, const Event& e)
{
    s.frame.unmask(e.data, e.length);
    switch (s.frame.opcode) {
    case PING:  return { WsState::Header, Request::read(2).andWrite(s.pong(e.data, e.length), s.pongLength()) };
    case CLOSE: return { WsState::Header, Request::close() };
    default:    s.onMessage(e.data, e.length);  return { WsState::Header, Request::read(2) };
    }
}

// 서버 push : 다른 thread 가 ConnectionBlock::write() 로 보낸 것의 완료. 수신 state 와 무관하므로 그냥 무시.
static Next ignore(WsSession&, Connection&, const Event&) { return { /*state 유지*/ ..., Request::none() }; }

static const Ws::Rule WS_RULES[] = {
    { (int)WsState::Header,    Event::READ,    onHeader    },
    { (int)WsState::ExtLength, Event::READ,    onExtLength },
    { (int)WsState::Mask,      Event::READ,    onMask      },
    { (int)WsState::Payload,   Event::READ,    onPayload   },
    { Ws::ANY_STATE,           Event::WRITTEN, ignore      },
    { Ws::ANY_STATE,           Event::CLOSED,  onClosed    },
};
```

> `ignore` 가 state 를 유지하려면 Handler 가 현재 state 를 알아야 한다. `Transition::stay(Request)` 를 두고
> dispatch 에서 "next 를 바꾸지 않음" 으로 처리하는 것이 간단하다.

##### 설계 포인트

1. **Handler 는 순수 함수**: 입력 (Session, Event) 과 출력 (Transition) 만 있다. connection 을 인자로 받지만 `read/write/suspend` 를 직접 호출하지 않는 것이 규칙이다 (`info()`, `block()` 조회 용도). IO 를 직접 호출하면 "Request 를 return 했는데 이미 read 가 걸려 있음" 같은 상태 불일치가 생긴다. 라이브러리가 debug 빌드에서 Handler 안에서의 IO 호출을 assert 로 막는다.
2. **Rule 테이블 = 문서**: `static const Rule[]` 하나가 프로토콜의 상태 전이표다. 목표 8번(AI 또는 사람이 읽기 좋은 문서)과 직접 연결된다. 매칭되지 않는 (state, event) 는 조용히 무시하지 않고 `CLOSED(UNHANDLED_EVENT)` 로 끊어서 프로토콜 정의 누락을 바로 드러낸다.
3. **메모리**: Rule 테이블은 static const, Session 은 Pool 에 고정 크기, `Event::data` 는 수신 Stage 를 직접 가리킨다. Handler 가 return 하는 순간 `consumer.commit()` 되므로 보관이 필요하면 Session 으로 복사한다 (Stage 섹션의 IOThread 의사코드와 일치). 런타임 동적 할당은 0.
4. **`Request::read*` 와 Stage 의 관계**: `read(exact)` 는 `consumer.prepare(exact)` 가 성공할 때, `readSome(max)` 는 `readable() > 0` 일 때, `readUntil` 은 consumer 영역을 scan 해서 delimiter 를 찾았을 때 READ 를 발생시킨다. `exact` 또는 `max` 가 Stage capacity 보다 크면 등록 시점에 CLOSED(REQUEST_TOO_LARGE). 큰 body 는 `readSome` 으로 streaming 하는 것이 프로토콜 작성자의 책임이다.
5. **Full-duplex**: 수신 state machine 은 한 번에 read 요청 하나만 가진다. write 는 (a) Transition 의 주 요청, (b) `andWrite` 로 read 에 동반, (c) 다른 thread 에서 `ConnectionBlock::write()` 로 push, 세 경로가 있고 완료는 모두 WRITTEN 으로 들어온다. 수신 흐름과 무관한 WRITTEN 은 `{ANY_STATE, WRITTEN, ignore}` 한 줄로 흡수한다.
6. **프로토콜 교체 (HTTP Upgrade -> WebSocket)**: machine 이 타입이 아니라 값(`const StateMachine*`) 이므로 Session 이 가리키는 machine 을 바꾸면 된다. `Request::handoff(const StateMachine* next_machine, int initial_state)` 를 제어 요청으로 추가하면 `onResponded` 에서 101 응답 완료 후 WS machine 으로 넘어갈 수 있다. Session 은 두 프로토콜의 union 또는 공용 base 로 둔다.
7. **Timeout**: `Request::timeout(ms)` 는 IO loop 에 timer 가 있어야 한다 (IOCP: timer queue 또는 GetQueuedCompletionStatusEx 의 wait, epoll: timerfd, kqueue: EVFILT_TIMER). 목표 10번(최대 32 client) 수준이면 Connection 별 deadline 하나를 가진 단순 배열 scan 으로 충분하다. 클라이언트 쪽 "재시도 횟수 / 시도 간 interval" 고민은 Handler 가 `TIMEOUT` 을 받아 Session 의 retry counter 를 보고 같은 Request 를 다시 return 하는 것으로 표현된다.
8. **C interface**: 동일 구조를 C 로 내린다. `kiotty_event_t`, `kiotty_request_t`, `kiotty_transition_t`, `kiotty_rule_t { int32_t state; int32_t event; kiotty_handler_t handler; }`, `kiotty_state_machine_t { const kiotty_rule_t* rules; size_t count; int32_t initial; size_t session_size; }`. Handler 는 `kiotty_transition_t (*)(void* session, kiotty_connection_t*, const kiotty_event_t*)`. C++ template 은 이 C 구조체 위의 얇은 typed wrapper 다.

##### Connection 섹션 변경점

- `ConnectionBlock Connection::suspend()` 를 **`ConnectionBlock Connection::block()` + `Request::suspend()`** 로 분리한다.
  `block()` 은 언제든 호출 가능한 handle 조회이고 polling 중단은 Request 로만 표현된다. Handler 안에서 worker 에 post 한 뒤 `Request::suspend()` 를 return 해도, worker 의 `resume()` 은 IO Thread 의 큐(eventfd / PostQueuedCompletionStatus)에 들어가므로 dispatch 가 끝난 뒤에 RESUMED 로 처리되어 race 가 없다.
- `ConnectionBlock::resume(void* payload)` 의 payload 는 `Event::payload` 로 그대로 전달된다.
- 서버 push 를 위해 `ConnectionBlock::write(const void*, size_t)` 를 추가한다 (IO Thread 로 post 되어 송신 Stage 에 복사됨).
- `ConnectionHandler` 의 5개 콜백은 외부 인터페이스에서 사라지고, 사용자는 `StateMachine` 과 `Session` 타입만 Server / EventClient 에 주입한다. 콜백 모델을 직접 쓰고 싶은 사용자를 위해 남겨둘지는 미결.

##### 미결

- `Transition::stay()` 표기 (state 유지) 를 둘지, Handler 가 `s.state` 를 그대로 return 하게 할지.
- `readUntil` 의 delimiter 최대 길이 (4 bytes 고정이면 memchr + 비교로 충분).
- 32 client 기준으로 Rule 테이블은 선형 탐색으로 충분하지만, 2차원 `[state][event]` 테이블을 compile time 에 만들어 O(1) 로 갈지.
- Rule 테이블의 `(int)HttpState::X` 캐스팅이 거슬린다. `Http::rule(HttpState::ReadingHeader, Event::READ, onHeader)` 같은 constexpr helper 로 감출 수 있다 (C++11 constexpr 생성자면 가능).

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

struct ConnectionSuspension {
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
    ConnectionSuspension suspend();
}

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

    void (*onAttached)(void* user_context, Connection* connection)
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

onConnection

onReadComplete(Connection* connection, void* user_context, const char* buffer, size_t length)
{
    // 1. 현재 상태
    auto& current_state = user_context->state;
    auto& next_state = when(current_state)
    {
        State::V1 -> doSomething(buffer, length);
        State::V2 -> doSomething(buffer, length);
        State::V3 -> doSomething(buffer, length);
        State::V4 -> {
            connection->detach(); 
            doLongtermSomething(connection, user_context, buffer, length);
            // 여기서 작업이 끝나면 connection->attach(user_context);
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

onWriteComplete(Connection* connection, void* user_context, const char* buffer, size_t length)
{
    // 1. 현재 상태
    auto& current_state = user_context->state;
    auto& next_state = when(current_state)
    {
        State::V1 -> doSomething(buffer, length);
        State::V2 -> doSomething(buffer, length);
        State::V3 -> doSomething(buffer, length);
        State::V4 -> doLongtermSomething(buffer, length, [](auto& next_state){
            // 상대방이 호출한 thread에서 실행
            when(next_state)
            {
                State::V1 -> connection->read(user_context->buffer, length);
                State::V2 -> connection->write(user_context->buffer, length);
                State::V3 -> connection->read(user_context->buffer, length);
            }
        });
    }
    when(next_state) 
    {
        State::V1 -> connection->read(user_context->buffer, length);
        State::V2 -> connection->write(user_context->buffer, length);
        State::V3 -> connection->read(user_context->buffer, length);
    }
}


```
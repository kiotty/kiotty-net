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
            kiotty-result.hpp // KiottyResult<Value, Code>. 내부 구현 전용. 공개 인터페이스에는 나오지 않는다
            kiotty-holder.hpp // Derived class의 최대 크기가 정해진 Base class를 별도로 동적할당하지 않고 보관하기 위한 container.
        
tests/ // 
docs/
    requirement/
        requirement_v0.md           // 이 문서. 외부 인터페이스와 규칙만
        implementation_guide_v0.md  // 구현 예시와 설계 근거



## 외부 인터페이스

이 장은 인터페이스 선언과 그 의미를 주석으로만 적는다. 구현 예시, 흐름 의사코드, 프로토콜 예제는 [implementation_guide_v0.md](implementation_guide_v0.md) 에 있다.

인터페이스는 C 와 C++ 두 벌이다. 규약은 다음과 같다.

- `include/kiotty-net.h` 하나에 C 인터페이스가 있고, C++ 로 빌드하면 같은 헤더가 C++ 인터페이스를 함께 노출한다.
- 두 벌 모두 런타임 동적 할당이 없다. 객체 생성(`KiottyNet_create*`) 시점에 `max_clients` 분량을 한 번 잡고 재사용한다.
- **thread 규칙.** 모든 콜백은 IO Thread 에서 순서대로 호출되고, Connection / WebSocket / HttpResponse 는 IO Thread 에서만 쓴다.
  다른 thread 가 할 수 있는 일은 `KiottyNet_startWork(handle, payload)`, Work 의 `finish*` (5절), 소유자의 `close()`, `await()` 뿐이다.
- 각 절은 **공용 타입 → C 함수 → C++ 래퍼** 순서로 적는다. 이름은 1:1 로 대응한다.
- **문자열 수명.** 설정 struct (ServerConfig, ClientConfig, TLS 설정) 안의 문자열은 포인터가 아니라 최대 길이가 정해진 `char[]` 에 값으로 들어간다. 사용자는 initialize / configure 함수 (3절) 로 넣고, 그 함수가 길이를 검사하며 복사한다. 함수의 `const char*` 인자는 따로 적지 않는 한 함수가 return 하기 전에 라이브러리 안으로 복사된다. 어느 쪽이든 호출자의 문자열(`std::string` 등)은 호출 뒤 사라져도 된다.

naming 규약 (C, C++ 공통)

1. 함수 이름은 CamelCase 다. snake_case 를 쓰지 않는다. C 함수는 `KiottyNet_` 뒤에 **타동사 + 명사** 를 소문자로 시작하는 CamelCase 로 잇는다 (`KiottyNet_createServer`, `KiottyNet_readConnection`, `KiottyNet_setConnectionTimeout`). C++ 멤버 함수도 소문자로 시작한다 (`readUntil`, `startWork`).
2. 클래스 이름은 첫 글자가 대문자인 CamelCase 다.
3. 사용자에게 공개되는 C++ 인터페이스는 별도의 namespace 를 쓰지 않고 `KiottyNet_` 접두어를 붙인다 (`KiottyNet_Server`, `KiottyNet_Connection`, `KiottyNet_Transition<State>`).
4. C 인터페이스는 모두 `KiottyNet_` 접두어를 붙인다. 함수와 매크로(`KIOTTY_RULE`)가 여기에 해당한다. C 가 쓰는 struct, enum, handle 은 7, 8번을 따른다.
5. 사용자에게 공개되는 C++ 클래스는 template class 가 아닌 이상 C 인터페이스의 얇은 래퍼다. 생성자 / 소멸자로 RAII 와 메모리 라이프사이클(생성, 파괴, 소유권 이동)을 책임지고, 그 외 멤버 함수는 대응하는 C 함수 하나를 부르는 것으로 끝난다. 로직은 C 인터페이스 아래에만 있다.
6. 사용자에게 공개되지 않고 내부 구현에서만 쓰는 타입과 함수는 아래 namespace 에 둔다. 접두어는 붙이지 않는다.
   ```cpp
   namespace kiotty
   {
       namespace net {
       }
   }
   ```
7. handle 이 아닌 단순 struct (`KiottyNetSpan`, `KiottyNetEndpoint`, 설정, 콜백 구조체 등) 와 공개 enum 은 C, C++ 이 같은 정의를 공용으로 쓴다. 한 벌만 정의하고 양쪽에서 같은 이름으로 쓴다. 이름은 `KiottyNet` 바로 뒤에 언더스코어 없이 잇는다 (`KiottyNetServerConfig`, `KiottyNetCode`). 그래서 외부로 공개되는 enum 은 `enum class` 를 쓰지 않으며 (C 에서 쓸 수 없다), enumerator 는 `KIOTTY_` 접두어로 전역 이름 충돌을 막는다.
   정리하면 **`KiottyNet_` 는 함수 또는 C++ 래퍼 클래스, `KiottyNet` 바로 뒤에 이름이 붙으면 C / C++ 공용 데이터 타입**이다.
8. handle 은 두 종류이며 둘 다 공용 타입이다.
   - **소유 handle.** 사용자가 `KiottyNet_createX` 로 만들고 `KiottyNet_destroyX` 로 없애는 객체. StreamClient, EventClient, Server, HttpServer, WebSocketServer, HttpClient, WebSocketClient 가 여기에 속한다. `typedef struct KiottyNetServer* KiottyNetServerHandle;` 처럼 불투명 포인터다. C++ 래퍼는 생성자에서 create, 소멸자에서 destroy 를 부르고, 복사는 없고 이동만 있다.
   - **pool handle.** 소유자의 pool 안에 할당되는 per-connection 객체. Connection, Work, WebSocket, HttpResponse 가 여기에 속한다. 외부에서는 `int64_t` 이고 (`typedef int64_t KiottyNetConnectionHandle;`), 내부에서는 `{ int32_t generation; int32_t index; }` 로 해석한다. 0 은 무효. 연결이 닫히면 generation 이 바뀌므로 같은 slot 이라도 값이 달라지고, 닫힌 handle 로 부른 함수는 아무 일도 하지 않는다 (코드를 돌려주는 함수는 `KIOTTY_PEER_CLOSED`). C++ 래퍼는 소유하지 않는 값 타입이며 콜백 안에서 handle 로 만든다. 예외는 `KiottyNet_Work` 하나다. Work 는 "finish 할 의무" 를 소유하므로 이동만 되고, 소멸자가 finish 하지 않은 Work 를 `KiottyNet_finishWorkWithClose` 로 닫는다 (5절).

공통 타입

```c
typedef struct KiottyNetSpan {        /* 읽기 전용 바이트 범위. 텍스트와 바이너리 모두 이것으로 받는다. 소유하지 않는다 */
    const char* data;
    size_t      length;
} KiottyNetSpan;

typedef int64_t KiottyNetConnectionHandle;   /* pool handle (규약 8). 다른 thread 에 넘겨도 되는 유일한 Connection 표현 */

/* 설정 struct 안 문자열 버퍼의 크기 (NUL 포함). struct 크기가 곧 ABI 이므로 사용자가 바꿀 수 없다 */
#define KIOTTY_MAX_HOST_LENGTH     256   /* hostname (DNS 최대 253), Windows pipe 이름 (최대 256), Unix domain 경로 (sun_path 104~108) */
#define KIOTTY_MAX_PATH_LENGTH     260   /* 인증서, key, CA 경로. Windows MAX_PATH 기준. 더 긴 경로는 상대 경로로 쓴다 */
#define KIOTTY_MAX_PASSWORD_LENGTH 128   /* key 파일 암호 */
```

```cpp
namespace kiotty { namespace net {
struct PoolHandle {                   // pool handle 의 내부 해석. 외부 int64_t 와 같은 크기. 소유자 pool 을 찾는 방법은 구현 가이드
    int32_t generation;
    int32_t index;
};
} }
```

### 1. Error 처리

C, C++ 공용 타입이다 (naming 규약 7번). `enum class` 가 아니며 enumerator 는 `KIOTTY_` 접두어를 가진다.

```c
typedef enum KiottyNetCode {
    KIOTTY_SUCCESS,
    KIOTTY_PEER_CLOSED,          /* 상대가 FIN 을 보냄. startWork / finish*: 그 handle 의 연결이 이미 닫힘 */
    KIOTTY_SOCKET_ERROR,         /* OS 소켓 에러 */
    KIOTTY_PROTOCOL_ERROR,       /* readUntil 의 max 안에 delimiter 가 없음, 프로토콜 파서 실패 */
    KIOTTY_REQUEST_TOO_LARGE,    /* read 요구 길이가 수신 Stage capacity 보다 큼 */
    KIOTTY_UNHANDLED_EVENT,      /* StateMachine 표에 (state, event) 행이 없음 */
    KIOTTY_ILLEGAL_TRANSITION,   /* StateMachine 핸들러가 .to() 목록에 없는 상태로 감 (debug) */
    KIOTTY_SESSION_EXHAUSTED,    /* max_clients 초과 */
    KIOTTY_DUPLICATE_RULE,       /* C StateMachine 표에 같은 (state, event) 가 두 번 */
    KIOTTY_TIMEOUT,              /* 동기 API 의 timeout 만료 */
    KIOTTY_TABLE_FULL,           /* 라우트 / 정적 경로 / 승격 경로 등록 표가 가득 참 */
    KIOTTY_BUFFER_FULL,          /* write 메시지가 send_capacity 보다 큼. push lane 이나 응답 버퍼에 여유가 없음 */
    KIOTTY_NOT_FOUND,            /* 파일 없음 */
    KIOTTY_INVALID_ARGUMENT,
    KIOTTY_CLOSED,               /* close 로 loop 가 끝남. run 과 await 의 반환값 */
    KIOTTY_BUSY                  /* startWork: 이미 detach 중이거나 worker 가 가진 연결. finish*: 이미 finish 한 Work.
                                    writeConnection: 이전 write 가 아직 끝나지 않음. notify / broadcast: 대기 slot 이 모두 사용 중 */
} KiottyNetCode;
```

### 2. 예외 처리

무조건 반환값으로 알린다. 예외를 던지지 않고 출력 인자(output parameter)도 쓰지 않는다.

- 돌려줄 값이 없으면 `KiottyNetCode` 를 돌려준다.
- 돌려줄 값이 있으면 값과 `code` 필드를 함께 가진 공용 struct 를 값으로 돌려준다 (`KiottyNetResult`, `KiottyNetHttpReply`). C++ 래퍼도 같은 struct 를 그대로 돌려준다.
- handle 을 만드는 함수는 실패하면 NULL 이다.
- 내부 구현은 `KiottyResult<Value, Code>` 를 쓰지만 공개 인터페이스 (C, C++ 모두) 에는 나오지 않는다.

```c
typedef struct KiottyNetResult {
    int32_t       bytes;   /* 전달 또는 송신한 길이. 에러면 0 */
    KiottyNetCode code;
} KiottyNetResult;
```

### 3. Configuration 구성 방식

1. 서버 Configuration과 클라이언트 Configuration은 구분되어야 한다.
    - TLS가 다르기 때문에 (TLS 설정도 서버용 / 클라이언트용이 따로 있다)
    - 동기 Connect, Read, Write 때문에 클라이언트 Configuration은 max_trial과 trial_interval_ms가 있는 것이 좋다. (아이디어의 유효성 검증 필요)

2. 연결 방식(transport)은 TCP 와 Pipe 두 가지다. **TLS 는 transport 가 아니라 그 위에 얹는 선택 계층이다.**
   create 함수에 TLS 설정을 넘기면 TLS 로 통신하고, NULL 을 넘기면 평문으로 통신한다. 설정 struct 와 TLS 설정은 서로를 모른다.
   - TLS 계층은 transport 가 주는 바이트 스트림 위에서만 동작하므로 (OpenSSL memory BIO) TCP / Pipe 어느 쪽에도 얹을 수 있다. TLS over Pipe 는 목표 use case 는 아니지만 막지 않는다.
   - Pipe 위의 TLS 에서는 hostname 이 없으므로 hostname 검증은 하지 않는다. 인증서 체인 검증은 그대로 한다.

3. TLS는 TLS V1.2, TLS V1.3을 지원한다. 버전과 CipherSuite 는 비트 합으로 고른다.

```c
typedef enum KiottyNetTlsVersion {
    KIOTTY_TLS_V12 = 1 << 0,
    KIOTTY_TLS_V13 = 1 << 1
} KiottyNetTlsVersion;

typedef enum KiottyNetTlsV12CipherSuite {
    KIOTTY_TLS12_ECDHE_ECDSA_AES256_GCM_SHA384 = 1 << 0,
    KIOTTY_TLS12_ECDHE_RSA_AES256_GCM_SHA384   = 1 << 1,
    KIOTTY_TLS12_ECDHE_ECDSA_CHACHA20_POLY1305 = 1 << 2,
    KIOTTY_TLS12_ECDHE_RSA_CHACHA20_POLY1305   = 1 << 3
} KiottyNetTlsV12CipherSuite;

typedef enum KiottyNetTlsV13CipherSuite {
    KIOTTY_TLS13_AES_128_GCM_SHA256       = 1 << 0,
    KIOTTY_TLS13_AES_256_GCM_SHA384       = 1 << 1,
    KIOTTY_TLS13_CHACHA20_POLY1305_SHA256 = 1 << 2,
    KIOTTY_TLS13_AES_128_CCM_SHA256       = 1 << 3,
    KIOTTY_TLS13_AES_128_CCM8_SHA256      = 1 << 4
} KiottyNetTlsV13CipherSuite;
```

4. 설정 struct 와 설정 함수. 아래 5, 6번의 모든 설정 (ServerConfig, ClientConfig, TlsServerConfig, TlsClientConfig) 이 같은 규칙을 따른다. 사용 방법은 하나뿐이다. **initialize 한 번, 필요한 configure 여러 번, 각 호출의 반환 코드 확인.**

   - 설정 struct 는 사용자가 잡는다 (스택, static). 필드는 읽어도 되지만 값은 함수로만 넣는다. 함수가 값을 검사하고 문자열을 길이 검사하며 복사하므로 사용자는 길이나 복사를 신경 쓰지 않는다.
   - `KiottyNet_initializeXxx` 는 모든 필드를 기본값으로 채운다. 기본값을 정할 수 없는 필수 값 (서버 / 클라이언트의 주소, TLS 서버의 인증서) 은 initialize 의 인자로 받는다. 그래서 **initialize 가 성공한 설정은 그대로 써도 유효하다.** 별도의 validate 는 없다.
   - `KiottyNet_configureXxx` 는 항목 하나를 바꾼다. 서로 묶여야 하는 값 (클라이언트 인증 방식과 trust store 등) 은 한 configure 의 인자로 같이 받는다. 그래서 configure 하나가 성공하면 설정 전체도 여전히 유효하다.
   - 모든 함수는 `KiottyNetCode` 를 돌려준다. 값이 잘못되면 struct 를 바꾸지 않고 에러를 돌려준다. 잘못된 값은 `KIOTTY_INVALID_ARGUMENT` (NULL, 빈 문자열, 최대 길이 초과, 모르는 비트, 0 이면 안 되는 값), 없는 파일이나 디렉토리는 `KIOTTY_NOT_FOUND` 다.
   - struct 의 `initialization_mark` 는 initialize 가 쓰는 표식이다. initialize 하지 않은 struct 를 configure 하거나 create 에 넘기면 `KIOTTY_INVALID_ARGUMENT` (create 는 NULL) 다.
   - C++ 의 `KiottyNet_XxxConfigBuilder` 는 외부 struct 를 참조로 받아서 생성 시점에 initialize 를 부르고, 각 메서드는 configure 하나를 부른다. 처음 실패한 코드를 기억해 두었다가 `build()` 가 돌려준다.

5. Endpoint 와 Server / Client 설정. TLS 여부는 여기에 없다 (2번).
   create 함수는 설정 struct 를 통째로 복사해 두므로 설정 struct 는 create 뒤 사라져도 된다. 크기는 각각 약 0.3 KiB 이며 대부분 `host` 버퍼다.

```c
typedef enum KiottyNetTransport { KIOTTY_TCP, KIOTTY_PIPE } KiottyNetTransport;

typedef struct KiottyNetEndpoint {
    KiottyNetTransport transport;
    char               host[KIOTTY_MAX_HOST_LENGTH];   /* Tcp: ip 또는 hostname. Pipe: pipe 이름 (Windows: \\.\pipe\name, Unix: 경로) */
    uint16_t           port;                           /* Pipe 면 0 */
} KiottyNetEndpoint;

typedef struct KiottyNetServerConfig {
    uint32_t          initialization_mark;
    KiottyNetEndpoint endpoint;
    size_t            max_clients;      /* 기본 32. 동시 연결 수. 이만큼의 Connection, Stage, Session, resume 큐가 생성 시점에 잡힌다 */
    size_t            recv_capacity;    /* 기본 4 KiB. Connection 당 수신 Stage 크기 */
    size_t            send_capacity;    /* 기본 4 KiB. Connection 당 write lane 크기 = write 메시지 하나의 최대 길이 (4절) */
    size_t            push_capacity;    /* 기본 4 KiB. Connection 당 push lane 크기. broadcast 와 WebSocket control frame 이 쌓이는 곳 (4절) */
} KiottyNetServerConfig;

typedef struct KiottyNetClientConfig {
    uint32_t          initialization_mark;
    KiottyNetEndpoint endpoint;
    size_t            recv_capacity;     /* 기본 4 KiB */
    size_t            send_capacity;     /* 기본 4 KiB */
    size_t            push_capacity;     /* 기본 4 KiB. notifyEventClient 가 쌓이는 곳. StreamClient 는 쓰지 않는다 */
    uint32_t          connect_timeout_ms;/* 기본 5000 */
    uint32_t          max_trial;         /* 기본 1. 연결 시도 횟수 */
    uint32_t          trial_interval_ms; /* 기본 1000. 시도 간격 */
} KiottyNetClientConfig;

KiottyNetCode KiottyNet_initializeTcpServerConfig(KiottyNetServerConfig* config, const char* host, uint16_t port);   /* port != 0 */
KiottyNetCode KiottyNet_initializePipeServerConfig(KiottyNetServerConfig* config, const char* pipe_name);
KiottyNetCode KiottyNet_configureServerMaxClients(KiottyNetServerConfig* config, size_t max_clients);                /* 전체 메모리 계산이 오버플로하면 KIOTTY_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureServerBufferCapacity(KiottyNetServerConfig* config, size_t recv_capacity, size_t send_capacity, size_t push_capacity);

KiottyNetCode KiottyNet_initializeTcpClientConfig(KiottyNetClientConfig* config, const char* host, uint16_t port);
KiottyNetCode KiottyNet_initializePipeClientConfig(KiottyNetClientConfig* config, const char* pipe_name);
KiottyNetCode KiottyNet_configureClientBufferCapacity(KiottyNetClientConfig* config, size_t recv_capacity, size_t send_capacity, size_t push_capacity);
KiottyNetCode KiottyNet_configureClientConnectTimeout(KiottyNetClientConfig* config, uint32_t timeout_ms);
KiottyNetCode KiottyNet_configureClientRetry(KiottyNetClientConfig* config, uint32_t max_trial, uint32_t trial_interval_ms);   /* max_trial >= 1 */
```

```cpp
class KiottyNet_ServerConfigBuilder {
public:
    static KiottyNet_ServerConfigBuilder tcp(KiottyNetServerConfig& config, const char* host, uint16_t port);   // KiottyNet_initializeTcpServerConfig
    static KiottyNet_ServerConfigBuilder pipe(KiottyNetServerConfig& config, const char* pipe_name);
    KiottyNet_ServerConfigBuilder& maxClients(size_t max_clients);
    KiottyNet_ServerConfigBuilder& bufferCapacity(size_t recv_capacity, size_t send_capacity, size_t push_capacity);
    KiottyNetCode build() const;                                                                                  // 처음 실패한 코드
};

class KiottyNet_ClientConfigBuilder {
public:
    static KiottyNet_ClientConfigBuilder tcp(KiottyNetClientConfig& config, const char* host, uint16_t port);
    static KiottyNet_ClientConfigBuilder pipe(KiottyNetClientConfig& config, const char* pipe_name);
    KiottyNet_ClientConfigBuilder& bufferCapacity(size_t recv_capacity, size_t send_capacity, size_t push_capacity);
    KiottyNet_ClientConfigBuilder& connectTimeout(uint32_t timeout_ms);
    KiottyNet_ClientConfigBuilder& retry(uint32_t max_trial, uint32_t trial_interval_ms);
    KiottyNetCode build() const;
};
```

6. TLS 설정. ServerConfig / ClientConfig 와 **따로** create 함수에 넘긴다 (`const KiottyNetTls*Config* tls`, 평문이면 NULL).
   C++ 래퍼는 NULL 포인터 대신 생성자 오버로드 두 개를 둔다. TLS 설정을 `const&` 로 받는 것이 TLS, 받지 않는 것이 평문이다.
   create 가 return 하기 전에 복사하므로 포인터는 create 호출 동안만 유효하면 된다. TLS 를 쓰지 않는 사용자는 TLS 설정 크기(약 0.9 KiB)를 부담하지 않는다.

   | 항목 | Server 기본값 | Client 기본값 |
   |---|---|---|
   | 버전 | 1.2 + 1.3 | 1.2 + 1.3 |
   | cipher suite | 전부 | 전부 |
   | 신원 (인증서, key) | initialize 인자 (필수) | 없음 |
   | 상대 검증 | 클라이언트 인증 안 함 | 서버 검증 + hostname 검증. trust store 는 OS 기본 저장소 |

```c
typedef enum KiottyNetClientAuth {       /* 서버가 클라이언트 신원을 요구하는 정도 */
    KIOTTY_CLIENT_AUTH_NONE,
    KIOTTY_CLIENT_AUTH_OPTIONAL,         /* 인증서를 주면 검증, 없으면 통과 */
    KIOTTY_CLIENT_AUTH_REQUIRED
} KiottyNetClientAuth;

typedef struct KiottyNetTlsIdentity {    /* 자기 신원. 빈 문자열 == 지정 안 함 */
    char cert_file[KIOTTY_MAX_PATH_LENGTH];
    char key_file[KIOTTY_MAX_PATH_LENGTH];
    char key_password[KIOTTY_MAX_PASSWORD_LENGTH];
} KiottyNetTlsIdentity;

typedef struct KiottyNetTlsServerConfig {
    uint32_t             initialization_mark;
    uint32_t             versions;           /* KiottyNetTlsVersion 비트 합 */
    uint32_t             v12_cipher_suites;  /* KiottyNetTlsV12CipherSuite 비트 합 */
    uint32_t             v13_cipher_suites;  /* KiottyNetTlsV13CipherSuite 비트 합 */
    KiottyNetTlsIdentity identity;
    KiottyNetClientAuth  client_auth;
    char                 trust_store_path[KIOTTY_MAX_PATH_LENGTH];
} KiottyNetTlsServerConfig;

typedef struct KiottyNetTlsClientConfig {
    uint32_t             initialization_mark;
    uint32_t             versions;
    uint32_t             v12_cipher_suites;
    uint32_t             v13_cipher_suites;
    int                  skip_server_verification;     /* 0 으로 채워진 설정이 안전하도록 skip 으로 이름 붙인다 */
    int                  skip_hostname_verification;   /* Pipe 면 무시 */
    char                 trust_store_path[KIOTTY_MAX_PATH_LENGTH];   /* 빈 문자열이면 OS 기본 저장소 */
    KiottyNetTlsIdentity identity;
} KiottyNetTlsClientConfig;

KiottyNetCode KiottyNet_initializeTlsServerConfig(KiottyNetTlsServerConfig* config, const char* cert_file, const char* key_file, const char* key_password);   /* key_password 는 NULL 가능 */
KiottyNetCode KiottyNet_configureTlsServerVersions(KiottyNetTlsServerConfig* config, bool tls_1_2, bool tls_1_3);                 /* 둘 다 false 면 KIOTTY_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureTlsServerV12CipherSuites(KiottyNetTlsServerConfig* config, uint32_t suites);                     /* 0 이거나 모르는 비트면 KIOTTY_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureTlsServerV13CipherSuites(KiottyNetTlsServerConfig* config, uint32_t suites);
KiottyNetCode KiottyNet_configureTlsServerClientAuth(KiottyNetTlsServerConfig* config, KiottyNetClientAuth mode, const char* trust_store_path);   /* mode == NONE 이면 trust_store_path 는 NULL */

KiottyNetCode KiottyNet_initializeTlsClientConfig(KiottyNetTlsClientConfig* config);
KiottyNetCode KiottyNet_configureTlsClientVersions(KiottyNetTlsClientConfig* config, bool tls_1_2, bool tls_1_3);
KiottyNetCode KiottyNet_configureTlsClientV12CipherSuites(KiottyNetTlsClientConfig* config, uint32_t suites);
KiottyNetCode KiottyNet_configureTlsClientV13CipherSuites(KiottyNetTlsClientConfig* config, uint32_t suites);
KiottyNetCode KiottyNet_configureTlsClientTrustStore(KiottyNetTlsClientConfig* config, const char* trust_store_path);                 /* OS 기본 저장소 대신 CA 파일 또는 디렉토리 */
KiottyNetCode KiottyNet_configureTlsClientVerification(KiottyNetTlsClientConfig* config, bool verify_server, bool verify_hostname);   /* verify_server == false 는 개발용. 서버를 검증하지 않으면서 hostname 만 검증하면 KIOTTY_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureTlsClientIdentity(KiottyNetTlsClientConfig* config, const char* cert_file, const char* key_file, const char* key_password);   /* 서버가 클라이언트 인증을 요구할 때 */
```

```cpp
class KiottyNet_TlsServerConfigBuilder {
public:
    KiottyNet_TlsServerConfigBuilder(KiottyNetTlsServerConfig& config, const char* cert_file, const char* key_file, const char* key_password = nullptr);
    KiottyNet_TlsServerConfigBuilder& versions(bool tls_1_2, bool tls_1_3);
    KiottyNet_TlsServerConfigBuilder& v12CipherSuites(uint32_t suites);
    KiottyNet_TlsServerConfigBuilder& v13CipherSuites(uint32_t suites);
    KiottyNet_TlsServerConfigBuilder& clientAuth(KiottyNetClientAuth mode, const char* trust_store_path);
    KiottyNetCode build() const;
};

class KiottyNet_TlsClientConfigBuilder {
public:
    explicit KiottyNet_TlsClientConfigBuilder(KiottyNetTlsClientConfig& config);
    KiottyNet_TlsClientConfigBuilder& versions(bool tls_1_2, bool tls_1_3);
    KiottyNet_TlsClientConfigBuilder& v12CipherSuites(uint32_t suites);
    KiottyNet_TlsClientConfigBuilder& v13CipherSuites(uint32_t suites);
    KiottyNet_TlsClientConfigBuilder& trustStore(const char* trust_store_path);
    KiottyNet_TlsClientConfigBuilder& verification(bool verify_server, bool verify_hostname);
    KiottyNet_TlsClientConfigBuilder& identity(const char* cert_file, const char* key_file, const char* key_password = nullptr);
    KiottyNetCode build() const;
};
```

사용 예

```cpp
KiottyNetServerConfig config;
KiottyNetCode code = KiottyNet_ServerConfigBuilder::tcp(config, "0.0.0.0", 8443)
    .maxClients(16)
    .build();
if (code != KIOTTY_SUCCESS) { }

KiottyNetTlsServerConfig tls;
code = KiottyNet_TlsServerConfigBuilder(tls, settings.certPath().c_str(), settings.keyPath().c_str())   // std::string 도 그대로. 함수 안에서 복사된다
    .versions(false, true)
    .clientAuth(KIOTTY_CLIENT_AUTH_REQUIRED, "./ca/")
    .build();
if (code != KIOTTY_SUCCESS) { }

KiottyNet_Server secure(config, tls, handler);   // TLS
KiottyNet_Server plain (config, handler);        // 평문. 같은 config 를 그대로 쓸 수 있다

KiottyNetClientConfig    client;
KiottyNetTlsClientConfig client_tls;
code = KiottyNet_ClientConfigBuilder::tcp(client, "device.local", 8443).retry(3, 500).build();
code = KiottyNet_TlsClientConfigBuilder(client_tls).build();                 // 기본값 그대로: OS 저장소로 서버 + hostname 검증
```

```c
KiottyNetServerConfig config;
if (KiottyNet_initializeTcpServerConfig(&config, "0.0.0.0", 8443) != KIOTTY_SUCCESS) { }
if (KiottyNet_configureServerMaxClients(&config, 16) != KIOTTY_SUCCESS) { }

KiottyNetTlsServerConfig tls;
if (KiottyNet_initializeTlsServerConfig(&tls, cert_path, key_path, NULL) != KIOTTY_SUCCESS) { }
if (KiottyNet_configureTlsServerVersions(&tls, false, true) != KIOTTY_SUCCESS) { }
if (KiottyNet_configureTlsServerClientAuth(&tls, KIOTTY_CLIENT_AUTH_REQUIRED, "./ca/") != KIOTTY_SUCCESS) { }

KiottyNetServerHandle secure = KiottyNet_createServer(&config, &tls, handler);
KiottyNetServerHandle plain  = KiottyNet_createServer(&config, NULL, handler);
```

### 4. Stage

Connection 하나는 고정 크기 버퍼 셋을 가진다. 모두 라이브러리 내부 객체이며 C 인터페이스로 노출하지 않는다 (naming 규약 6번). 생성 시점에 `max_clients` 분량을 잡고 런타임 할당은 없다.

| 버퍼 | 크기 | 들어가는 것 | 쓰는 쪽 | 읽는 쪽 |
|---|---|---|---|---|
| 수신 Stage | `recv_capacity` | recv 한 바이트 | IO Thread | IO Thread (onRead 전달) |
| write lane | `send_capacity` | `writeConnection`, `finishWorkWithWrite` 의 메시지 **하나** | RUNNING 이면 IO Thread, WORKING 이면 worker. 동시에 둘인 순간은 없다 | IO Thread (send) |
| push lane | `push_capacity` | `notifyConnection`, `notifyEventClient`, `broadcastServer`, WebSocket control frame. `[u32 길이][바이트]` 가 여러 개 | IO Thread | IO Thread (send) |

- **수신 Stage.** IO Thread 는 read 요청이 있든 없든 계속 recv 해서 쌓고, `KiottyNet_readConnection*` 는 "이 조건이 되면 onRead 한 번" 이라는 조건 등록일 뿐이다. 수신 흐름(ReadRequest, pump)은 구현 가이드 1절에 있다.
- **write lane.** 사용자의 프로토콜 흐름이다. 한 번에 메시지 하나만 들어 있고 (`write → onWritten → write`), 그래서 메시지 경계를 추적하는 큐가 없다. 다 나가면 `onWritten(length)` 한 번. 이전 메시지의 onWritten 전에 다시 write 하면 사용자 규칙 위반이며 `close(KIOTTY_BUSY)`. 메시지가 `send_capacity` 보다 크면 `close(KIOTTY_BUFFER_FULL)`.
- **push lane.** 사용자의 프로토콜 흐름과 무관하게 다른 thread 나 라이브러리가 끼워 넣는 바이트다. 넣는 동작의 이름은 **notify** 다. 완료 콜백이 없다. 여유가 없으면 그 notify 는 들어가지 않고 연결은 그대로 둔다. 끊을지는 호출자가 정한다. 두 lane 을 나눈 이유는, 사용자가 자기 흐름을 지켜도 broadcast 가 몰리면 자기 write 가 BUFFER_FULL 로 끊기는 일을 막기 위해서다. push lane 은 IO Thread 혼자 쓰고 읽으므로 Work 상태와 무관하게 Work 중인 연결에도 들어간다.
- **어느 lane 으로 보낼까.** 송신이 프로토콜 흐름의 한 단계이면 (요청에 대한 응답처럼, 다 보낸 뒤에 다음 단계로 가야 하면) write 다. 흐름과 무관하게 아무 때나 나가는 것이면 (서버 알림, WebSocket 메시지처럼 수신 상태와 상관없는 송신이면) notify 다. 전이중 프로토콜에서 수신 흐름을 건드리지 않고 보내는 길이 notify 다.
- **송신 순서.** IO Thread 의 drain 하나가 두 lane 을 소비하고 socket 에 쓴다. 보내던 메시지는 끝까지 보내고 메시지 경계에서만 lane 을 바꾸며, 둘 다 있으면 write lane 먼저다. WORKING 인 연결에서는 push lane 만 보낸다 (write lane 은 worker 것이다). 구현은 구현 가이드 1.6.

`Stage` 는 수신 Stage 의 구현이다. push lane 도 같은 클래스로 만든다.

```cpp
namespace kiotty { namespace net {

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

} }
```

### 5. Connection

- 비동기 Socket IO 모델에서 연결을 추상화하는 객체. EventClient, Server 의 pool 안에 있고 밖에서는 `KiottyNetConnectionHandle` 로만 보인다.
- Pipe, TLS, TCP 에 따라 read / write 의 내부가 달라지지만 인터페이스는 같다.
- **IO Thread 에서만 사용한다.** 다른 thread 가 쓸 수 있는 것은 `KiottyNet_startWork` 와 Work 뿐이다. handle 은 정수라서 복사해 두어도 dangling 이 없다.
- read 요청은 두 종류뿐이고 한 번에 하나만 걸린다. 새 요청은 이전 요청을 대체한다. write 도 한 번에 하나다 (4절 write lane). 둘은 서로 독립이다.

**Work : Connection 을 IO Thread 에서 떼어 다른 thread 에게 넘기는 방법.** 긴 작업과, 연결 하나를 골라 보내는 push 가 이것이다. 모든 연결에 같은 데이터를 보내는 것은 7절 broadcast 다. Work 가 진행 중인 동안 IO Thread 는 그 연결에 어떤 콜백도 내지 않으므로, 다른 thread 가 business logic 을 돌려도 흐름이 예측 가능하다.

1. 떼어내고 싶은 쪽이 `KiottyNet_startWork(handle, payload)` 를 부른다. IO Thread 의 콜백 안에서도, 앱 thread 에서도 된다.
2. IO Thread 는 그 연결에 걸려 있던 요청을 마저 처리한다. write lane 의 메시지는 끝까지 보내고 `onWritten` 을 낸다. 걸려 있던 read 요청은 조건이 되면 `onRead` 를 내고, 아직 조건이 안 됐으면 **기억해 두고** 멈춘다 (수신 Stage 의 데이터는 그대로 남는다). 남은 deadline 도 기억해 둔다. 이 동안 새 요청은 받지 않는다.
3. 다 끝나면 **다음 loop 에서** `onWorkStarted(ctx, handle, work, payload)` 를 한 번 낸다. 콜백 안에서 startWork 를 불렀더라도 그 콜백 안에서 중첩 호출되지 않는다.
4. onWorkStarted 가 나간 뒤 그 연결에는 `finish*` 가 올 때까지 어떤 콜백도 나가지 않는다. IO Thread 가 그 연결에 하는 일은 수신 Stage 를 채우는 것, push lane 을 보내는 것 (broadcast, WebSocket control frame), peer 가 끊겼는지 기록하는 것뿐이다. 수신 Stage 가 가득 차면 recv 를 멈추고 OS 버퍼와 TCP 흐름 제어에 맡긴다. 데이터는 유실되지 않는다. deadline 은 멈춰 있다.
5. 사용자는 `work` 를 받은 thread 에서 쓰거나 worker thread 에 넘긴다. **Work 를 받았으면 반드시 finish 해야 한다.** 작업이 끝나면 결과를 **다음 IO 요청** 으로 돌려준다. `finishWorkWithWrite` 는 그 자리에서 write lane 으로 복사하므로 return 한 뒤 data 를 버려도 된다 (WORKING 중 IO Thread 는 write lane 을 읽지도 쓰지도 않는다. push lane 만 보낸다).
6. IO Thread 가 깨어나 요청을 다시 건다. 그 뒤는 보통의 `onRead` / `onWritten` 이다. 수신 Stage 에 이미 조건이 맞는 데이터가 있으면 바로 `onRead` 가 나온다. 별도의 "재개" 콜백은 없다.

finish 의 네 가지. 어느 thread 에서 불러도 되고, onWorkStarted 안에서 바로 불러도 된다 (push 가 이 모양이다). "요청 없이 돌려주기" 는 없다. 할 일이 없어졌으면 다음 read 를 걸거나 닫는다.

| finish | 하는 일 | read 요청 | deadline |
|---|---|---|---|
| `finishWorkWithRead(exact, timeout_ms)` | 새 read | 대체 | timeout_ms (0 이면 없음) |
| `finishWorkWithReadUntil(delimiter, max, timeout_ms)` | 새 readUntil | 대체 | timeout_ms |
| `finishWorkWithWrite(data)` | write lane 으로 복사하고 제출. 완료는 onWritten | startWork 전의 것을 복원 | 남은 시간 복원 |
| `finishWorkWithClose(reason)` | 닫는다. onDisconnected(reason). C++ 소멸자의 기본 동작 | 없음 | 없음 |

규칙

- Work 는 연결당 하나다. 이미 Work 가 진행 중이거나 떼어내는 중이면 startWork 는 `KIOTTY_BUSY` 다. IO Thread 콜백 안에서 부른 startWork 도 앱 thread 가 먼저 startWork 했으면 BUSY 가 될 수 있으므로 반환값을 확인한다. 닫힌 연결이면 `KIOTTY_PEER_CLOSED`.
- `finish*` 는 한 번만 유효하다. 두 번째는 `KIOTTY_BUSY`. 소유자가 destroy 된 뒤에 불러도 아무 일도 하지 않고 `KIOTTY_PEER_CLOSED`.
- `finishWorkWithWrite` 의 data 가 `send_capacity` 보다 크면 `KIOTTY_BUFFER_FULL` 이고 Work 는 유효하게 남는다. 다른 finish 로 끝내야 한다. write lane 은 떼어낼 때 비워졌으므로 그 외의 이유로 여유가 없는 일은 없다.
- Work 가 진행 중일 때 peer 가 끊으면 slot 은 붙잡아 두고, `finish*` 가 온 뒤에 `onDisconnected(KIOTTY_PEER_CLOSED)` 를 낸다. 감시는 사용자 몫이다. 오래 걸릴 것 같으면 `KiottyNet_isWorkConnected(work)` 로 주기적으로 확인한다. IO Thread 가 기록해 둔 상태를 읽을 뿐이라 비용이 없고 socket 을 건드리지 않는다.
- 소유자의 `close()` 는 Work 중인 연결에도 바로 `onDisconnected(KIOTTY_CLOSED)` 를 낸다. 그 Work 의 `finish*` 는 `KIOTTY_PEER_CLOSED` 다. 그렇지 않으면 `run` 이 worker 를 기다려야 해서 끝나지 않는다. 이것이 4번 "콜백 없음" 의 유일한 예외다.
- 떼어내는 중(2번)에 deadline 이 만료되면 `onTimeout` 대신 `close(KIOTTY_TIMEOUT)` 이고 onWorkStarted 는 오지 않는다. onWorkStarted 가 나간 뒤에는 deadline 이 멈춘다. 라이브러리는 Work 자체에 timeout 을 두지 않는다. IO Thread 가 감시를 넘긴 상태에서 라이브러리가 끼어들면 흐름이 예측 불가능해지기 때문이다.
- C 에서 `finish*` 를 영영 부르지 않으면 slot 이 영영 잡혀 있다. C 로는 막을 수 없는 사용자 책임이다. C++ 은 `KiottyNet_Work` 의 소멸자가 `finishWithClose()` 를 불러서 막는다. 소멸자까지 왔다는 것은 finish 를 잊었거나 예외가 났다는 뜻이므로, 이전 read 를 조용히 되살리는 대신 연결을 닫아 드러낸다.
- `onRead` 의 data 는 return 하면 무효이므로, worker 가 쓸 데이터는 startWork 전에 context 로 복사한다.
- push 로 보낼 연결이 마침 Work 중이면 startWork 가 BUSY 다. 그 메시지는 사용자가 context 에 쌓아 두었다가 그 Work 가 끝난 뒤 (onWritten 등) 보낸다. 모든 연결에 같은 데이터면 broadcast (7절) 가 Work 중인 연결에도 들어가므로 이 문제가 없다.

```c
typedef struct KiottyNetConnectionInfo {
    char    hostname[KIOTTY_MAX_HOST_LENGTH];   /* ip 또는 pipe 이름 */
    int32_t port;
} KiottyNetConnectionInfo;

typedef int64_t KiottyNetWorkHandle;   /* pool handle. 그 연결의 handle 과 같은 generation 을 가진다 */

/* handler 는 함수 표만 담는다. 그래서 static const 로 두고 여러 객체가 같이 쓸 수 있다.
   handler 와 app_context 는 create 가 아니라 run 에 따로 넘긴다 (7절). 둘 다 run 이 return 할 때까지만 살아 있으면 되므로 run 을 부른 함수의 stack 에 두어도 된다.
   context 는 둘이다. KiottyNetAppContext 는 handler 소유자(앱)의 것이고 onConnected 와 onDisconnected 가 받는다.
   KiottyNetConnectionContext 는 연결마다의 데이터다. onConnected 가 만들어 return 하면 라이브러리가 그 Connection 에 묶어 두고, 나머지 콜백의 첫 인자로 돌려준다.
   onDisconnected 는 둘 다 받는다. connection_context 를 만든 쪽(app_context)이 마지막 콜백에서 돌려받아 해제할 수 있어야 하기 때문이다.
   둘 다 void* 의 typedef 다. 이름으로 역할을 구분할 뿐이고 라이브러리는 어느 쪽도 읽지 않는다 */
typedef void* KiottyNetAppContext;
typedef void* KiottyNetConnectionContext;

typedef struct KiottyNetConnectionHandler {
    KiottyNetConnectionContext (*onConnected)(KiottyNetAppContext app_context, KiottyNetConnectionHandle c);                                                      /* accept / connect 완료. 돌려준 값이 이 연결의 connection_context */
    void (*onDisconnected)(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle c, KiottyNetCode reason);   /* 마지막 콜백. connection_context 해제는 여기서 */
    void (*onRead)        (KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle c, KiottyNetSpan data);                                        /* data 는 수신 Stage. return 하면 무효 */
    void (*onWritten)     (KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle c, size_t length);                                             /* write 하나의 완료 */
    void (*onWorkStarted) (KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle c, KiottyNetWorkHandle work);                                  /* 연결이 IO Thread 에서 떨어졌다 */
    void (*onTimeout)     (KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle c);                                                            /* setTimeout 만료 */
} KiottyNetConnectionHandler;

KiottyNetConnectionInfo KiottyNet_getConnectionInfo(KiottyNetConnectionHandle c);   /* 값으로 돌려준다. pool 안을 가리키는 포인터를 내주지 않기 위해서다. 닫힌 handle 이면 0 으로 채운 값 */

/* 수신 Stage 에 정확히 exact 바이트가 모이면 onRead(data) 한 번. data.length == exact.
   exact 가 recv_capacity 보다 크면 즉시 close(KIOTTY_REQUEST_TOO_LARGE) */
void  KiottyNet_readConnection(KiottyNetConnectionHandle c, size_t exact);

/* delimiter 가 나타나면 onRead(data) 한 번. data 는 delimiter 를 포함한다.
   max 바이트 안에 delimiter 가 없으면 close(KIOTTY_PROTOCOL_ERROR). delimiter 는 1..4 바이트이며 등록 시 Connection 안으로 복사된다 */
void  KiottyNet_readConnectionUntil(KiottyNetConnectionHandle c, KiottyNetSpan delimiter, size_t max);

/* write lane 으로 복사하고 제출. 연결당 한 번에 하나. 완료되면 onWritten(length) 한 번.
   이전 write 의 onWritten 전에 다시 부르면 close(KIOTTY_BUSY). data 가 send_capacity 보다 크면 close(KIOTTY_BUFFER_FULL) */
void  KiottyNet_writeConnection(KiottyNetConnectionHandle c, KiottyNetSpan data);

/* 종료. 마지막으로 onDisconnected(reason) 한 번이 오고, 그 뒤 이 Connection 에 콜백은 없다 */
void  KiottyNet_closeConnection(KiottyNetConnectionHandle c, KiottyNetCode reason);

/* deadline 하나. 만료되면 onTimeout 한 번이 오고 deadline 은 해제된다. 0 이면 해제 */
void  KiottyNet_setConnectionTimeout(KiottyNetConnectionHandle c, uint32_t ms);

/* 아무 thread 에서나. 위 Work 절차를 시작한다 */
KiottyNetCode KiottyNet_startWork(KiottyNetConnectionHandle c);

/* 아무 thread 에서나. 이 연결의 push lane 으로 data 를 복사하고 돌아온다 (4절). 돌아온 뒤 data 는 버려도 된다. 완료 콜백도 이벤트도 없다.
   다른 thread 에서 부르면 IO Thread 가 복사할 때까지 기다린다. IO Thread 콜백 안에서 부르면 그 자리에서 복사한다.
   push lane 에 여유가 없으면 KIOTTY_BUFFER_FULL 이고 아무것도 넣지 않으며 연결은 유지된다. 닫힌 handle 이면 KIOTTY_PEER_CLOSED.
   data 가 비었거나 push_capacity 보다 크면 KIOTTY_INVALID_ARGUMENT. 대기 slot 이 모두 사용 중이면 KIOTTY_BUSY. Work 중인 연결에도 들어간다 */
KiottyNetCode KiottyNet_notifyConnection(KiottyNetConnectionHandle c, KiottyNetSpan data);

/* 아무 thread 에서나. 한 번만 */
KiottyNetCode KiottyNet_finishWorkWithRead(KiottyNetWorkHandle work, size_t exact, uint32_t timeout_ms);
KiottyNetCode KiottyNet_finishWorkWithReadUntil(KiottyNetWorkHandle work, KiottyNetSpan delimiter, size_t max, uint32_t timeout_ms);
KiottyNetCode KiottyNet_finishWorkWithWrite(KiottyNetWorkHandle work, KiottyNetSpan data);
KiottyNetCode KiottyNet_finishWorkWithClose(KiottyNetWorkHandle work, KiottyNetCode reason);
bool          KiottyNet_isWorkConnected(KiottyNetWorkHandle work);   /* peer 가 끊었거나 소유자가 close 했으면 false */
```

```cpp
class KiottyNet_Connection {                       // pool handle 의 비소유 래퍼. 값 타입. 콜백 안에서 handle 로 만든다
public:
    explicit KiottyNet_Connection(KiottyNetConnectionHandle handle);
    KiottyNetConnectionHandle handle() const;      // 다른 thread 에 넘길 값. Session 에 저장해 둔다

    KiottyNetConnectionInfo info() const;
    void  read(size_t exact);
    void  readUntil(KiottyNetSpan delimiter, size_t max);
    void  write(KiottyNetSpan data);
    void  close(KiottyNetCode reason = KIOTTY_SUCCESS);
    void  setTimeout(uint32_t ms);
    KiottyNetCode startWork();                      // startWork 와 notify 만 다른 thread 에서 불러도 된다
    KiottyNetCode notify(KiottyNetSpan data);
};

class KiottyNet_Work {                             // finish 의무를 소유한다. 복사 없음, 이동만. worker thread 로 이동해서 넘긴다
public:
    explicit KiottyNet_Work(KiottyNetWorkHandle work);
    KiottyNet_Work(KiottyNet_Work&& other);
    ~KiottyNet_Work();                             // 아직 finish 하지 않았으면 finishWithClose()

    bool          connected() const;               // finish* 는 모두 한 번만. 부르고 나면 비어 있다
    KiottyNetCode finishWithRead(size_t exact, uint32_t timeout_ms = 0);
    KiottyNetCode finishWithReadUntil(KiottyNetSpan delimiter, size_t max, uint32_t timeout_ms = 0);
    KiottyNetCode finishWithWrite(KiottyNetSpan data);
    KiottyNetCode finishWithClose(KiottyNetCode reason = KIOTTY_SUCCESS);
};
```

사용 예

```cpp
// 연결마다의 데이터 : onConnected 가 만들어 돌려주고, onDisconnected 가 돌려받아 해제한다
KiottyNetConnectionContext onConnected(KiottyNetAppContext ctx, KiottyNetConnectionHandle c)
{
    App*     app = static_cast<App*>(ctx);
    Session* s   = app->sessions.acquire();
    if (s == nullptr) { KiottyNet_Connection(c).close(KIOTTY_SESSION_EXHAUSTED); return nullptr; }
    s->app        = app;                                             // 뒤의 콜백은 app_context 를 받지 않는다. 필요하면 Session 에 넣어 둔다
    s->connection = c;
    KiottyNet_Connection(c).readUntil(CRLF, 1024);
    return s;
}
void onDisconnected(KiottyNetAppContext ctx, KiottyNetConnectionContext session, KiottyNetConnectionHandle, KiottyNetCode)
{
    static_cast<App*>(ctx)->sessions.release(static_cast<Session*>(session));
}

// 긴 작업 : onRead 에서 떼어내고, worker 가 응답을 돌려준다
void onRead(KiottyNetConnectionContext session, KiottyNetConnectionHandle c, KiottyNetSpan data)
{
    Session* s = static_cast<Session*>(session);
    s->request.assign(data.data, data.length);                       // return 하면 data 가 무효라 복사
    KiottyNet_Connection(c).startWork();
}
void onWorkStarted(KiottyNetConnectionContext session, KiottyNetConnectionHandle, KiottyNetWorkHandle handle)
{
    Session*       s = static_cast<Session*>(session);
    KiottyNet_Work work(handle);
    s->app->pool.post([s, work = std::move(work)]() mutable {        // C++14 init capture. C++11 은 shared_ptr 로
        s->response = compute(s->request);
        if (!work.connected()) { return; }                            // 끊겼으면 소멸자의 finishWithClose() 가 onDisconnected 를 끌어낸다
        work.finishWithWrite(s->response.span());                    // 완료는 onWritten 으로 온다
    });
}

// push : 앱 thread 가 살아 있는 연결에 끼어든다. 보낼 것은 Session 에 쌓고 startWork 만 건다. BUSY 면 이미 Work 중이고, 다음 WORK_STARTED 가 쌓인 것을 비운다
for (Session* s : app.sessions.live()) { s->pending.push(message); KiottyNet_Connection(s->connection).startWork(); }
```

### 6. Client

1. Client는 Configuration과 상관 없이 동일한 기능을 제공해야 한다.

2. StreamClient와 EventClient를 구분한다.
    2.1 StreamClient의 목적은 User-space의 데이터를 direct하게 바로 network로 보내주기 위함이다. 동기 API 이며 IO Thread 가 없다.
    2.2 EventClient의 목적은 서버에서 먼저 날아오는 이벤트를 효과적으로 handle하면서도 User-space의 데이터를 direct하게 network로 보내는 것이 목적이다. Connection 하나와 IO Thread 를 가지며 KiottyNetConnectionHandler (또는 8절의 StateMachine) 를 run 에서 주입받는다.

둘 다 소유 handle 이다 (규약 8). 소유 handle 의 C++ 래퍼는 모두 같은 골격이라 아래 절부터는 생략한다.

```cpp
class KiottyNet_StreamClient {
public:
    explicit KiottyNet_StreamClient(const KiottyNetClientConfig& config);   // KiottyNet_createStreamClient
    KiottyNet_StreamClient(const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls);   // TLS
    ~KiottyNet_StreamClient();                                              // KiottyNet_destroyStreamClient
    KiottyNet_StreamClient(KiottyNet_StreamClient&& other);                 // handle 이동. 복사는 없다
    KiottyNet_StreamClient& operator=(KiottyNet_StreamClient&& other);
    bool valid() const;                                                     // create 가 실패하면 false. 예외는 없다
    KiottyNetStreamClientHandle handle() const;
    // 이하 메서드는 같은 이름의 C 함수를 그대로 부른다
    KiottyNetCode   connect();
    void            disconnect();
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_ms);
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_ms);
};
```

```c
typedef struct KiottyNetStreamClient* KiottyNetStreamClientHandle;
KiottyNetStreamClientHandle KiottyNet_createStreamClient(const KiottyNetClientConfig* config, const KiottyNetTlsClientConfig* tls);   /* 설정 오류나 메모리 부족이면 NULL */
void            KiottyNet_destroyStreamClient(KiottyNetStreamClientHandle c);
KiottyNetCode   KiottyNet_connectStreamClient(KiottyNetStreamClientHandle c);                  /* max_trial / trial_interval_ms 만큼 재시도 */
void            KiottyNet_disconnectStreamClient(KiottyNetStreamClientHandle c);
KiottyNetResult KiottyNet_readStreamClient(KiottyNetStreamClientHandle c, void* buffer, size_t length, uint32_t timeout_ms);   /* 최대 length. 0 바이트면 KIOTTY_PEER_CLOSED */
KiottyNetResult KiottyNet_writeStreamClient(KiottyNetStreamClientHandle c, KiottyNetSpan data, uint32_t timeout_ms);

typedef struct KiottyNetEventClient* KiottyNetEventClientHandle;
KiottyNetEventClientHandle KiottyNet_createEventClient(KiottyNetContext context, const KiottyNetClientConfig* config, const KiottyNetTlsClientConfig* tls);
void          KiottyNet_destroyEventClient(KiottyNetContext context, KiottyNetEventClientHandle c);
KiottyNetCode KiottyNet_runEventClient(KiottyNetEventClientHandle c, const KiottyNetConnectionHandler* handler, KiottyNetAppContext app_context);   /* 연결 (max_trial 만큼 재시도) 후 loop. 연결되면 onConnected */
void          KiottyNet_closeEventClient(KiottyNetEventClientHandle c);
KiottyNetCode KiottyNet_awaitEventClient(KiottyNetEventClientHandle c, uint64_t timeout_ms);
KiottyNetConnectionHandle KiottyNet_getEventClientConnection(KiottyNetEventClientHandle c);   /* 연결 전이면 0. startWork 는 이 handle 로 */
/* EventClient 가 가진 연결 하나의 KiottyNet_notifyConnection 이다. 규칙도 같다. 연결 전이면 KIOTTY_PEER_CLOSED */
KiottyNetCode KiottyNet_notifyEventClient(KiottyNetEventClientHandle c, KiottyNetSpan data);
```

```cpp
class KiottyNet_EventClient {                      // 골격은 KiottyNet_StreamClient 와 같다
public:
    KiottyNet_EventClient(KiottyNetContext context, const KiottyNetClientConfig& config);
    KiottyNet_EventClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls);   // TLS
    KiottyNetCode run(const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context);
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);
    KiottyNetConnectionHandle connection() const;
    KiottyNetCode notify(KiottyNetSpan data);
};
```

> 고민되는 부분. 생각보다 network 환경이 열악할 때도 있기 때문에 timeout만 걸어두는 게 아니라,
> 재시도 횟수와 시도와 시도 사이의 time interval도 걸어두는 게 좋다. (KiottyNetClientConfig 의 max_trial / trial_interval_ms 로 반영)

### 7. Server

1. Server는 Configuration과 상관 없이 동일한 기능으로 돌아간다.
2. 생성 시점에 `max_clients` 분량의 Connection, Stage, Work 큐를 모두 잡는다. 이후 런타임 할당은 없다.
3. 프로토콜은 `run` 에 `KiottyNetConnectionHandler` 와 `app_context` 를 따로 넘겨 주입한다. create 는 handler 를 받지 않는다. StateMachine 을 쓰면 `KiottyNet_runStatefulServer` 가 라이브러리의 handler 표를 대신 넘긴다 (8절). HTTP / WebSocket 은 9, 10절의 전용 서버가 내부에서 이것을 쓴다.
4. `KiottyNet_startWork` 와 Work 의 `finish*` 가 다른 thread 에서 IO Thread 로 들어오는 유일한 길이다 (5절). 긴 작업의 완료와 Event 모델의 push 가 모두 이것이다.
5. 수명은 `run / close / await` 셋이다. 규칙은 아래 표와 같고 EventClient, HttpServer, WebSocketServer, WebSocketClient 도 같다.

**loop 를 가진 객체의 수명 (EventClient, Server, HttpServer, WebSocketServer, WebSocketClient 공통).** 함수는 `run`, `close`, `await` 셋뿐이다.

| 함수 | 부르는 thread | 하는 일 | 반환 |
|---|---|---|---|
| `run(handler, app_context)` | loop 를 돌릴 thread | 연다 (Server: bind + listen, Client: connect + 재시도) 그리고 loop 를 돈다. `close` 까지 return 하지 않는다 | `close` 로 끝나면 `KIOTTY_CLOSED`. 열지 못했거나 loop 가 실패하면 그 이유 (`KIOTTY_SOCKET_ERROR`, `KIOTTY_TIMEOUT`, Client 는 서버가 끊으면 `KIOTTY_PEER_CLOSED`) |
| `close()` | 아무 thread | 모든 Connection 을 닫고 (`onDisconnected(KIOTTY_SUCCESS)`) loop 를 끝낸다. 기다리지 않고 바로 return 한다 | 없음 |
| `await(timeout_ms)` | 아무 thread | loop 가 완전히 돌기 시작할 때까지 기다린다 | 돌고 있으면 `KIOTTY_SUCCESS`. `run` 이 이미 끝났으면 `run` 의 반환값. 시간이 지나면 `KIOTTY_TIMEOUT` |

- `run` 이 return 한 뒤 다시 `run` 하면 다시 연다 (재접속, 재시작). 이때 다른 handler 와 app_context 를 넘겨도 된다. 이미 돌고 있는데 `run` 하면 `KIOTTY_INVALID_ARGUMENT`. handler 가 NULL 이어도 `KIOTTY_INVALID_ARGUMENT`.
- 모든 콜백은 `run` 을 부른 thread 에서 `run` 안에서만 일어난다. `run` 은 모든 연결의 `onDisconnected` 를 낸 뒤에만 return 한다 (정상 종료, `close`, 오류 종료 모두). 그래서 handler 와 app_context 는 `run` 이 return 할 때까지만 살아 있으면 된다. 이 보장 때문에 `run` 은 자기 thread 를 따로 띄우지 않는 blocking 함수로 남는다.
- Work 의 worker thread 는 이 보장 밖이다. `close` 는 worker 를 기다리지 않으므로 `run` 이 return 한 뒤에도 worker 가 남을 수 있다. worker 에는 app_context 를 넘기지 않는다.
- `destroy` (C++ 소멸자) 는 `close` 를 부르고 `run` 이 return 할 때까지 기다린 뒤 해제한다. 그래서 IO Thread 의 콜백 안에서 `destroy` 하면 안 된다 (debug 빌드는 assert).

```cpp
KiottyNet_Server server(context.handle(), config);
std::thread io([&] { server.run(handler, &app); });   // handler 는 static const 표, app 은 app_context
if (server.await(3000) != KIOTTY_SUCCESS) { /* bind 실패 등. run 의 반환값과 같다 */ }
// ...
server.close();
io.join();
```

```c
typedef struct KiottyNetServer* KiottyNetServerHandle;
KiottyNetServerHandle KiottyNet_createServer(KiottyNetContext context, const KiottyNetServerConfig* config, const KiottyNetTlsServerConfig* tls);   /* 실패하면 NULL */
void          KiottyNet_destroyServer(KiottyNetContext context, KiottyNetServerHandle s);
KiottyNetCode KiottyNet_runServer(KiottyNetServerHandle s, const KiottyNetConnectionHandler* handler, KiottyNetAppContext app_context);   /* bind + listen 후 loop */
void          KiottyNet_closeServer(KiottyNetServerHandle s);
KiottyNetCode KiottyNet_awaitServer(KiottyNetServerHandle s, uint64_t timeout_ms);

/* 아무 thread 에서나. onConnected 가 나간 모든 연결 (Work 중인 연결 포함) 에 notifyConnection 한 것과 같다 (5절).
   돌아온 시점에 복사는 끝났고 data 는 버려도 된다. 완료 콜백은 없다. socket 으로 나가는 것은 IO Thread 의 drain 이 평소처럼 한다.
   Server 의 broadcast 버퍼는 없다. 호출자가 돌아올 때까지 block 되므로 IO Thread 가 호출자의 data 를 직접 읽는다. IO Thread 를 깨우는 것은 한 번이다.
   push lane 에 여유가 없는 연결은 건너뛰고 끊지 않는다. 모두 들어갔으면 KIOTTY_SUCCESS, 하나라도 건너뛰었으면 KIOTTY_BUFFER_FULL.
   data 가 비었거나 push_capacity 보다 크면 KIOTTY_INVALID_ARGUMENT. 대기 slot 은 notifyConnection 과 같이 쓰며 4개이고 넘치면 KIOTTY_BUSY.
   IO Thread 콜백 안에서 부르면 기다리지 않고 그 자리에서 복사한다. close 된 뒤면 KIOTTY_CLOSED. 구현은 구현 가이드 1.6 */
KiottyNetCode KiottyNet_broadcastServer(KiottyNetServerHandle s, KiottyNetSpan data);
```

```cpp
class KiottyNet_Server {
public:
    KiottyNet_Server(KiottyNetContext context, const KiottyNetServerConfig& config);
    KiottyNet_Server(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls);   // TLS
    KiottyNetCode run(const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context);
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);
    KiottyNetCode broadcast(KiottyNetSpan data);
};
```

### 8. StateMachine

HTTP, WebSocket, Custom Protocol 작성을 돕는 **선택적 계층**이다. Connection 과 KiottyNetConnectionHandler (5절) 위에 얹히며 그 둘은 이 계층을 모른다.
프로토콜 작성자는 **(1) State enum, (2) Session 과 그것을 만들고 돌려받는 함수 둘, (3) transition action 함수들, (4) rules 표** 네 가지만 쓴다.
콜백을 직접 구현하지 않고, transition action 안에서 Connection 의 IO 함수를 직접 부르지도 않는다. transition action 은 `(Session, Event) -> Transition` 인 순수 함수이고, Transition 의 entry action 을 라이브러리가 Connection 호출로 바꾼다.
이 계층은 `KiottyNet_runStatefulServer` / `KiottyNet_runStatefulEventClient` 하나로 들어간다. 7절의 run 에 라이브러리가 가진 handler 표와 `KiottyNetStatefulAppContext` 를 대신 넘기는 함수다. Runner 같은 객체도, 사용자가 잡아 두는 pool 도 없다.

규칙

- 라이브러리가 기억하는 것은 둘이다. **state** 는 연결마다 하나이고 Connection 안에 있다. **machine** (표 + 초기 상태) 은 run 마다 하나이고 `KiottyNetStatefulAppContext` 안에 있다. Session 에 state 멤버를 두지 않는다.
- Session 의 저장소는 사용자 몫이다. `createConnectionContext(app_context, c)` 가 연결마다 하나를 돌려주고, `releaseConnectionContext(app_context, session, c)` 가 돌려받는다. create 가 NULL 을 돌려주면 그 연결은 `close(KIOTTY_SESSION_EXHAUSTED)` 다. create 가 NULL 함수이면 session 은 NULL 이고 release 는 부르지 않는다. release 가 NULL 함수이면 부르지 않는다.
- transition action 은 app_context 를 받지 않는다. action 이 앱 객체에 닿아야 하면 create 가 Session 에 그 포인터를 넣어 둔다.
- `Event` 는 IO Thread 가 알려주는 "무슨 일이 일어났는가", `EntryAction` 은 표가 돌려주는 "다음 상태에 들어가면서 해 달라" 다. 한 Transition 은 entry action 하나만 가진다.
- Transition 은 언제나 다음 상태를 정한다. 상태를 그대로 두는 Transition 은 없다. ANY 행도 갈 곳을 안다 (오류면 오류 상태, 닫히면 닫힌 상태). 다른 일 없이 머물려면 자기 상태로 move 한다.
- 표는 수신 흐름과 그에 딸린 응답만 다룬다. 흐름과 무관한 송신은 표를 거치지 않고 `KiottyNet_notifyConnection` 으로 보낸다 (4절). 그래서 "어느 상태에서든 와서 상태를 건드리지 않는" 이벤트가 없다.
- `CONNECTED` 는 첫 Event 다. create 가 Session 을 돌려준 뒤에 온다. 이 행이 첫 read / write 를 entry action 으로 낸다.
- `WORK_STARTED` 에서 돌려준 entry action 은 Connection 이 아니라 Work 에 적용된다. read / readUntil / write / close 면 그 자리에서 `finishWorkWith*`. 없음(none) 이면 Work 를 그대로 두는데, action 이 `work` 를 worker 에게 넘겼다는 뜻이다.
- `CLOSED` 는 마지막 Event 다. 그 action 의 Transition 은 무시된다. 그 뒤에 release 가 불린다.
- 표에 (state, event) 행이 없으면 `close(KIOTTY_UNHANDLED_EVENT)`. 조용히 무시하지 않는다. 정확 일치 행이 ANY 행보다 우선한다.
- `.to(...)` 는 action 이 갈 수 있는 다음 상태 목록이다. debug 빌드는 목록 밖으로 가면 `close(KIOTTY_ILLEGAL_TRANSITION)`. `.to()` 가 없는 행은 검사하지 않는다.
- `event.as.read.data` 는 수신 Stage 를 가리키며 action 이 return 하면 무효다. 보관하려면 Session 으로 복사한다.
- 표는 컴파일 타임 상수다. C++ 의 `KiottyNet_rules()` 는 (state, event) 로 정렬하고 중복 행을 컴파일 에러로 만든다. C 의 표는 `static const` 배열이며 run 이 시작할 때 중복을 검사해 `KIOTTY_DUPLICATE_RULE` 을 돌려준다. machine 이나 표가 NULL 이면 `KIOTTY_INVALID_ARGUMENT`.

공용 타입

```c
#define KIOTTY_ANY_STATE  (-1)
#define KIOTTY_MAX_NEXT   4

typedef enum KiottyNetConnectionEventType { KIOTTY_CONNECTION_EVENT_CONNECTED, KIOTTY_CONNECTION_EVENT_READ, KIOTTY_CONNECTION_EVENT_WRITTEN, KIOTTY_CONNECTION_EVENT_WORK_STARTED, KIOTTY_CONNECTION_EVENT_TIMEOUT, KIOTTY_CONNECTION_EVENT_CLOSED } KiottyNetConnectionEventType;

typedef struct KiottyNetConnectionEvent {
    KiottyNetConnectionEventType type;
    union {
        struct { KiottyNetSpan data; }       read;           /* return 하면 무효 */
        struct { size_t length; }            written;
        struct { KiottyNetWorkHandle work; } work_started;
        struct { KiottyNetCode reason; }     closed;
    } as;
} KiottyNetConnectionEvent;

typedef enum KiottyNetEntryActionType { KIOTTY_ENTRY_ACTION_NONE, KIOTTY_ENTRY_ACTION_READ, KIOTTY_ENTRY_ACTION_READ_UNTIL, KIOTTY_ENTRY_ACTION_WRITE, KIOTTY_ENTRY_ACTION_START_WORK, KIOTTY_ENTRY_ACTION_CLOSE } KiottyNetEntryActionType;

typedef struct KiottyNetEntryAction {
    KiottyNetEntryActionType type;
    uint32_t                 timeout_ms;      /* 0 == 없음. 만료되면 TIMEOUT. action 이 순수 함수인 모델에서 시간이 들어오는 유일한 입구 */
    union {
        struct { size_t exact; }                        read;
        struct { KiottyNetSpan delimiter; size_t max; } read_until;   /* delimiter 는 등록 시 복사된다 */
        struct { KiottyNetSpan data; }                  write;
        struct { KiottyNetCode reason; }                close;
    } as;
} KiottyNetEntryAction;

typedef struct KiottyNetTransition {
    int32_t              next;                /* 다음 상태 */
    KiottyNetEntryAction entry_action;
} KiottyNetTransition;

typedef KiottyNetTransition (*KiottyNetTransitionAction)(KiottyNetConnectionContext session, KiottyNetConnectionHandle c, KiottyNetConnectionEvent event);

typedef struct KiottyNetRule {
    int32_t                      state;                    /* 사용자 enum 값 또는 KIOTTY_ANY_STATE */
    KiottyNetConnectionEventType event;
    KiottyNetTransitionAction    transition_action;
    int32_t                      next[KIOTTY_MAX_NEXT];    /* 갈 수 있는 다음 상태. debug 에서 검사 */
    uint32_t                     next_count;               /* 0 이면 검사 안 함 */
} KiottyNetRule;

typedef struct KiottyNetStateMachine {
    const KiottyNetRule* rules;
    size_t               count;
    int32_t              initial;
} KiottyNetStateMachine;

typedef struct KiottyNetStatefulAppContext {
    KiottyNetAppContext          app_context;              /* create / release 가 받는다 */
    const KiottyNetStateMachine* machine;
    KiottyNetConnectionContext (*createConnectionContext)(KiottyNetAppContext app_context, KiottyNetConnectionHandle c);                                 /* 연결마다 Session 하나. NULL 이면 KIOTTY_SESSION_EXHAUSTED 로 닫는다 */
    void (*releaseConnectionContext)(KiottyNetAppContext app_context, KiottyNetConnectionContext session, KiottyNetConnectionHandle c);              /* CLOSED 행 뒤에 */
} KiottyNetStatefulAppContext;
```

C 함수. 표는 `static const KiottyNetRule[]` 이고 `KIOTTY_RULE` 매크로가 next 개수를 컴파일 타임에 센다 (C99 이상).

```c
#define KIOTTY_RULE(state, event, transition_action, ...)      { (state), (event), (transition_action), { __VA_ARGS__ }, (uint32_t)(sizeof((int32_t[]){ __VA_ARGS__ }) / sizeof(int32_t)) }
#define KIOTTY_RULE_UNCHECKED(state, event, transition_action) { (state), (event), (transition_action), { 0 }, 0 }
#define KIOTTY_RULE_COUNT(table)                               (sizeof(table) / sizeof((table)[0]))

KiottyNetEntryAction KiottyNet_makeNoEntryAction(void);
KiottyNetEntryAction KiottyNet_makeReadEntryAction(size_t exact);                               /* 정확히 exact 바이트가 모이면 READ 한 번 */
KiottyNetEntryAction KiottyNet_makeReadUntilEntryAction(KiottyNetSpan delimiter, size_t max);   /* delimiter 까지 포함해서 READ 한 번. max 안에 없으면 CLOSED(KIOTTY_PROTOCOL_ERROR) */
KiottyNetEntryAction KiottyNet_makeWriteEntryAction(KiottyNetSpan data);                        /* write lane 으로 복사. 완료되면 WRITTEN 한 번 */
KiottyNetEntryAction KiottyNet_makeStartWorkEntryAction(void);                                  /* 연결을 IO Thread 에서 떼어낸다. WORK_STARTED 로 온다 */
KiottyNetEntryAction KiottyNet_makeCloseEntryAction(KiottyNetCode reason);                      /* 종료. 마지막으로 CLOSED(reason) 한 번 */
KiottyNetEntryAction KiottyNet_setEntryActionTimeout(KiottyNetEntryAction entry_action, uint32_t ms);
KiottyNetTransition  KiottyNet_moveToNextState(int32_t next, KiottyNetEntryAction entry_action);

/* 7절 run 의 Stateful 판. 라이브러리의 handler 표와 app_context 를 KiottyNet_runServer / KiottyNet_runEventClient 에 넘긴다.
   표 검사에 실패하면 열지 않고 바로 돌아온다 (KIOTTY_DUPLICATE_RULE, KIOTTY_INVALID_ARGUMENT). 그 외의 반환과 close / await 는 7절과 같다 */
KiottyNetCode KiottyNet_runStatefulServer(KiottyNetServerHandle server, const KiottyNetStatefulAppContext* app_context);
KiottyNetCode KiottyNet_runStatefulEventClient(KiottyNetEventClientHandle client, const KiottyNetStatefulAppContext* app_context);
```

```c
/* 사용 예 */
static const KiottyNetRule HTTP_RULES[] = {
    KIOTTY_RULE(KIOTTY_ANY_STATE,    KIOTTY_CONNECTION_EVENT_CONNECTED,    on_connected, HTTP_READING_HEADER),
    KIOTTY_RULE(HTTP_READING_HEADER, KIOTTY_CONNECTION_EVENT_READ,         on_header,    HTTP_READING_BODY, HTTP_PROCESSING, HTTP_RESPONDING),
    KIOTTY_RULE_UNCHECKED(HTTP_PROCESSING, KIOTTY_CONNECTION_EVENT_WORK_STARTED, on_work_started),
    KIOTTY_RULE(HTTP_RESPONDING,     KIOTTY_CONNECTION_EVENT_WRITTEN,      on_responded, HTTP_READING_HEADER, HTTP_IDLE),
    KIOTTY_RULE_UNCHECKED(KIOTTY_ANY_STATE, KIOTTY_CONNECTION_EVENT_CLOSED, on_closed),
};
static const KiottyNetStateMachine HTTP_MACHINE = { HTTP_RULES, KIOTTY_RULE_COUNT(HTTP_RULES), HTTP_IDLE };

static HttpSession sessions[32];                                   /* Session 저장소는 사용자 것. 크기와 재사용 규칙도 사용자가 정한다 */
static KiottyNetConnectionContext acquire_session(KiottyNetAppContext app, KiottyNetConnectionHandle c);
static void                       release_session(KiottyNetAppContext app, KiottyNetConnectionContext session, KiottyNetConnectionHandle c);

HttpApp                     app = { ... };
KiottyNetStatefulAppContext stateful = { &app, &HTTP_MACHINE, acquire_session, release_session };   /* run 동안만 살아 있으면 된다 */
KiottyNetServerHandle       server = KiottyNet_createServer(context, &config, NULL);
KiottyNet_runStatefulServer(server, &stateful);
```

C++. 표를 읽기 쉽게 쓰고 컴파일 타임에 정렬 / 중복 검사하는 builder 만 있다. action 의 시그니처와 run 은 C 와 같다.

```cpp
class KiottyNet_EntryAction {                                     // KiottyNetEntryAction 의 얇은 래퍼. 암묵적으로 변환된다
public:
    KiottyNet_EntryAction();                                      // 기본값 = 없음
    static KiottyNet_EntryAction read(size_t exact);
    static KiottyNet_EntryAction readUntil(KiottyNetSpan delimiter, size_t max);
    static KiottyNet_EntryAction write(KiottyNetSpan data);
    static KiottyNet_EntryAction startWork();
    static KiottyNet_EntryAction close(KiottyNetCode reason = KIOTTY_SUCCESS);
    KiottyNet_EntryAction& timeout(uint32_t ms);
    operator KiottyNetEntryAction() const;
};

template <typename State>
struct KiottyNet_Transition {                                     // State enum 을 int32_t 로 바꿔 주는 이름 공간. 값은 C 의 KiottyNetTransition
    static KiottyNetTransition moveToNextState(State next, KiottyNetEntryAction entry_action = KiottyNetEntryAction());
};

// 표 작성. 한 행은 kiotty::net::Rule 이고 안에 KiottyNetRule 하나가 있다
//   KiottyNet_from(S).when(E).then(action).to(S1, S2)   이 (상태, 이벤트) 에서 action 이 돌고 S1 또는 S2 로 간다
//   KiottyNet_when(E).then(action)                      모든 상태에서 (ANY). 다음 상태 검사 없음
template <typename State> constexpr kiotty::net::From KiottyNet_from(State state);
constexpr kiotty::net::FromWhen KiottyNet_when(KiottyNetConnectionEventType event);

// 행들을 (state, event) 순으로 컴파일 타임 정렬한 KiottyNetRule 배열. 같은 (state, event) 가 두 번이면 컴파일 에러
template <typename... Rs> constexpr kiotty::net::Table<1 + sizeof...(Rs)> KiottyNet_rules(const kiotty::net::Rule& first, Rs... rest);

// 표 + 초기 상태 → C 의 KiottyNetStateMachine. 표는 static 이어야 한다 (포인터를 담는다)
template <size_t N, typename State> constexpr KiottyNetStateMachine KiottyNet_makeStateMachine(const kiotty::net::Table<N>& table, State initial);
```

사용 예

```cpp
enum class HttpState { Idle, ReadingHeader, ReadingBody, Processing, Responding };   // 사용자 enum 은 공개 타입이 아니므로 enum class 여도 된다
struct HttpSession { HttpApp* app; KiottyNetConnectionHandle connection; /* per-connection 데이터. state 멤버 없음 */ };
typedef KiottyNet_Transition<HttpState> Next;

static KiottyNetTransition onHeader(KiottyNetConnectionContext session, KiottyNetConnectionHandle c, KiottyNetConnectionEvent e)
{
    HttpSession* s = static_cast<HttpSession*>(session);
    // ...
    return Next::moveToNextState(HttpState::ReadingBody, KiottyNet_EntryAction::read(s->content_length).timeout(10000));
}

static constexpr auto HTTP_RULES = KiottyNet_rules(
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_CONNECTED)                                  .then(onConnected)  .to(HttpState::ReadingHeader),
    KiottyNet_from(HttpState::ReadingHeader).when(KIOTTY_CONNECTION_EVENT_READ)        .then(onHeader)     .to(HttpState::ReadingBody, HttpState::Processing, HttpState::Responding),
    KiottyNet_from(HttpState::ReadingBody)  .when(KIOTTY_CONNECTION_EVENT_READ)        .then(onBody)       .to(HttpState::Processing, HttpState::Responding),
    KiottyNet_from(HttpState::Processing)   .when(KIOTTY_CONNECTION_EVENT_WORK_STARTED).then(onWorkStarted),
    KiottyNet_from(HttpState::Responding)   .when(KIOTTY_CONNECTION_EVENT_WRITTEN)     .then(onResponded)  .to(HttpState::ReadingHeader, HttpState::Idle),
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_TIMEOUT)                                    .then(onTimeout),
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_CLOSED)                                     .then(onClosed));
static constexpr KiottyNetStateMachine HTTP_MACHINE = KiottyNet_makeStateMachine(HTTP_RULES, HttpState::Idle);

HttpApp                     app;
KiottyNetStatefulAppContext stateful = { &app, &HTTP_MACHINE, &HttpApp::acquireSession, &HttpApp::releaseSession };
KiottyNet_Context           context;
KiottyNet_Server            server(context.handle(), config);
std::thread                 io([&] { KiottyNet_runStatefulServer(server.handle(), &stateful); });   // stateful 은 io 가 끝날 때까지 살아 있다
```

### 9. HttpServer

HTTP/1.1 서버. 7절 Server 위에 라이브러리가 구현한 HTTP StateMachine 을 얹은 것이다. 사용자는 라우트를 등록하고 응답을 채운다.

- 라우트는 `KiottyNet_addHttpRoute(method, path, route)` 로 등록하며 C++ 의 `get / head / post / put / patch / del / options` 는 그 별칭이다 (`delete` 는 C++ 예약어라 `del`). 콜백을 등록하는 방식이지 하나의 handler 구조체를 넘기는 방식이 아니다. 등록은 `run` 전에만 되고 표는 고정 크기다 (`KiottyNetHttpLimits::max_routes`).
- path 매칭은 정확 일치와 `/*` 접미 wildcard 두 가지다. 등록 순서대로 검사한다. `:id` 같은 파라미터 파싱은 하지 않으며 `request.path` 를 사용자가 파싱한다.
- 정적 서빙은 세 가지다. 디렉토리 매핑, 파일 하나 매핑, 메모리 바이너리 매핑. 라우트와 같은 표에서 등록 순서대로 매칭하며 GET / HEAD 에만 적용된다.
- path 가 맞는 라우트가 하나도 없으면 `notFound` 라우트, 그것도 없으면 404. path 는 맞지만 method 가 없으면 405 다 (아래 메서드별 동작).
- 요청은 공용 struct `KiottyNetHttpRequest` 로 오고, 응답은 pool handle `KiottyNetHttpResponseHandle` 로 채운다. 둘 다 Connection 마다 고정 크기 버퍼(`KiottyNetHttpLimits`)에 있다. body 가 `max_body_bytes` 를 넘으면 413 으로 라이브러리가 거절한다. 응답 버퍼가 넘치면 setter 가 `KIOTTY_BUFFER_FULL` 을 돌려준다.
- 긴 작업은 `KiottyNet_deferHttpResponse` 로 Connection 을 suspend 하고, worker 가 결과를 만든 뒤 `KiottyNet_resumeConnection(request.connection, payload)` 를 부르면 IO Thread 에서 같은 라우트의 `resumed` 콜백이 호출된다. 응답 객체는 IO Thread 에서만 만지므로 worker 는 결과를 자기 메모리에 만들어 payload 로 넘긴다. defer 한 뒤 resumed 까지 request 는 유효하다.
- keep-alive 는 라이브러리가 처리한다. `Connection: close` 또는 HTTP/1.0 이면 응답 후 닫는다.

메서드별 동작

지원하는 메서드는 아래 7개다. 표의 "자동" 열은 사용자가 그 메서드 라우트를 등록하지 않았을 때 라이브러리가 하는 일이다. 사용자가 직접 등록하면 자동 처리는 하지 않는다.

| 메서드 | 용도 | 요청 body | 자동 |
|---|---|---|---|
| GET | 조회 | 없음 (와도 읽고 버린다) | 없음 |
| HEAD | GET 과 같은 헤더만 | 없음 | 같은 path 의 GET 라우트를 실행하고 body 는 보내지 않는다. Content-Length 는 GET 과 같게 둔다. 정적 서빙도 같다 |
| POST | 생성, 명령 | 있음 | 없음 |
| PUT | 전체 교체 | 있음 | 없음 |
| PATCH | 부분 수정 | 있음 | 없음 |
| DELETE | 삭제 | 보통 없음 (오면 `request.body` 로 준다) | 없음 |
| OPTIONS | 허용 메서드 조회, CORS preflight | 없음 | 204 + `Allow` 헤더. 값은 그 path 에 등록된 메서드 + HEAD (GET 이 있으면) + OPTIONS. `OPTIONS *` 는 서버 전체에 등록된 메서드 |

- **405 Method Not Allowed.** path 가 맞는 라우트나 정적 경로가 있지만 그 method 가 없으면 405 와 `Allow` 헤더 (OPTIONS 자동 응답과 같은 값) 를 보낸다. `notFound` 는 부르지 않는다.
- **501 Not Implemented.** CONNECT, TRACE, 그리고 7개 밖의 메서드. CONNECT 는 프록시용이고 TRACE 는 보안상 쓰지 않으므로 라우트로 등록할 수도 없다.
- **CORS.** 라이브러리는 CORS 헤더를 자동으로 붙이지 않는다. preflight 에 응답하려면 OPTIONS 라우트를 직접 등록하고 `Access-Control-Allow-*` 헤더를 넣는다 (미결: 간단한 CORS 설정 제공 여부).
- **body 를 읽는 기준은 메서드가 아니라 헤더다.** 어느 메서드든 `Content-Length` 가 있으면 그만큼 읽어서 연결의 다음 요청 경계를 맞춘다. `Transfer-Encoding: chunked` 요청은 라이브러리가 합쳐서 `request.body` 로 준다.

공용 타입

```c
typedef enum KiottyNetHttpMethod {
    KIOTTY_HTTP_GET,
    KIOTTY_HTTP_HEAD,
    KIOTTY_HTTP_POST,
    KIOTTY_HTTP_PUT,
    KIOTTY_HTTP_PATCH,
    KIOTTY_HTTP_DELETE,
    KIOTTY_HTTP_OPTIONS,
    KIOTTY_HTTP_METHOD_COUNT
} KiottyNetHttpMethod;
#define KIOTTY_HTTP_METHOD_BIT(method) (1u << (method))   /* 메서드 집합을 uint32_t 비트 합으로 다룰 때 (Allow 헤더 등) */

typedef struct KiottyNetHttpHeader { KiottyNetSpan name; KiottyNetSpan value; } KiottyNetHttpHeader;

typedef struct KiottyNetHttpRequest {
    KiottyNetConnectionHandle  connection;      /* resume 과 peer 정보 (KiottyNet_getConnectionInfo) 에 쓴다 */
    KiottyNetHttpMethod        method;
    KiottyNetSpan              path;            /* query 제외. percent-decoding 하지 않음 */
    KiottyNetSpan              query;           /* '?' 뒤. 없으면 길이 0 */
    const KiottyNetHttpHeader* headers;
    size_t                     header_count;    /* 최대 KiottyNetHttpLimits::max_headers */
    KiottyNetSpan              body;            /* Content-Length 만큼. 없으면 길이 0 */
    int                        keep_alive;
} KiottyNetHttpRequest;

typedef int64_t KiottyNetHttpResponseHandle;    /* pool handle. IO Thread 전용 */

typedef struct KiottyNetHttpRoute {
    void* user_context;
    void (*handle) (void* user_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response);
    void (*resumed)(void* user_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response, void* payload);   /* defer 한 뒤 resume 이 오면. defer 하지 않는 라우트는 NULL */
} KiottyNetHttpRoute;

typedef struct KiottyNetHttpLimits {
    size_t   max_routes;            /* 라우트 + 정적 경로 + 승격 경로 합계. 기본 64 */
    size_t   max_header_bytes;      /* request line + headers. 기본 8 KiB */
    size_t   max_headers;           /* 기본 32 */
    size_t   max_body_bytes;        /* 기본 64 KiB. 초과시 413 */
    size_t   response_bytes;        /* 응답 버퍼 (status + headers + 복사된 body). 기본 64 KiB */
    uint32_t header_timeout_ms;     /* 헤더 수신 완료까지. 기본 10000 */
    uint32_t keep_alive_timeout_ms; /* 다음 요청 대기. 기본 15000 */
} KiottyNetHttpLimits;
```

C 함수

```c
KiottyNetSpan KiottyNet_findHttpHeader(const KiottyNetHttpRequest* request, const char* name);   /* 대소문자 무시. 없으면 { NULL, 0 } */

KiottyNetCode KiottyNet_setHttpStatus(KiottyNetHttpResponseHandle r, int code);                                            /* 기본 200. 호출하지 않으면 404 */
KiottyNetCode KiottyNet_setHttpHeader(KiottyNetHttpResponseHandle r, const char* name, const char* value);                  /* Content-Length, Connection, Date 는 라이브러리가 채운다 */
KiottyNetCode KiottyNet_setHttpBody(KiottyNetHttpResponseHandle r, KiottyNetSpan data, const char* content_type);           /* 응답 버퍼로 복사 */
KiottyNetCode KiottyNet_setHttpBodyRef(KiottyNetHttpResponseHandle r, KiottyNetSpan data, const char* content_type);        /* 복사하지 않음. data 는 송신 완료까지 유효해야 한다 (정적 데이터, 메모리 바이너리) */
KiottyNetCode KiottyNet_setHttpFile(KiottyNetHttpResponseHandle r, const char* path, const char* content_type);             /* 파일을 chunk 로 스트리밍. 가능하면 sendfile / TransmitFile. 없으면 KIOTTY_NOT_FOUND */
void          KiottyNet_deferHttpResponse(KiottyNetHttpResponseHandle r);                                                   /* 긴 작업. handle 이 return 하면 Connection 은 suspend. resume 으로 돌아온다 */

typedef struct KiottyNetHttpServer* KiottyNetHttpServerHandle;
KiottyNetHttpServerHandle KiottyNet_createHttpServer(const KiottyNetServerConfig* config, const KiottyNetTlsServerConfig* tls, const KiottyNetHttpLimits* limits);
void          KiottyNet_destroyHttpServer(KiottyNetHttpServerHandle s);
KiottyNetCode KiottyNet_addHttpRoute(KiottyNetHttpServerHandle s, KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
KiottyNetCode KiottyNet_setHttpNotFound(KiottyNetHttpServerHandle s, KiottyNetHttpRoute route);                            /* 아무 라우트도 안 맞을 때 */
KiottyNetCode KiottyNet_serveHttpDirectory(KiottyNetHttpServerHandle s, const char* url_prefix, const char* directory);    /* "/static/" -> "./www/". ".." 포함 경로는 403. content type 은 확장자로 */
KiottyNetCode KiottyNet_serveHttpFile(KiottyNetHttpServerHandle s, const char* url_path, const char* file_path);           /* "/favicon.ico" -> "./www/favicon.ico" */
KiottyNetCode KiottyNet_serveHttpMemory(KiottyNetHttpServerHandle s, const char* url_path, KiottyNetSpan data, const char* content_type);   /* 메모리 바이너리. 복사하지 않으며 서버 생명 동안 유효해야 한다 */
KiottyNetCode KiottyNet_addWebSocketUpgrade(KiottyNetHttpServerHandle s, const char* url_path, KiottyNetWebSocketHandler handler, const KiottyNetWebSocketLimits* limits);   /* 10절 */
KiottyNetCode KiottyNet_runHttpServer(KiottyNetHttpServerHandle s);
void          KiottyNet_closeHttpServer(KiottyNetHttpServerHandle s);
KiottyNetCode KiottyNet_awaitHttpServer(KiottyNetHttpServerHandle s, uint64_t timeout_ms);
```

C++ 래퍼

```cpp
class KiottyNet_HttpResponse {                       // pool handle 의 비소유 래퍼. 콜백 안에서 handle 로 만든다
public:
    explicit KiottyNet_HttpResponse(KiottyNetHttpResponseHandle handle);
    KiottyNetCode status(int code);
    KiottyNetCode header(const char* name, const char* value);
    KiottyNetCode body(KiottyNetSpan data, const char* content_type);
    KiottyNetCode bodyRef(KiottyNetSpan data, const char* content_type);
    KiottyNetCode file(const char* path, const char* content_type);
    void          defer();
};

class KiottyNet_HttpServer {
public:
    KiottyNet_HttpServer(const KiottyNetServerConfig& config, const KiottyNetHttpLimits& limits);
    KiottyNet_HttpServer(const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls, const KiottyNetHttpLimits& limits);   // TLS

    // 라우트 등록. 두 가지 오버로드가 있다.
    //   KiottyNetHttpRoute : user_context + 함수 포인터. C 와 같은 모양
    //   함수 포인터        : capture 없는 람다가 여기로 온다. C 콜백 시그니처 그대로라서 변환 코드가 없다
    //                        server.get("/health", [](void*, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r) { KiottyNet_HttpResponse(r).status(200); });
    typedef void (*Handle) (void* user_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response);
    typedef void (*Resumed)(void* user_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response, void* payload);
    KiottyNetCode route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
    KiottyNetCode route(KiottyNetHttpMethod method, const char* path, Handle handle, Resumed resumed = nullptr, void* user_context = nullptr);
    // 메서드별 별칭 : route(KIOTTY_HTTP_XXX, ...) 와 같다. 각각 위와 같은 두 오버로드가 있다
    KiottyNetCode get    (const char* path, KiottyNetHttpRoute route);
    KiottyNetCode get    (const char* path, Handle handle, Resumed resumed = nullptr, void* user_context = nullptr);
    KiottyNetCode head   (const char* path, KiottyNetHttpRoute route);       // 등록하지 않으면 GET 라우트로 자동 처리
    KiottyNetCode head   (const char* path, Handle handle, Resumed resumed = nullptr, void* user_context = nullptr);
    KiottyNetCode post   (const char* path, KiottyNetHttpRoute route);
    KiottyNetCode post   (const char* path, Handle handle, Resumed resumed = nullptr, void* user_context = nullptr);
    KiottyNetCode put    (const char* path, KiottyNetHttpRoute route);
    KiottyNetCode put    (const char* path, Handle handle, Resumed resumed = nullptr, void* user_context = nullptr);
    KiottyNetCode patch  (const char* path, KiottyNetHttpRoute route);
    KiottyNetCode patch  (const char* path, Handle handle, Resumed resumed = nullptr, void* user_context = nullptr);
    KiottyNetCode del    (const char* path, KiottyNetHttpRoute route);       // DELETE
    KiottyNetCode del    (const char* path, Handle handle, Resumed resumed = nullptr, void* user_context = nullptr);
    KiottyNetCode options(const char* path, KiottyNetHttpRoute route);       // 등록하지 않으면 204 + Allow 자동 응답
    KiottyNetCode options(const char* path, Handle handle, Resumed resumed = nullptr, void* user_context = nullptr);
    KiottyNetCode notFound(KiottyNetHttpRoute route);

    KiottyNetCode serveDirectory(const char* url_prefix, const char* directory);
    KiottyNetCode serveFile(const char* url_path, const char* file_path);
    KiottyNetCode serveMemory(const char* url_path, KiottyNetSpan data, const char* content_type);
    KiottyNetCode upgrade(const char* url_path, KiottyNetWebSocketHandler handler, const KiottyNetWebSocketLimits& limits);

    KiottyNetCode run();
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);
};
```

사용 예

```cpp
KiottyNet_HttpServer server(config, limits);

server.get("/health", [](void*, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r) { KiottyNet_HttpResponse(r).status(200); });

// 긴 작업 : handle 에서 defer, worker 가 KiottyNet_resumeConnection(q->connection, result), resumed 에서 응답
server.get("/report",
    [](void* ctx, const KiottyNetHttpRequest* q, KiottyNetHttpResponseHandle r) {
        KiottyNet_HttpResponse(r).defer();
        static_cast<App*>(ctx)->workerPool.post(new_job(q->connection));
    },
    [](void*, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r, void* result) {
        KiottyNet_HttpResponse(r).body(static_cast<Result*>(result)->span(), "application/json");
    },
    &app);

server.serveDirectory("/", "./www/");
server.serveMemory("/app.wasm", wasm, "application/wasm");
server.upgrade("/ws", wsHandler, wsLimits);
std::thread io([&] { server.run(); });
server.await(3000);
```

### 10. WebSocket / WebSocketServer

WebSocket (RFC 6455) 은 HTTP Upgrade 로만 시작된다. 서버 쪽 진입점은 두 가지다.

- `KiottyNet_WebSocketServer` : WebSocket 만 쓰는 앱. 경로 하나와 handler 로 만든다. 내부는 승격 경로 하나만 등록한 HttpServer 이며 다른 요청은 404.
- `KiottyNet_HttpServer::upgrade(url_path, handler, limits)` : HTTP 라우트와 WebSocket 을 같이 쓰는 앱.

101 응답이 송신 완료되면 그 Connection 은 HTTP StateMachine 에서 WebSocket StateMachine 으로 넘어가고, 이후 HTTP 콜백은 오지 않는다.

- 수신 frame 의 조각(fragment)은 라이브러리가 합쳐서 `onMessage` 에 메시지 하나로 준다. `max_message_bytes` 를 넘으면 1009 로 close 한다.
- ping / pong 은 라이브러리가 처리한다. 들어온 ping 에는 pong 을 보내고, `ping_interval_ms` 마다 ping 을 보내며 `pong_timeout_ms` 안에 pong 이 없으면 1001 로 close 한다.
- `send` 는 frame 으로 감싸 write lane 으로 복사한다. 여유가 없으면 `KIOTTY_BUFFER_FULL` 이며 아무것도 보내지 않는다. 송신 frame 은 masking 하지 않는다 (서버).
- `close` 는 close handshake 를 시작한다. 상대의 close frame 이 오거나 `close_timeout_ms` 가 지나면 `onClose`.
- **다른 thread 의 push** 는 `KiottyNet_resumeWebSocket(ws, payload)` 다. IO Thread 에서 `onResumed(ctx, ws, payload)` 가 호출되고 거기서 send 한다. 다른 thread 에서 WebSocket 을 직접 만지지 않는다.

공용 타입

```c
typedef enum KiottyNetWebSocketOpcode { KIOTTY_WS_TEXT = 1, KIOTTY_WS_BINARY = 2 } KiottyNetWebSocketOpcode;

typedef struct KiottyNetWebSocketMessage {
    KiottyNetWebSocketOpcode opcode;
    KiottyNetSpan            data;      /* 수신 버퍼. onMessage 가 return 하면 무효 */
} KiottyNetWebSocketMessage;

typedef int64_t KiottyNetWebSocketHandle;   /* pool handle. Connection 위의 per-connection 객체. IO Thread 전용 */

typedef struct KiottyNetWebSocketHandler {
    void* user_context;
    int  (*onUpgrade)(void* user_context, const KiottyNetHttpRequest* request, KiottyNetWebSocketHandle ws);   /* 0 이면 403 으로 거절하고 HTTP 로 남는다. NULL 이면 모두 허용. 여기서만 KiottyNet_setWebSocketProtocol */
    void (*onOpen)   (void* user_context, KiottyNetWebSocketHandle ws);                                        /* 101 송신 완료 */
    void (*onMessage)(void* user_context, KiottyNetWebSocketHandle ws, const KiottyNetWebSocketMessage* message);
    void (*onResumed)(void* user_context, KiottyNetWebSocketHandle ws, void* payload);                         /* KiottyNet_resumeWebSocket(ws, payload). push 는 여기서 send */
    void (*onClose)  (void* user_context, KiottyNetWebSocketHandle ws, uint16_t code, KiottyNetCode reason);   /* code: close frame 의 status. 없으면 1006. 마지막 콜백 */
} KiottyNetWebSocketHandler;

typedef struct KiottyNetWebSocketLimits {
    size_t   max_message_bytes;     /* 조각을 합친 메시지 최대 크기. 기본 64 KiB. 초과시 1009 */
    uint32_t ping_interval_ms;      /* 0 이면 ping 을 보내지 않음. 기본 30000 */
    uint32_t pong_timeout_ms;       /* 기본 10000 */
    uint32_t close_timeout_ms;      /* close handshake 대기. 기본 5000 */
} KiottyNetWebSocketLimits;
```

C 함수

```c
KiottyNetConnectionInfo KiottyNet_getWebSocketInfo(KiottyNetWebSocketHandle ws);
void*         KiottyNet_getWebSocketContext(KiottyNetWebSocketHandle ws);                /* per-connection slot. 라이브러리는 건드리지 않는다 */
void          KiottyNet_setWebSocketContext(KiottyNetWebSocketHandle ws, void* context);
KiottyNetCode KiottyNet_setWebSocketProtocol(KiottyNetWebSocketHandle ws, const char* subprotocol);   /* onUpgrade 안에서만. Sec-WebSocket-Protocol. 요청에 없던 값이면 KIOTTY_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_sendWebSocket(KiottyNetWebSocketHandle ws, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
KiottyNetCode KiottyNet_closeWebSocket(KiottyNetWebSocketHandle ws, uint16_t code, const char* reason);   /* reason 은 123 바이트 이하 */
KiottyNetCode KiottyNet_resumeWebSocket(KiottyNetWebSocketHandle ws, void* payload);                     /* 아무 thread 에서나. 규칙은 KiottyNet_resumeConnection 과 같다 */

typedef struct KiottyNetWebSocketServer* KiottyNetWebSocketServerHandle;
KiottyNetWebSocketServerHandle KiottyNet_createWebSocketServer(const KiottyNetServerConfig* config, const KiottyNetTlsServerConfig* tls, const char* url_path, KiottyNetWebSocketHandler handler, const KiottyNetWebSocketLimits* limits);
void          KiottyNet_destroyWebSocketServer(KiottyNetWebSocketServerHandle s);
KiottyNetCode KiottyNet_runWebSocketServer(KiottyNetWebSocketServerHandle s);
void          KiottyNet_closeWebSocketServer(KiottyNetWebSocketServerHandle s);
KiottyNetCode KiottyNet_awaitWebSocketServer(KiottyNetWebSocketServerHandle s, uint64_t timeout_ms);
```

C++ 래퍼

```cpp
class KiottyNet_WebSocket {                          // pool handle 의 비소유 래퍼
public:
    explicit KiottyNet_WebSocket(KiottyNetWebSocketHandle handle);
    KiottyNetWebSocketHandle handle() const;         // 다른 thread 에 넘길 값
    KiottyNetConnectionInfo info() const;
    void*         context() const;
    void          setContext(void* context);
    KiottyNetCode protocol(const char* subprotocol); // onUpgrade 안에서만
    KiottyNetCode send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
    KiottyNetCode close(uint16_t code, const char* reason);
    KiottyNetCode resume(void* payload);             // 유일하게 다른 thread 에서 불러도 되는 메서드
};

class KiottyNet_WebSocketServer {
public:
    KiottyNet_WebSocketServer(const KiottyNetServerConfig& config, const char* url_path, KiottyNetWebSocketHandler handler, const KiottyNetWebSocketLimits& limits);
    KiottyNet_WebSocketServer(const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls, const char* url_path, KiottyNetWebSocketHandler handler, const KiottyNetWebSocketLimits& limits);   // TLS
    KiottyNetCode run();
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);
};
```

사용 예

```cpp
// IO Thread
void onOpen(void* ctx, KiottyNetWebSocketHandle ws)                                      { app.clients.add(ws); }
void onResumed(void* ctx, KiottyNetWebSocketHandle ws, void* payload)                    { KiottyNet_WebSocket(ws).send(KIOTTY_WS_TEXT, static_cast<Message*>(payload)->span()); }
void onClose(void* ctx, KiottyNetWebSocketHandle ws, uint16_t, KiottyNetCode)            { app.clients.remove(ws); }

KiottyNetWebSocketHandler handler = { &app, nullptr, onOpen, onMessage, onResumed, onClose };
KiottyNet_WebSocketServer server(config, "/ws", handler, wsLimits);

// 앱 thread : 센서 값이 바뀌면 모든 클라이언트에게 push
for (KiottyNetWebSocketHandle ws : app.clients.snapshot()) KiottyNet_WebSocket(ws).resume(&message);   // 이미 닫힌 handle 은 KIOTTY_PEER_CLOSED 를 돌려준다
```

### 11. HttpClient / WebSocketClient (초안)

이 절은 초안이다. "정말 간략하고 쓰기 쉬운" 인터페이스를 목표로 피드백을 받으며 다듬는다. 열린 질문은 절 끝에 있다.

**HttpClient.** 6절 StreamClient 위에 얹은 동기 API 다. IO Thread 가 없고 호출한 thread 에서 보내고 기다린다.

- 첫 요청에서 연결하고 keep-alive 로 유지한다. 서버가 끊었으면 (`KIOTTY_PEER_CLOSED`) 다음 요청에서 다시 연결한다. 사용자는 연결을 신경 쓰지 않는다.
- 응답은 client 안의 고정 크기 버퍼에 들어가고 `KiottyNetHttpReply` 는 그 버퍼를 가리킨다. **다음 요청까지만 유효하다.** 보관하려면 복사한다.
- 응답 body 가 `max_body_bytes` 를 넘으면 `KIOTTY_BUFFER_FULL` 이고 연결을 끊는다. chunked 응답은 라이브러리가 합친다.
- redirect, 쿠키, 압축, 인증은 하지 않는다. 필요한 헤더는 사용자가 넣는다. 매 요청에 같은 헤더 (`Authorization` 등) 를 넣어야 하면 `KiottyNet_setHttpClientHeader` 로 한 번 등록한다.

메서드별 함수. 자주 쓰는 4개 (GET, POST, PUT, DELETE) 와 PATCH, HEAD, OPTIONS 에 전용 함수가 있다. 전용 함수에 없는 조합 (요청마다 다른 헤더, body 있는 DELETE 등) 은 `KiottyNet_requestHttp` 하나로 한다.

| 메서드 | C | C++ | body 인자 | 응답에서 볼 것 |
|---|---|---|---|---|
| GET | `KiottyNet_getHttp` | `get` | 없음 | status, body |
| POST | `KiottyNet_postHttp` | `post` | 있음 | status, body |
| PUT | `KiottyNet_putHttp` | `put` | 있음 | status, body |
| DELETE | `KiottyNet_deleteHttp` | `del` | 없음 | status |
| PATCH | `KiottyNet_patchHttp` | `patch` | 있음 | status, body |
| HEAD | `KiottyNet_headHttp` | `head` | 없음 | status, headers. body 는 항상 길이 0 이고 라이브러리는 body 를 읽으려 하지 않는다 |
| OPTIONS | `KiottyNet_optionsHttp` | `options` | 없음 | `KiottyNet_getHttpAllowedMethods` 로 Allow 헤더를 비트 합으로 |

C 의 `headHttp`, `optionsHttp` 는 HTTP 메서드 이름을 그대로 동사로 쓴 것이다 (naming 규약 1번의 예외).

```c
typedef struct KiottyNetHttpClientLimits {
    size_t   max_header_bytes;      /* 응답 status line + headers. 기본 8 KiB */
    size_t   max_headers;           /* 기본 32 */
    size_t   max_body_bytes;        /* 응답 body. 기본 64 KiB */
    size_t   request_bytes;         /* 요청 버퍼 (request line + headers). body 는 복사하지 않고 그대로 보낸다. 기본 8 KiB */
    size_t   default_header_bytes;  /* setHttpClientHeader 로 등록한 헤더 저장 공간. 기본 1 KiB */
} KiottyNetHttpClientLimits;

typedef struct KiottyNetHttpReply {         /* 값 + code (2절). C++ 도 이 struct 를 그대로 돌려준다. code != KIOTTY_SUCCESS 면 나머지 필드는 0. 다음 요청까지 유효 */
    KiottyNetCode              code;
    int                        status;
    const KiottyNetHttpHeader* headers;
    size_t                     header_count;
    KiottyNetSpan              body;
} KiottyNetHttpReply;

typedef struct KiottyNetHttpClient* KiottyNetHttpClientHandle;
KiottyNetHttpClientHandle KiottyNet_createHttpClient(const KiottyNetClientConfig* config, const KiottyNetTlsClientConfig* tls, const KiottyNetHttpClientLimits* limits);   /* endpoint.host 가 Host 헤더가 된다 */
void          KiottyNet_destroyHttpClient(KiottyNetHttpClientHandle c);

/* 요청 하나. headers 는 사용자 배열 (NULL, 0 가능). body.length == 0 이면 body 없음. timeout_ms 는 연결 + 송신 + 응답 수신 전체 */
KiottyNetHttpReply KiottyNet_requestHttp(KiottyNetHttpClientHandle c, KiottyNetHttpMethod method, const char* path,
                                         const KiottyNetHttpHeader* headers, size_t header_count,
                                         KiottyNetSpan body, const char* content_type, uint32_t timeout_ms);
/* 메서드별. requestHttp(c, KIOTTY_HTTP_XXX, path, NULL, 0, body, content_type, timeout_ms) 와 같다 */
KiottyNetHttpReply KiottyNet_getHttp    (KiottyNetHttpClientHandle c, const char* path, uint32_t timeout_ms);
KiottyNetHttpReply KiottyNet_postHttp   (KiottyNetHttpClientHandle c, const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_ms);
KiottyNetHttpReply KiottyNet_putHttp    (KiottyNetHttpClientHandle c, const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_ms);
KiottyNetHttpReply KiottyNet_deleteHttp (KiottyNetHttpClientHandle c, const char* path, uint32_t timeout_ms);
KiottyNetHttpReply KiottyNet_patchHttp  (KiottyNetHttpClientHandle c, const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_ms);
KiottyNetHttpReply KiottyNet_headHttp   (KiottyNetHttpClientHandle c, const char* path, uint32_t timeout_ms);
KiottyNetHttpReply KiottyNet_optionsHttp(KiottyNetHttpClientHandle c, const char* path, uint32_t timeout_ms);   /* path 가 "*" 면 서버 전체 */

KiottyNetSpan KiottyNet_findHttpReplyHeader(const KiottyNetHttpReply* reply, const char* name);    /* 대소문자 무시. 없으면 { NULL, 0 } */
uint32_t      KiottyNet_getHttpAllowedMethods(const KiottyNetHttpReply* reply);                    /* Allow 헤더를 KIOTTY_HTTP_METHOD_BIT 의 합으로. 헤더가 없으면 0 */

/* 모든 요청에 붙는 헤더. 같은 name 을 다시 부르면 값을 바꾸고, value == NULL 이면 지운다.
   name + value 합이 KiottyNetHttpClientLimits::default_header_bytes 를 넘으면 KIOTTY_BUFFER_FULL */
KiottyNetCode KiottyNet_setHttpClientHeader(KiottyNetHttpClientHandle c, const char* name, const char* value);
void          KiottyNet_disconnectHttpClient(KiottyNetHttpClientHandle c);   /* 명시적으로 끊고 싶을 때. 다음 요청에서 다시 연결된다 */
```

```cpp
class KiottyNet_HttpClient {
public:
    KiottyNet_HttpClient(const KiottyNetClientConfig& config, const KiottyNetHttpClientLimits& limits);
    KiottyNet_HttpClient(const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls, const KiottyNetHttpClientLimits& limits);   // TLS
    KiottyNetHttpReply request(KiottyNetHttpMethod method, const char* path, const KiottyNetHttpHeader* headers, size_t header_count,
                               KiottyNetSpan body, const char* content_type, uint32_t timeout_ms);
    KiottyNetHttpReply get    (const char* path, uint32_t timeout_ms);
    KiottyNetHttpReply post   (const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_ms);
    KiottyNetHttpReply put    (const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_ms);
    KiottyNetHttpReply del    (const char* path, uint32_t timeout_ms);                    // DELETE
    KiottyNetHttpReply patch  (const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_ms);
    KiottyNetHttpReply head   (const char* path, uint32_t timeout_ms);
    KiottyNetHttpReply options(const char* path, uint32_t timeout_ms);
    KiottyNetCode setHeader(const char* name, const char* value);           // 모든 요청에 붙는 헤더
    void          disconnect();
};
```

```cpp
KiottyNet_HttpClient client(config, limits);
KiottyNetHttpReply   reply = client.get("/api/status", 3000);
if (reply.code == KIOTTY_SUCCESS && reply.status == 200) parse(reply.body);

client.setHeader("Authorization", "Bearer abc123");                        // 이후 모든 요청에 붙는다
client.put("/api/devices/7", json, "application/json", 3000);
client.del("/api/devices/7", 3000);

KiottyNetHttpReply allow = client.options("/api/devices", 3000);
if (allow.code == KIOTTY_SUCCESS && (KiottyNet_getHttpAllowedMethods(&allow) & KIOTTY_HTTP_METHOD_BIT(KIOTTY_HTTP_PATCH))) { /* PATCH 지원 */ }
```

**WebSocketClient.** 6절 EventClient 위에 10절과 같은 `KiottyNetWebSocketHandler` 를 올린 것이다. 서버와 같은 콜백을 쓰므로 서버 쪽 코드를 그대로 옮길 수 있다.

- `run` 이 연결, HTTP Upgrade 핸드셰이크, loop 를 모두 한다. 101 을 받으면 `onOpen` 이고 그때부터 `await` 가 `KIOTTY_SUCCESS` 다. `onUpgrade` 는 클라이언트에서 호출되지 않는다 (NULL 로 둔다).
- 송신 frame 은 masking 한다 (클라이언트). 나머지 규칙(조각 합치기, ping / pong, close handshake, limits)은 10절과 같다.
- 앱 thread 에서 보내려면 서버와 같이 `KiottyNet_resumeWebSocket(ws, payload)` 로 IO Thread 에 넘기고 `onResumed` 에서 send 한다.

```c
typedef struct KiottyNetWebSocketClient* KiottyNetWebSocketClientHandle;
KiottyNetWebSocketClientHandle KiottyNet_createWebSocketClient(const KiottyNetClientConfig* config, const KiottyNetTlsClientConfig* tls, const char* url_path, KiottyNetWebSocketHandler handler, const KiottyNetWebSocketLimits* limits);
void          KiottyNet_destroyWebSocketClient(KiottyNetWebSocketClientHandle c);
KiottyNetCode KiottyNet_runWebSocketClient(KiottyNetWebSocketClientHandle c);                          /* 연결 + Upgrade + loop */
void          KiottyNet_closeWebSocketClient(KiottyNetWebSocketClientHandle c);                        /* close handshake 없이 끊는다. 정상 종료는 KiottyNet_closeWebSocket */
KiottyNetCode KiottyNet_awaitWebSocketClient(KiottyNetWebSocketClientHandle c, uint64_t timeout_ms);
KiottyNetWebSocketHandle KiottyNet_getWebSocketClientSocket(KiottyNetWebSocketClientHandle c);   /* onOpen 전이면 0. 앱 thread 의 resume 에 쓴다 */
```

```cpp
class KiottyNet_WebSocketClient {
public:
    KiottyNet_WebSocketClient(const KiottyNetClientConfig& config, const char* url_path, KiottyNetWebSocketHandler handler, const KiottyNetWebSocketLimits& limits);
    KiottyNet_WebSocketClient(const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls, const char* url_path, KiottyNetWebSocketHandler handler, const KiottyNetWebSocketLimits& limits);   // TLS
    KiottyNetCode run();
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);
    KiottyNetWebSocketHandle socket() const;
};
```

열린 질문

1. WebSocketClient 를 Event 모델 대신 **동기 모델** (`send(opcode, data, timeout)`, `receive(message, timeout)`, IO Thread 없음) 로 만드는 쪽이 더 단순하다. 서버 push 를 받으려면 receive 를 polling 해야 한다는 것이 단점이다. 둘 다 둘지, 하나만 둘지.
2. HttpClient 의 응답 이름. 서버의 `HttpResponse` (사용자가 채우는 것) 와 구분하려고 `HttpReply` (사용자가 읽는 것) 로 두었다.
3. HttpClient 에 비동기 버전(EventClient 위, 콜백으로 응답)이 필요한지. 동기 버전만으로 시작하는 것을 제안한다.
4. `request_bytes` 버퍼 없이 요청 헤더를 바로 write lane 에 쓰면 버퍼 하나가 준다. 대신 ClientConfig 의 `send_capacity` 가 요청 크기 상한이 된다.

미결

- **8, 9, 10, 11절의 suspend / resume / onResumed / defer 는 5절 Work 로 바뀐다. 아직 고치지 않았다.** StateMachine 은 `Request::suspend()` + `RESUMED` 가 `Request::startWork()` + `WORK_STARTED(work)` 로, HttpServer 는 `defer` + `resumed` 콜백이 HTTP 전용 Work (`finishWithBody` 등) 로, WebSocket 의 push 는 `startWork` + `onWorkStarted` 에서 `send` 로 바뀐다.
- WebSocket 연결이 Work 중일 때도 ping / pong 과 close frame 은 IO Thread 가 계속 처리한다 (라이브러리 내부 로직이고 사용자 콜백이 아니므로 5절 4번 규칙과 어긋나지 않는다). 송신 control frame 은 push lane 으로 보낸다 (4절). 데이터 frame 은 finish 뒤에 `onMessage` 로 낸다. 10절을 고칠 때 반영한다.
- pool handle 을 소유자 pool 로 되돌리는 방법 (전역 소유자 표 vs index 상위 비트에 소유자 번호). 구현 가이드에서 정한다.
- HttpServer 라우트에 capture 있는 람다 / functor 를 받는 template 오버로드. 얇은 래퍼 규칙 때문에 뺐다. 필요하면 `KiottyNetHttpRoute::user_context` 로 대신한다.
- WebSocketServer 와 HttpServer::upgrade 를 둘 다 둘지, 하나로 합칠지.
- HttpServer 에 간단한 CORS 설정 (허용 origin 목록 하나로 preflight 자동 응답 + 헤더 자동 추가) 을 둘지.
- 조각을 합치지 않고 그대로 전달하는 streaming 모드 (`max_message_bytes` 보다 큰 메시지).
- permessage-deflate 확장은 지원하지 않는다.

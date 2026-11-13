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
10. 서버의 성능은 고성능이 아니라 일반 PC 또는 모바일 디바이스에서 서버 하나에 기본 1024 Client (`max_clients` 로 1 ~ 65536 을 실행 시간에 정한다) 를 지원하는 것으로 가정하고 있음. 서버와 클라이언트의 개수에는 상한이 없다.
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
    kiotty_net_base.h          // 1~7절. C interface. C++ interface는 C++로 build 시 포함됨.
    kiotty_net_statemachine.h  // 8절
    kiotty_net_http.h          // 9절
    kiotty_net_websocket.h     // 10절, 11절의 WebSocketClient
src/ // 
    kiotty_net_base.cpp, kiotty_net_statemachine.cpp, kiotty_net_http.cpp, kiotty_net_websocket.cpp // 각 헤더의 구현
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

- `include/` 의 헤더 네 개 (`kiotty_net_base.h`, `kiotty_net_statemachine.h`, `kiotty_net_http.h`, `kiotty_net_websocket.h`) 에 C 인터페이스가 있고, C++ 로 빌드하면 같은 헤더가 C++ 인터페이스를 함께 노출한다.
- 두 벌 모두 런타임 동적 할당이 없다. 객체 생성(`KiottyNet_create*`) 시점에 `max_clients` 분량을 한 번 잡고 재사용한다.
- **thread 규칙.** 모든 콜백은 IO Thread 에서 순서대로 호출되고, Connection 과 HttpResponse 는 IO Thread 에서만 쓴다.
  다른 thread 가 할 수 있는 일은 `KiottyNet_notifyConnection`, `KiottyNet_getConnectionInfo`, Work 를 받은 thread 의 `KiottyNet_*ConnectionStream` (5절) 과 HttpResponse setter, `KiottyNet_finishHttpWork*` (9절), WebSocket 의 `send` / `close` (10절), 소유자의 `close()`, `await()`, `broadcast` / `notify` 뿐이다.
- 각 절은 **공용 타입 → C 함수 → C++ 래퍼** 순서로 적는다. 이름은 1:1 로 대응한다.
- **문자열 수명.** 설정 struct (ServerConfig, ClientConfig, TLS 설정) 안의 문자열은 포인터가 아니라 최대 길이가 정해진 `char[]` 에 값으로 들어간다. 사용자는 initialize / configure 함수 (3절) 로 넣고, 그 함수가 길이를 검사하며 복사한다. 함수의 `const char*` 인자는 따로 적지 않는 한 함수가 return 하기 전에 라이브러리 안으로 복사된다. 어느 쪽이든 호출자의 문자열(`std::string` 등)은 호출 뒤 사라져도 된다.

naming 규약 (C, C++ 공통)

1. 함수 이름은 CamelCase 다. snake_case 를 쓰지 않는다. C 함수는 `KiottyNet_` 뒤에 **타동사 + 명사** 를 소문자로 시작하는 CamelCase 로 잇는다 (`KiottyNet_createServer`, `KiottyNet_notifyConnection`, `KiottyNet_readConnectionStream`). 콜백이 돌려주는 action 을 만드는 함수만 `KiottyNet_then*` (`KiottyNet_thenRead`) 로, return 한 뒤에 일어날 일을 이름에 담는다. C++ 멤버 함수도 소문자로 시작한다 (`bufferCapacity`, `writeFile`).
2. 클래스 이름은 첫 글자가 대문자인 CamelCase 다.
3. 사용자에게 공개되는 C++ 인터페이스는 별도의 namespace 를 쓰지 않고 `KiottyNet_` 접두어를 붙인다 (`KiottyNet_Server`, `KiottyNet_Connection`, `KiottyNet_Transition<State>`).
4. C 인터페이스는 모두 `KiottyNet_` 접두어를 붙인다. 함수와 매크로(`KIOTTY_NET_RULE`)가 여기에 해당한다. C 가 쓰는 struct, enum, handle 은 7, 8번을 따른다.
5. 사용자에게 공개되는 C++ 클래스는 template class 가 아닌 이상 C 인터페이스의 얇은 래퍼다. 생성자 / 소멸자로 RAII 와 메모리 라이프사이클(생성, 파괴, 소유권 이동)을 책임지고, 그 외 멤버 함수는 대응하는 C 함수 하나를 부르는 것으로 끝난다. 로직은 C 인터페이스 아래에만 있다.
6. 사용자에게 공개되지 않고 내부 구현에서만 쓰는 타입과 함수는 아래 namespace 에 둔다. 접두어는 붙이지 않는다.
   ```cpp
   namespace kiotty
   {
       namespace net {
       }
   }
   ```
7. handle 이 아닌 단순 struct (`KiottyNetSpan`, `KiottyNetAddress`, 설정, 콜백 구조체 등) 와 공개 enum 은 C, C++ 이 같은 정의를 공용으로 쓴다. 한 벌만 정의하고 양쪽에서 같은 이름으로 쓴다. 이름은 `KiottyNet` 바로 뒤에 언더스코어 없이 잇는다 (`KiottyNetServerConfiguration`, `KiottyNetCode`). 그래서 외부로 공개되는 enum 은 `enum class` 를 쓰지 않으며 (C 에서 쓸 수 없다), enumerator 는 `KIOTTY_` 접두어로 전역 이름 충돌을 막는다.
   정리하면 **`KiottyNet_` 는 함수 또는 C++ 래퍼 클래스, `KiottyNet` 바로 뒤에 이름이 붙으면 C / C++ 공용 데이터 타입**이다.
8. handle 은 두 종류이며 둘 다 공용 타입이다.
   - **소유 handle.** 사용자가 `KiottyNet_createX` 로 만들고 `KiottyNet_destroyX` 로 없애는 객체. StreamClient, EventClient, Server, HttpServer, HttpClient, WebSocketClient 가 여기에 속한다. `typedef struct KiottyNetServer* KiottyNetServerHandle;` 처럼 불투명 포인터다. C++ 래퍼는 생성자에서 create, 소멸자에서 destroy 를 부르고, 복사는 없고 이동만 있다.
   - **pool handle.** 소유자의 pool 안에 할당되는 per-connection 객체. Connection, ConnectionStream, WebSocket, HttpResponse, HttpWork 가 여기에 속한다. 외부에서는 소유자 포인터와 정수 id 를 나란히 둔 struct 다 (`KiottyNetConnectionHandle { KiottyNetEndpoint* owner; int64_t id; }`). `owner` 는 그 연결을 가진 Server / EventClient 의 불투명 포인터이고 `id` 는 내부에서 `{ int32_t generation; int32_t index; }` 로 해석한다. `owner == NULL` 또는 `id == 0` 은 무효 (`KIOTTY_NET_CODE_INVALID_ARGUMENT`). 연결이 닫히면 generation 이 바뀌므로 같은 slot 이라도 id 가 달라지고, 닫힌 handle 로 부른 함수는 아무 일도 하지 않는다 (코드를 돌려주는 함수는 `KIOTTY_NET_CODE_PEER_CLOSED`). 소유자를 destroy 해도 `owner` 포인터는 `releaseContext` 전까지 유효하다 (은퇴할 뿐 해제되지 않는다). 그래서 handle 은 복사해 두어도 되고 다른 thread 에 넘겨도 된다. C 에서 두 handle 이 같은지는 `owner` 와 `id` 둘을 비교한다. C++ 래퍼는 소유하지 않는 값 타입이며 콜백 안에서 handle 로 만들고 `operator==` / `operator<` 가 있어 map 의 key 가 된다. HttpResponse 와 HttpWork 의 handle 은 같은 모양의 `{ KiottyNetHttpServerHandle server; int64_t id; }` 이다 (9절). WebSocket 의 handle 도 같은 모양의 `{ KiottyNetWebSocketEndpoint* owner; int64_t id; }` 이다 (10절. owner 는 HttpServer 또는 WebSocketClient). 예외는 `KiottyNet_ConnectionStream` 과 `KiottyNet_HttpWork` 둘이다. Stream 은 "close 할 의무" 를 소유하므로 이동만 되고, 소멸자가 close 하지 않은 Stream 을 `KiottyNet_closeConnectionStream(stream, KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS))` 로 닫는다 (5절). HttpWork 도 "finish 할 의무" 를 소유하며 소멸자가 `finishWithClose()` 를 부른다 (9절).

공통 타입

```c
typedef int32_t KiottyNetBool;        /* C ABI 의 참/거짓. 0 이 거짓, 1 이 참. C 의 bool 은 쓰지 않는다 (수칙 5.7, C# 마샬링) */

typedef struct KiottyNetSpan {        /* 읽기 전용 바이트 범위. 텍스트와 바이너리 모두 이것으로 받는다. 소유하지 않는다 */
    const char* data;
    size_t      length;
} KiottyNetSpan;

typedef struct KiottyNetEndpoint KiottyNetEndpoint;   /* 불투명. 연결을 가진 Server / EventClient */
typedef struct KiottyNetConnectionHandle {   /* pool handle (규약 8). 다른 thread 에 넘겨도 되는 유일한 Connection 표현 */
    KiottyNetEndpoint* owner;         /* NULL 이면 무효 */
    int64_t                   id;            /* 상위 32 generation, 하위 16 slot. 0 이면 무효 */
} KiottyNetConnectionHandle;

/* 설정 struct 안 문자열 버퍼의 크기 (NUL 포함). struct 크기가 곧 ABI 이므로 사용자가 바꿀 수 없다 */
#define KIOTTY_NET_MAX_HOST_LENGTH     256   /* hostname (DNS 최대 253), Windows pipe 이름 (최대 256), Unix domain 경로 (sun_path 104~108) */
#define KIOTTY_NET_MAX_PATH_LENGTH     260   /* 인증서, key, CA 경로. Windows MAX_PATH 기준. 더 긴 경로는 상대 경로로 쓴다 */
#define KIOTTY_NET_MAX_PASSWORD_LENGTH 128   /* key 파일 암호 */
```

```cpp
namespace kiotty { namespace net {
struct PoolHandle {                   // handle 의 id 의 내부 해석. int64_t 와 같은 크기. 소유자는 handle 의 owner 포인터가 곧 가리킨다 (설계 v4 13절)
    int32_t generation;
    int32_t index;
};
} }
```

### 1. Error 처리

C, C++ 공용 타입이다 (naming 규약 7번). `enum class` 가 아니며 enumerator 는 `KIOTTY_` 접두어를 가진다.

```c
typedef enum KiottyNetCode {
    KIOTTY_NET_CODE_SUCCESS,
    KIOTTY_NET_CODE_PEER_CLOSED,          /* 상대가 FIN 을 보냄. notify / Stream 함수: 그 handle 의 연결이 이미 닫힘 */
    KIOTTY_NET_CODE_SOCKET_ERROR,         /* OS 소켓 에러 */
    KIOTTY_NET_CODE_PROTOCOL_ERROR,       /* readUntil 의 max 안에 delimiter 가 없음, 프로토콜 파서 실패 */
    KIOTTY_NET_CODE_REQUEST_TOO_LARGE,    /* read 요구 길이가 수신 Stage capacity 보다 큼 */
    KIOTTY_NET_CODE_UNHANDLED_EVENT,      /* StateMachine 표에 (state, event) 행이 없음 */
    KIOTTY_NET_CODE_ILLEGAL_TRANSITION,   /* StateMachine action 이 .to() 목록에 없는 상태로 감. release 빌드에서도 검사한다 */
    KIOTTY_NET_CODE_SESSION_EXHAUSTED,    /* StateMachine 의 createConnectionContext 가 NULL 을 돌려줌 (Session 이 다 닳음). onError 로 알리고 연결을 받지 않는다 */
    KIOTTY_NET_CODE_DUPLICATE_RULE,       /* C StateMachine 표에 같은 (state, event) 가 두 번 */
    KIOTTY_NET_CODE_TIMEOUT,              /* 동기 API 의 timeout 만료 */
    KIOTTY_NET_CODE_TABLE_FULL,           /* 라우트 / 정적 경로 / 승격 경로 등록 표가 가득 참 */
    KIOTTY_NET_CODE_BUFFER_FULL,          /* write 메시지가 send_capacity 보다 큼. push lane 이나 응답 버퍼에 여유가 없음 */
    KIOTTY_NET_CODE_NOT_FOUND,            /* 파일 없음 */
    KIOTTY_NET_CODE_INVALID_ARGUMENT,
    KIOTTY_NET_CODE_CLOSED,               /* close 로 loop 가 끝남. run 과 await 의 반환값 */
    KIOTTY_NET_CODE_BUSY,                 /* Stream 함수: 이미 close 한 Stream, IO Thread 에서 부름. onError: 요청이 걸린 채 온 onTimeout 이 WRITE / START_WORK 를 return 함 */
    KIOTTY_NET_CODE_TLS_NOT_SUPPORTED,          /* TLS 없이 빌드한 라이브러리에 TLS 설정을 넘김 */
    KIOTTY_NET_CODE_TLS_CERTIFICATE_NOT_FOUND,  /* create : 인증서, key, CA 파일 없음 */
    KIOTTY_NET_CODE_TLS_CERTIFICATE_INVALID,    /* create : 파싱 실패, key 불일치, 암호 틀림 */
    KIOTTY_NET_CODE_TLS_VERIFY_FAILED,          /* handshake : 상대 인증서 검증 실패 */
    KIOTTY_NET_CODE_TLS_HOSTNAME_MISMATCH,      /* handshake : 클라이언트의 hostname 검증 실패 */
    KIOTTY_NET_CODE_TLS_HANDSHAKE_FAILED,       /* handshake : 버전 / cipher 불일치, alert, 평문 상대 */
    KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT,
    KIOTTY_NET_CODE_TLS_RECORD_ERROR,           /* handshake 뒤 : 복호화 실패 */
    KIOTTY_NET_CODE_OUT_OF_MEMORY,              /* create* : 소유자 하나를 만들 메모리가 없음. 그 소유자의 실행 중에는 나오지 않는다 */
    KIOTTY_NET_CODE_INVALID_ENCODING,           /* 문자열 인자 (host, pipe 이름, 경로, password) 가 UTF-8 이 아님 */
    KIOTTY_NET_CODE_PEER_NOT_READING            /* 상대가 읽지 않아 send_timeout 안에 송신이 끝나지 않음. onDisconnected 의 reason (4절) */
} KiottyNetCode;
```

### 2. 예외 처리

무조건 반환값으로 알린다. 예외를 던지지 않고 출력 인자(output parameter)도 쓰지 않는다.

- 돌려줄 값이 없으면 `KiottyNetCode` 를 돌려준다.
- 돌려줄 값이 있으면 값과 `code` 필드를 함께 가진 공용 struct 를 값으로 돌려준다 (`KiottyNetResult`, `KiottyNetHttpReply`). C++ 래퍼도 같은 struct 를 그대로 돌려준다.
- 메모리가 보장되는 시점은 `KiottyNet_create*` 다. 소유자 (Server, StreamClient, EventClient. 공통 타입은 `KiottyNetEndpoint`) 하나를 만들 때 그 소유자가 실행 중에 쓸 것 (Server 는 `max_clients` 만큼의 연결과 버퍼, 완료 배열) 을 한 번에 잡는다. 만들었으면 그 소유자의 실행 중에 메모리 부족은 생기지 않는다. 할당은 표준 라이브러리로만 하고 라이브러리는 예외를 끄고 빌드하므로, 잡지 못하면 process 가 끝난다 (`KIOTTY_NET_CODE_OUT_OF_MEMORY` 를 돌려주지 않는다). 소유자의 개수에는 상한이 없다. 소유자를 destroy 해도 그 메모리는 OS 에 돌려주지 않고 같은 타입의 다음 create 가 재사용한다 (Server 는 더 큰 `max_clients` 가 올 때만 연결 풀을 다시 잡는다). `KiottyNet_releaseContext` 도 메모리를 유지하고 다음 `initializeContext` 뒤의 create 가 다시 쓴다. 연결 하나의 버퍼는 설정한 capacity (`receive / send / push_capacity`) 만큼 create 때 잡는다 (TLS 설정이 있으면 암호문 버퍼 둘을 더 잡는다). capacity 의 상한은 라이브러리 상수다: 수신 64 KiB, 송신 16 KiB, push 16 KiB. 빌드 설정으로 바꾸지 않는다. `KiottyNet_serverMemorySize(configuration)` 와 `KiottyNet_clientMemorySize(configuration)` 가 그 설정의 소유자 하나가 잡는 평문 크기를 알려준다. 잡은 메모리는 쓰기 전까지 OS 가 실제 RAM 에 올리지 않는다 (Windows 의 "커밋 크기" 는 잡은 크기를 센다. 줄이려면 `max_clients` 를 낮춘다).
- Context 는 process 에 하나다. `KiottyNet_initializeContext` 를 두 번 부르면 같은 handle 과 `KIOTTY_NET_CODE_SUCCESS` 이고, `KiottyNet_releaseContext` 한 번이 둘 다 끝낸다.
- handle 을 만드는 함수 (`KiottyNet_initializeContext`, `KiottyNet_create*`) 는 `{ code, handle }` struct (`KiottyNetServerResult` 등) 를 돌려준다. 실패하면 handle 이 NULL 이고 code 가 이유다. C++ 래퍼는 `explicit operator bool` 과 `code()` 로 보여준다 (판 v1, 2026-11-09. 전에는 `valid()`). HttpServer 와 WebSocketClient 의 create 도 같다 (`KiottyNetHttpServerResult`, `KiottyNetWebSocketClientResult`. 9, 11절).
- 내부 구현은 `KiottyResult<Value, Code>` 를 쓰지만 공개 인터페이스 (C, C++ 모두) 에는 나오지 않는다.

```c
typedef struct KiottyNetResult {
    int32_t       bytes;   /* 전달 또는 송신한 길이. 에러면 0 */
    KiottyNetCode code;
} KiottyNetResult;

typedef struct KiottyNetContextImpl* KiottyNetContext;
typedef struct KiottyNetContextResult { KiottyNetCode code; KiottyNetContext handle; } KiottyNetContextResult;
KiottyNetContextResult KiottyNet_initializeContext(void);
void                   KiottyNet_releaseContext(KiottyNetContext context);

size_t KiottyNet_serverMemorySize(const KiottyNetServerConfiguration* configuration);
size_t KiottyNet_clientMemorySize(const KiottyNetClientConfiguration* configuration);
```

```cpp
class KiottyNet_Context {                            // KiottyNet_initializeContext / KiottyNet_releaseContext. 복사 없음, 이동만
public:
    KiottyNet_Context();
    KiottyNet_Context(KiottyNet_Context&& other);
    KiottyNet_Context& operator=(KiottyNet_Context&& other);
    ~KiottyNet_Context();
    explicit operator bool() const;
    KiottyNetCode    code() const;
    KiottyNetContext handle() const;
};
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
    KIOTTY_NET_TLS_VERSION_V12 = 1 << 0,
    KIOTTY_NET_TLS_VERSION_V13 = 1 << 1
} KiottyNetTlsVersion;

typedef enum KiottyNetTlsV12CipherSuite {
    KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_AES256_GCM_SHA384 = 1 << 0,
    KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_AES256_GCM_SHA384   = 1 << 1,
    KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_CHACHA20_POLY1305 = 1 << 2,
    KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_CHACHA20_POLY1305   = 1 << 3
} KiottyNetTlsV12CipherSuite;

typedef enum KiottyNetTlsV13CipherSuite {
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_GCM_SHA256       = 1 << 0,
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_256_GCM_SHA384       = 1 << 1,
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_CHACHA20_POLY1305_SHA256 = 1 << 2,
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM_SHA256       = 1 << 3,
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM8_SHA256      = 1 << 4
} KiottyNetTlsV13CipherSuite;
```

4. 설정 struct 와 설정 함수. 아래 5, 6번의 모든 설정 (ServerConfig, ClientConfig, TlsServerConfig, TlsClientConfig) 이 같은 규칙을 따른다. 사용 방법은 하나뿐이다. **initialize 한 번, 필요한 configure 여러 번, 각 호출의 반환 코드 확인.**

   - 설정 struct 는 사용자가 잡는다 (스택, static). 필드는 읽어도 되지만 값은 함수로만 넣는다. 함수가 값을 검사하고 문자열을 길이 검사하며 복사하므로 사용자는 길이나 복사를 신경 쓰지 않는다.
   - `KiottyNet_initializeXxx` 는 모든 필드를 기본값으로 채운다. 기본값을 정할 수 없는 필수 값 (주소의 host / port, TLS 서버의 인증서) 은 initialize 의 인자로 받는다. 그래서 **initialize 가 성공한 설정은 그대로 써도 유효하다.** 별도의 validate 는 없다.
   - `KiottyNet_configureXxx` 는 항목 하나를 바꾼다. 서로 묶여야 하는 값 (클라이언트 인증 방식과 trust store 등) 은 한 configure 의 인자로 같이 받는다. 그래서 configure 하나가 성공하면 설정 전체도 여전히 유효하다.
   - 모든 함수는 `KiottyNetCode` 를 돌려준다. 값이 잘못되면 struct 를 바꾸지 않고 에러를 돌려준다. 잘못된 값은 `KIOTTY_NET_CODE_INVALID_ARGUMENT` (NULL, 빈 문자열, 최대 길이 초과, 모르는 비트, 0 이면 안 되는 값), 없는 파일이나 디렉토리는 `KIOTTY_NET_CODE_NOT_FOUND` 다.
   - struct 의 `initialization_mark` 는 initialize 가 쓰는 표식이다. initialize 하지 않은 struct 를 configure 하거나 create / run / connect 에 넘기면 `KIOTTY_NET_CODE_INVALID_ARGUMENT` (create 는 handle NULL) 다. `KiottyNetAddress` 는 표식이 없고 run / connect 가 값을 다시 본다 (5번).
   - C++ 의 `KiottyNet_XxxConfigurationBuilder` 는 외부 struct 를 참조로 받아서 생성 시점에 initialize 를 부르고, 각 메서드는 configure 하나를 부른다. 처음 실패한 코드를 기억해 두었다가 `build()` 가 돌려준다.

5. 주소와 Server / Client 설정. 설정에는 자원 (연결 수, 버퍼, timeout, 재시도) 만 있고 **주소와 TLS 는 없다.** 주소와 TLS 는 run / connect 가 받는다 (`design_http_v0.html` 2.3, 결정 18). 그래서 create 때 자원을 전부 잡아 둔 endpoint 하나로 실행 중에 다른 상대, 다른 TLS 설정에 붙을 수 있다.
   create 함수는 설정 struct 를 통째로 복사해 두므로 설정 struct 는 create 뒤 사라져도 된다. run / connect 는 주소를 복사해 두므로 주소 struct 는 호출 동안만 살아 있으면 된다.
   run / connect 는 주소를 다시 본다: transport 가 TCP / PIPE 중 하나, host 가 비지 않고 배열 안에서 NUL 로 끝남, TCP 면 port 가 0 이 아님. 어긋나면 소켓을 열지 않고 `KIOTTY_NET_CODE_INVALID_ARGUMENT`.

```c
typedef enum KiottyNetTransport { KIOTTY_NET_TRANSPORT_TCP, KIOTTY_NET_TRANSPORT_PIPE } KiottyNetTransport;

typedef struct KiottyNetAddress {
    KiottyNetTransport transport;
    char               host[KIOTTY_NET_MAX_HOST_LENGTH];   /* Tcp: ip 또는 hostname. Pipe: pipe 이름 (Windows: \\.\pipe\name, Unix: 경로) */
    uint16_t           port;                           /* Pipe 면 0 */
} KiottyNetAddress;

typedef enum KiottyNetNoDelay {                        /* TCP_NODELAY. C ABI 에 bool 을 쓰지 않으므로 enum 이다 (수칙 5.7) */
    KIOTTY_NET_NO_DELAY_OFF,
    KIOTTY_NET_NO_DELAY_ON
} KiottyNetNoDelay;

typedef struct KiottyNetServerConfiguration {
    uint32_t          initialization_mark;
    size_t            max_clients;      /* 기본 1024, 1 ~ 65536. 동시 연결 수. create 때 이만큼의 연결을 잡는다 */
    size_t            receive_capacity;    /* 기본 4 KiB, 상한 64 KiB. Connection 당 수신 Stage 크기. create 때 이만큼 잡는다 */
    size_t            send_capacity;    /* 기본 4 KiB, 상한 16 KiB. Connection 당 write lane 크기 = write 메시지 하나의 최대 길이 (4절) */
    size_t            push_capacity;    /* 기본 4 KiB, 상한 16 KiB. Connection 당 push lane 크기. broadcast 와 WebSocket 의 송신 frame (send, ping / pong, close) 이 쌓이는 곳. frame 하나의 상한이기도 하다 (4절, 10절) */
    KiottyNetNoDelay  no_delay;         /* 기본 KIOTTY_NET_NO_DELAY_ON. TCP_NODELAY. KIOTTY_NET_TRANSPORT_PIPE 는 무시 */
    uint32_t          send_timeout_milliseconds;   /* 기본 30000. 걸린 송신이 이 시간 안에 끝나지 않으면 KIOTTY_NET_CODE_PEER_NOT_READING 으로 끊는다. 0 은 무한 (4절) */
} KiottyNetServerConfiguration;

typedef struct KiottyNetClientConfiguration {
    uint32_t          initialization_mark;
    size_t            receive_capacity;     /* 기본 4 KiB */
    size_t            send_capacity;     /* 기본 4 KiB */
    size_t            push_capacity;     /* 기본 4 KiB. notifyEventClient 가 쌓이는 곳. StreamClient 는 쓰지 않는다 */
    uint32_t          connect_timeout_milliseconds;/* 기본 5000 */
    uint32_t          max_trial;         /* 기본 1. 연결 시도 횟수 */
    uint32_t          trial_interval_milliseconds; /* 기본 1000. 시도 간격 */
    KiottyNetNoDelay  no_delay;          /* 기본 KIOTTY_NET_NO_DELAY_ON. KIOTTY_NET_TRANSPORT_PIPE 는 무시 */
    uint32_t          send_timeout_milliseconds;   /* 기본 30000. EventClient 만 쓴다. StreamClient 는 호출마다 timeout 을 받는다 */
} KiottyNetClientConfiguration;

KiottyNetCode KiottyNet_initializeTcpAddress(KiottyNetAddress* address, const char* host, uint16_t port);   /* port != 0 */
KiottyNetCode KiottyNet_initializePipeAddress(KiottyNetAddress* address, const char* pipe_name);

KiottyNetCode KiottyNet_initializeServerConfiguration(KiottyNetServerConfiguration* configuration);
KiottyNetCode KiottyNet_configureServerMaxClients(KiottyNetServerConfiguration* configuration, size_t max_clients);                /* 0 또는 65536 초과면 KIOTTY_NET_CODE_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureServerBufferCapacity(KiottyNetServerConfiguration* configuration, size_t receive_capacity, size_t send_capacity, size_t push_capacity);   /* 0 이거나 상한 (수신 64 KiB, 송신 16 KiB, push 16 KiB) 초과면 KIOTTY_NET_CODE_INVALID_ARGUMENT. Client 도 같다 */
KiottyNetCode KiottyNet_configureServerNoDelay(KiottyNetServerConfiguration* configuration, KiottyNetNoDelay no_delay);            /* 모르는 값이면 KIOTTY_NET_CODE_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureServerSendTimeout(KiottyNetServerConfiguration* configuration, uint32_t timeout_milliseconds);    /* 0 은 무한 */

KiottyNetCode KiottyNet_initializeClientConfiguration(KiottyNetClientConfiguration* configuration);
KiottyNetCode KiottyNet_configureClientBufferCapacity(KiottyNetClientConfiguration* configuration, size_t receive_capacity, size_t send_capacity, size_t push_capacity);
KiottyNetCode KiottyNet_configureClientConnectTimeout(KiottyNetClientConfiguration* configuration, uint32_t timeout_milliseconds);
KiottyNetCode KiottyNet_configureClientRetry(KiottyNetClientConfiguration* configuration, uint32_t max_trial, uint32_t trial_interval_milliseconds);   /* max_trial >= 1 */
KiottyNetCode KiottyNet_configureClientNoDelay(KiottyNetClientConfiguration* configuration, KiottyNetNoDelay no_delay);
KiottyNetCode KiottyNet_configureClientSendTimeout(KiottyNetClientConfiguration* configuration, uint32_t timeout_milliseconds);
```

```cpp
class KiottyNet_ServerConfigurationBuilder {
public:
    explicit KiottyNet_ServerConfigurationBuilder(KiottyNetServerConfiguration& configuration);   // KiottyNet_initializeServerConfiguration
    KiottyNet_ServerConfigurationBuilder& maxClients(size_t max_clients);
    KiottyNet_ServerConfigurationBuilder& bufferCapacity(size_t receive_capacity, size_t send_capacity, size_t push_capacity);
    KiottyNet_ServerConfigurationBuilder& noDelay(KiottyNetNoDelay no_delay);
    KiottyNet_ServerConfigurationBuilder& sendTimeout(uint32_t timeout_milliseconds);
    KiottyNetCode build() const;                                                                                  // 처음 실패한 코드
};

class KiottyNet_ClientConfigurationBuilder {
public:
    explicit KiottyNet_ClientConfigurationBuilder(KiottyNetClientConfiguration& configuration);   // KiottyNet_initializeClientConfiguration
    KiottyNet_ClientConfigurationBuilder& bufferCapacity(size_t receive_capacity, size_t send_capacity, size_t push_capacity);
    KiottyNet_ClientConfigurationBuilder& connectTimeout(uint32_t timeout_milliseconds);
    KiottyNet_ClientConfigurationBuilder& retry(uint32_t max_trial, uint32_t trial_interval_milliseconds);
    KiottyNet_ClientConfigurationBuilder& noDelay(KiottyNetNoDelay no_delay);
    KiottyNet_ClientConfigurationBuilder& sendTimeout(uint32_t timeout_milliseconds);
    KiottyNetCode build() const;
};
```

6. TLS 설정. ServerConfig / ClientConfig 와 **따로** run / connect 에 넘긴다 (`const KiottyNetTls*Configuration* tls`, 평문이면 NULL). 같은 주소에 http:// 와 https:// 로 붙는 것처럼 run / connect 마다 바꿔도 된다.
   C++ 래퍼는 NULL 포인터 대신 run / connect 의 오버로드 두 개를 둔다. TLS 설정을 `const&` 로 받는 것이 TLS, 받지 않는 것이 평문이다.
   run / connect 는 이 struct 로 SSL_CTX 를 **매번 새로** 만들고 (인증서 파일을 읽는다) 그 뒤로는 struct 를 보지 않는다. 같은 경로의 인증서 파일이 바뀌어도 다음 run / connect 가 새 파일을 읽는다. connect 는 return 뒤, run 은 `await` 가 `KIOTTY_NET_CODE_SUCCESS` 를 돌려준 뒤 struct 를 버려도 된다. SSL_CTX 와 연결마다의 SSL 은 OpenSSL 이 자기 할당자로 잡는다 (런타임 할당 0 의 예외).
   endpoint 는 TLS 를 쓰든 안 쓰든 TLS Stage 를 create 때 최대치로 잡아 둔다 (TLS 를 넣어 빌드한 경우). TLS 를 빼고 빌드하면 TLS Stage 는 없고, tls 를 넘기면 `KIOTTY_NET_CODE_TLS_NOT_SUPPORTED`.

   | 항목 | Server 기본값 | Client 기본값 |
   |---|---|---|
   | 버전 | 1.2 + 1.3 | 1.2 + 1.3 |
   | cipher suite | 전부 | 전부 |
   | 신원 (인증서, key) | initialize 인자 (필수) | 없음 |
   | 상대 검증 | 클라이언트 인증 안 함 | 서버 검증 + hostname 검증. trust store 는 OS 기본 저장소 |

```c
typedef enum KiottyNetClientAuthentication {       /* 서버가 클라이언트 신원을 요구하는 정도 */
    KIOTTY_NET_CLIENT_AUTHENTICATION_NONE,
    KIOTTY_NET_CLIENT_AUTHENTICATION_OPTIONAL,         /* 인증서를 주면 검증, 없으면 통과 */
    KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED
} KiottyNetClientAuthentication;

typedef enum KiottyNetTlsVerification {            /* 클라이언트가 서버를 검증하는 정도. 0 인 첫 값이 가장 안전하다 */
    KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME,   /* 기본. 인증서 체인 + hostname. Pipe 면 hostname 은 보지 않는다 */
    KIOTTY_NET_TLS_VERIFICATION_SERVER,                /* 인증서 체인만 */
    KIOTTY_NET_TLS_VERIFICATION_NONE                   /* 개발용 */
} KiottyNetTlsVerification;

typedef struct KiottyNetTlsIdentity {    /* 자기 신원. 빈 문자열 == 지정 안 함 */
    char certificate_file[KIOTTY_NET_MAX_PATH_LENGTH];
    char key_file[KIOTTY_NET_MAX_PATH_LENGTH];
    char key_password[KIOTTY_NET_MAX_PASSWORD_LENGTH];
} KiottyNetTlsIdentity;

typedef struct KiottyNetTlsServerConfiguration {
    uint32_t             initialization_mark;
    uint32_t             versions;           /* KiottyNetTlsVersion 비트 합 */
    uint32_t             v12_cipher_suites;  /* KiottyNetTlsV12CipherSuite 비트 합 */
    uint32_t             v13_cipher_suites;  /* KiottyNetTlsV13CipherSuite 비트 합 */
    KiottyNetTlsIdentity identity;
    KiottyNetClientAuthentication  client_authentication;
    char                 trust_store_path[KIOTTY_NET_MAX_PATH_LENGTH];
} KiottyNetTlsServerConfiguration;

typedef struct KiottyNetTlsClientConfiguration {
    uint32_t             initialization_mark;
    uint32_t             versions;
    uint32_t             v12_cipher_suites;
    uint32_t             v13_cipher_suites;
    KiottyNetTlsVerification verification;             /* 기본 KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME */
    char                 trust_store_path[KIOTTY_NET_MAX_PATH_LENGTH];   /* 빈 문자열이면 OS 기본 저장소 */
    KiottyNetTlsIdentity identity;
} KiottyNetTlsClientConfiguration;

KiottyNetCode KiottyNet_initializeTlsServerConfiguration(KiottyNetTlsServerConfiguration* configuration, const char* certificate_file, const char* key_file, const char* key_password);   /* key_password 는 NULL 가능 */
KiottyNetCode KiottyNet_configureTlsServerVersions(KiottyNetTlsServerConfiguration* configuration, uint32_t versions);                  /* KiottyNetTlsVersion 비트 합. 0 이거나 모르는 비트면 KIOTTY_NET_CODE_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureTlsServerV12CipherSuites(KiottyNetTlsServerConfiguration* configuration, uint32_t suites);              /* 0 이거나 모르는 비트면 KIOTTY_NET_CODE_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureTlsServerV13CipherSuites(KiottyNetTlsServerConfiguration* configuration, uint32_t suites);
KiottyNetCode KiottyNet_configureTlsServerClientAuthentication(KiottyNetTlsServerConfiguration* configuration, KiottyNetClientAuthentication mode, const char* trust_store_path);   /* mode == NONE 이면 trust_store_path 는 NULL */

KiottyNetCode KiottyNet_initializeTlsClientConfiguration(KiottyNetTlsClientConfiguration* configuration);
KiottyNetCode KiottyNet_configureTlsClientVersions(KiottyNetTlsClientConfiguration* configuration, uint32_t versions);
KiottyNetCode KiottyNet_configureTlsClientV12CipherSuites(KiottyNetTlsClientConfiguration* configuration, uint32_t suites);
KiottyNetCode KiottyNet_configureTlsClientV13CipherSuites(KiottyNetTlsClientConfiguration* configuration, uint32_t suites);
KiottyNetCode KiottyNet_configureTlsClientTrustStore(KiottyNetTlsClientConfiguration* configuration, const char* trust_store_path);          /* OS 기본 저장소 대신 CA 파일 또는 디렉토리 */
KiottyNetCode KiottyNet_configureTlsClientVerification(KiottyNetTlsClientConfiguration* configuration, KiottyNetTlsVerification verification);   /* KIOTTY_NET_TLS_VERIFICATION_NONE 은 개발용. 모르는 값이면 KIOTTY_NET_CODE_INVALID_ARGUMENT */
KiottyNetCode KiottyNet_configureTlsClientIdentity(KiottyNetTlsClientConfiguration* configuration, const char* certificate_file, const char* key_file, const char* key_password);   /* 서버가 클라이언트 인증을 요구할 때 */
```

```cpp
class KiottyNet_TlsServerConfigurationBuilder {
public:
    KiottyNet_TlsServerConfigurationBuilder(KiottyNetTlsServerConfiguration& configuration, const char* certificate_file, const char* key_file, const char* key_password = nullptr);
    KiottyNet_TlsServerConfigurationBuilder& versions(uint32_t versions);
    KiottyNet_TlsServerConfigurationBuilder& v12CipherSuites(uint32_t suites);
    KiottyNet_TlsServerConfigurationBuilder& v13CipherSuites(uint32_t suites);
    KiottyNet_TlsServerConfigurationBuilder& clientAuthentication(KiottyNetClientAuthentication mode, const char* trust_store_path);
    KiottyNetCode build() const;
};

class KiottyNet_TlsClientConfigurationBuilder {
public:
    explicit KiottyNet_TlsClientConfigurationBuilder(KiottyNetTlsClientConfiguration& configuration);
    KiottyNet_TlsClientConfigurationBuilder& versions(uint32_t versions);
    KiottyNet_TlsClientConfigurationBuilder& v12CipherSuites(uint32_t suites);
    KiottyNet_TlsClientConfigurationBuilder& v13CipherSuites(uint32_t suites);
    KiottyNet_TlsClientConfigurationBuilder& trustStore(const char* trust_store_path);
    KiottyNet_TlsClientConfigurationBuilder& verification(KiottyNetTlsVerification verification);
    KiottyNet_TlsClientConfigurationBuilder& identity(const char* certificate_file, const char* key_file, const char* key_password = nullptr);
    KiottyNetCode build() const;
};
```

사용 예

```cpp
KiottyNetServerConfiguration config;
KiottyNetCode code = KiottyNet_ServerConfigurationBuilder(config)
    .maxClients(16)
    .build();
if (code != KIOTTY_NET_CODE_SUCCESS) { }

KiottyNetAddress address;
code = KiottyNet_initializeTcpAddress(&address, "0.0.0.0", 8443);

KiottyNetTlsServerConfiguration tls;
code = KiottyNet_TlsServerConfigurationBuilder(tls, settings.certPath().c_str(), settings.keyPath().c_str())   // std::string 도 그대로. 함수 안에서 복사된다
    .versions(KIOTTY_NET_TLS_VERSION_V13)
    .clientAuthentication(KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED, "./ca/")
    .build();
if (code != KIOTTY_NET_CODE_SUCCESS) { }

KiottyNet_Server server(context.handle(), config);       // 자원만 잡는다
server.run(address, tls, handler, &app);                   // TLS. close 뒤 server.run(address, handler, &app) 이면 평문

KiottyNetClientConfiguration    client;
KiottyNetTlsClientConfiguration client_tls;
code = KiottyNet_ClientConfigurationBuilder(client).retry(3, 500).build();
code = KiottyNet_TlsClientConfigurationBuilder(client_tls).build();                 // 기본값 그대로: OS 저장소로 서버 + hostname 검증
KiottyNetAddress device;
code = KiottyNet_initializeTcpAddress(&device, "device.local", 8443);             // host 가 SNI 와 hostname 검증의 이름이 된다
```

```c
KiottyNetServerConfiguration config;
if (KiottyNet_initializeServerConfiguration(&config) != KIOTTY_NET_CODE_SUCCESS) { }
if (KiottyNet_configureServerMaxClients(&config, 16) != KIOTTY_NET_CODE_SUCCESS) { }
KiottyNetAddress address;
if (KiottyNet_initializeTcpAddress(&address, "0.0.0.0", 8443) != KIOTTY_NET_CODE_SUCCESS) { }

KiottyNetTlsServerConfiguration tls;
if (KiottyNet_initializeTlsServerConfiguration(&tls, cert_path, key_path, NULL) != KIOTTY_NET_CODE_SUCCESS) { }
if (KiottyNet_configureTlsServerVersions(&tls, KIOTTY_NET_TLS_VERSION_V13) != KIOTTY_NET_CODE_SUCCESS) { }
if (KiottyNet_configureTlsServerClientAuthentication(&tls, KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED, "./ca/") != KIOTTY_NET_CODE_SUCCESS) { }

KiottyNetServerResult server = KiottyNet_createServer(context, &config);
KiottyNetCode         ran    = KiottyNet_runServer(server.handle, &address, &tls, &handler, &app);   /* 평문이면 tls 자리에 NULL */
/* 예: KIOTTY_NET_CODE_TLS_CERTIFICATE_NOT_FOUND. bind 전에 돌아오고 await 도 같은 값 */
```

### 4. Stage

Connection 하나는 고정 크기 버퍼 셋을 가진다. 모두 라이브러리 내부 객체이며 C 인터페이스로 노출하지 않는다 (naming 규약 6번). 생성 시점에 `max_clients` 분량을 잡고 런타임 할당은 없다.

| 버퍼 | 크기 | 들어가는 것 | 쓰는 쪽 | 읽는 쪽 |
|---|---|---|---|---|
| 수신 Stage | `receive_capacity` | recv 한 바이트 | IO Thread | IO Thread (onRead 전달) |
| write lane | `send_capacity` | WRITE action (콜백의 return 또는 `closeConnectionStream`) 의 메시지 **하나** | RUNNING 이면 IO Thread, WORKING 이면 worker. 동시에 둘인 순간은 없다 | IO Thread (send) |
| push lane | `push_capacity` | `notifyConnection`, `notifyEventClient`, `broadcastServer`, WebSocket 의 send / broadcast / ping / pong / close frame. `[u32 길이][바이트]` 가 여러 개 | IO Thread | IO Thread (send) |

- **수신 Stage.** IO Thread 는 read 요청이 있든 없든 계속 recv 해서 쌓고, READ / READ_UNTIL action 은 "이 조건이 되면 onRead 한 번" 이라는 조건 등록일 뿐이다. 수신 흐름(ReadRequest, pump)은 구현 가이드 1절에 있다.
- **write lane.** 사용자의 프로토콜 흐름이다. 한 번에 메시지 하나만 들어 있고 (`write → onWritten → write`), 그래서 메시지 경계를 추적하는 큐가 없다. 다 나가면 `onWritten(length)` 한 번. 다음 action 은 onWritten 이 return 하므로 이전 메시지가 남은 채 write 가 걸리는 일은 없다. 메시지가 `send_capacity` 보다 크면 `close(KIOTTY_NET_CODE_BUFFER_FULL)`.
- **push lane.** 사용자의 프로토콜 흐름과 무관하게 다른 thread 나 라이브러리가 끼워 넣는 바이트다. 넣는 동작의 이름은 **notify** 다. 완료 콜백이 없다. 여유가 없으면 그 notify 는 들어가지 않고 연결은 그대로 둔다. 끊을지는 호출자가 정한다. 두 lane 을 나눈 이유는, 사용자가 자기 흐름을 지켜도 broadcast 가 몰리면 자기 write 가 BUFFER_FULL 로 끊기는 일을 막기 위해서다. push lane 은 IO Thread 혼자 쓰고 읽으므로 Work 상태와 무관하게 Work 중인 연결에도 들어간다.
- **어느 lane 으로 보낼까.** 송신이 프로토콜 흐름의 한 단계이면 (요청에 대한 응답처럼, 다 보낸 뒤에 다음 단계로 가야 하면) write 다. 흐름과 무관하게 아무 때나 나가는 것이면 (서버 알림, WebSocket 메시지처럼 수신 상태와 상관없는 송신이면) notify 다. 전이중 프로토콜에서 수신 흐름을 건드리지 않고 보내는 길이 notify 다.
- **송신 순서.** IO Thread 가 연결마다 송신 하나 (WRITE) 를 건다. 한 WRITE 에는 그때 write lane 에 있는 것 전부 **다음에** push lane 에 있는 것 전부가 실린다 (버퍼 둘을 한 번에 보내는 gather). 그래서 같은 WRITE 안에서는 write lane 이 먼저이고, push lane 이 계속 차도 write lane 이 밀리지 않는다. 걸린 WRITE 가 끝나야 다음 WRITE 를 건다. Work 중인 연결에서는 아무것도 보내지 않는다 (write lane 은 worker 것이고, push lane 은 쌓였다가 Work 가 끝난 뒤 나간다). TLS 에서도 같은 순서로 암호화한다. 구현은 구현 가이드 1.6.
- **송신 timeout.** 걸린 WRITE 가 `send_timeout_milliseconds` (기본 30000) 안에 끝나지 않으면 (상대가 읽지 않아 상대 수신 창과 OS 송신 버퍼가 가득 차면) 그 WRITE 를 취소하고 `KIOTTY_NET_CODE_PEER_NOT_READING` 으로 끊는다. 콜백 API 는 `onDisconnected` 의 reason, StateMachine 은 CLOSED 이벤트의 reason 으로 받는다. 0 은 무한이다. 끊긴 상대는 자기가 가진 session 정보로 다시 연결해 이어 간다. Work 중 worker 의 동기 write 는 대상이 아니다 (호출마다 자기 timeout 이 있다). push lane 이 차서 notify 가 `KIOTTY_NET_CODE_BUFFER_FULL` 을 돌려주는 동안에도 그 연결의 WRITE 는 걸려 있으므로, 읽지 않는 상대는 결국 이 timeout 으로 끊긴다.

`Stage` 는 수신 Stage 의 구현이다. push lane 도 같은 클래스로 만든다.

```cpp
namespace kiotty {

class Stage
{
public:
    explicit Stage(size_t capacity);          // 0 이면 잡지 않는다. 할당 실패면 valid() == false
    ~Stage();

    class Provider {                          // 쓰는 쪽 (recv 완료, write 요청). Stage 를 가리키는 값
        size_t writable() const;              // 현재 쓸 수 있는 길이
        char*  prepare(size_t requested);     // requested 만큼 쓸 수 있으면 버퍼, 아니면 nullptr
        void   commit(size_t written);        // 실제 쓴 길이. writable 을 넘기면 debug assert, release clamp
    };

    class Consumer {                          // 읽는 쪽 (onRead 전달, send 제출)
        size_t readable() const;              // 현재 읽을 수 있는 길이
        char*  peek();                        // 읽을 자리. commit 전까지는 같은 자리
        void   commit(size_t consumed);       // 실제 소비한 길이. readable 을 넘기면 debug assert, release clamp
    };

    bool     valid() const;
    Provider provider();
    Consumer consumer();
    size_t   capacity() const;
    void     compact();                       // 남은 데이터를 버퍼 앞으로 memmove 해서 provider 공간 확보. 걸린 IO 가 없을 때만

private:
    char*  _buffer;
    size_t _capacity;
    size_t _begin_pos;                        // 데이터가 시작하는 index
    size_t _end_pos;                          // 데이터가 끝나는 index
    bool   _valid;
};

}
```

### 5. Connection

- 비동기 Socket IO 모델에서 연결을 추상화하는 객체. EventClient, Server 의 pool 안에 있고 밖에서는 `KiottyNetConnectionHandle` 로만 보인다.
- Pipe, TLS, TCP 에 따라 read / write 의 내부가 달라지지만 인터페이스는 같다.
- **콜백이 다음 일을 return 한다.** `onReady`, `onRead`, `onWritten`, `onTimeout` 은 다음 IO 요청 하나를 `KiottyNetConnectionAction` 값으로 돌려준다. handle 을 받아 IO 를 거는 함수는 없다. 그래서 콜백 안에서 같은 연결에 재진입하는 길이 없고, 한 연결에 걸린 요청은 언제나 하나다.
- action 은 값이다. `KiottyNet_thenRead` 같은 action 함수는 struct 를 채워 돌려줄 뿐 검사하지 않는다. 검사는 Connection 이 action 을 받을 때 한 번 한다.
- 콜백 안에서 handle 로 할 수 있는 것은 `KiottyNet_getConnectionInfo` 와 `KiottyNet_notifyConnection` 둘뿐이다. handle 은 소유자 포인터와 정수 id 의 값이라 복사해 두어도 `releaseContext` 전까지 dangling 이 없다 (규약 8).

**action 여섯 가지.** `timeout_milliseconds` 는 그 action 이 시작한 기다림의 한도이고 0 이면 없다. 지나면 `onTimeout` 이 한 번 오고 그 return 이 다음 action 이다.

| action | 하는 일 | 완료 | timeout_milliseconds 가 재는 것 |
|---|---|---|---|
| `thenHold(timeout_milliseconds)` | 아무것도 걸지 않고 기다린다. notify 는 계속 나간다 | 없음 | 기다리는 한도 |
| `thenRead(exact, timeout_milliseconds)` | 수신 Stage 에 정확히 exact 바이트가 모이면 | `onRead(data)`, `data.length == exact` | 조건이 찰 때까지 |
| `thenReadUntil(delimiter, max, timeout_milliseconds)` | delimiter 가 나타나면. delimiter 는 1..4 바이트이며 Connection 안으로 복사된다 | `onRead(data)`, data 는 delimiter 를 포함 | 조건이 찰 때까지 |
| `thenWrite(data, timeout_milliseconds)` | write lane 으로 복사하고 보낸다. return 한 뒤 data 는 버려도 된다. 그때 나가고 있는 notify 가 있으면 그 뒤에 나간다 | `onWritten(length)` | onWritten 까지 (notify 뒤에서 기다리는 시간 포함) |
| `thenWork()` | 연결을 IO Thread 에서 떼어 Work 로 넘긴다 (아래) | `onWorkStarted(stream)` | 없음 |
| `thenClose(reason)` | 닫는다 | `onDisconnected(reason)` 마지막 한 번 | 없음 |

action 을 받을 때의 거부 (연결은 닫히고 `onDisconnected(code)`)

- `exact` 가 `receive_capacity` 보다 크면 `KIOTTY_NET_CODE_REQUEST_TOO_LARGE`. `max` 안에 delimiter 가 없으면 `KIOTTY_NET_CODE_PROTOCOL_ERROR`.
- write 의 data 가 `send_capacity` 보다 크면 `KIOTTY_NET_CODE_BUFFER_FULL`.
- `exact == 0`, write 길이 0, delimiter 길이 0 또는 5 이상은 `onError(KIOTTY_NET_CODE_INVALID_ARGUMENT)` 뒤 닫는다.

**onTimeout 이 요청이 걸린 채로 올 때.** READ / READ_UNTIL / WRITE 의 timeout 은 그 요청이 아직 OS 에 걸려 있는 상태에서 온다. 이때 return 은 다음과 같이 받는다.

| onTimeout 의 return | 일어나는 일 |
|---|---|
| CLOSE | 닫는다 |
| HOLD, READ, READ_UNTIL | 연장. 걸린 요청은 그대로 두고 deadline 만 그 action 의 timeout_milliseconds 뒤로 |
| WRITE, START_WORK | `onError(KIOTTY_NET_CODE_BUSY, "action rejected: io outstanding. use notify")` 한 번, 그리고 연장 |

HOLD 뒤의 timeout 처럼 걸린 요청이 없으면 return 은 보통의 action 이다.

**Work : 연결을 IO Thread 에서 떼어 다른 thread 가 동기로 쓰게 하는 방법.** 긴 작업, 큰 파일 송신, 요청-응답을 직접 주고받는 흐름이 이것이다. 연결 하나를 골라 보내는 push 는 Work 가 아니라 `notifyConnection` 이고, 모든 연결에 같은 데이터를 보내는 것은 7절 broadcast 다. Work 가 진행 중인 동안 IO Thread 는 그 연결에 어떤 콜백도 내지 않고 socket 도 만지지 않는다.

1. 콜백이 `KiottyNet_thenWork()` 를 return 한다. 떼어내는 것은 IO Thread 의 콜백 return 으로만 일어난다.
2. 그 연결에 걸려 있던 송신 (notify 포함) 이 끝나면 같은 loop 안에서 `onWorkStarted(ctx, stream)` 가 한 번 나간다. deadline 은 없어진다.
3. onWorkStarted 가 나간 뒤 `closeConnectionStream` 까지 연결은 worker 의 것이다. worker 는 `KiottyNetConnectionStream` 으로 동기 read / write / writeFile 을 한다. 수신 Stage 에 남아 있던 바이트는 read 가 먼저 돌려준다. 그 동안 들어온 notify 는 쌓이기만 하고 Work 가 끝난 뒤에 나간다.
4. 사용자는 `stream` 을 그 자리에서 쓰거나 worker thread 에 넘긴다. **Stream 을 받았으면 반드시 close 해야 한다.** 다 끝나면 `KiottyNet_closeConnectionStream(stream, then)` 으로 **다음 action** 을 돌려준다. `thenWrite` 의 data 는 그 자리에서 send Stage 로 복사되므로 return 한 뒤 버려도 된다.
5. IO Thread 가 깨어나 action 을 건다. 그 뒤는 보통의 `onRead` / `onWritten` 이다. 수신 Stage 에 이미 조건이 맞는 데이터가 있으면 바로 `onRead` 가 나온다. 별도의 "재개" 콜백은 없다. action 의 timeout_milliseconds 가 다음 deadline 이다.

| Stream 함수 | 하는 일 |
|---|---|
| `readConnectionStream(stream, buffer, length, timeout_milliseconds)` | 수신 Stage 에 남은 것을 먼저, 비면 socket 에서 최대 length. 0 바이트 (peer 가 닫음) 면 `KIOTTY_NET_CODE_PEER_CLOSED` |
| `writeConnectionStream(stream, data, timeout_milliseconds)` | 전부 보낼 때까지 돌아오지 않는다. 평문은 사용자 버퍼에서 바로 보낸다 (복사 없음). TLS 는 send_capacity 씩 암호화한다 |
| `writeConnectionStreamFile(stream, path, offset, length, timeout_milliseconds)` | 파일의 [offset, offset + length) 를 send_capacity 씩 읽어 write 와 같은 길로 보낸다. 파일이 없거나 범위가 넘으면 `KIOTTY_NET_CODE_INVALID_ARGUMENT` |
| `isConnectionStreamOpen(stream)` | peer 가 아직 연결돼 있는가. 비용 없는 확인 (recv peek). 오래 걸리는 작업 중에 주기적으로 본다 |
| `closeConnectionStream(stream, then)` | Work 를 끝내고 다음 action 을 돌려준다. 한 번만 |

규칙 (검사는 한 자리에서 한다)

| 오용 | 결과 |
|---|---|
| IO Thread (콜백 안) 에서 read / write / writeFile | `KIOTTY_NET_CODE_BUSY` |
| close 한 뒤, 또는 Work 가 아닌 연결의 Stream | `KIOTTY_NET_CODE_BUSY` |
| 연결이 닫히고 slot 이 재사용됨 | `KIOTTY_NET_CODE_PEER_CLOSED` |
| 소유자가 Work 중에 close | read / write 는 socket 의 끊김을 그대로 (`KIOTTY_NET_CODE_PEER_CLOSED` / `KIOTTY_NET_CODE_SOCKET_ERROR`). `isConnectionStreamOpen` 은 false. `closeConnectionStream` 은 `KIOTTY_NET_CODE_CLOSED` 를 돌려주되 Work 는 끝난다 |
| `owner` 가 NULL 이거나 `id` 가 0 인 Stream | `KIOTTY_NET_CODE_INVALID_ARGUMENT`. Connection handle 을 Stream 함수에 넘기는 것은 타입이 달라 compile 되지 않는다 |
| `closeConnectionStream(stream, thenWork())` | `KIOTTY_NET_CODE_INVALID_ARGUMENT`. Stream 은 유효하게 남는다 |
| `closeConnectionStream(stream, thenWrite(data))` 의 data 가 `send_capacity` 초과 | `KIOTTY_NET_CODE_BUFFER_FULL`. Stream 은 유효하게 남는다. 다른 action 으로 다시 close 해야 한다 |

- Work 가 진행 중일 때 peer 가 끊으면 worker 의 Stream 이 그것을 직접 본다 (read 가 `KIOTTY_NET_CODE_PEER_CLOSED`, `isConnectionStreamOpen` 이 false). 그 뒤의 close 가 `thenClose` 든 무엇이든 연결은 닫히고 `onDisconnected` 가 온다.
- 소유자의 `close()` 는 Work 중인 연결에 FIN 만 보내고 `onDisconnected(KIOTTY_NET_CODE_CLOSED)` 를 낸다. socket 을 닫고 slot 을 돌려받는 것은 worker 가 `closeConnectionStream` 한 뒤다. 그래서 **Work 중인 연결이 있으면 `run` 은 그 worker 가 close 할 때까지 돌아오지 않는다** (7절).
- 라이브러리는 Work 자체에 timeout 을 두지 않는다. Stream 함수마다 timeout_milliseconds 가 있다.
- C 에서 `closeConnectionStream` 을 영영 부르지 않으면 slot 이 영영 잡혀 있고 소유자의 close 도 끝나지 않는다. C 로는 막을 수 없는 사용자 책임이다. C++ 은 `KiottyNet_ConnectionStream` 의 소멸자가 `close(KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS))` 를 불러서 막는다. 소멸자까지 왔다는 것은 close 를 잊었거나 예외가 났다는 뜻이므로 연결을 닫아 드러낸다.
- `onRead` 의 data 는 return 하면 무효이므로, worker 가 쓸 데이터는 thenWork 를 return 하기 전에 context 로 복사한다.

**연결의 시작과 끝.**

- `onConnected(app_context, info)` 는 전송 계층 연결이 생긴 직후 (서버의 accept, 클라이언트의 connect) 한 번 온다. 아직 handle 은 없다. `KiottyNet_acceptConnection(ctx)` 를 return 하면 그 ctx 가 이 연결의 connection_context 가 되고, `KiottyNet_ignoreConnection()` 을 return 하면 socket 을 바로 닫는다. ignore 한 연결에는 다른 콜백이 오지 않는다 (onDisconnected 도 없다).
- `onReady(ctx, connection)` 는 accept 한 연결이 쓸 수 있게 된 때 한 번 온다. 평문은 onConnected 바로 뒤, TLS 는 handshake 가 끝난 뒤다. handle 이 처음 보이는 자리이며 첫 action 을 return 한다.
- `onDisconnected(app_context, ctx, reason)` 는 accept 한 연결마다 정확히 한 번, 마지막으로 온다. connection_context 해제는 여기서 한다. 연결의 handle 은 받지 않는다. 끊긴 뒤라 쓸 곳이 없고, 연결마다 남길 것은 ctx 에 있다.
- `onError(app_context, info, code, message)` 는 onDisconnected 와 따로, 이유를 사람이 읽을 수 있게 알린다. TLS handshake / 복호화 실패, 거부된 action, accept 실패가 이것이다. info 는 그 연결의 상대이고, 연결이 없는 오류 (accept 실패) 는 소유자의 endpoint 다. message 는 static 문자열이다. onError 뒤에 그 연결이 닫히면 onDisconnected 가 따로 온다.

```c
typedef struct KiottyNetConnectionInfo {
    char    hostname[KIOTTY_NET_MAX_HOST_LENGTH];   /* ip 또는 pipe 이름 */
    int32_t port;
} KiottyNetConnectionInfo;

typedef struct KiottyNetConnectionStream {   /* pool handle. onWorkStarted 가 준다. closeConnectionStream 까지 유효. 그 연결의 handle 과 같은 owner / id 의 다른 타입이다 */
    KiottyNetEndpoint* owner;
    int64_t                   id;
} KiottyNetConnectionStream;

/* handler 는 함수 표만 담는다. 그래서 static const 로 두고 여러 객체가 같이 쓸 수 있다.
   handler 와 app_context 는 create 가 아니라 run 에 따로 넘긴다 (7절). 둘 다 run 이 return 할 때까지만 살아 있으면 되므로 run 을 부른 함수의 stack 에 두어도 된다.
   context 는 둘이다. KiottyNetAppContext 는 handler 소유자(앱)의 것이고 onConnected, onDisconnected, onError 가 받는다.
   KiottyNetConnectionContext 는 연결마다의 데이터다. onConnected 가 accept 와 함께 돌려주면 라이브러리가 그 Connection 에 묶어 두고, 나머지 콜백의 첫 인자로 돌려준다.
   둘 다 void* 의 typedef 다. 이름으로 역할을 구분할 뿐이고 라이브러리는 어느 쪽도 읽지 않는다 */
typedef void* KiottyNetAppContext;
typedef void* KiottyNetConnectionContext;

typedef enum KiottyNetActionType {
    KIOTTY_NET_ACTION_TYPE_HOLD,
    KIOTTY_NET_ACTION_TYPE_READ,
    KIOTTY_NET_ACTION_TYPE_READ_UNTIL,
    KIOTTY_NET_ACTION_TYPE_WRITE,
    KIOTTY_NET_ACTION_TYPE_START_WORK,
    KIOTTY_NET_ACTION_TYPE_CLOSE
} KiottyNetActionType;

typedef struct KiottyNetConnectionAction {      /* 값. 콜백과 closeConnectionStream 이 돌려주는 다음 IO 요청 하나 */
    KiottyNetActionType type;
    uint32_t            timeout_milliseconds;             /* 0 이면 없음 */
    size_t              exact;                            /* READ */
    KiottyNetSpan       delimiter;                        /* READ_UNTIL */
    size_t              max;                              /* READ_UNTIL */
    KiottyNetSpan       data;                             /* WRITE */
    KiottyNetCode       reason;                           /* CLOSE */
} KiottyNetConnectionAction;                              /* union 없이 순서대로 둔다 (수칙 5.7). type 이 아닌 필드는 0 */

KiottyNetConnectionAction KiottyNet_thenHold(uint32_t timeout_milliseconds);
KiottyNetConnectionAction KiottyNet_thenRead(size_t exact, uint32_t timeout_milliseconds);
KiottyNetConnectionAction KiottyNet_thenReadUntil(KiottyNetSpan delimiter, size_t max, uint32_t timeout_milliseconds);
KiottyNetConnectionAction KiottyNet_thenWrite(KiottyNetSpan data, uint32_t timeout_milliseconds);
KiottyNetConnectionAction KiottyNet_thenWork(void);
KiottyNetConnectionAction KiottyNet_thenClose(KiottyNetCode reason);

typedef struct KiottyNetConnectionDecision {
    KiottyNetBool              accepted;
    KiottyNetConnectionContext connection_context;
} KiottyNetConnectionDecision;

KiottyNetConnectionDecision KiottyNet_acceptConnection(KiottyNetConnectionContext connection_context);
KiottyNetConnectionDecision KiottyNet_ignoreConnection(void);

typedef struct KiottyNetConnectionHandler {
    KiottyNetConnectionDecision (*onConnected)   (KiottyNetAppContext app_context, KiottyNetConnectionInfo info);                                                       /* handle 은 아직 없다 */
    KiottyNetConnectionAction   (*onReady)       (KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection);                                          /* handshake 뒤 (평문은 바로). 첫 action */
    KiottyNetConnectionAction   (*onRead)        (KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, KiottyNetSpan data);                      /* data 는 수신 Stage. return 하면 무효 */
    KiottyNetConnectionAction   (*onWritten)     (KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, size_t length);                           /* write 하나의 완료 */
    KiottyNetConnectionAction   (*onTimeout)     (KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection);                                          /* action 의 timeout_milliseconds 만료 */
    void                        (*onWorkStarted) (KiottyNetConnectionContext connection_context, KiottyNetConnectionStream stream);                                       /* 이 순간부터 closeConnectionStream 까지 연결은 worker 의 것 */
    void                        (*onDisconnected)(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context, KiottyNetCode reason);   /* 마지막 콜백. handle 없음 */
    void                        (*onError)       (KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message);         /* info 는 상대. 연결 없는 오류는 소유자의 address. message 는 static */
} KiottyNetConnectionHandler;

KiottyNetConnectionInfo KiottyNet_getConnectionInfo(KiottyNetConnectionHandle connection);   /* 아무 thread. 값으로 돌려준다. 닫힌 handle 이면 0 으로 채운 값 */

/* 아무 thread 에서나. 이 연결의 notify Stage 로 data 를 복사하고 바로 돌아온다 (4절). 돌아온 뒤 data 는 버려도 된다. 완료 콜백은 없다.
   action 을 return 하지 않고 보내는 유일한 길이다. Work 중인 연결에도 들어간다.
   여유가 없으면 KIOTTY_NET_CODE_BUFFER_FULL 이고 아무것도 넣지 않으며 연결은 유지된다. 닫힌 handle 이면 KIOTTY_NET_CODE_PEER_CLOSED */
KiottyNetCode KiottyNet_notifyConnection(KiottyNetConnectionHandle connection, KiottyNetSpan data);

/* 동기 통로. Work 를 받은 thread 만. IO Thread 에서 부르면 KIOTTY_NET_CODE_BUSY */
KiottyNetResult KiottyNet_readConnectionStream     (KiottyNetConnectionStream stream, void* buffer, size_t length, uint32_t timeout_milliseconds);
KiottyNetResult KiottyNet_writeConnectionStream    (KiottyNetConnectionStream stream, KiottyNetSpan data, uint32_t timeout_milliseconds);
KiottyNetCode   KiottyNet_writeConnectionStreamFile(KiottyNetConnectionStream stream, const char* path, uint64_t offset, uint64_t length, uint32_t timeout_milliseconds);
KiottyNetBool   KiottyNet_isConnectionStreamOpen   (KiottyNetConnectionStream stream);
KiottyNetCode   KiottyNet_closeConnectionStream    (KiottyNetConnectionStream stream, KiottyNetConnectionAction then);   /* 한 번. thenWork 는 KIOTTY_NET_CODE_INVALID_ARGUMENT */
```

```cpp
class KiottyNet_Connection {                       // pool handle 의 비소유 래퍼. 값 타입. 콜백 안에서 handle 로 만든다
public:
    explicit KiottyNet_Connection(KiottyNetConnectionHandle handle);
    KiottyNetConnectionHandle handle() const;      // 다른 thread 에 넘길 값. Session 에 저장해 둔다
    KiottyNetConnectionInfo   info() const;
    KiottyNetCode             notify(KiottyNetSpan data);
    bool operator==(const KiottyNet_Connection& other) const;   // owner 와 id 둘 다 같다
    bool operator!=(const KiottyNet_Connection& other) const;
    bool operator<(const KiottyNet_Connection& other) const;    // owner (std::less), 그 다음 id. std::map 의 key 로 쓴다
};

class KiottyNet_ConnectionStream {                 // close 의무를 소유한다. 복사 없음, 이동만. worker thread 로 이동해서 넘긴다
public:
    explicit KiottyNet_ConnectionStream(KiottyNetConnectionStream stream);
    KiottyNet_ConnectionStream(KiottyNet_ConnectionStream&& other);
    KiottyNet_ConnectionStream& operator=(KiottyNet_ConnectionStream&& other);   // 받는 쪽이 아직 close 하지 않았으면 먼저 소멸자와 같이 닫는다
    ~KiottyNet_ConnectionStream();                 // 아직 close 하지 않았으면 close(KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS))
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_milliseconds);
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);
    KiottyNetCode   writeFile(const char* path, uint64_t offset, uint64_t length, uint32_t timeout_milliseconds);
    bool            open() const;
    KiottyNetCode   close(KiottyNetConnectionAction then);   // 한 번만. BUFFER_FULL, INVALID_ARGUMENT 면 Stream 이 남고 그 외에는 비어 있다
};
```

action 은 C++ 에서도 같은 C 함수로 만든다. 값을 돌려주는 함수라 래퍼가 필요 없다.

사용 예

```cpp
// 연결마다의 데이터 : onConnected 가 만들어 accept 와 함께 돌려주고, onDisconnected 가 돌려받아 해제한다
KiottyNetConnectionDecision onConnected(KiottyNetAppContext ctx, KiottyNetConnectionInfo)
{
    App*     app = static_cast<App*>(ctx);
    Session* s   = app->sessions.acquire();
    if (s == nullptr) { return KiottyNet_ignoreConnection(); }       // 받지 않는다. 다른 콜백은 오지 않는다
    s->app = app;                                                    // 뒤의 콜백은 app_context 를 받지 않는다. 필요하면 Session 에 넣어 둔다
    return KiottyNet_acceptConnection(s);
}
KiottyNetConnectionAction onReady(KiottyNetConnectionContext session, KiottyNetConnectionHandle c)
{
    static_cast<Session*>(session)->connection = c;                  // handle 이 처음 보이는 자리
    return KiottyNet_thenReadUntil(CRLF, 1024, 30000);
}
void onDisconnected(KiottyNetAppContext ctx, KiottyNetConnectionContext session, KiottyNetCode)
{
    static_cast<App*>(ctx)->sessions.release(static_cast<Session*>(session));
}

// 긴 작업 : onRead 에서 떼어내고, worker 가 Stream 으로 응답을 보낸 뒤 다음 action 을 돌려준다
KiottyNetConnectionAction onRead(KiottyNetConnectionContext session, KiottyNetConnectionHandle, KiottyNetSpan data)
{
    Session* s = static_cast<Session*>(session);
    s->request.assign(data.data, data.length);                       // return 하면 data 가 무효라 복사
    return KiottyNet_thenWork();
}
void onWorkStarted(KiottyNetConnectionContext session, KiottyNetConnectionStream handle)
{
    Session*                   s = static_cast<Session*>(session);
    KiottyNet_ConnectionStream stream(handle);
    s->app->pool.post([s, stream = std::move(stream)]() mutable {    // C++14 init capture. C++11 은 shared_ptr 로
        s->response = compute(s->request);
        if (!stream.open()) { return; }                               // 끊겼으면 소멸자의 close 가 onDisconnected 를 끌어낸다
        stream.write(s->response.span(), 10000);                      // 동기. 다 보낼 때까지
        stream.writeFile(s->attachment_path, 0, s->attachment_size, 60000);
        stream.close(KiottyNet_thenReadUntil(CRLF, 1024, 30000));     // 다음 요청은 다시 IO Thread 가 받는다
    });
}

// push : 앱 thread 가 살아 있는 연결에 끼어든다. Work 가 필요 없다
for (Session* s : app.sessions.live()) { KiottyNet_Connection(s->connection).notify(message); }
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
    KiottyNet_StreamClient(KiottyNetContext context, const KiottyNetClientConfiguration& configuration);   // KiottyNet_createStreamClient
    // 소멸자 (KiottyNet_destroyStreamClient) 와 이동은 멤버 kiotty::net::OwnedHandle<KiottyNetStreamClientHandle, KiottyNet_destroyStreamClient> 가 맡는다. 복사는 없다
    explicit operator bool() const;                                         // create 가 실패하면 false. 예외는 없다
    KiottyNetCode code() const;                                             // create 의 Result.code. 실패 이유
    KiottyNetStreamClientHandle handle() const;
    // 이하 메서드는 같은 이름의 C 함수를 그대로 부른다
    KiottyNetCode   connect(const KiottyNetAddress& address);                                               // 평문
    KiottyNetCode   connect(const KiottyNetAddress& address, const KiottyNetTlsClientConfiguration& tls);   // TLS
    void            disconnect();
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_milliseconds);
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);
};
```

```c
typedef struct KiottyNetStreamClient* KiottyNetStreamClientHandle;
typedef struct KiottyNetStreamClientResult { KiottyNetCode code; KiottyNetStreamClientHandle handle; } KiottyNetStreamClientResult;
KiottyNetStreamClientResult KiottyNet_createStreamClient(KiottyNetContext context, const KiottyNetClientConfiguration* configuration);   /* 설정 오류나 메모리 부족이면 handle NULL, code 가 이유 */
void            KiottyNet_destroyStreamClient(KiottyNetContext context, KiottyNetStreamClientHandle client);
KiottyNetCode   KiottyNet_connectStreamClient(KiottyNetStreamClientHandle client, const KiottyNetAddress* address, const KiottyNetTlsClientConfiguration* tls);   /* 연결돼 있으면 먼저 끊는다. tls 가 NULL 이면 평문. max_trial / trial_interval_milliseconds 만큼 재시도 */
void            KiottyNet_disconnectStreamClient(KiottyNetStreamClientHandle client);
KiottyNetResult KiottyNet_readStreamClient(KiottyNetStreamClientHandle client, void* buffer, size_t length, uint32_t timeout_milliseconds);   /* 최대 length. 0 바이트면 KIOTTY_NET_CODE_PEER_CLOSED */
KiottyNetResult KiottyNet_writeStreamClient(KiottyNetStreamClientHandle client, KiottyNetSpan data, uint32_t timeout_milliseconds);

typedef struct KiottyNetEventClient* KiottyNetEventClientHandle;
typedef struct KiottyNetEventClientResult { KiottyNetCode code; KiottyNetEventClientHandle handle; } KiottyNetEventClientResult;
KiottyNetEventClientResult KiottyNet_createEventClient(KiottyNetContext context, const KiottyNetClientConfiguration* configuration);
void          KiottyNet_destroyEventClient(KiottyNetContext context, KiottyNetEventClientHandle client);
KiottyNetCode KiottyNet_runEventClient(KiottyNetEventClientHandle client, const KiottyNetAddress* address, const KiottyNetTlsClientConfiguration* tls, const KiottyNetConnectionHandler* handler, KiottyNetAppContext app_context);   /* 연결 (max_trial 만큼 재시도) 후 loop. 연결되면 onConnected, onReady. tls 가 NULL 이면 평문 */
void          KiottyNet_closeEventClient(KiottyNetEventClientHandle client);
KiottyNetCode KiottyNet_awaitEventClient(KiottyNetEventClientHandle client, uint64_t timeout_milliseconds);
KiottyNetConnectionHandle KiottyNet_getEventClientConnection(KiottyNetEventClientHandle client);   /* onReady 전이면 0 */
/* EventClient 가 가진 연결 하나의 KiottyNet_notifyConnection 이다. 규칙도 같다. 연결 전이면 KIOTTY_NET_CODE_PEER_CLOSED */
KiottyNetCode KiottyNet_notifyEventClient(KiottyNetEventClientHandle client, KiottyNetSpan data);
```

```cpp
class KiottyNet_EventClient {                      // 골격은 KiottyNet_StreamClient 와 같다
public:
    KiottyNet_EventClient(KiottyNetContext context, const KiottyNetClientConfiguration& configuration);
    KiottyNetCode run(const KiottyNetAddress& address, const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context);
    KiottyNetCode run(const KiottyNetAddress& address, const KiottyNetTlsClientConfiguration& tls, const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context);   // TLS
    void          close();
    KiottyNetCode await(uint64_t timeout_milliseconds);
    KiottyNetConnectionHandle connection() const;
    KiottyNetCode notify(KiottyNetSpan data);
};
```

> 고민되는 부분. 생각보다 network 환경이 열악할 때도 있기 때문에 timeout만 걸어두는 게 아니라,
> 재시도 횟수와 시도와 시도 사이의 time interval도 걸어두는 게 좋다. (KiottyNetClientConfiguration 의 max_trial / trial_interval_milliseconds 로 반영)

### 7. Server

1. Server는 Configuration과 상관 없이 동일한 기능으로 돌아간다.
2. 생성 시점에 `max_clients` 분량의 Connection, Stage 를 모두 잡는다. 이후 런타임 할당은 없다.
3. 프로토콜은 `run` 에 `KiottyNetConnectionHandler` 와 `app_context` 를 따로 넘겨 주입한다. create 는 handler 를 받지 않는다. StateMachine 을 쓰면 `KiottyNet_runStatefulServer` 가 라이브러리의 handler 표를 대신 넘긴다 (8절). HTTP 와 WebSocket 은 9절의 HttpServer (WebSocket 은 그 Upgrade 경로, 10절) 가 내부에서 이것을 쓴다.
4. 다른 thread 에서 IO Thread 로 들어오는 길은 셋이다 (5절). Work 의 끝은 `KiottyNet_closeConnectionStream`, 연결 하나로의 push 는 `KiottyNet_notifyConnection`, 모든 연결로의 push 는 `KiottyNet_broadcastServer`.
5. 수명은 `run / close / await` 셋이다. 규칙은 아래 표와 같고 EventClient, HttpServer, WebSocketClient 도 같다 (WebSocketClient 의 await 는 11절).

**loop 를 가진 객체의 수명 (EventClient, Server, HttpServer, WebSocketClient 공통).** 함수는 `run`, `close`, `await` 셋뿐이다.

| 함수 | 부르는 thread | 하는 일 | 반환 |
|---|---|---|---|
| `run(address, tls, handler, app_context)` | loop 를 돌릴 thread | 연다 (TLS 면 SSL_CTX 를 새로 만들고, Server: bind + listen, Client: connect + 재시도) 그리고 loop 를 돈다. `close` 까지 return 하지 않는다 | `close` 로 끝나면 `KIOTTY_NET_CODE_CLOSED`. 열지 못했거나 loop 가 실패하면 그 이유 (`KIOTTY_NET_CODE_SOCKET_ERROR`, `KIOTTY_NET_CODE_TIMEOUT`, Client 는 서버가 끊으면 `KIOTTY_NET_CODE_PEER_CLOSED`) |
| `close()` | 아무 thread | 모든 Connection 을 닫고 (`onDisconnected(KIOTTY_NET_CODE_CLOSED)`) loop 를 끝낸다. 기다리지 않고 바로 return 한다. Work 중인 연결은 FIN 만 보내고, loop 는 그 worker 가 `closeConnectionStream` 할 때까지 끝나지 않는다 | 없음 |
| `await(timeout_milliseconds)` | 아무 thread | loop 가 완전히 돌기 시작할 때까지 기다린다 | 돌고 있으면 `KIOTTY_NET_CODE_SUCCESS`. `run` 이 이미 끝났으면 `run` 의 반환값. 시간이 지나면 `KIOTTY_NET_CODE_TIMEOUT` |

- `run` 이 return 한 뒤 다시 `run` 하면 다시 연다 (재접속, 재시작). 이때 다른 주소, 다른 TLS 설정, 다른 handler 와 app_context 를 넘겨도 된다. 이미 돌고 있는데 `run` 하면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. handler 나 address 가 NULL 이거나 주소 / TLS 설정이 잘못돼도 `KIOTTY_NET_CODE_INVALID_ARGUMENT` 이고 소켓을 열지 않는다.
- 새 `run` 이 들어가기 전의 짧은 동안 `await` 는 지난 `run` 의 반환값을 돌려준다. 다시 run 한 뒤 기다릴 때는 `KIOTTY_NET_CODE_SUCCESS` 가 될 때까지 짧게 반복해서 묻는다.
- 모든 콜백은 `run` 을 부른 thread 에서 `run` 안에서만 일어난다. `run` 은 모든 연결의 `onDisconnected` 를 낸 뒤에만 return 한다 (정상 종료, `close`, 오류 종료 모두). 그래서 handler 와 app_context 는 `run` 이 return 할 때까지만 살아 있으면 된다. 이 보장 때문에 `run` 은 자기 thread 를 따로 띄우지 않는 blocking 함수로 남는다.
- Work 의 worker thread 는 이 보장 밖이다. `close` 는 worker 를 기다리지 않으므로 `run` 이 return 한 뒤에도 worker 가 남을 수 있다. worker 에는 app_context 를 넘기지 않는다.
- `destroy` (C++ 소멸자) 는 `close` 를 부르고 `run` 이 return 할 때까지 기다린 뒤 해제한다. 그래서 IO Thread 의 콜백 안에서 `destroy` 하면 안 된다 (debug 빌드는 assert).

```cpp
KiottyNet_Server server(context.handle(), config);
std::thread io([&] { server.run(address, handler, &app); });   // handler 는 static const 표, app 은 app_context. TLS 면 server.run(address, tls, handler, &app)
if (server.await(3000) != KIOTTY_NET_CODE_SUCCESS) { /* bind 실패 등. run 의 반환값과 같다 */ }
// ...
server.close();
io.join();
```

```c
typedef struct KiottyNetServer* KiottyNetServerHandle;
typedef struct KiottyNetServerResult { KiottyNetCode code; KiottyNetServerHandle handle; } KiottyNetServerResult;
KiottyNetServerResult KiottyNet_createServer(KiottyNetContext context, const KiottyNetServerConfiguration* configuration);   /* 실패하면 handle NULL, code 가 이유 */
void          KiottyNet_destroyServer(KiottyNetContext context, KiottyNetServerHandle server);
KiottyNetCode KiottyNet_runServer(KiottyNetServerHandle server, const KiottyNetAddress* address, const KiottyNetTlsServerConfiguration* tls, const KiottyNetConnectionHandler* handler, KiottyNetAppContext app_context);   /* (TLS 면 SSL_CTX) bind + listen 후 loop. tls 가 NULL 이면 평문 */
void          KiottyNet_closeServer(KiottyNetServerHandle server);
KiottyNetCode KiottyNet_awaitServer(KiottyNetServerHandle server, uint64_t timeout_milliseconds);

/* 아무 thread 에서나. onReady 가 나간 모든 연결 (Work 중인 연결 포함) 의 notify Stage 에 data 를 복사하고 바로 돌아온다 (5절 notifyConnection).
   돌아온 시점에 복사는 끝났고 data 는 버려도 된다. 완료 콜백은 없다. socket 으로 나가는 것은 IO Thread 가 평소처럼 한다.
   하나라도 여유가 없으면 아무 데도 넣지 않고 KIOTTY_NET_CODE_BUFFER_FULL. 연결은 끊지 않는다. 연결이 없으면 KIOTTY_NET_CODE_SUCCESS */
KiottyNetCode KiottyNet_broadcastServer(KiottyNetServerHandle server, KiottyNetSpan data);
```

```cpp
class KiottyNet_Server {
public:
    KiottyNet_Server(KiottyNetContext context, const KiottyNetServerConfiguration& configuration);
    KiottyNetCode run(const KiottyNetAddress& address, const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context);
    KiottyNetCode run(const KiottyNetAddress& address, const KiottyNetTlsServerConfiguration& tls, const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context);   // TLS
    void          close();
    KiottyNetCode await(uint64_t timeout_milliseconds);
    KiottyNetCode broadcast(KiottyNetSpan data);
};
```

### 8. StateMachine

HTTP, WebSocket, Custom Protocol 작성을 돕는 **선택적 계층**이다. Connection 과 KiottyNetConnectionHandler (5절) 위에 얹히며 그 둘은 이 계층을 모른다.
프로토콜 작성자는 **(1) State enum, (2) Session 과 그것을 만들고 돌려받는 함수 둘, (3) transition action 함수들, (4) rules 표** 네 가지만 쓴다.
콜백을 직접 구현하지 않는다. transition action 은 `(Session, Event) -> Transition` 인 순수 함수이고, Transition 의 entry action 은 5절의 `KiottyNetConnectionAction` 그대로다. 라이브러리가 그것을 콜백의 return 으로 돌려준다.
이 계층은 `KiottyNet_runStatefulServer` / `KiottyNet_runStatefulEventClient` 하나로 들어간다. 7절의 run 에 라이브러리가 가진 handler 표와 `KiottyNetStatefulAppContext` 를 대신 넘기는 함수다. Runner 같은 객체도, 사용자가 잡아 두는 pool 도 없다.

규칙

- 라이브러리가 기억하는 것은 둘이다. **state** 는 연결마다 하나이고 Connection 안에 있다. **machine** (표 + 초기 상태) 은 run 마다 하나이고 `KiottyNetStatefulAppContext` 안에 있다. Session 에 state 멤버를 두지 않는다.
- Session 의 저장소는 사용자 몫이다. `createConnectionContext(app_context, info)` 가 5절의 onConnected 자리에서 연결마다 하나를 돌려주고 (handle 은 아직 없다), `releaseConnectionContext(app_context, session)` 가 돌려받는다 (handle 은 받지 않는다). create 가 NULL 을 돌려주면 Session 이 다 닳은 것으로 보고 `onError(KIOTTY_NET_CODE_SESSION_EXHAUSTED)` 로 알린 뒤 그 연결은 받지 않는다 (`ignoreConnection`. CONNECTED 도 CLOSED 행도 release 도 오지 않는다). create 가 NULL 함수이면 session 은 NULL 이고 release 는 부르지 않는다. release 가 NULL 함수이면 부르지 않는다.
- `onError(app_context, info, code, message)` 는 라이브러리가 표 밖의 일을 알리는 자리다. `SESSION_EXHAUSTED`, `UNHANDLED_EVENT`, `ILLEGAL_TRANSITION`, 그리고 `WORK_STARTED` 행이 돌려준 action 이 잘못되었을 때 (`INVALID_ARGUMENT`, `BUFFER_FULL`) 부른다. NULL 이면 부르지 않고 그 뒤의 처리 (닫기) 는 같다. 5절의 `onError` 와 같은 시그니처이고 IO Thread 에서 불린다.
- transition action 은 app_context 를 받지 않는다. action 이 앱 객체에 닿아야 하면 create 가 Session 에 그 포인터를 넣어 둔다.
- `Event` 는 IO Thread 가 알려주는 "무슨 일이 일어났는가", entry action (`KiottyNetConnectionAction`) 은 표가 돌려주는 "다음 상태에 들어가면서 해 달라" 다. 한 Transition 은 entry action 하나만 가진다.
- Transition 은 언제나 다음 상태를 정한다. 상태를 그대로 두는 Transition 은 없다. ANY 행도 갈 곳을 안다 (오류면 오류 상태, 닫히면 닫힌 상태). 다른 일 없이 머물려면 자기 상태로 move 하고 entry action 은 `KiottyNet_thenHold(0)` 이다.
- 표는 수신 흐름과 그에 딸린 응답만 다룬다. 흐름과 무관한 송신은 표를 거치지 않고 `KiottyNet_notifyConnection` 으로 보낸다 (4절). 그래서 "어느 상태에서든 와서 상태를 건드리지 않는" 이벤트가 없다.
- `CONNECTED` 는 첫 Event 다. 5절의 `onReady` 에 해당하며 create 가 Session 을 돌려준 뒤에 온다. 이 행이 첫 read / write 를 entry action 으로 낸다.
- `WORK_STARTED` 에서 돌려준 entry action 은 Connection 이 아니라 Stream 에 적용된다. HOLD / READ / READ_UNTIL / WRITE / CLOSE 면 그 자리에서 `KiottyNet_closeConnectionStream(stream, entry_action)`. `thenWork()` 면 Stream 을 그대로 두는데, action 이 `stream` 을 worker 에게 넘겼다는 뜻이다. 그 action 이 잘못되었으면 (`INVALID_ARGUMENT`, `BUFFER_FULL`) `onError` 로 알리고 Stream 은 열린 채 남는다. `WORK_STARTED` 행이 없으면 `UNHANDLED_EVENT` 로 Stream 이 닫힌다.
- `CLOSED` 는 마지막 Event 다. 그 action 의 Transition 은 무시된다. 그 뒤에 release 가 불린다. `CLOSED` 행이 없어도 release 는 하고, `UNHANDLED_EVENT` 는 알리지 않는다 (이미 닫힌 연결이다).
- 표에 (state, event) 행이 없으면 `onError(KIOTTY_NET_CODE_UNHANDLED_EVENT)` 뒤 `thenClose`. 조용히 무시하지 않는다. 정확 일치 행이 ANY 행보다 우선한다.
- `.to(...)` 는 action 이 갈 수 있는 다음 상태 목록이다. 목록 밖으로 가면 `onError(KIOTTY_NET_CODE_ILLEGAL_TRANSITION)` 뒤 `thenClose`. release 빌드에서도 검사한다 (사용자 결정 2026-11-09). `.to()` 가 없는 행 (`next.count` 0) 은 검사하지 않는다. 한 행의 다음 상태는 `KIOTTY_NET_MAX_NEXT` (8) 개까지다 (2026-11-12 4 에서 8 로. HTTP 헤더 줄 행이 다섯을 쓴다). C++ 의 `.to()` 는 가변 인자 하나이고 개수와 타입을 컴파일 타임에 검사한다.
- `event.data` 는 수신 Stage 를 가리키며 action 이 return 하면 무효다. 보관하려면 Session 으로 복사한다.
- 표는 컴파일 타임 상수다. C++ 의 `KiottyNet_rules()` 는 (state, event) 로 정렬하고 중복 행을 컴파일 에러로 만든다. C 의 표는 `static const` 배열이며 run 이 시작할 때 중복을 검사해 `KIOTTY_NET_CODE_DUPLICATE_RULE` 을 돌려준다. machine 이나 표가 NULL 이면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`.

공용 타입

```c
#define KIOTTY_NET_ANY_STATE  (-1)
#define KIOTTY_NET_MAX_NEXT   8

typedef enum KiottyNetConnectionEventType { KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN, KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED, KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT, KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED } KiottyNetConnectionEventType;

typedef struct KiottyNetConnectionEvent {
    KiottyNetConnectionEventType type;
    KiottyNetSpan                data;           /* READ. return 하면 무효 */
    size_t                       length;         /* WRITTEN */
    KiottyNetConnectionStream    stream;         /* WORK_STARTED */
    KiottyNetCode                reason;         /* CLOSED */
} KiottyNetConnectionEvent;                      /* union 없이 순서대로 둔다 (수칙 5.7). type 이 아닌 필드는 0 */

typedef struct KiottyNetTransition {
    int32_t                   next;           /* 다음 상태 */
    KiottyNetConnectionAction entry_action;   /* 5절의 action 함수로 만든다. timeout_milliseconds 가 시간이 들어오는 유일한 입구 */
} KiottyNetTransition;

typedef KiottyNetTransition (*KiottyNetTransitionAction)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event);

typedef struct KiottyNetNextStates {
    int32_t  at[KIOTTY_NET_MAX_NEXT];                      /* 갈 수 있는 다음 상태. release 에서도 검사 */
    uint32_t count;                                        /* 0 이면 검사 안 함 */
} KiottyNetNextStates;

typedef struct KiottyNetRule {
    int32_t                      state;                    /* 사용자 enum 값 또는 KIOTTY_NET_ANY_STATE */
    KiottyNetConnectionEventType event;
    KiottyNetTransitionAction    transition_action;
    KiottyNetNextStates          next;
} KiottyNetRule;

typedef struct KiottyNetStateMachine {
    const KiottyNetRule* rules;
    size_t               count;
    int32_t              initial;
} KiottyNetStateMachine;

typedef struct KiottyNetStatefulAppContext {
    KiottyNetAppContext          app_context;              /* create / release 가 받는다 */
    const KiottyNetStateMachine* machine;
    KiottyNetConnectionContext (*createConnectionContext)(KiottyNetAppContext app_context, KiottyNetConnectionInfo info);                               /* 연결마다 Session 하나. NULL 이면 받지 않는다 (ignore) */
    void (*releaseConnectionContext)(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context);                                                  /* CLOSED 행 뒤에 */
    void (*onError)(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message);                         /* 표 밖의 일. NULL 이면 부르지 않는다 */
} KiottyNetStatefulAppContext;
```

C 함수. 표는 `static const KiottyNetRule[]` 이고 `KIOTTY_NET_RULE` 매크로가 next 개수를 컴파일 타임에 센다 (C99 의 compound literal. C++ 에서는 정의되지 않고 아래의 builder 를 쓴다).

```c
#define KIOTTY_NET_RULE(state, event, transition_action, ...)      { (state), (event), (transition_action), { { __VA_ARGS__ }, (uint32_t)(sizeof((int32_t[]){ __VA_ARGS__ }) / sizeof(int32_t)) } }
#define KIOTTY_NET_RULE_UNCHECKED(state, event, transition_action) { (state), (event), (transition_action), { { 0 }, 0 } }
#define KIOTTY_NET_RULE_COUNT(table)                               (sizeof(table) / sizeof((table)[0]))

KiottyNetTransition KiottyNet_moveToNextState(int32_t next, KiottyNetConnectionAction entry_action);

/* 7절 run 의 Stateful 판. 라이브러리의 handler 표와 app_context 를 KiottyNet_runServer / KiottyNet_runEventClient 에 넘긴다.
   표 검사에 실패하면 열지 않고 바로 돌아온다 (KIOTTY_NET_CODE_DUPLICATE_RULE, KIOTTY_NET_CODE_INVALID_ARGUMENT). 그 외의 반환과 close / await 는 7절과 같다 */
KiottyNetCode KiottyNet_runStatefulServer(KiottyNetServerHandle server, const KiottyNetAddress* address, const KiottyNetTlsServerConfiguration* tls, const KiottyNetStatefulAppContext* app_context);
KiottyNetCode KiottyNet_runStatefulEventClient(KiottyNetEventClientHandle client, const KiottyNetAddress* address, const KiottyNetTlsClientConfiguration* tls, const KiottyNetStatefulAppContext* app_context);
```

```c
/* 사용 예 */
static const KiottyNetRule HTTP_RULES[] = {
    KIOTTY_NET_RULE(KIOTTY_NET_ANY_STATE,    KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED,    on_connected, HTTP_READING_HEADER),
    KIOTTY_NET_RULE(HTTP_READING_HEADER, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ,         on_header,    HTTP_READING_BODY, HTTP_PROCESSING, HTTP_RESPONDING),
    KIOTTY_NET_RULE_UNCHECKED(HTTP_PROCESSING, KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED, on_work_started),
    KIOTTY_NET_RULE(HTTP_RESPONDING,     KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN,      on_responded, HTTP_READING_HEADER, HTTP_IDLE),
    KIOTTY_NET_RULE_UNCHECKED(KIOTTY_NET_ANY_STATE, KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED, on_closed),
};
static const KiottyNetStateMachine HTTP_MACHINE = { HTTP_RULES, KIOTTY_NET_RULE_COUNT(HTTP_RULES), HTTP_IDLE };

static HttpSession sessions[32];                                   /* Session 저장소는 사용자 것. 크기와 재사용 규칙도 사용자가 정한다 */
static KiottyNetConnectionContext acquire_session(KiottyNetAppContext app, KiottyNetConnectionInfo info);
static void                       release_session(KiottyNetAppContext app, KiottyNetConnectionContext session, KiottyNetConnectionHandle c);

HttpApp                     app = { ... };
KiottyNetStatefulAppContext stateful = { &app, &HTTP_MACHINE, acquire_session, release_session, NULL };   /* run 동안만 살아 있으면 된다. onError 는 NULL 이어도 된다 */
KiottyNetServerResult       server = KiottyNet_createServer(context, &config);
KiottyNet_runStatefulServer(server.handle, &address, NULL, &stateful);
```

C++. 표를 읽기 쉽게 쓰고 컴파일 타임에 정렬 / 중복 검사하는 builder 만 있다. action 의 시그니처, entry action 을 만드는 함수, run 은 C 와 같다.

```cpp
template <typename State>
struct KiottyNet_Transition {                                     // State enum 을 int32_t 로 바꿔 주는 이름 공간. 값은 C 의 KiottyNetTransition
    static KiottyNetTransition moveToNextState(State next, KiottyNetConnectionAction entry_action);
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
    return Next::moveToNextState(HttpState::ReadingBody, KiottyNet_thenRead(s->content_length, 10000));
}

static constexpr auto HTTP_RULES = KiottyNet_rules(
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED)                                  .then(onConnected)  .to(HttpState::ReadingHeader),
    KiottyNet_from(HttpState::ReadingHeader).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ)        .then(onHeader)     .to(HttpState::ReadingBody, HttpState::Processing, HttpState::Responding),
    KiottyNet_from(HttpState::ReadingBody)  .when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ)        .then(onBody)       .to(HttpState::Processing, HttpState::Responding),
    KiottyNet_from(HttpState::Processing)   .when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED).then(onWorkStarted),
    KiottyNet_from(HttpState::Responding)   .when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN)     .then(onResponded)  .to(HttpState::ReadingHeader, HttpState::Idle),
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT)                                    .then(onTimeout),
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED)                                     .then(onClosed));
static constexpr KiottyNetStateMachine HTTP_MACHINE = KiottyNet_makeStateMachine(HTTP_RULES, HttpState::Idle);

HttpApp                     app;
KiottyNetStatefulAppContext stateful = { &app, &HTTP_MACHINE, &HttpApp::acquireSession, &HttpApp::releaseSession, &HttpApp::onError };
KiottyNet_Context           context;
KiottyNet_Server            server(context.handle(), config);
std::thread                 io([&] { KiottyNet_runStatefulServer(server.handle(), &address, nullptr, &stateful); });   // stateful 은 io 가 끝날 때까지 살아 있다
```

### 9. HttpServer

> 이 절은 `include/kiotty_net_http.h` 의 공개 API 판 v2 (cycle H1, 2026-11-12) 와 그 구현 (cycle H3 ~ H5) 을 적는다. 설계의 근거는 [design_http_v0.html](../design/design_http_v0.html), 사용법은 [user_guide_http.md](user_guide_http.md), 안의 모양은 [implementation_guide_v0.md](implementation_guide_v0.md) 4절이다. 판 v1 의 `deferHttpResponse` + `resumeConnection(payload)` + `resumedWork` 는 없다.

HTTP/1.1 서버. 7절 Server 위에 라이브러리가 구현한 HTTP StateMachine (8절) 을 얹은 것이다. 사용자는 라우트를 등록하고 응답을 채운다. 응답은 두 모양이다.

| | 완성 응답 | Work 스트림 |
|---|---|---|
| body | 복사 / 참조 / 파일 / 없음 | 스트림 (`setHttpBodyStream`) |
| 누가 채우나 | IO Thread 의 `onRequest`, 또는 Work 를 든 worker | Work 를 든 worker 하나 |
| 언제 나가나 | `onRequest` 가 return 할 때, 또는 `finishHttpWorkWithResponse` | `writeHttpWorkChunk` 마다 바로 (동기) |
| 끝 | body 끝이 응답 끝 | `finishHttpWorkWithResponse` 가 끝 chunk |
| framing 헤더 | `Content-Length` | `Transfer-Encoding: chunked` |
| MCP 에서 | JSON 응답, 202, 404, 405, 406 | POST 에 대한 SSE, GET 스트림 |

- 라우트는 `KiottyNet_addHttpRoute(server, method, path, route)` 로 등록하며 C++ 의 `get / head / post / put / patch / del / options` 는 그 별칭이다 (`delete` 는 C++ 예약어라 `del`). 콜백을 등록하는 방식이지 하나의 handler 구조체를 넘기는 방식이 아니다. 등록은 `run` 전에만 되고 (run 중이면 `KIOTTY_NET_CODE_BUSY`) 표는 고정 크기다 (`KiottyNetHttpLimits::max_routes`). 같은 (method, path) 는 `KIOTTY_NET_CODE_DUPLICATE_RULE`, 같은 path 에 다른 method 는 중복이 아니다.
- path 매칭은 정확 일치와 `/*` 접미 wildcard 두 가지다. 등록 순서대로 검사한다. `:id` 같은 파라미터 파싱과 percent-decoding 은 하지 않으며 `request.path` 를 사용자가 파싱한다.
- 정적 서빙은 세 가지다. 디렉토리 매핑, 파일 하나 매핑, 메모리 바이너리 매핑. 라우트와 같은 표에서 등록 순서대로 매칭하며 GET / HEAD 에만 적용된다 (아래 정적 서빙).
- path 가 맞는 라우트가 하나도 없으면 `notFound` 라우트, 그것도 없으면 404. path 는 맞지만 method 가 없으면 405 다 (아래 메서드별 동작).
- 요청은 공용 struct `KiottyNetHttpRequest` 로 오고, 응답은 pool handle `KiottyNetHttpResponseHandle` 로 채운다. 둘 다 Connection 마다 하나씩 있는 Session 의 고정 크기 버퍼 (`KiottyNetHttpLimits`) 에 있다. body 가 `max_body_bytes` 를 넘으면 413 으로 라이브러리가 거절한다. 응답 버퍼가 넘치면 setter 가 `KIOTTY_NET_CODE_BUFFER_FULL` 을 돌려준다.
- status 는 `KiottyNetHttpStatus`, content type 은 `KiottyNetHttpContentType` enum 이다. status 인자는 정수라 목록 밖의 코드 (100..599) 도 받는다. content type 은 body 를 정할 때만 적는다 (아래 content type).
- 아무 setter 도 부르지 않은 라우트는 404, setter 를 불렀고 status 가 없으면 200 이다. 1xx / 204 / 304 는 Content-Length, Content-Type, body 를 보내지 않는다. 라이브러리가 `Date`, `Content-Length` 또는 `Transfer-Encoding`, `Content-Type`, 닫을 때의 `Connection: close` 를 채운다.
- 긴 작업은 5절 Work 를 HTTP 에 맞게 줄인 것이고, 스트림 응답 (SSE) 은 그 Work 위의 Work 스트림이다 (아래 Work 와 Work 스트림).
- keep-alive 는 라이브러리가 처리한다. `Connection: close`, 또는 `Connection: keep-alive` 가 없는 HTTP/1.0 이면 응답 후 닫는다. 라이브러리의 거절 (400 / 413 / 414 / 431 / 501) 과 Work 없는 스트림 body 의 500 도 `Connection: close` 를 붙이고 닫는다.
- `Expect: 100-continue` 는 라이브러리가 답한다. HTTP/1.1 요청에 body (Content-Length 가 0 보다 크거나 chunked) 가 있으면 헤더를 다 읽은 직후 `HTTP/1.1 100 Continue` 를 보내고 body 를 읽는다. Content-Length 가 `max_body_bytes` 를 넘으면 100 없이 413 을 보내고 닫는다. HTTP/1.0, body 없는 요청, `100-continue` 가 아닌 값은 무시한다 (417 은 보내지 않는다).
- 연결 단위의 끝과 오류는 끊김 / 오류 action 으로 받는다 (아래, 설계 결정 19). 요청 단위의 성공은 알리지 않는다 (라우트가 안다).

메서드별 동작

지원하는 메서드는 아래 7개다. 표의 "자동" 열은 사용자가 그 메서드 라우트를 등록하지 않았을 때 라이브러리가 하는 일이다. 사용자가 직접 등록하면 자동 처리는 하지 않는다.

| 메서드 | 용도 | 요청 body | 자동 |
|---|---|---|---|
| GET | 조회 | 없음 (와도 읽고 버린다) | 없음 |
| HEAD | GET 과 같은 헤더만 | 없음 | 같은 path 의 GET 라우트를 실행하고 body 는 보내지 않는다. Content-Length 는 GET 과 같게 둔다. 정적 서빙도 같다. 스트림 라우트는 아래 HEAD |
| POST | 생성, 명령 | 있음 | 없음 |
| PUT | 전체 교체 | 있음 | 없음 |
| PATCH | 부분 수정 | 있음 | 없음 |
| DELETE | 삭제 | 보통 없음 (오면 `request.body` 로 준다) | 없음 |
| OPTIONS | 허용 메서드 조회, CORS preflight | 없음 | 204 + `Allow` 헤더 (Content-Length 없음). 값은 그 path 에 등록된 메서드 + HEAD (GET 이 있으면) + OPTIONS. `OPTIONS *` 는 서버 전체에 등록된 메서드 |

- **405 Method Not Allowed.** path 가 맞는 라우트나 정적 경로가 있지만 그 method 가 없으면 405 와 `Allow` 헤더 (OPTIONS 자동 응답과 같은 값) 를 보낸다. `notFound` 는 부르지 않는다. 정적 경로의 `Allow` 는 `GET, HEAD, OPTIONS` 다.
- **501 Not Implemented.** CONNECT, TRACE, 그리고 7개 밖의 메서드. CONNECT 는 프록시용이고 TRACE 는 보안상 쓰지 않으므로 라우트로 등록할 수도 없다.
- **CORS.** 라이브러리는 CORS 헤더를 자동으로 붙이지 않는다. preflight 에 응답하려면 OPTIONS 라우트를 직접 등록하고 `Access-Control-Allow-*` 헤더를 넣는다 (미결: 간단한 CORS 설정 제공 여부).
- **body 를 읽는 기준은 메서드가 아니라 헤더다.** 어느 메서드든 `Content-Length` 가 있으면 그만큼 읽어서 연결의 다음 요청 경계를 맞춘다. `Transfer-Encoding: chunked` 요청은 라이브러리가 합쳐서 `request.body` 로 준다 (chunk 확장과 trailer 는 버린다). `Content-Length` 와 `Transfer-Encoding` 이 같이 오면 400, chunked 가 아닌 transfer coding 은 501 이다.

HEAD (설계 결정 16)

라이브러리가 GET 라우트 (와 정적 경로) 로 처리하고 body 바이트만 보내지 않는다. `onRequest` 는 `request->method` 로 HEAD 인 것을 알 수 있지만 따로 처리하지 않아도 된다. HEAD 요청은 등록 순서대로 HEAD 나 GET 이 맞는 첫 항목으로 간다 (같은 path 에 GET 라우트와 HEAD 라우트가 둘 다 있으면 먼저 등록한 쪽).

- 완성 응답: `Content-Length` 는 GET 일 때의 길이를 적고 body 를 떼고 보낸다.
- `onRequest` 에서 `setHttpBodyStream` 과 `startHttpWork` 를 부른 스트림 라우트: Work 를 열지 않는다. `startHttpWork` 는 SUCCESS 이고 `onWorkStarted` 는 불리지 않으며, 그때까지 놓인 status, 헤더, `Content-Type`, `Transfer-Encoding: chunked` 만 보내고 keep-alive 로 다음 요청을 읽는다. 그래서 스트림 라우트는 status 와 body 종류를 `onRequest` 에서 정하는 것을 권한다.
- 그 밖의 Work (body 종류를 worker 가 정하는 것): Work 를 그대로 연다. worker 가 완성 응답으로 끝내면 body 를 떼고 보낸다. worker 가 스트림을 열면 첫 `writeHttpWorkChunk` 가 헤더만 보내고 `KIOTTY_NET_CODE_CLOSED` 를 돌려주며, 그 뒤의 write 도 CLOSED 다. `finishHttpWorkWithResponse` 는 keep-alive 로 다음 요청을 읽는다. body 를 보내지 않는 status (1xx / 204 / 304) 에 스트림을 연 경우도 같다.

content type

- 목록 안이면 enum 하나로 끝난다. 라이브러리가 표에서 문자열을 꺼내 `Content-Type` 을 쓴다. 텍스트 계열 (TEXT_*, JAVASCRIPT, EVENT_STREAM) 은 `; charset=utf-8` 을 붙인다.
- 목록 밖이거나 파라미터가 필요하면 (`multipart/related; boundary=...`) `KiottyNet_setHttpCustomContentType(response, text)` 로 이름을 먼저 정하고 body setter 에 `KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM` 을 넘긴다. 이름은 `token/token` 뒤에 `; 이름=값` 이 0 개 이상, CR / LF 없음, UTF-8, `KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH` 미만이다. 그 응답 하나에만 유효하다. 이름 없이 CUSTOM 이 오면 body setter 가 `KIOTTY_NET_CODE_INVALID_ARGUMENT`.
- `setHttpHeader(response, "Content-Type", ...)` 는 `KIOTTY_NET_CODE_INVALID_ARGUMENT` 다 (이름 비교는 대소문자 무시).
- 자리가 정해진 값이 둘이다. `BY_EXTENSION` 은 파일 (`setHttpFile`, 정적 경로) 에만, `EVENT_STREAM` 은 스트림 (`setHttpBodyStream`) 에만 쓴다. 다른 자리면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. `BY_EXTENSION` 은 enum 주석의 확장자로 고르고 (대소문자 무시), 모르는 확장자는 `OCTET_STREAM` 이다.
- 텍스트 계열 (구조화 텍스트와 DICOM / FHIR 의 json / xml, SVG 포함) 은 UTF-8 을 본다. 복사 / 참조 body 는 setter 를 부를 때, 스트림은 chunk 마다, `serveHttpMemory` 는 등록 때. UTF-8 이 아니면 `KIOTTY_NET_CODE_INVALID_ENCODING` 이고 body 는 바뀌지 않는다. 파일과 CUSTOM 은 보지 않는다.
- status 만 놓고 body 가 없으면 `Content-Type` 은 없다 (202, 204 등).

정적 서빙

| 함수 | 매핑 | 요청 때 |
|---|---|---|
| `serveHttpDirectory(server, url_prefix, directory)` | `url_prefix` 는 `/` 로 끝나야 한다. `directory` 끝에 `/` 나 `\` 가 없으면 등록 때 `/` 를 붙인다 | prefix 뒤의 경로에 `..` 조각, `\`, `:` 가 있으면 403 (Windows 의 구분자, 드라이브, ADS 를 막는다). 붙인 경로가 `KIOTTY_NET_MAX_PATH_LENGTH` 를 넘거나, 파일이 없거나, 디렉토리면 404. index 파일 (`index.html`) 은 찾지 않는다. content type 은 확장자 |
| `serveHttpFile(server, url_path, file_path)` | content type 은 등록 때 확장자로 정한다 | 요청 때 열고 없으면 404 |
| `serveHttpMemory(server, url_path, type, data)` | 복사하지 않는다. data 는 서버 생명 동안 유효해야 한다. type 은 목록 안이어야 하고 `BY_EXTENSION`, `EVENT_STREAM`, `CUSTOM` 은 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. 텍스트 계열이면 등록 때 UTF-8 | 참조 body |

- 라우트 안의 파일은 `KiottyNet_setHttpFile(response, type, path)` 다. type 은 보통 `BY_EXTENSION`. 경로에 `..` 조각 (`/` 나 `\` 로 나눈) 이 있으면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`, 열지 못하면 `KIOTTY_NET_CODE_NOT_FOUND`. 절대 경로는 된다. Work 안에서도 쓸 수 있다.
- 파일은 응답 버퍼로 `send_capacity` 만큼씩 읽어 write lane 으로 보낸다. 헤더 블록이 이미 나간 응답 버퍼의 앞부분이 읽기 자리다. `sendfile` / `TransmitFile` 은 쓰지 않는다 (설계 결정 6). 보내는 중에 파일을 읽지 못하면 `KIOTTY_NET_CODE_NOT_FOUND` 로 닫는다. HEAD 는 Content-Length 만 보낸다.
- 파일은 Session 마다 하나 열 수 있고 (create 때 잡는다), 다음 요청이나 연결이 끝날 때 닫는다.

Work 와 Work 스트림

긴 작업. `onRequest` 안에서 `KiottyNet_startHttpWork(response)` 를 부르고 return 하면 응답은 보내지 않고 연결을 IO Thread 에서 떼어낸 뒤, 같은 loop 안에서 라우트의 `onWorkStarted(app_context, request, response, work)` 가 IO Thread 에서 한 번 온다. `onWorkStarted` 는 response 와 work 를 worker 에게 넘기고 return 한다. worker 는 둘 중 하나를 한다.

- 완성 응답: setter 로 `response` 를 채우고 `KiottyNet_finishHttpWorkWithResponse(work)`. 응답 버퍼의 내용이 나가고 keep-alive 면 다음 요청을 읽는다.
- 스트림: `KiottyNet_setHttpBodyStream(response, type)` 뒤 `KiottyNet_writeHttpWorkChunk(work, data, timeout)` 를 여러 번, 끝에 `finishHttpWorkWithResponse(work)`.
- 어느 쪽이든 끊으려면 `KiottyNet_finishHttpWorkWithClose(work, reason)`. 끝 chunk 없이 닫는다.

| 함수 | 규칙 |
|---|---|
| `startHttpWork` | `onRequest` 안에서 한 번만. 두 번째와 worker 에서의 호출은 `KIOTTY_NET_CODE_BUSY`. 라우트에 `onWorkStarted` 가 없으면 `KIOTTY_NET_CODE_INVALID_ARGUMENT` |
| `setHttpBodyStream` | body 를 스트림으로 정한다. `Transfer-Encoding: chunked`, Content-Length 없음. `onRequest` 에서도 worker 에서도 된다. `onRequest` 에서 정했는데 `startHttpWork` 없이 return 하면 라이브러리가 500 을 보내고 닫는다 (보낼 사람이 없다. 끊김 reason 은 SUCCESS) |
| `writeHttpWorkChunk` | chunk 하나를 보내고 다 나갈 때까지 기다린다 (동기). 첫 호출이면 상태줄 + 헤더를 먼저 보낸다. 라이브러리가 chunk 머리 (길이 hex CRLF) 와 꼬리 (CRLF) 를 씌운다. 돌려주는 `bytes` 는 `data.length` 다. timeout 은 호출 전체의 한도이고 0 은 무한. 돌아오면 data 는 버려도 된다 |
| `finishHttpWorkWithResponse` | 스트림이면 끝 chunk `0\r\n\r\n` 을 보낸다. chunk 를 한 번도 쓰지 않은 스트림이면 헤더 + 끝 chunk. 실패한 스트림이면 그 코드로 닫는다. 스트림이 아니면 완성 응답을 보낸다 |
| `finishHttpWorkWithClose` | 보내지 않고 닫는다. reason 은 끊김 action 의 reason 이 된다 |
| `isHttpWorkConnected` | peer 가 살아 있고 서버가 close 중이 아니면 참. 이벤트를 기다리는 worker 가 끊김을 볼 때 |

- finish 는 한 번만이다. 그때까지 request 는 유효하다. worker 가 오래 쓰는 값 (MCP 의 `Mcp-Session-Id`) 은 복사해 두는 것이 안전하다.
- setter 는 Work 중에도 된다. 스트림의 첫 write 뒤와 finish 뒤에는 `KIOTTY_NET_CODE_BUSY` (헤더가 이미 나갔다).
- `writeHttpWorkChunk` 의 코드: `SUCCESS`. `PEER_CLOSED` (상대가 끊음). `TIMEOUT` (일부가 나갔을 수 있다). `CLOSED` (서버 close, 또는 body 를 보내지 않는 응답). `BUSY` (finish 뒤). `INVALID_ARGUMENT` (body 가 스트림이 아님, 빈 data. 길이 0 chunk 는 끝이라). `INVALID_ENCODING` (텍스트 계열인데 UTF-8 아님. 아무것도 보내지 않았다). 한 번 실패하면 다음 write 도 같은 코드이고, 그 뒤는 `finishHttpWorkWithClose` 뿐이다.
- 텍스트 계열 스트림은 chunk 마다 UTF-8 을 보므로 문자 하나를 두 chunk 에 나눠 쓰면 안 된다. SSE 이벤트는 줄 단위라 그럴 일이 없다.
- 끝을 알리는 콜백은 없다. 상대가 끊으면 다음 write 가 `PEER_CLOSED`, 서버가 close 하면 `CLOSED` 다. 이벤트가 없는 동안 끊김을 빨리 알려면 worker 가 주기적으로 `: ping\n\n` 을 쓰거나 `isHttpWorkConnected` 를 본다.
- 큰 chunk 를 여러 write 로 나눠 보내도 사이에 끼어드는 것은 없다. Work 중에는 base 가 이 연결의 send / notify Stage 를 고르지 않는다 (설계 결정 7).
- 오래 가는 스트림 (MCP 의 GET 스트림) 은 그동안 worker 하나를 쓴다. 동시 스트림 수만큼 worker 가 필요하다 (설계 결정 4, 11).
- 서버 close: Work 중인 연결의 끊김 action (`CLOSED`) 은 worker 의 finish 전에 온다. worker 의 다음 write 가 `CLOSED` 를 돌려주므로 worker 가 finish 하고 끝난다. run 은 모든 Work 가 finish 된 뒤 `KIOTTY_NET_CODE_CLOSED` 로 돌아온다.

끊김 / 오류 action (설계 결정 19)

| 등록 | 언제 | 인자 |
|---|---|---|
| `KiottyNet_onHttpDisconnected(server, action, app_context)` | 연결마다 한 번, 끝날 때 | `info` (상대 주소), `reason`: `SUCCESS` (keep-alive 아님, 다 보냄), `TIMEOUT` (header / keep-alive timeout), `PEER_CLOSED`, `PEER_NOT_READING` (send timeout), `PROTOCOL_ERROR` (400 / 413 / 414 / 431 / 501 뒤, 줄 하나가 receive_capacity 를 넘음), `CLOSED` (서버 close), 또는 `finishHttpWorkWithClose` 의 reason |
| `KiottyNet_onHttpError(server, action, app_context)` | 연결이 생기기 전이나 연결과 따로 생기는 문제 | `info`, `code`, `message`: TLS handshake 실패 (`TLS_*`), accept 실패, 연결 수 초과. base 의 onError 가 받는 것을 그대로 넘긴다 |

- 라우트처럼 run 전에 등록하고 run 중의 등록은 `KIOTTY_NET_CODE_BUSY` 다. 다시 부르면 바꾸고, action 이 NULL 이면 지운다. app_context 는 라우트의 것과 따로다.
- 둘 다 IO Thread 에서 불린다. 끊김 action 은 연결의 handle 을 받지 않는다 (끊긴 뒤라 쓸 곳이 없다).
- 잘못된 요청은 끊김 action 의 reason 으로 오고 오류 action 으로 두 번 오지 않는다. base 는 연결 자리가 없으면 accept 를 멈추고 다음 연결을 OS backlog 에 두므로 연결 수 초과는 경합 때만 온다.

timeout

| 구간 | 한도 | 넘으면 |
|---|---|---|
| 요청 줄 | `header_timeout_milliseconds`. keep-alive 로 다음 요청을 기다릴 때는 `keep_alive_timeout_milliseconds` | 408 없이 닫는다. 끊김 reason `TIMEOUT` |
| 헤더 | 요청 줄이 온 때부터 `header_timeout_milliseconds` 안에 빈 줄까지 | 같다 |
| body 조각, chunk 크기 줄, trailer | 조각마다 `header_timeout_milliseconds` | 같다 |
| 응답 송신 (write lane 조각) | base 의 `send_timeout_milliseconds` (7절) | 끊김 reason `PEER_NOT_READING` |
| Work 의 write | `writeHttpWorkChunk` 의 인자 | 그 호출이 `TIMEOUT`. 연결은 worker 가 닫는다 |

- 줄 하나는 `min(receive_capacity, max_header_bytes + 2)` 까지 읽는다. 넘으면 base 가 `PROTOCOL_ERROR` 로 닫는다 (응답 없음). 헤더 공간이나 개수가 모자라면 431 이다.

thread

| 어디서든 | IO Thread (`onRequest` 안) 또는 Work 를 든 worker | IO Thread 만 |
|---|---|---|
| `finishHttpWork*`, `isHttpWorkConnected`, `closeHttpServer`, `awaitHttpServer` | setter 일곱. `startHttpWork` 는 `onRequest` 안만. `writeHttpWorkChunk` 는 worker 만 | `findHttpHeader` (request 가 유효한 동안), 등록 함수 (run 전) |

- 같은 응답을 두 thread 가 동시에 만지는 순간은 없다. `onRequest` 동안은 IO Thread, Work 중이면 worker 다.
- response / work handle 은 요청 하나의 것이다. 완성 응답은 `onRequest` return 까지 (Work 면 finish 까지) 쓸 수 있다. 송신이 시작된 뒤에는 `KIOTTY_NET_CODE_BUSY`, 다음 요청이 시작되거나 연결이 끝나면 `KIOTTY_NET_CODE_PEER_CLOSED` 다. HttpServer 를 destroy 한 뒤에도 `PEER_CLOSED` 만 돌아온다 (Context 의 풀이 은퇴만 한다).

공용 타입

```c
#define KIOTTY_NET_MAX_URL_LENGTH          256   /* 라우트 path, url_prefix, url_path 버퍼 (NUL 포함) */
#define KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH 64    /* content_type 버퍼 (NUL 포함) */

typedef enum KiottyNetHttpMethod {
    KIOTTY_NET_HTTP_METHOD_GET,
    KIOTTY_NET_HTTP_METHOD_HEAD,
    KIOTTY_NET_HTTP_METHOD_POST,
    KIOTTY_NET_HTTP_METHOD_PUT,
    KIOTTY_NET_HTTP_METHOD_PATCH,
    KIOTTY_NET_HTTP_METHOD_DELETE,
    KIOTTY_NET_HTTP_METHOD_OPTIONS,
    KIOTTY_NET_HTTP_METHOD_COUNT
} KiottyNetHttpMethod;
#define KIOTTY_NET_HTTP_METHOD_BIT(method) (1u << (method))   /* 메서드 집합을 uint32_t 비트 합으로 다룰 때 (Allow 헤더 등) */

typedef struct KiottyNetHttpLimits {     /* 3절의 설정 struct 규칙을 따른다. initialize 한 번, configure 여러 번 */
    uint32_t initialization_mark;
    size_t   max_routes;            /* 라우트 + 정적 경로 합계 (WebSocket 경로도 여기에 든다). 기본 64, 1..4096 */
    size_t   max_header_bytes;      /* request line + headers. 기본 8 KiB, 64..1 MiB. 넘으면 431 */
    size_t   max_headers;           /* 기본 32, 1..1024. 넘으면 431 */
    size_t   max_body_bytes;        /* 기본 64 KiB, 0..64 MiB. 넘으면 413. Upgrade 뒤 WebSocket 메시지 버퍼도 이 크기 */
    size_t   response_bytes;        /* 응답 버퍼. 상태줄 + 헤더 + 복사된 body. 스트림 chunk 의 조립, 파일 조각의 읽기 자리. 기본 64 KiB, 320..64 MiB */
    uint32_t header_timeout_milliseconds;     /* 요청 줄 / 헤더 / body 조각. 기본 10000 */
    uint32_t keep_alive_timeout_milliseconds; /* 다음 요청 대기. 기본 15000 */
    uint32_t websocket_ping_interval_milliseconds;  /* 기본 30000. WebSocket 경로가 있을 때만 쓴다. 0 이면 ping 을 보내지 않는다 */
    uint32_t websocket_pong_timeout_milliseconds;   /* 기본 10000 */
    uint32_t websocket_close_timeout_milliseconds;  /* 기본 5000 */
} KiottyNetHttpLimits;

KiottyNetCode KiottyNet_initializeHttpLimits(KiottyNetHttpLimits* limits);
KiottyNetCode KiottyNet_configureHttpMaxRoutes(KiottyNetHttpLimits* limits, size_t max_routes);
KiottyNetCode KiottyNet_configureHttpHeaderCapacity(KiottyNetHttpLimits* limits, size_t max_header_bytes, size_t max_headers);
KiottyNetCode KiottyNet_configureHttpBodyCapacity(KiottyNetHttpLimits* limits, size_t max_body_bytes);
KiottyNetCode KiottyNet_configureHttpResponseCapacity(KiottyNetHttpLimits* limits, size_t response_bytes);
KiottyNetCode KiottyNet_configureHttpTimeouts(KiottyNetHttpLimits* limits, uint32_t header_timeout_milliseconds, uint32_t keep_alive_timeout_milliseconds);
KiottyNetCode KiottyNet_configureHttpWebSocketTimers(KiottyNetHttpLimits* limits, uint32_t ping_interval_milliseconds, uint32_t pong_timeout_milliseconds, uint32_t close_timeout_milliseconds);

typedef enum KiottyNetHttpStatus {       /* setHttpStatus 의 인자는 int32_t 라 목록 밖의 코드 (100..599) 도 받는다 */
    KIOTTY_NET_HTTP_STATUS_SWITCHING_PROTOCOLS     = 101,   /* WebSocket Upgrade. 라이브러리만 보낸다 */
    KIOTTY_NET_HTTP_STATUS_OK                      = 200,
    KIOTTY_NET_HTTP_STATUS_CREATED                 = 201,
    KIOTTY_NET_HTTP_STATUS_ACCEPTED                = 202,   /* MCP : 알림 / 응답만 담은 POST */
    KIOTTY_NET_HTTP_STATUS_NO_CONTENT              = 204,
    KIOTTY_NET_HTTP_STATUS_MOVED_PERMANENTLY       = 301,
    KIOTTY_NET_HTTP_STATUS_FOUND                   = 302,
    KIOTTY_NET_HTTP_STATUS_NOT_MODIFIED            = 304,
    KIOTTY_NET_HTTP_STATUS_BAD_REQUEST             = 400,   /* 라이브러리 : 요청 형식 */
    KIOTTY_NET_HTTP_STATUS_UNAUTHORIZED            = 401,
    KIOTTY_NET_HTTP_STATUS_FORBIDDEN               = 403,   /* 라이브러리 : 정적 디렉토리를 벗어나는 경로 */
    KIOTTY_NET_HTTP_STATUS_NOT_FOUND               = 404,   /* 라이브러리 : 맞는 경로 없음, 없는 정적 파일 */
    KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED      = 405,   /* 라이브러리 : path 는 맞고 method 가 없음 */
    KIOTTY_NET_HTTP_STATUS_NOT_ACCEPTABLE          = 406,   /* MCP : Accept 가 맞지 않음 */
    KIOTTY_NET_HTTP_STATUS_REQUEST_TIMEOUT         = 408,
    KIOTTY_NET_HTTP_STATUS_CONFLICT                = 409,
    KIOTTY_NET_HTTP_STATUS_CONTENT_TOO_LARGE       = 413,   /* 라이브러리 : max_body_bytes */
    KIOTTY_NET_HTTP_STATUS_URI_TOO_LONG            = 414,   /* 라이브러리 : KIOTTY_NET_MAX_URL_LENGTH */
    KIOTTY_NET_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE  = 415,
    KIOTTY_NET_HTTP_STATUS_UPGRADE_REQUIRED        = 426,
    KIOTTY_NET_HTTP_STATUS_TOO_MANY_REQUESTS       = 429,
    KIOTTY_NET_HTTP_STATUS_HEADER_FIELDS_TOO_LARGE = 431,   /* 라이브러리 : max_header_bytes, max_headers */
    KIOTTY_NET_HTTP_STATUS_INTERNAL_SERVER_ERROR   = 500,   /* 라이브러리 : Work 없는 스트림 body */
    KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED         = 501,   /* 라이브러리 : CONNECT, TRACE, 모르는 method, chunked 아닌 transfer coding */
    KIOTTY_NET_HTTP_STATUS_SERVICE_UNAVAILABLE     = 503
} KiottyNetHttpStatus;

typedef enum KiottyNetHttpContentType {  /* 주석은 Content-Type 의 값과 BY_EXTENSION 의 확장자. (u) 는 UTF-8 을 보는 텍스트 계열 */
    KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM,      /* application/octet-stream .bin */
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN,        /* text/plain .txt (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_HTML,         /* text/html .html .htm (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_CSS,          /* text/css .css (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_CSV,          /* text/csv .csv (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_MARKDOWN,     /* text/markdown .md (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_JAVASCRIPT,        /* text/javascript .js .mjs (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM,      /* text/event-stream (u). setHttpBodyStream 만 */
    KIOTTY_NET_HTTP_CONTENT_TYPE_JSON,              /* application/json .json .map (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_PROBLEM_JSON,      /* application/problem+json (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_NDJSON,            /* application/x-ndjson .ndjson (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_MANIFEST_JSON,     /* application/manifest+json .webmanifest (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_XML,               /* application/xml .xml (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_YAML,              /* application/yaml .yaml .yml (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_FORM_URLENCODED,   /* application/x-www-form-urlencoded (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM,             /* application/dicom .dcm */
    KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM_JSON,        /* application/dicom+json (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM_XML,         /* application/dicom+xml (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_FHIR_JSON,         /* application/fhir+json (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_FHIR_XML,          /* application/fhir+xml (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_PROTOBUF,          /* application/x-protobuf .pb */
    KIOTTY_NET_HTTP_CONTENT_TYPE_MSGPACK,           /* application/msgpack .msgpack */
    KIOTTY_NET_HTTP_CONTENT_TYPE_CBOR,              /* application/cbor .cbor */
    KIOTTY_NET_HTTP_CONTENT_TYPE_PNG,               /* image/png .png */
    KIOTTY_NET_HTTP_CONTENT_TYPE_JPEG,              /* image/jpeg .jpg .jpeg */
    KIOTTY_NET_HTTP_CONTENT_TYPE_GIF,               /* image/gif .gif */
    KIOTTY_NET_HTTP_CONTENT_TYPE_WEBP,              /* image/webp .webp */
    KIOTTY_NET_HTTP_CONTENT_TYPE_AVIF,              /* image/avif .avif */
    KIOTTY_NET_HTTP_CONTENT_TYPE_BMP,               /* image/bmp .bmp */
    KIOTTY_NET_HTTP_CONTENT_TYPE_SVG,               /* image/svg+xml .svg (u) */
    KIOTTY_NET_HTTP_CONTENT_TYPE_ICON,              /* image/x-icon .ico */
    KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF,              /* font/woff .woff */
    KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF2,             /* font/woff2 .woff2 */
    KIOTTY_NET_HTTP_CONTENT_TYPE_TTF,               /* font/ttf .ttf */
    KIOTTY_NET_HTTP_CONTENT_TYPE_OTF,               /* font/otf .otf */
    KIOTTY_NET_HTTP_CONTENT_TYPE_MP3,               /* audio/mpeg .mp3 */
    KIOTTY_NET_HTTP_CONTENT_TYPE_WAV,               /* audio/wav .wav */
    KIOTTY_NET_HTTP_CONTENT_TYPE_OGG,               /* audio/ogg .ogg */
    KIOTTY_NET_HTTP_CONTENT_TYPE_MP4,               /* video/mp4 .mp4 */
    KIOTTY_NET_HTTP_CONTENT_TYPE_WEBM,              /* video/webm .webm */
    KIOTTY_NET_HTTP_CONTENT_TYPE_PDF,               /* application/pdf .pdf */
    KIOTTY_NET_HTTP_CONTENT_TYPE_ZIP,               /* application/zip .zip */
    KIOTTY_NET_HTTP_CONTENT_TYPE_GZIP,              /* application/gzip .gz */
    KIOTTY_NET_HTTP_CONTENT_TYPE_TAR,               /* application/x-tar .tar */
    KIOTTY_NET_HTTP_CONTENT_TYPE_WASM,              /* application/wasm .wasm */
    KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION,      /* 파일에만. 위의 확장자로 고르고 모르면 OCTET_STREAM */
    KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM,            /* 목록 밖, 또는 파라미터가 필요한 것. setHttpCustomContentType 의 문자열 */
    KIOTTY_NET_HTTP_CONTENT_TYPE_COUNT
} KiottyNetHttpContentType;

typedef struct KiottyNetHttpHeader { KiottyNetSpan name; KiottyNetSpan value; } KiottyNetHttpHeader;

typedef struct KiottyNetHttpRequest {
    KiottyNetConnectionHandle  connection;      /* peer 정보 (KiottyNet_getConnectionInfo) 에 쓴다 */
    KiottyNetHttpMethod        method;
    KiottyNetSpan              path;            /* query 제외. percent-decoding 하지 않음 */
    KiottyNetSpan              query;           /* '?' 뒤. 없으면 길이 0 */
    const KiottyNetHttpHeader* headers;
    size_t                     header_count;    /* 최대 KiottyNetHttpLimits::max_headers */
    KiottyNetSpan              body;            /* Content-Length 만큼, 또는 chunked 를 합친 것. 없으면 길이 0 */
    KiottyNetBool              keep_alive;
} KiottyNetHttpRequest;                         /* onRequest 가 return 하면 무효. startHttpWork 했으면 finish 까지 유효 */

typedef struct KiottyNetHttpServer* KiottyNetHttpServerHandle;

typedef struct KiottyNetHttpResponseHandle {    /* pool handle (규약 8). 응답을 채운다. id 는 Session index + generation */
    KiottyNetHttpServerHandle server;
    int64_t                   id;
} KiottyNetHttpResponseHandle;

typedef struct KiottyNetHttpWorkHandle {        /* pool handle. onWorkStarted 가 준다. finish 할 의무 */
    KiottyNetHttpServerHandle server;
    int64_t                   id;
} KiottyNetHttpWorkHandle;

typedef void (*KiottyNetHttpRequestAction)(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response);
typedef void (*KiottyNetHttpWorkAction)   (KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work);

typedef struct KiottyNetHttpRoute {
    KiottyNetAppContext        app_context;     /* onRequest / onWorkStarted 의 첫 인자로 돌아온다. 라이브러리는 읽지 않는다 */
    KiottyNetHttpRequestAction onRequest;       /* 요청이 다 들어오면 IO Thread 에서 한 번. NULL 이면 KIOTTY_NET_CODE_INVALID_ARGUMENT. 블로킹하지 않는다 */
    KiottyNetHttpWorkAction    onWorkStarted;   /* startHttpWork 뒤 IO Thread 에서 한 번. 긴 작업이 없는 라우트는 NULL */
} KiottyNetHttpRoute;

typedef struct KiottyNetHttpServerResult { KiottyNetCode code; KiottyNetHttpServerHandle handle; } KiottyNetHttpServerResult;

typedef void (*KiottyNetHttpDisconnectedAction)(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode reason);
typedef void (*KiottyNetHttpErrorAction)       (KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message);
```

C 함수

```c
KiottyNetHttpRoute KiottyNet_makeHttpRoute(KiottyNetAppContext app_context, KiottyNetHttpRequestAction onRequest, KiottyNetHttpWorkAction onWorkStarted);

KiottyNetSpan KiottyNet_findHttpHeader(const KiottyNetHttpRequest* request, const char* name);   /* 대소문자 무시. 없으면 { NULL, 0 } */

/* 응답 setter. onRequest 안 (IO Thread) 또는 Work 중 (worker). 닫힌 handle 은 KIOTTY_NET_CODE_PEER_CLOSED, 헤더가 이미 나갔으면 KIOTTY_NET_CODE_BUSY */
KiottyNetCode KiottyNet_setHttpStatus(KiottyNetHttpResponseHandle response, int32_t status);                                            /* 100..599, 밖이면 INVALID_ARGUMENT. 부르지 않으면 200. 아무 setter 도 부르지 않으면 404 */
KiottyNetCode KiottyNet_setHttpHeader(KiottyNetHttpResponseHandle response, const char* name, const char* value);                        /* name 은 token. Content-Length, Transfer-Encoding, Connection, Date, Content-Type 과 값의 제어 문자 (탭 제외) 는 INVALID_ARGUMENT. 값이 UTF-8 아니면 INVALID_ENCODING. 자리가 없으면 BUFFER_FULL */
KiottyNetCode KiottyNet_setHttpCustomContentType(KiottyNetHttpResponseHandle response, const char* content_type);                        /* 이 응답의 CUSTOM 이름. 다시 부르면 바꾼다 */
KiottyNetCode KiottyNet_setHttpBody(KiottyNetHttpResponseHandle response, KiottyNetHttpContentType type, KiottyNetSpan data);             /* 응답 버퍼로 복사. 자리가 없으면 BUFFER_FULL */
KiottyNetCode KiottyNet_setHttpBodyRef(KiottyNetHttpResponseHandle response, KiottyNetHttpContentType type, KiottyNetSpan data);          /* 복사하지 않음. data 는 송신 완료까지 유효해야 한다 */
KiottyNetCode KiottyNet_setHttpFile(KiottyNetHttpResponseHandle response, KiottyNetHttpContentType type, const char* path);               /* 파일. 보통 BY_EXTENSION. 응답 버퍼 조각으로 보낸다. ".." 은 INVALID_ARGUMENT, 열지 못하면 NOT_FOUND */
KiottyNetCode KiottyNet_setHttpBodyStream(KiottyNetHttpResponseHandle response, KiottyNetHttpContentType type);                           /* 스트림. Work 와 함께만 보낼 수 있다. BY_EXTENSION 은 INVALID_ARGUMENT */
KiottyNetCode KiottyNet_startHttpWork(KiottyNetHttpResponseHandle response);                                                             /* onRequest 안에서 한 번. 라우트에 onWorkStarted 가 없으면 INVALID_ARGUMENT, 두 번째와 worker 에서는 BUSY */
/* body setter 넷은 서로를 대체한다 (마지막 것이 body). 텍스트 계열인데 UTF-8 이 아니면 INVALID_ENCODING, 자리에 맞지 않는 type 이나 이름 없는 CUSTOM 은 INVALID_ARGUMENT */

/* Work 를 받은 worker 에서. finish 는 한 번만. finish 뒤와 Work 가 아닌 handle 은 KIOTTY_NET_CODE_BUSY, 닫힌 handle 은 KIOTTY_NET_CODE_PEER_CLOSED */
KiottyNetResult KiottyNet_writeHttpWorkChunk(KiottyNetHttpWorkHandle work, KiottyNetSpan data, uint32_t timeout_milliseconds);          /* chunk 하나를 보내고 기다린다. 첫 호출이 헤더를 보낸다. timeout 0 은 무한 */
KiottyNetCode   KiottyNet_finishHttpWorkWithResponse(KiottyNetHttpWorkHandle work);                                                      /* 완성 응답을 보내거나, 스트림의 끝 chunk 를 보낸다 */
KiottyNetCode   KiottyNet_finishHttpWorkWithClose(KiottyNetHttpWorkHandle work, KiottyNetCode reason);                                  /* 보내지 않고 닫는다 */
KiottyNetBool   KiottyNet_isHttpWorkConnected(KiottyNetHttpWorkHandle work);

KiottyNetHttpServerResult KiottyNet_createHttpServer(KiottyNetContext context, const KiottyNetServerConfiguration* configuration, const KiottyNetHttpLimits* limits);   /* 주소와 TLS 는 run 이 받는다. 실패하면 { code, NULL }: INVALID_ARGUMENT (설정이나 limits 미초기화), TABLE_FULL */
void          KiottyNet_destroyHttpServer(KiottyNetContext context, KiottyNetHttpServerHandle server);
KiottyNetCode KiottyNet_addHttpRoute(KiottyNetHttpServerHandle server, KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
KiottyNetCode KiottyNet_setHttpNotFound(KiottyNetHttpServerHandle server, KiottyNetHttpRoute route);                                    /* 아무 라우트도 안 맞을 때 */
KiottyNetCode KiottyNet_onHttpDisconnected(KiottyNetHttpServerHandle server, KiottyNetHttpDisconnectedAction on_disconnected, KiottyNetAppContext app_context);   /* NULL 이면 지운다 */
KiottyNetCode KiottyNet_onHttpError(KiottyNetHttpServerHandle server, KiottyNetHttpErrorAction on_error, KiottyNetAppContext app_context);                        /* NULL 이면 지운다 */
KiottyNetCode KiottyNet_serveHttpDirectory(KiottyNetHttpServerHandle server, const char* url_prefix, const char* directory);            /* "/static/" -> "./www/". 벗어나는 경로는 403. content type 은 확장자로 */
KiottyNetCode KiottyNet_serveHttpFile(KiottyNetHttpServerHandle server, const char* url_path, const char* file_path);                   /* "/favicon.ico" -> "./www/favicon.ico". content type 은 확장자로 */
KiottyNetCode KiottyNet_serveHttpMemory(KiottyNetHttpServerHandle server, const char* url_path, KiottyNetHttpContentType type, KiottyNetSpan data);   /* 메모리 바이너리. 복사하지 않으며 서버 생명 동안 유효해야 한다 */
/* 등록 함수 공통: run 중이면 BUSY, 같은 (method, path) 는 DUPLICATE_RULE, max_routes 초과는 TABLE_FULL, path 형식 / 길이 / onRequest NULL 은 INVALID_ARGUMENT */
KiottyNetCode KiottyNet_runHttpServer(KiottyNetHttpServerHandle server, const KiottyNetAddress* address, const KiottyNetTlsServerConfiguration* tls);   /* tls 가 NULL 이면 평문. close 뒤 KIOTTY_NET_CODE_CLOSED 로 돌아온다 */
void          KiottyNet_closeHttpServer(KiottyNetHttpServerHandle server);
KiottyNetCode KiottyNet_awaitHttpServer(KiottyNetHttpServerHandle server, uint64_t timeout_milliseconds);
```

C++ 래퍼

```cpp
class KiottyNet_HttpLimitsBuilder {                  // 3절의 builder 와 같다. 생성 시점에 KiottyNet_initializeHttpLimits
public:
    explicit KiottyNet_HttpLimitsBuilder(KiottyNetHttpLimits& limits);
    KiottyNet_HttpLimitsBuilder& maxRoutes(size_t max_routes);
    KiottyNet_HttpLimitsBuilder& headerCapacity(size_t max_header_bytes, size_t max_headers);
    KiottyNet_HttpLimitsBuilder& bodyCapacity(size_t max_body_bytes);
    KiottyNet_HttpLimitsBuilder& responseCapacity(size_t response_bytes);
    KiottyNet_HttpLimitsBuilder& timeouts(uint32_t header_timeout_milliseconds, uint32_t keep_alive_timeout_milliseconds);
    KiottyNet_HttpLimitsBuilder& webSocketTimers(uint32_t ping_interval_milliseconds, uint32_t pong_timeout_milliseconds, uint32_t close_timeout_milliseconds);
    KiottyNetCode build() const;                     // 처음 실패한 코드
};

class KiottyNet_HttpResponse {                       // pool handle 의 비소유 값 타입. 콜백 안에서 handle 로 만든다
public:
    explicit KiottyNet_HttpResponse(KiottyNetHttpResponseHandle handle);
    KiottyNetHttpResponseHandle handle() const;
    KiottyNetCode status(int32_t status);            // KiottyNetHttpStatus 를 그대로 넘긴다
    KiottyNetCode header(const char* name, const char* value);
    KiottyNetCode customContentType(const char* content_type);
    KiottyNetCode body(KiottyNetHttpContentType type, KiottyNetSpan data);
    KiottyNetCode bodyRef(KiottyNetHttpContentType type, KiottyNetSpan data);
    KiottyNetCode file(const char* path, KiottyNetHttpContentType type = KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION);   // 기본값 때문에 C 와 인자 순서가 다르다
    KiottyNetCode bodyStream(KiottyNetHttpContentType type);
    KiottyNetCode startWork();
};

class KiottyNet_HttpWork {                           // finish 의무를 소유한다. 복사 없음, 이동만. worker thread 로 이동해서 넘긴다
public:
    explicit KiottyNet_HttpWork(KiottyNetHttpWorkHandle work);
    KiottyNet_HttpWork(KiottyNet_HttpWork&& other);
    KiottyNet_HttpWork& operator=(KiottyNet_HttpWork&& other);   // 받는 쪽이 아직 finish 하지 않았으면 먼저 finishWithClose()
    ~KiottyNet_HttpWork();                           // 아직 finish 하지 않았으면 finishWithClose()
    bool            connected() const;
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);   // writeHttpWorkChunk
    KiottyNetCode   finishWithResponse();
    KiottyNetCode   finishWithClose(KiottyNetCode reason = KIOTTY_NET_CODE_SUCCESS);
};

class KiottyNet_HttpServer {                         // destroy 를 소유한다 (OwnedHandle). 이동만
public:
    KiottyNet_HttpServer(KiottyNetContext context, const KiottyNetServerConfiguration& configuration, const KiottyNetHttpLimits& limits);
    explicit operator bool() const;                  // create 가 실패하면 false
    KiottyNetCode             code() const;          // create 의 코드
    KiottyNetHttpServerHandle handle() const;

    // 라우트 등록. 두 가지 오버로드가 있다.
    //   KiottyNetHttpRoute         : app_context + onRequest + onWorkStarted. C 와 같은 모양
    //   KiottyNetHttpRequestAction : onRequest 하나. app_context, onWorkStarted 는 NULL. capture 없는 람다가 여기로 온다
    //                                server.get("/health", [](KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r) { KiottyNet_HttpResponse(r).status(200); });
    KiottyNetCode route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
    KiottyNetCode route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRequestAction on_request);
    // 메서드별 별칭 : route(KIOTTY_NET_HTTP_METHOD_XXX, ...) 와 같다. 각각 위와 같은 두 오버로드가 있다
    KiottyNetCode get    (const char* path, KiottyNetHttpRoute route);
    KiottyNetCode get    (const char* path, KiottyNetHttpRequestAction on_request);
    KiottyNetCode head   (const char* path, KiottyNetHttpRoute route);       // 등록하지 않으면 GET 라우트로 자동 처리
    KiottyNetCode head   (const char* path, KiottyNetHttpRequestAction on_request);
    KiottyNetCode post   (const char* path, KiottyNetHttpRoute route);
    KiottyNetCode post   (const char* path, KiottyNetHttpRequestAction on_request);
    KiottyNetCode put    (const char* path, KiottyNetHttpRoute route);
    KiottyNetCode put    (const char* path, KiottyNetHttpRequestAction on_request);
    KiottyNetCode patch  (const char* path, KiottyNetHttpRoute route);
    KiottyNetCode patch  (const char* path, KiottyNetHttpRequestAction on_request);
    KiottyNetCode del    (const char* path, KiottyNetHttpRoute route);       // DELETE
    KiottyNetCode del    (const char* path, KiottyNetHttpRequestAction on_request);
    KiottyNetCode options(const char* path, KiottyNetHttpRoute route);       // 등록하지 않으면 204 + Allow 자동 응답
    KiottyNetCode options(const char* path, KiottyNetHttpRequestAction on_request);
    KiottyNetCode notFound(KiottyNetHttpRoute route);
    KiottyNetCode notFound(KiottyNetHttpRequestAction on_request);
    KiottyNetCode onDisconnected(KiottyNetHttpDisconnectedAction on_disconnected, KiottyNetAppContext app_context);
    KiottyNetCode onError(KiottyNetHttpErrorAction on_error, KiottyNetAppContext app_context);

    KiottyNetCode serveDirectory(const char* url_prefix, const char* directory);
    KiottyNetCode serveFile(const char* url_path, const char* file_path);
    KiottyNetCode serveMemory(const char* url_path, KiottyNetHttpContentType type, KiottyNetSpan data);

    KiottyNetCode run(const KiottyNetAddress& address);
    KiottyNetCode run(const KiottyNetAddress& address, const KiottyNetTlsServerConfiguration& tls);   // TLS
    void          close();
    KiottyNetCode await(uint64_t timeout_milliseconds);
};
```

사용 예

```cpp
KiottyNet_HttpServer server(context.handle(), config, limits);
if (!server) { return server.code(); }

server.get("/health", [](KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r) {
    KiottyNet_HttpResponse(r).body(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, KiottyNetSpan{ "{\"ok\":true}", 11 });
});

// 긴 작업 : onRequest 에서 startWork, onWorkStarted 에서 worker 로 넘기고, worker 가 응답을 채워 finish
server.get("/report", KiottyNet_makeHttpRoute(&app,
    [](KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r) {
        KiottyNet_HttpResponse(r).startWork();
    },
    [](KiottyNetAppContext ctx, const KiottyNetHttpRequest* q, KiottyNetHttpResponseHandle r, KiottyNetHttpWorkHandle w) {
        std::string        query(q->query.data, q->query.length);
        KiottyNet_HttpWork work(w);
        static_cast<App*>(ctx)->workerPool.post([query, r, work = std::move(work)]() mutable {   // C++14 init capture
            Result result = compute(query);
            if (!work.connected()) { return; }                                                // 끊겼으면 소멸자의 finishWithClose
            KiottyNet_HttpResponse(r).body(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, result.span());
            work.finishWithResponse();
        });
    }));

// 스트림 (SSE) : 헤더와 body 종류는 onRequest 에서 정한다. HEAD 면 이것만 나가고 Work 는 열리지 않는다
server.get("/events", KiottyNet_makeHttpRoute(&app,
    [](KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r) {
        KiottyNet_HttpResponse response(r);
        response.header("Cache-Control", "no-cache");
        response.bodyStream(KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
        response.startWork();
    },
    [](KiottyNetAppContext ctx, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle, KiottyNetHttpWorkHandle w) {
        KiottyNet_HttpWork work(w);
        static_cast<App*>(ctx)->workerPool.post([ctx, work = std::move(work)]() mutable {
            std::string event;
            while (static_cast<App*>(ctx)->events.waitFor(event, 15000))                    // 이벤트가 없으면 끝
            {
                KiottyNetResult sent = work.write(KiottyNetSpan{ event.data(), event.size() }, 5000);   // "data: ...\n\n"
                if (sent.code != KIOTTY_NET_CODE_SUCCESS) { return; }                        // PEER_CLOSED, CLOSED, TIMEOUT. 소멸자가 finishWithClose
            }
            work.finishWithResponse();                                                       // 끝 chunk. keep-alive 면 다음 요청
        });
    }));

server.onDisconnected(onDisconnected, &app);                                    // 감사 로그 : info 와 끝난 이유
server.serveDirectory("/", "./www/");                                           // 라우트 뒤에 등록한다 (등록 순서 = 매칭 순서)
server.serveMemory("/app.wasm", KIOTTY_NET_HTTP_CONTENT_TYPE_WASM, wasm);
KiottyNetAddress address;
KiottyNet_initializeTcpAddress(&address, "0.0.0.0", 8080);
std::thread io([&] { server.run(address); });                                   // TLS 면 server.run(address, tls)
server.await(3000);
```

MCP Streamable HTTP 서버의 골격 (POST / GET / DELETE `/mcp`, 202, 404, 406, SSE 응답, GET 스트림) 은 [design_http_v0.html](../design/design_http_v0.html) 6절과 `tests/integration/kiotty_test_http_mcp.cpp` 다.

### 10. WebSocket (HttpServer 의 Upgrade 경로)

> 이 절은 `include/kiotty_net_websocket.h` 의 공개 API 판 v2 (cycle W1, 2026-11-12) 와 그 구현 (cycle W2 ~ W4, 2026-11-12 ~ 13) 을 적는다. 설계의 근거는 [design_websocket_v0.html](../design/design_websocket_v0.html) 의 결정 1 ~ 13, 사용법은 [user_guide_websocket.md](user_guide_websocket.md), 안의 모양은 [implementation_guide_v0.md](implementation_guide_v0.md) 5절이다. 판 v1 의 `KiottyNet_WebSocketServer`, `KiottyNetWebSocketConfiguration` 과 그 builder 는 없다.

WebSocket (RFC 6455) 은 HTTP 연결에서 시작한다. 서버 쪽 WebSocket 은 따로 선 서버가 아니라 **HttpServer (9절) 에 등록한 Upgrade 경로**다. 한 HttpServer, 한 포트에 HTTP 라우트, 정적 경로, WebSocket 경로 여럿이 같이 있다. 클라이언트 쪽은 6절 EventClient 위의 WebSocketClient 다 (11절).

- **include.** `kiotty_net_websocket.h` 가 `kiotty_net_http.h` 를 include 한다 (그 안에 base.h). 반대 방향은 없다. `kiotty_net_statemachine.h` 는 include 하지 않는다.
- **등록.** `KiottyNet_addWebSocket(server, path, handler, app_context)`. run 전에만 되고 (run 중이면 `KIOTTY_NET_CODE_BUSY`) handler 는 값으로 복사해 고정한다. `app_context` 는 run 이 return 할 때까지 살아 있어야 한다. path 는 HTTP 라우트와 같은 규칙 (`/` 로 시작, `KIOTTY_NET_MAX_URL_LENGTH` 미만) 이고 WebSocket 경로는 그 path 의 모든 메서드를 차지한다. 같은 path 의 라우트나 WebSocket 경로가 있으면 `KIOTTY_NET_CODE_DUPLICATE_RULE`, 표가 차면 `KIOTTY_NET_CODE_TABLE_FULL` (`max_routes` 에 든다). handler 의 `onOpen` 이나 `onMessage` 가 NULL 이면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`.
- **설정.** WebSocket 전용 설정 struct 는 없다. HttpServer 의 `KiottyNetHttpLimits` 한 벌을 쓴다. 메시지 상한은 `max_body_bytes`, 핸드셰이크는 `max_header_bytes` / `max_headers` / `header_timeout_milliseconds`, 타이머 셋은 `websocket_ping_interval_milliseconds` / `websocket_pong_timeout_milliseconds` / `websocket_close_timeout_milliseconds` 다 (`KiottyNet_configureHttpWebSocketTimers`). 경로마다 다른 값은 둘 수 없다. 타이머의 0 은 무한이다: ping 간격 0 은 ping 을 보내지 않고, pong timeout 0 은 ping 은 보내되 pong 을 검사하지 않으며, close timeout 0 은 상대의 close frame 을 끝없이 기다린다.
- **Upgrade 요청의 처리.** 등록된 path 의 요청은 HTTP 헤더처럼 읽은 뒤 이렇게 갈린다. WebSocket 경로는 라우트 표에 GET 하나로 들어 있으므로 GET / HEAD 밖의 메서드는 9절의 자동 응답이다. HEAD 와 GET 의 거절 (405 / 400 / 426 / 403) 은 `Connection: close` 를 붙이고 닫는다.

| 요청 | 응답 |
|---|---|
| OPTIONS | 9절의 자동 204, `Allow: GET, HEAD, OPTIONS` |
| GET / HEAD / OPTIONS 밖의 메서드 | 9절의 자동 405, `Allow: GET, HEAD, OPTIONS` |
| HEAD | 405, `Allow: GET` |
| body 가 있는 GET | 400 |
| `Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Version: 13`, `Sec-WebSocket-Key` 중 하나가 없음 | 426, `Upgrade: websocket`, `Sec-WebSocket-Version: 13`. `onUpgrade` 는 부르지 않는다 |
| `Sec-WebSocket-Key` 가 16 바이트의 base64 (24 글자) 가 아님 | 400 |
| `onUpgrade` 가 0 | 403 |
| `onUpgrade` 가 1 이거나 NULL | 101, `Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Accept`, (골랐으면) `Sec-WebSocket-Protocol`. 송신이 끝나면 `onOpen` |

- **101 뒤.** 같은 연결, 같은 HttpSession 이 WebSocket frame 행으로 넘어간다. 이 연결에 HTTP 는 다시 없다. 101 을 만들 때 Session 의 generation 을 올리므로 **`onUpgrade` 가 받은 handle 과 `onOpen` 이 받은 handle 은 다르다.** `onUpgrade` 의 handle 은 `onUpgrade` 안에서만 쓰고 (`findWebSocketUpgradeHeader`, `selectWebSocketProtocol`), 연결이 끝날 때까지 쓰는 것은 `onOpen` 의 handle 이다.
- **context 는 둘이다** (5절과 같다). `KiottyNetAppContext` 는 `onUpgrade`, `onOpen`, `onClose` 가 받는다. `onOpen` 이 return 한 `KiottyNetWebSocketContext` 는 소켓에 묶여 `onMessage` 의 첫 인자와 `onClose` 로 돌아온다.
- **수신.** 조각 (fragment) 은 라이브러리가 합쳐서 `onMessage` 에 메시지 하나로 준다. 메시지는 body 의 Stage 에 모인다. frame payload 는 `receive_capacity` 단위로 나눠 읽으므로 메시지가 `receive_capacity` 보다 커도 된다. 합친 크기가 `max_body_bytes` 를 넘으면 1009, TEXT 가 UTF-8 이 아니면 1007, 그 밖의 잘못된 frame (예약 비트, 모르는 opcode, 앞 조각 없는 continuation, 서버가 받은 frame 에 mask 가 없음, 클라이언트가 받은 frame 에 mask 가 있음, 최소 표현이 아닌 길이, 잘못된 close payload) 은 1002 로 닫는다.
- **ping / pong.** 라이브러리가 처리한다. 들어온 ping 에는 같은 payload 의 pong 을 보낸다 (닫는 중이면 보내지 않는다). 받은 frame 없이 `websocket_ping_interval_milliseconds` 가 지나면 ping 을 보내고, `websocket_pong_timeout_milliseconds` 안에 pong 이 없으면 1001 close frame 을 보내고 `websocket_close_timeout_milliseconds` 동안 상대의 close frame 을 기다린다.
- **송신의 두 길.** 사용자의 `send` / `broadcast`, 라이브러리의 ping / pong, `closeWebSocket` 과 pong 늦음의 close frame 은 **push lane** 이다 (4절의 notify 와 같은 길). 그래서 `send` 는 아무 thread 에서나 부르고 완료 콜백이 없다. frame 머리와 payload 를 한 번에 넣고 전부 들어가지 않으면 아무것도 넣지 않는다 (`KIOTTY_NET_CODE_BUFFER_FULL`). frame 하나가 `push_capacity` 를 넘으면 `KIOTTY_NET_CODE_INVALID_ARGUMENT` 이고 조각으로 나눠 보내지 않는다. **write lane** 은 핸드셰이크 (서버의 101 / 403 / 426 / 405 / 400, 클라이언트의 Upgrade 요청) 와, frame 을 읽던 행이 닫을 때의 close frame (1002 / 1007 / 1009, 상대의 close 에 대한 답) 이다. 뒤의 것은 close frame 을 다 보낸 뒤에 연결을 닫아야 해서다 (close 는 push lane 의 남은 것을 보내지 않는다).
- **mask.** 서버가 보내는 frame 은 mask 하지 않는다. 클라이언트가 보내는 frame 은 frame 마다 새 mask key 로 mask 한다 (11절).
- **close.** `KiottyNet_closeWebSocket` 은 아무 thread 에서나 close handshake 를 시작한다. 소켓의 닫힘 플래그를 세우고 close frame 을 push lane 에 넣는다. 그 뒤의 `send` 는 `KIOTTY_NET_CODE_CLOSED`, 두 번째 close 는 `KIOTTY_NET_CODE_BUSY`, 그 사이에 온 데이터 frame 은 버린다. IO Thread 콜백 안에서 부르면 다음 read 부터 `close_timeout` 이 걸린다. 다른 thread 에서 부르면 IO Thread 를 깨우지 않으므로, 상대가 끝까지 답하지 않으면 걸려 있던 read 의 남은 시간 (최대 `websocket_ping_interval_milliseconds`) 뒤에 `TIMEOUT` 으로 끊긴다 (설계 결정 3, 9).
- **onClose.** 마지막 콜백이고 `onOpen` 이 나간 소켓에만 한 번 온다. `KiottyNetWebSocketClose` 의 `code` 는 끊긴 이유, `status` / `reason` 은 상대의 close frame 의 것이다. 아래 표.

| `code` | `status` | 언제 |
|---|---|---|
| `KIOTTY_NET_CODE_SUCCESS` | 상대가 보낸 값. 코드 없는 close frame 이면 1005 | close handshake 가 끝났다. 누가 시작했든 |
| `KIOTTY_NET_CODE_PEER_CLOSED` | 1006 | close frame 없이 TCP 가 끊겼다 |
| `KIOTTY_NET_CODE_SOCKET_ERROR` | 1006 | OS 소켓 에러 |
| `KIOTTY_NET_CODE_TIMEOUT` | 우리가 보낸 값 (pong 늦음이면 1001) | 우리 close frame 에 상대가 `close_timeout` 안에 답하지 않았다 |
| `KIOTTY_NET_CODE_PROTOCOL_ERROR` | 우리가 보낸 1002 / 1007 | 받은 frame 이 잘못됐다 |
| `KIOTTY_NET_CODE_REQUEST_TOO_LARGE` | 우리가 보낸 1009 | 메시지가 `max_body_bytes` 를 넘었다 |
| `KIOTTY_NET_CODE_CLOSED` | 1001 | 소유자의 close (`KiottyNet_closeHttpServer` / `KiottyNet_closeWebSocketClient`). close handshake 없이 끊는다 |

- 상대가 close frame 을 보냈으면 `code` 가 무엇이든 `status` / `reason` 은 그 frame 의 것이다. HttpServer 에서는 `onClose` 뒤에 HttpServer 의 끊김 action (`onHttpDisconnected`) 도 온다.
- **broadcast.** `KiottyNet_broadcastWebSocket(server, path, opcode, data)` 는 등록에 쓴 path 문자열로 그 경로의 열린 (`onOpen` 이 나갔고 닫는 중이 아닌) 소켓 전부에 `send` 한다. path 가 NULL 이면 그 서버의 모든 WebSocket 소켓. 등록되지 않은 path 는 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. 여유 없는 소켓은 건너뛰고 끊지 않으며 하나라도 건너뛰면 `KIOTTY_NET_CODE_BUFFER_FULL`.
- **subprotocol 은 서버만 고른다** (설계 결정 12). `onUpgrade` 안에서 `KiottyNet_selectWebSocketProtocol` 로 요청의 후보 중 하나를 고르면 101 에 실린다. 후보에 없으면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`, `onUpgrade` 밖이면 `KIOTTY_NET_CODE_BUSY`.
- **두지 않는 것.** WebSocket 의 Work (긴 작업은 `onMessage` 에서 복사해 worker 로, 결과는 `send`), 큰 메시지의 조각 송신, 수신 메시지의 streaming 전달, permessage-deflate 등 확장, 경로마다 다른 상한 / 타이머.

공용 타입

```c
#define KIOTTY_NET_MAX_URL_PATH_LENGTH            256   /* 지금은 쓰지 않는다. 경로의 상한은 KIOTTY_NET_MAX_URL_LENGTH (9절) */
#define KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH 128   /* 고른 subprotocol 버퍼 (NUL 포함) */
#define KIOTTY_NET_MAX_WEB_SOCKET_REASON_LENGTH   124   /* close reason (NUL 포함) */

typedef enum KiottyNetWebSocketOpcode { KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT = 1, KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY = 2 } KiottyNetWebSocketOpcode;

typedef enum KiottyNetWebSocketStatus {      /* close frame 의 status. 함수는 uint16_t 로 받으므로 앱 정의 값 (3000~4999) 도 넣는다 */
    KIOTTY_NET_WEB_SOCKET_STATUS_NORMAL_CLOSURE   = 1000,
    KIOTTY_NET_WEB_SOCKET_STATUS_GOING_AWAY       = 1001,
    KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR   = 1002,
    KIOTTY_NET_WEB_SOCKET_STATUS_UNSUPPORTED_DATA = 1003,
    KIOTTY_NET_WEB_SOCKET_STATUS_NO_STATUS        = 1005,   /* 받기만 한다. 보낼 수 없다 */
    KIOTTY_NET_WEB_SOCKET_STATUS_ABNORMAL_CLOSURE = 1006,   /* 받기만 한다. 보낼 수 없다 */
    KIOTTY_NET_WEB_SOCKET_STATUS_INVALID_PAYLOAD  = 1007,
    KIOTTY_NET_WEB_SOCKET_STATUS_POLICY_VIOLATION = 1008,
    KIOTTY_NET_WEB_SOCKET_STATUS_MESSAGE_TOO_BIG  = 1009,
    KIOTTY_NET_WEB_SOCKET_STATUS_INTERNAL_ERROR   = 1011
} KiottyNetWebSocketStatus;

typedef struct KiottyNetWebSocketMessage {
    KiottyNetWebSocketOpcode opcode;
    KiottyNetSpan            data;      /* 수신 Stage. onMessage 가 return 하면 무효. TEXT 는 UTF-8 검사를 지난 것 */
} KiottyNetWebSocketMessage;

typedef struct KiottyNetWebSocketClose {    /* onClose 가 받는다. 위의 표 */
    KiottyNetCode code;
    uint16_t      status;
    KiottyNetSpan reason;                   /* 상대 close frame 의 reason. onClose 가 return 하면 무효 */
} KiottyNetWebSocketClose;

typedef struct KiottyNetWebSocketEndpoint KiottyNetWebSocketEndpoint;   /* 소켓을 가진 쪽 (HttpServer 또는 WebSocketClient). 불투명 */

typedef struct KiottyNetWebSocketHandle {   /* pool handle (규약 8). 무효는 { NULL, 0 } */
    KiottyNetWebSocketEndpoint* owner;
    int64_t                     id;         /* generation + Session index */
} KiottyNetWebSocketHandle;

typedef void* KiottyNetWebSocketContext;    /* 소켓마다의 데이터. onOpen 이 돌려준다. 라이브러리는 읽지 않는다 */

typedef struct KiottyNetWebSocketUpgrade {  /* onUpgrade 가 받는 Upgrade 요청. 요청 버퍼를 가리키며 return 하면 무효 */
    KiottyNetSpan path;
    KiottyNetSpan query;
    KiottyNetSpan host;
    KiottyNetSpan origin;
    KiottyNetSpan protocols;                /* 요청의 Sec-WebSocket-Protocol 원문 */
} KiottyNetWebSocketUpgrade;

typedef struct KiottyNetWebSocketInfo {
    KiottyNetConnectionInfo connection;
    char                    protocol[KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH];   /* 고른 subprotocol. 없으면 빈 문자열 */
} KiottyNetWebSocketInfo;

typedef struct KiottyNetWebSocketHandler {  /* 함수 표만 담는다 */
    KiottyNetBool             (*onUpgrade)(KiottyNetAppContext app_context, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketUpgrade upgrade);   /* 서버만. 0 이면 403. NULL 이면 모두 허용 */
    KiottyNetWebSocketContext (*onOpen)   (KiottyNetAppContext app_context, KiottyNetWebSocketHandle websocket);                                        /* 101 송신 완료 (서버) / 101 수신 (클라이언트). 필수 */
    void                      (*onMessage)(KiottyNetWebSocketContext websocket_context, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketMessage message);   /* 필수 */
    void                      (*onClose)  (KiottyNetAppContext app_context, KiottyNetWebSocketContext websocket_context, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketClose close);   /* 마지막 콜백. NULL 이어도 된다 */
} KiottyNetWebSocketHandler;
```

C 함수

```c
/* 소켓 하나. owner 가 NULL 인 handle 은 PEER_CLOSED (info 는 빈 값, header 는 { NULL, 0 }) */
KiottyNetWebSocketInfo KiottyNet_getWebSocketInfo(KiottyNetWebSocketHandle websocket);                                     /* 열린 소켓이 아니면 빈 값 */
KiottyNetSpan KiottyNet_findWebSocketUpgradeHeader(KiottyNetWebSocketHandle websocket, const char* name);                  /* onUpgrade 안에서만. 대소문자 무시. 없으면 { NULL, 0 } */
KiottyNetCode KiottyNet_selectWebSocketProtocol(KiottyNetWebSocketHandle websocket, const char* protocol);                 /* onUpgrade 안에서만 (밖이면 BUSY). 요청의 후보가 아니면 INVALID_ARGUMENT. 다시 부르면 바꾼다 */
KiottyNetCode KiottyNet_sendWebSocket(KiottyNetWebSocketHandle websocket, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);   /* 아무 thread. 빈 data, 모르는 opcode, frame 이 push_capacity 초과는 INVALID_ARGUMENT. 닫은 뒤 CLOSED. 닫힌 handle 은 PEER_CLOSED */
KiottyNetCode KiottyNet_closeWebSocket(KiottyNetWebSocketHandle websocket, uint16_t status, const char* reason);           /* 아무 thread. status 는 보낼 수 있는 값 (1000 ~ 1014 중 1004 / 1005 / 1006 제외, 3000 ~ 4999). reason 은 NULL 이거나 124 미만의 UTF-8. 두 번째는 BUSY */

/* 서버 : HttpServer 의 Upgrade 경로 */
KiottyNetCode KiottyNet_addWebSocket(KiottyNetHttpServerHandle server, const char* path, KiottyNetWebSocketHandler handler, KiottyNetAppContext app_context);   /* run 전. handler 는 복사 */
KiottyNetCode KiottyNet_broadcastWebSocket(KiottyNetHttpServerHandle server, const char* path, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);          /* 아무 thread. path NULL 이면 모든 경로 */
```

run / close / await 는 HttpServer 의 것이다 (9절). WebSocketClient 의 함수는 11절.

C++ 래퍼

```cpp
class KiottyNet_WebSocket {                          // pool handle 의 비소유 값 타입
public:
    explicit KiottyNet_WebSocket(KiottyNetWebSocketHandle handle);
    KiottyNetWebSocketHandle handle() const;         // 다른 thread 에 넘길 값
    KiottyNetWebSocketInfo   info() const;
    KiottyNetSpan            upgradeHeader(const char* name) const;   // onUpgrade 안에서만
    KiottyNetCode            selectProtocol(const char* protocol);    // onUpgrade 안에서만
    KiottyNetCode            send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
    KiottyNetCode            close(uint16_t status = KIOTTY_NET_WEB_SOCKET_STATUS_NORMAL_CLOSURE, const char* reason = nullptr);
    bool operator==(const KiottyNet_WebSocket& other) const;          // owner 와 id
    bool operator!=(const KiottyNet_WebSocket& other) const;
    bool operator<(const KiottyNet_WebSocket& other) const;           // owner (std::less), 그 다음 id
};
```

`KiottyNet_HttpServer` 에는 WebSocket 메서드가 없다 (`kiotty_net_http.h` 는 이 헤더를 모른다). `KiottyNet_addWebSocket(server.handle(), ...)` 로 부른다.

사용 예

```cpp
// IO Thread
KiottyNetWebSocketContext onOpen(KiottyNetAppContext ctx, KiottyNetWebSocketHandle ws)                                         { static_cast<App*>(ctx)->clients.add(ws); return nullptr; }
void onMessage(KiottyNetWebSocketContext, KiottyNetWebSocketHandle ws, KiottyNetWebSocketMessage message)                        { KiottyNet_WebSocket(ws).send(message.opcode, message.data); }   // echo
void onClose(KiottyNetAppContext ctx, KiottyNetWebSocketContext, KiottyNetWebSocketHandle ws, KiottyNetWebSocketClose)          { static_cast<App*>(ctx)->clients.remove(ws); }

static const KiottyNetWebSocketHandler CHAT = { nullptr, onOpen, onMessage, onClose };
KiottyNetHttpLimits limits;
KiottyNet_HttpLimitsBuilder(limits).bodyCapacity(16 * 1024).webSocketTimers(30000, 10000, 5000).build();
KiottyNet_HttpServer server(context.handle(), config, limits);
server.serveDirectory("/", "./www/");                                           // 같은 포트의 웹 UI
KiottyNet_addWebSocket(server.handle(), "/chat", CHAT, &app);
std::thread io([&] { server.run(address); });

// 앱 thread : 아무 thread 에서나
KiottyNet_broadcastWebSocket(server.handle(), "/chat", KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, message.span());
for (KiottyNetWebSocketHandle ws : app.clients.snapshot()) KiottyNet_WebSocket(ws).send(KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, message.span());   // 이미 닫힌 handle 은 KIOTTY_NET_CODE_PEER_CLOSED
```

### 11. HttpClient (초안) / WebSocketClient

HttpClient 는 초안이다. "정말 간략하고 쓰기 쉬운" 인터페이스를 목표로 피드백을 받으며 다듬는다. 열린 질문은 절 끝에 있다.

**HttpClient.** 6절 StreamClient 위에 얹은 동기 API 다. IO Thread 가 없고 호출한 thread 에서 보내고 기다린다.

- 첫 요청에서 연결하고 keep-alive 로 유지한다. 서버가 끊었으면 (`KIOTTY_NET_CODE_PEER_CLOSED`) 다음 요청에서 다시 연결한다. 사용자는 연결을 신경 쓰지 않는다.
- 응답은 client 안의 고정 크기 버퍼에 들어가고 `KiottyNetHttpReply` 는 그 버퍼를 가리킨다. **다음 요청까지만 유효하다.** 보관하려면 복사한다.
- 응답 body 가 `max_body_bytes` 를 넘으면 `KIOTTY_NET_CODE_BUFFER_FULL` 이고 연결을 끊는다. chunked 응답은 라이브러리가 합친다.
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

typedef struct KiottyNetHttpReply {         /* 값 + code (2절). C++ 도 이 struct 를 그대로 돌려준다. code != KIOTTY_NET_CODE_SUCCESS 면 나머지 필드는 0. 다음 요청까지 유효 */
    KiottyNetCode              code;
    int                        status;
    const KiottyNetHttpHeader* headers;
    size_t                     header_count;
    KiottyNetSpan              body;
} KiottyNetHttpReply;

typedef struct KiottyNetHttpClient* KiottyNetHttpClientHandle;
KiottyNetHttpClientHandle KiottyNet_createHttpClient(const KiottyNetClientConfiguration* config, const KiottyNetTlsClientConfiguration* tls, const KiottyNetHttpClientLimits* limits);   /* address.host 가 Host 헤더가 된다 */
void          KiottyNet_destroyHttpClient(KiottyNetHttpClientHandle c);

/* 요청 하나. headers 는 사용자 배열 (NULL, 0 가능). body.length == 0 이면 body 없음. timeout_milliseconds 는 연결 + 송신 + 응답 수신 전체 */
KiottyNetHttpReply KiottyNet_requestHttp(KiottyNetHttpClientHandle c, KiottyNetHttpMethod method, const char* path,
                                         const KiottyNetHttpHeader* headers, size_t header_count,
                                         KiottyNetSpan body, const char* content_type, uint32_t timeout_milliseconds);
/* 메서드별. requestHttp(c, KIOTTY_NET_HTTP_METHOD_XXX, path, NULL, 0, body, content_type, timeout_milliseconds) 와 같다 */
KiottyNetHttpReply KiottyNet_getHttp    (KiottyNetHttpClientHandle c, const char* path, uint32_t timeout_milliseconds);
KiottyNetHttpReply KiottyNet_postHttp   (KiottyNetHttpClientHandle c, const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_milliseconds);
KiottyNetHttpReply KiottyNet_putHttp    (KiottyNetHttpClientHandle c, const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_milliseconds);
KiottyNetHttpReply KiottyNet_deleteHttp (KiottyNetHttpClientHandle c, const char* path, uint32_t timeout_milliseconds);
KiottyNetHttpReply KiottyNet_patchHttp  (KiottyNetHttpClientHandle c, const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_milliseconds);
KiottyNetHttpReply KiottyNet_headHttp   (KiottyNetHttpClientHandle c, const char* path, uint32_t timeout_milliseconds);
KiottyNetHttpReply KiottyNet_optionsHttp(KiottyNetHttpClientHandle c, const char* path, uint32_t timeout_milliseconds);   /* path 가 "*" 면 서버 전체 */

KiottyNetSpan KiottyNet_findHttpReplyHeader(const KiottyNetHttpReply* reply, const char* name);    /* 대소문자 무시. 없으면 { NULL, 0 } */
uint32_t      KiottyNet_getHttpAllowedMethods(const KiottyNetHttpReply* reply);                    /* Allow 헤더를 KIOTTY_NET_HTTP_METHOD_BIT 의 합으로. 헤더가 없으면 0 */

/* 모든 요청에 붙는 헤더. 같은 name 을 다시 부르면 값을 바꾸고, value == NULL 이면 지운다.
   name + value 합이 KiottyNetHttpClientLimits::default_header_bytes 를 넘으면 KIOTTY_NET_CODE_BUFFER_FULL */
KiottyNetCode KiottyNet_setHttpClientHeader(KiottyNetHttpClientHandle c, const char* name, const char* value);
void          KiottyNet_disconnectHttpClient(KiottyNetHttpClientHandle c);   /* 명시적으로 끊고 싶을 때. 다음 요청에서 다시 연결된다 */
```

```cpp
class KiottyNet_HttpClient {
public:
    KiottyNet_HttpClient(const KiottyNetClientConfiguration& config, const KiottyNetHttpClientLimits& limits);
    KiottyNet_HttpClient(const KiottyNetClientConfiguration& config, const KiottyNetTlsClientConfiguration& tls, const KiottyNetHttpClientLimits& limits);   // TLS
    KiottyNetHttpReply request(KiottyNetHttpMethod method, const char* path, const KiottyNetHttpHeader* headers, size_t header_count,
                               KiottyNetSpan body, const char* content_type, uint32_t timeout_milliseconds);
    KiottyNetHttpReply get    (const char* path, uint32_t timeout_milliseconds);
    KiottyNetHttpReply post   (const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_milliseconds);
    KiottyNetHttpReply put    (const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_milliseconds);
    KiottyNetHttpReply del    (const char* path, uint32_t timeout_milliseconds);                    // DELETE
    KiottyNetHttpReply patch  (const char* path, KiottyNetSpan body, const char* content_type, uint32_t timeout_milliseconds);
    KiottyNetHttpReply head   (const char* path, uint32_t timeout_milliseconds);
    KiottyNetHttpReply options(const char* path, uint32_t timeout_milliseconds);
    KiottyNetCode setHeader(const char* name, const char* value);           // 모든 요청에 붙는 헤더
    void          disconnect();
};
```

```cpp
KiottyNet_HttpClient client(config, limits);
KiottyNetHttpReply   reply = client.get("/api/status", 3000);
if (reply.code == KIOTTY_NET_CODE_SUCCESS && reply.status == 200) parse(reply.body);

client.setHeader("Authorization", "Bearer abc123");                        // 이후 모든 요청에 붙는다
client.put("/api/devices/7", json, "application/json", 3000);
client.del("/api/devices/7", 3000);

KiottyNetHttpReply allow = client.options("/api/devices", 3000);
if (allow.code == KIOTTY_NET_CODE_SUCCESS && (KiottyNet_getHttpAllowedMethods(&allow) & KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_PATCH))) { /* PATCH 지원 */ }
```

**WebSocketClient.** 6절 EventClient 위에 Upgrade 요청 / 101 검사 행과 10절의 WebSocket 행을 얹은 것이다 (cycle W4, 2026-11-13). 서버와 같은 `KiottyNetWebSocketHandler` 를 쓰므로 콜백을 그대로 옮길 수 있다. 선언은 `kiotty_net_websocket.h` 에 있고, 사용법은 [user_guide_websocket.md](user_guide_websocket.md) 7절이다.

- **create** 는 `(context, configuration, limits, path)` 이고 `KiottyNetWebSocketClientResult { code, handle }` 을 돌려준다. `limits` 가 NULL 이면 `KiottyNet_initializeHttpLimits` 의 기본값이다. 쓰는 필드는 `max_body_bytes` (메시지 상한), `max_header_bytes` / `max_headers` / `header_timeout_milliseconds` (101 응답), 타이머 셋이다. `path` 는 요청 경로 (query 를 붙여도 된다) 이고 `/` 로 시작하는 보이는 ASCII 글자 (`KIOTTY_NET_MAX_URL_LENGTH` 미만) 여야 한다. 초기화하지 않은 limits, NULL 설정, 잘못된 path 는 `{ KIOTTY_NET_CODE_INVALID_ARGUMENT, NULL }`.
- **run** 은 `(client, address, tls, handler, app_context)` 이다. 연결 (6절의 재시도), Upgrade 요청, 101 검사, frame loop 를 모두 한다. `tls` 가 NULL 이면 ws, 아니면 wss 이고 한 클라이언트로 ws 와 wss 에 차례로 붙을 수 있다. handler 의 `onOpen` 이나 `onMessage` 가 NULL 이거나 이미 돌고 있으면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. `onUpgrade` 는 부르지 않는다 (NULL 로 둔다). handler 와 address 는 run 이 복사한다.
- **Upgrade 요청.** `GET <path> HTTP/1.1`, `Host`, `Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Key` (run 마다 새 16 바이트 난수의 base64), `Sec-WebSocket-Version: 13`. `Sec-WebSocket-Protocol` 은 보내지 않는다 (설계 결정 12). **Host 와 TLS SNI 는 run 의 address 의 host** 다 (설계 결정 6). IPv6 literal 이면 `[...]` 로 감싸고, 포트가 ws 의 80 / wss 의 443 이 아니면 `:port` 를 붙인다. 요청은 write lane 으로 나간다.
- **101 검사.** 상태줄이 `HTTP/1.1 101` 이고, `Upgrade` 에 `websocket`, `Connection` 에 `upgrade` (대소문자 무시), `Sec-WebSocket-Accept` 가 보낸 키의 값, `Sec-WebSocket-Protocol` 이 없어야 한다 (RFC 6455 4.1). 아니면 `onOpen` 없이 끊고 run 은 `KIOTTY_NET_CODE_PROTOCOL_ERROR`. 101 이 `header_timeout_milliseconds` 안에 다 오지 않으면 `KIOTTY_NET_CODE_TIMEOUT`.
- **mask.** 보내는 frame 은 모두 frame 마다 새 mask key 로 mask 한다 (`send`, ping, pong, close). 받은 frame 에 mask 가 있으면 1002 로 닫는다. 나머지 규칙 (조각 합치기, ping / pong, close handshake, onClose 의 표) 은 10절과 같다.
- **await** 는 `onOpen` 이 나가면 `KIOTTY_NET_CODE_SUCCESS`, 그 전에 run 이 끝났으면 run 의 반환값, 시간이 지나면 `KIOTTY_NET_CODE_TIMEOUT` 이다 (7절의 "loop 가 돌기 시작" 대신 "소켓이 열림"). 끝난 run 의 결과는 다음 run 이 시작할 때까지 남으므로 같은 클라이언트로 다시 run 한 직후의 await 는 앞 run 의 결과를 볼 수 있다 (7절과 같은 주의).
- **close** 는 close handshake 없이 끊는다 (`onClose(KIOTTY_NET_CODE_CLOSED, 1001)`). 정상 종료는 `KiottyNet_closeWebSocket(KiottyNet_getWebSocketClientSocket(client), ...)` 로 handshake 를 하면 상대가 끊고 run 이 `KIOTTY_NET_CODE_PEER_CLOSED` 로 돌아온다.
- 앱 thread 에서 보내려면 서버와 같이 `KiottyNet_sendWebSocket(ws, opcode, data)` 를 부른다. `KiottyNet_sendWebSocketClient(client, opcode, data)` 는 그 소켓 하나의 `sendWebSocket` 이다 (`notifyEventClient` 와 같은 관계). `KiottyNet_getWebSocketClientSocket` 은 `onOpen` 전과 `onClose` 뒤에 `{ NULL, 0 }` 이고 그때의 `sendWebSocketClient` 는 `KIOTTY_NET_CODE_PEER_CLOSED` 다.

run 의 반환값

| 코드 | 뜻 |
|---|---|
| `KIOTTY_NET_CODE_CLOSED` | `KiottyNet_closeWebSocketClient` 로 끝났다 |
| `KIOTTY_NET_CODE_PEER_CLOSED` | 서버가 끊었다. close handshake 를 마친 정상 종료도 이것이다 (끝난 모양은 `onClose` 의 `code` 가 말한다) |
| `KIOTTY_NET_CODE_PROTOCOL_ERROR` | 101 이 아닌 응답, `Sec-WebSocket-Accept` 가 틀림, 요청하지 않은 `Sec-WebSocket-Protocol` |
| `KIOTTY_NET_CODE_TIMEOUT` | 101 이 `header_timeout_milliseconds` 안에 오지 않았다 |
| `KIOTTY_NET_CODE_INVALID_ARGUMENT` | handler / address 가 NULL, `onOpen` / `onMessage` 가 NULL, 이미 돌고 있음, Upgrade 요청이 768 바이트에 들지 않음 (path + host) |
| 그 밖 | 6절 EventClient 의 run 과 같다 (연결 재시도 실패, `SOCKET_ERROR`, TLS 실패 등) |

```c
typedef struct KiottyNetWebSocketClient* KiottyNetWebSocketClientHandle;
typedef struct KiottyNetWebSocketClientResult { KiottyNetCode code; KiottyNetWebSocketClientHandle handle; } KiottyNetWebSocketClientResult;

KiottyNetWebSocketClientResult KiottyNet_createWebSocketClient(KiottyNetContext context, const KiottyNetClientConfiguration* configuration, const KiottyNetHttpLimits* limits /* NULL = 기본값 */, const char* path);
void          KiottyNet_destroyWebSocketClient(KiottyNetContext context, KiottyNetWebSocketClientHandle client);
KiottyNetCode KiottyNet_runWebSocketClient(KiottyNetWebSocketClientHandle client, const KiottyNetAddress* address, const KiottyNetTlsClientConfiguration* tls /* NULL = ws */, const KiottyNetWebSocketHandler* handler, KiottyNetAppContext app_context);
void          KiottyNet_closeWebSocketClient(KiottyNetWebSocketClientHandle client);                     /* close handshake 없이 끊는다 */
KiottyNetCode KiottyNet_awaitWebSocketClient(KiottyNetWebSocketClientHandle client, uint64_t timeout_milliseconds);   /* onOpen 이 나가면 SUCCESS */
KiottyNetWebSocketHandle KiottyNet_getWebSocketClientSocket(KiottyNetWebSocketClientHandle client);    /* 열린 소켓이 없으면 { NULL, 0 } */
KiottyNetCode KiottyNet_sendWebSocketClient(KiottyNetWebSocketClientHandle client, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);   /* 열린 소켓이 없으면 PEER_CLOSED */
```

```cpp
class KiottyNet_WebSocketClient {
public:
    KiottyNet_WebSocketClient(KiottyNetContext context, const KiottyNetClientConfiguration& configuration, const KiottyNetHttpLimits* limits, const char* path);
    explicit operator bool() const;                  // create 가 실패하면 false
    KiottyNetCode                  code() const;     // create 의 이유
    KiottyNetWebSocketClientHandle handle() const;
    KiottyNetCode            run(const KiottyNetAddress& address, const KiottyNetWebSocketHandler& handler, KiottyNetAppContext app_context);
    KiottyNetCode            run(const KiottyNetAddress& address, const KiottyNetTlsClientConfiguration& tls, const KiottyNetWebSocketHandler& handler, KiottyNetAppContext app_context);   // wss
    void                     close();
    KiottyNetCode            await(uint64_t timeout_milliseconds);
    KiottyNetWebSocketHandle socket() const;
    KiottyNetCode            send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
};
```

```cpp
KiottyNet_WebSocketClient client(context.handle(), configuration, nullptr, "/chat");
std::thread io([&] { client.run(address, tls, HANDLER, &session); });   // 연결 + Upgrade + loop
if (client.await(5000) == KIOTTY_NET_CODE_SUCCESS) client.send(KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, hello);
KiottyNet_WebSocket(client.socket()).close();                            // close handshake. 서버가 답하고 끊으면 run 이 PEER_CLOSED
io.join();
```

열린 질문

1. (닫힘, 2026-11-11) 동기 WebSocketClient 는 두지 않는다. 서버 push 를 받으려면 결국 loop 가 필요하다 (user_guide_websocket 10절).
2. HttpClient 의 응답 이름. 서버의 `HttpResponse` (사용자가 채우는 것) 와 구분하려고 `HttpReply` (사용자가 읽는 것) 로 두었다.
3. HttpClient 에 비동기 버전(EventClient 위, 콜백으로 응답)이 필요한지. 동기 버전만으로 시작하는 것을 제안한다.
4. `request_bytes` 버퍼 없이 요청 헤더를 바로 write lane 에 쓰면 버퍼 하나가 준다. 대신 ClientConfig 의 `send_capacity` 가 요청 크기 상한이 된다.

미결

- (닫힘, 2026-11-11) WebSocket 의 Work 는 두지 않는다. 다른 thread 의 송신은 push lane 의 `send` 다 (설계 결정 3).
- (닫힘, 2026-11-11) pool handle 의 소유자: handle 이 `{ owner, id }` 를 든다 (설계 결정 7).
- (닫힘, 2026-11-11) WebSocketServer 는 없애고 HttpServer 의 Upgrade 경로 하나로 합쳤다 (설계 결정 1).
- HttpServer 라우트에 capture 있는 람다 / functor 를 받는 template 오버로드. 얇은 래퍼 규칙 때문에 뺐다. 필요하면 `KiottyNetHttpRoute::app_context` 로 대신한다.
- HttpServer 에 간단한 CORS 설정 (허용 origin 목록 하나로 preflight 자동 응답 + 헤더 자동 추가) 을 둘지.
- 조각을 합치지 않고 그대로 전달하는 streaming 모드 (`max_body_bytes` 보다 큰 메시지).
- permessage-deflate 확장은 지원하지 않는다.

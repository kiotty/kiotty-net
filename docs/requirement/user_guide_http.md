# Kiotty-Net 사용 가이드

사용자가 헤더를 어떻게 쓰는지 적는 메모다. 인터페이스의 기준은 [requirement_v0.md](requirement_v0.md) 이고, 여기서는 "무엇을 어떤 순서로 부르는가" 만 다룬다. 내부 동작은 [implementation_guide_v0.md](implementation_guide_v0.md) 에 있다.

이 문서는 `include/kiotty_net_http.h` 를 따른다. 1절 서버는 공개 API 판 v2 ([design_http_v0.html](../design/design_http_v0.html), 사이클 H1 ~ H6), 2절 클라이언트는 [design_http_client_v0.html](../design/design_http_client_v0.html) (사이클 C1 ~ C5) 이다. 둘 다 구현되어 있다.

## 헤더 구성

| 헤더 | 내용 | 누가 include 하나 |
|---|---|---|
| `kiotty_net_base.h` | 공통 타입 (`KiottyNetCode`, `KiottyNetSpan`), 설정 struct, Context, Connection / Work, StreamClient, EventClient, Server | 직접 TCP / Pipe 프로토콜을 짜는 사용자 |
| `kiotty_net_statemachine.h` | Connection 위에 얹는 표 기반 StateMachine | 자기 프로토콜을 표로 쓰려는 사용자. HTTP / WebSocket 사용자는 필요 없다 |
| `kiotty_net_http.h` | HTTP/1.1 서버 (Rest API 라우트와 정적 서빙) 와 동기 클라이언트 (2절) | HTTP 서버 / 클라이언트를 쓰는 사용자. **이 헤더 하나만 include 하면 된다** |
| `kiotty_net_websocket.h` | WebSocket 서버 / 클라이언트 | WebSocket 사용자 |

- 각 헤더는 위에 C 인터페이스, 아래에 (`__cplusplus` 일 때만) 그 C 인터페이스를 부르는 C++ 래퍼가 있다. C 로 빌드하면 아래 절반은 보이지 않는다.
- `kiotty_net_http.h` 는 `kiotty_net_base.h` 를 include 한다. 공통 타입과 `KiottyNetServerConfiguration`, `KiottyNetContext` 가 거기 있기 때문이다. StateMachine 은 HttpServer 의 내부 구현이라 `kiotty_net_statemachine.h` 는 include 하지 않는다.
- C++ 소유 handle 래퍼는 `kiotty::net::OwnedHandle<Handle, Destroy>` 로 RAII 를 한다. 이 template 은 `KIOTTY_NET_OWNED_HANDLE_DEFINED` 매크로로 감싸 `kiotty_net_base.h`, `kiotty_net_http.h`, `kiotty_net_websocket.h` 에 같은 정의가 있다. 먼저 include 된 쪽이 정의하고 나머지는 건너뛴다. 정의들은 글자까지 같아야 하고, 고칠 때는 함께 고친다.

## 1. HTTP 서버

### 1.1 준비 : Context, ServerConfig, HttpLimits

HttpServer 는 소유 handle 이다. `KiottyNet_createHttpServer` 가 만들고 `KiottyNet_destroyHttpServer` 가 없앤다. create 는 `KiottyNetHttpServerResult { code, handle }` 를 돌려준다. 실패하면 handle 이 `NULL` 이고 code 가 이유다 (`KIOTTY_NET_CODE_INVALID_ARGUMENT` 설정 미초기화 또는 `response_bytes` 가 상태줄 + 라이브러리 헤더 자리보다 작음, `KIOTTY_NET_CODE_TABLE_FULL`. 메모리를 잡지 못하면 process 가 끝난다). 만들 때 세 가지를 넘긴다.

1. `KiottyNetContext` : `KiottyNet_initializeContext()` 가 돌려주는 `KiottyNetContextResult` 의 `handle`. 라이브러리 전역 컨텍스트다. 실패하면 `code` 가 `KIOTTY_NET_CODE_SUCCESS` 가 아니다. 소유 handle 은 모두 같은 context 를 받고, destroy 때도 같은 context 를 넘긴다.
2. `KiottyNetServerConfiguration` : `max_clients`, 버퍼 크기. `KiottyNet_initializeServerConfiguration(&configuration)` 로 채우고 `KiottyNet_configureServer*` 로 바꾼다. 주소와 TLS 는 여기에 없고 `KiottyNet_runHttpServer` 가 받는다. 주소는 `KiottyNet_initializeTcpAddress(&address, host, port)` 또는 `KiottyNet_initializePipeAddress(&address, name)` 로 만든다. TLS 를 쓰면 `KiottyNetTlsServerConfiguration` 를 run 에 넘기고, 평문이면 `NULL`.
3. `KiottyNetHttpLimits` : HTTP 층의 고정 크기 버퍼와 timeout. 아래 표.

`KiottyNet_initializeHttpLimits(&limits)` 가 모든 필드를 기본값으로 채우고, `KiottyNet_configureHttp*` 가 하나씩 바꾼다. 다른 설정 struct 와 같은 규칙이다. 함수가 값을 검사하고 `KiottyNetCode` 를 돌려주며, 잘못된 값이면 struct 를 바꾸지 않는다.

| 필드 | 기본값 | 뜻 | configure 함수 |
|---|---|---|---|
| `max_routes` | 64 | 라우트 + 정적 경로 + Upgrade 경로의 합. 표는 create 때 이 크기로 잡힌다 | `KiottyNet_configureHttpMaxRoutes(limits, max_routes)` |
| `max_header_bytes` | 8 KiB | request line + 헤더 전체. 넘으면 431 로 거절하고 닫는다 | `KiottyNet_configureHttpHeaderCapacity(limits, max_header_bytes, max_headers)` |
| `max_headers` | 32 | 요청 헤더 개수. `request.header_count` 의 상한 | 위와 같음 |
| `max_body_bytes` | 64 KiB | 요청 body. 넘으면 413 | `KiottyNet_configureHttpBodyCapacity(limits, max_body_bytes)` |
| `response_bytes` | 64 KiB | 응답 버퍼 (status line + 헤더 + 복사된 body). 넘치면 setter 가 `KIOTTY_NET_CODE_BUFFER_FULL` | `KiottyNet_configureHttpResponseCapacity(limits, response_bytes)` |
| `header_timeout_milliseconds` | 10000 | 헤더 수신 완료까지. 지나면 408 없이 닫는다 | `KiottyNet_configureHttpTimeouts(limits, header_timeout_milliseconds, keep_alive_timeout_milliseconds)` |
| `keep_alive_timeout_milliseconds` | 15000 | 응답 뒤 다음 요청을 기다리는 시간 | 위와 같음 |
| `websocket_ping_interval_milliseconds` | 30000 | WebSocket 경로가 있을 때 ping 간격. 0 이면 보내지 않는다 | `KiottyNet_configureHttpWebSocketTimers(limits, ping_interval, pong_timeout, close_timeout)` |
| `websocket_pong_timeout_milliseconds` | 10000 | ping 뒤 pong 을 기다리는 시간 | 위와 같음 |
| `websocket_close_timeout_milliseconds` | 5000 | close frame 을 보낸 뒤 상대의 close 를 기다리는 시간 | 위와 같음 |

WebSocket 전용 설정 struct 는 없다. 메시지 상한은 `max_body_bytes`, 핸드셰이크 timeout 은 `header_timeout_milliseconds` 를 같이 쓰고 남는 타이머 셋만 여기에 있다 (WebSocket 경로 등록은 `kiotty_net_websocket.h`).

버퍼는 Connection 마다 (`max_clients` 개) 잡히므로 메모리는 `max_clients × (max_header_bytes + max_body_bytes + response_bytes)` 정도다. 임베디드 타깃에서는 `max_clients` 와 함께 줄인다.

C++ 은 `KiottyNet_HttpLimitsBuilder` 가 같은 함수를 부른다. 처음 실패한 코드를 `build()` 가 돌려준다.

```cpp
KiottyNetHttpLimits limits;
KiottyNetCode code = KiottyNet_HttpLimitsBuilder(limits).bodyCapacity(16 * 1024).timeouts(5000, 10000).webSocketTimers(30000, 10000, 5000).build();
```

### 1.2 라우트 등록

라우트는 `run` 전에 등록한다. run 이 시작된 뒤의 등록은 `KIOTTY_NET_CODE_BUSY` 다.

```c
KiottyNetCode KiottyNet_addHttpRoute(KiottyNetHttpServerHandle server, KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
KiottyNetCode KiottyNet_setHttpNotFound(KiottyNetHttpServerHandle server, KiottyNetHttpRoute route);
```

- `path` 는 `/` 로 시작하는 URL 경로다. 정확 일치, 또는 `/*` 로 끝나면 접두 wildcard 다 (`/api/devices/*`). `:id` 같은 파라미터 파싱은 없다. `request.path` 를 사용자가 자른다. 길이는 `KIOTTY_NET_MAX_URL_LENGTH` (256) 미만이어야 하며 넘으면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`.
- 라우트, 정적 경로, Upgrade 경로는 한 표에 **등록한 순서대로** 매칭한다. 좁은 경로를 먼저, `/*` 나 `/` 디렉토리 매핑을 마지막에 등록한다.
- 같은 (method, path) 를 두 번 등록하면 `KIOTTY_NET_CODE_DUPLICATE_RULE`. 표가 가득 차면 `KIOTTY_NET_CODE_TABLE_FULL`.
- `KiottyNetHttpRoute` 는 두 필드다. `KiottyNet_makeHttpRoute(onRequest, onWorkStarted)` 로 만들면 C 에서도 한 줄이다.
- 콜백의 첫 인자 `app_context` 는 `KiottyNet_runHttpServer` 에 준 사용자 포인터 하나다 (라우트마다 따로 두지 않는다). 라이브러리는 읽지 않는다. 라우트마다 다른 데이터가 필요하면 그 안에 두고 `request->path` 로 가른다.

| 필드 | 뜻 |
|---|---|
| `onRequest(app_context, request, response)` | 요청이 다 들어오면 IO Thread 에서 한 번. 여기서 응답을 채우거나 `KiottyNet_startHttpWork` 를 부른다. `NULL` 이면 `KIOTTY_NET_CODE_INVALID_ARGUMENT` |
| `onWorkStarted(app_context, request, response, work)` | `startHttpWork` 뒤 다음 loop 에서 한 번 (1.5). 긴 작업이 없는 라우트는 `NULL` |

메서드별 동작. 사용자가 그 메서드를 등록하지 않았을 때 라이브러리가 하는 일이 "자동" 열이다.

| 메서드 | 요청 body | 자동 |
|---|---|---|
| GET | 없음. 와도 읽고 버린다 | 없음 |
| HEAD | 없음 | 같은 path 의 GET 라우트를 실행하고 body 는 보내지 않는다. 정적 경로도 같다 |
| POST / PUT / PATCH | 있음 | 없음 |
| DELETE | 보통 없음. 오면 `request.body` 로 준다 | 없음 |
| OPTIONS | 없음 | 204 + `Allow`. 값은 그 path 에 등록된 메서드 + HEAD (GET 이 있으면) + OPTIONS. `OPTIONS *` 는 서버 전체 |

- path 가 맞는 항목이 하나도 없으면 `notFound` 라우트, 그것도 없으면 404.
- path 는 맞지만 그 method 가 없으면 405 + `Allow`. `notFound` 는 부르지 않는다.
- CONNECT, TRACE 와 그 밖의 메서드는 501. 등록할 수도 없다.
- CORS 헤더는 자동으로 붙이지 않는다. preflight 가 필요하면 OPTIONS 라우트를 등록하고 `Access-Control-Allow-*` 를 직접 넣는다.
- body 는 메서드가 아니라 헤더로 읽는다. `Content-Length` 가 있으면 그만큼 읽고, `Transfer-Encoding: chunked` 는 라이브러리가 합쳐서 `request.body` 로 준다.

### 1.3 요청 읽기

`onRequest` 가 받는 `const KiottyNetHttpRequest*` 는 Connection 의 수신 버퍼를 가리킨다. **`onRequest` 가 return 하면 무효** 다. `startHttpWork` 를 불렀으면 Work 가 finish 될 때까지 유효하다. 보관할 것은 그 안에서 복사한다.

| 필드 | 뜻 |
|---|---|
| `connection` | `KiottyNet_getConnectionInfo(connection)` 로 peer 주소를 볼 때 쓴다 |
| `method` | `KIOTTY_NET_HTTP_METHOD_GET` 등 7개 중 하나 |
| `path` | `?` 앞. percent-decoding 하지 않는다 |
| `query` | `?` 뒤. 없으면 길이 0 |
| `headers`, `header_count` | 이름 / 값 span 배열. `KiottyNet_findHttpHeader(request, "Content-Type")` 가 대소문자 무시로 찾고 없으면 `{ NULL, 0 }` |
| `body` | `Content-Length` 또는 chunked 를 합친 것. 없으면 길이 0 |
| `keep_alive` | 응답 뒤 연결을 유지할지 (`KiottyNetBool`, 0 이 아니면 유지). 라이브러리가 처리하므로 보통 볼 일이 없다 |

### 1.4 응답 채우기

응답은 handle `KiottyNetHttpResponseHandle { server, id }` 로 채운다 (base 의 `KiottyNetConnectionHandle` 과 같은 모양). Connection 마다 `response_bytes` 크기의 버퍼가 있고 setter 는 거기에 쓴다.

| 함수 | 하는 일 | 실패 |
|---|---|---|
| `KiottyNet_setHttpStatus(r, status)` | status code. 100..599. `KiottyNetHttpStatus` 의 값 (`KIOTTY_NET_HTTP_STATUS_ACCEPTED` 등) 을 넣는다. 인자는 정수라 목록 밖의 코드도 범위 안이면 된다 | 범위 밖 `KIOTTY_NET_CODE_INVALID_ARGUMENT` |
| `KiottyNet_setHttpHeader(r, name, value)` | 헤더 추가. 이름과 값에 CR / LF 가 있으면 거른다 | `KIOTTY_NET_CODE_BUFFER_FULL`. 라이브러리 헤더 (`Content-Length`, `Transfer-Encoding`, `Connection`, `Date`) 와 `Content-Type` (대소문자 무시) 은 `KIOTTY_NET_CODE_INVALID_ARGUMENT` |
| `KiottyNet_setHttpCustomContentType(r, text)` | content type 이 목록 밖일 때 그 이름. body setter 에 `KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM` 을 넘기기 전에 부른다. 다시 부르면 바꾼다 | 모양 / 길이 `KIOTTY_NET_CODE_INVALID_ARGUMENT`, UTF-8 아님 `KIOTTY_NET_CODE_INVALID_ENCODING` |
| `KiottyNet_setHttpBody(r, type, data)` | body 를 응답 버퍼로 **복사**. return 뒤 data 를 버려도 된다 | `KIOTTY_NET_CODE_BUFFER_FULL`, `KIOTTY_NET_CODE_INVALID_ENCODING`, `KIOTTY_NET_CODE_INVALID_ARGUMENT` (아래) |
| `KiottyNet_setHttpBodyRef(r, type, data)` | 복사하지 않고 가리킨다. 송신 완료까지 data 가 살아 있어야 한다 (static 데이터, 메모리 바이너리). 버퍼 크기와 무관 | `KIOTTY_NET_CODE_INVALID_ENCODING`, `KIOTTY_NET_CODE_INVALID_ARGUMENT` |
| `KiottyNet_setHttpFile(r, type, path)` | 파일을 응답 버퍼 조각으로 나눠 보낸다. type 은 보통 `KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION` | 없는 파일 `KIOTTY_NET_CODE_NOT_FOUND`, `..` 포함 경로 `KIOTTY_NET_CODE_INVALID_ARGUMENT` |
| `KiottyNet_setHttpBodyStream(r, type)` | body 를 스트림으로 (`Transfer-Encoding: chunked`). Work 안에서 `writeHttpWorkChunk` 로 쓴다 (1.5) | `KIOTTY_NET_CODE_INVALID_ARGUMENT` (`BY_EXTENSION`) |
| `KiottyNet_startHttpWork(r)` | 응답을 지금 보내지 않고 연결을 IO Thread 에서 떼어낸다 (1.5) | 1.5 |

- body 는 넷 (복사, 참조, 파일, 스트림) 중 하나만 둔다. 다시 부르면 이전 것을 대체한다.
- 헤더가 이미 나간 뒤 (송신 중, 스트림의 첫 write 뒤) 의 setter 는 `KIOTTY_NET_CODE_BUSY` 다.
- 응답은 **`onRequest` 가 return 하는 순간** 보내진다. status 를 부르지 않았으면 200 이고, 아무 setter 도 부르지 않고 return 했으면 404 다.
- 모든 setter 는 닫힌 handle 이면 `KIOTTY_NET_CODE_PEER_CLOSED` 를 돌려주고 아무 일도 하지 않는다.
- HEAD 요청에 GET 라우트가 실행될 때도 setter 는 그대로 동작한다. 라이브러리가 body 를 떼고 `Content-Length` 만 같게 보낸다.
- status 만 두고 body 가 없으면 `Content-Type` 도 없다 (202, 204 등). body 가 없는 202 는 `Content-Length: 0` 이다.

**content type.** `Content-Type` 은 body setter 의 `KiottyNetHttpContentType` 으로만 정한다. 라이브러리가 표에서 문자열을 꺼내 쓰므로 오타가 없다.

- 목록은 텍스트 (`TEXT_PLAIN`, `TEXT_HTML`, `TEXT_CSS`, `TEXT_CSV`, `TEXT_MARKDOWN`, `JAVASCRIPT`, `EVENT_STREAM`), 구조화 데이터 (`JSON`, `PROBLEM_JSON`, `NDJSON`, `MANIFEST_JSON`, `XML`, `YAML`, `FORM_URLENCODED`), 의료 (`DICOM`, `DICOM_JSON`, `DICOM_XML`, `FHIR_JSON`, `FHIR_XML`), 바이너리 직렬화 (`PROTOBUF`, `MSGPACK`, `CBOR`), 이미지, 글꼴, 소리 / 영상, 문서 / 묶음 (`PDF`, `ZIP`, `GZIP`, `TAR`, `WASM`), `OCTET_STREAM` 이다. 각 값의 문자열과 확장자는 설계 4.3절의 표다.
- 텍스트 묶음 (`TEXT_*`, `JAVASCRIPT`, `EVENT_STREAM`) 은 `; charset=utf-8` 이 붙는다. 그 묶음과 구조화 데이터, `DICOM_JSON` / `DICOM_XML` / `FHIR_*`, `SVG` 는 UTF-8 을 검사한다. 복사 / 참조 body 는 setter 를 부를 때, 스트림은 chunk 마다다. UTF-8 이 아니면 `KIOTTY_NET_CODE_INVALID_ENCODING` 이고 body 는 바뀌지 않는다. 문법 (JSON 등) 은 보지 않는다.
- `BY_EXTENSION` 은 파일 (`setHttpFile`, 정적 경로) 에만, `EVENT_STREAM` 은 스트림에만 된다. 다른 자리면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`.
- 목록 밖이거나 파라미터가 필요하면 (`multipart/related; type="application/dicom"; boundary=b1`) `setHttpCustomContentType` 으로 이름을 정하고 `CUSTOM` 을 넘긴다. 이름은 `KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH` (64) 미만, `token/token` 뒤에 `; 이름=값` 이 0 개 이상, CR / LF 없음. 이름 없이 `CUSTOM` 이 오면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. 그 응답 하나에만 유효하고, `CUSTOM` body 는 UTF-8 을 보지 않는다.

C++ 은 `KiottyNet_HttpResponse(r)` 가 같은 함수를 `status / header / customContentType / body / bodyRef / file / bodyStream / startWork` 로 부른다. `file(path, type)` 은 path 가 먼저이고 type 의 기본값이 `BY_EXTENSION` 이다. 비소유 값 타입이라 콜백 안에서 handle 로 만들어 쓰고 버린다.

### 1.5 긴 작업 : Work

DB 조회, 리포트 생성처럼 IO Thread 를 붙잡으면 안 되는 일은 base 의 Work 모델을 HTTP 에 맞게 줄인 것으로 처리한다. 흐름은 세 단계다.

1. `onRequest` 안에서 `KiottyNet_startHttpWork(response)` 를 부르고 return 한다. 응답은 아직 보내지지 않는다. 라우트의 `onWorkStarted` 가 `NULL` 이면 `KIOTTY_NET_CODE_INVALID_ARGUMENT`, 이미 불렀으면 `KIOTTY_NET_CODE_BUSY`.
2. IO Thread 가 그 연결에 걸린 송신을 마치고 떼어낸 뒤, **같은 loop 안에서** `onWorkStarted(app_context, request, response, work)` 를 한 번 낸다. 여기서 `response` 와 `work` 를 worker 에게 넘긴다. 둘 다 정수 handle 이라 복사해 두어도 dangling 이 없다. `request` 가 가리키는 데이터도 finish 까지 유효하다.
3. worker 가 결과를 만들어 setter 로 응답을 채우고 (`KiottyNet_setHttpStatus`, `KiottyNet_setHttpBody` 등. 어느 thread 에서든 된다. 연결이 떼어져 있는 동안 IO Thread 는 응답 버퍼를 건드리지 않는다) `KiottyNet_finishHttpWorkWithResponse(work)` 로 보낸다. 보내지 않고 끊으려면 `KiottyNet_finishHttpWorkWithClose(work, reason)`.

규칙은 base 의 Work 와 같다.

- **Work 를 받았으면 반드시 finish 한다.** finish 는 한 번만 유효하고 두 번째는 `KIOTTY_NET_CODE_BUSY`. 소유자가 destroy 된 뒤에 불러도 `KIOTTY_NET_CODE_PEER_CLOSED` 만 돌아온다.
- finish 전까지 그 연결에는 어떤 콜백도 없다. deadline 도 멈춘다. 라이브러리는 Work 에 timeout 을 두지 않는다. 오래 걸릴 것 같으면 `KiottyNet_isHttpWorkConnected(work)` 로 peer 가 살아 있는지 확인한다. 반환은 `KiottyNetBool` 이고 0 이면 끊겼다 (C++ 의 `connected()` 는 `bool`). 끊겼으면 finish 는 `KIOTTY_NET_CODE_PEER_CLOSED` 다.
- 소유자의 `close()` 는 Work 중인 연결에 FIN 만 보낸다. 연결은 finish 뒤에 닫히고, 그때까지 `run` 은 돌아오지 않는다. 그 finish 는 `KIOTTY_NET_CODE_CLOSED` 를 돌려준다. 끊김 action (`KIOTTY_NET_CODE_CLOSED`) 은 worker 의 finish 보다 **먼저** 온다 (base 가 close 때 연결의 끝을 알리고, Session 반납만 finish 까지 미룬다).
- Work 중에 그 연결로 들어온 notify (WebSocket 송신, broadcast) 는 쌓이기만 하고 finish 뒤에 나간다.
- `finishHttpWorkWithResponse` 는 응답 버퍼의 내용을 그대로 보낸다. body 가 `response_bytes` 보다 커서 setter 가 `KIOTTY_NET_CODE_BUFFER_FULL` 이었다면 `setHttpBodyRef` 나 `setHttpFile` 로 바꾼다.

C++ 은 `KiottyNet_HttpWork` 가 finish 의무를 소유한다. 이동만 되고, finish 하지 않은 채 소멸되면 `finishWithClose()` 를 불러 연결을 닫는다. 그래서 worker 로 `std::move` 해서 넘긴다 (C++14 init capture, C++11 은 `shared_ptr`).

**Work 스트림 (SSE).** 진행 알림 여러 개 뒤 최종 응답, 또는 GET 으로 연 오래 가는 이벤트 스트림은 Work 안에서 body 를 스트림으로 쓴다.

1. worker 가 `setHttpStatus`, `setHttpHeader(r, "Cache-Control", "no-cache")` 등을 놓고 `KiottyNet_setHttpBodyStream(r, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM)`.
2. `KiottyNet_writeHttpWorkChunk(work, data, timeout_milliseconds)` 를 여러 번. 첫 호출이 상태줄과 헤더를 먼저 보낸다. 라이브러리가 chunk 머리 (길이 hex CRLF) 와 꼬리 (CRLF) 를 씌운다. 동기이고, 전부 보내거나 실패하며, 돌아오면 data 를 버려도 된다. `timeout_milliseconds` 는 호출 전체의 한도이고 0 은 무한이다. 반환 `KiottyNetResult` 의 `bytes` 는 `data.length` 다.
3. `KiottyNet_finishHttpWorkWithResponse(work)` 가 끝 chunk (`0\r\n\r\n`) 를 보내고 keep-alive 면 다음 요청을 읽는다. chunk 를 한 번도 쓰지 않았으면 헤더와 끝 chunk 만 간다.

- write 의 실패: `KIOTTY_NET_CODE_PEER_CLOSED` (상대가 끊음), `KIOTTY_NET_CODE_TIMEOUT` (일부가 나갔을 수 있다), `KIOTTY_NET_CODE_CLOSED` (서버 close), `KIOTTY_NET_CODE_BUSY` (이미 finish), `KIOTTY_NET_CODE_INVALID_ARGUMENT` (body 가 스트림이 아님, 빈 data. 길이 0 chunk 는 끝이라 직접 쓰지 않는다), `KIOTTY_NET_CODE_INVALID_ENCODING` (텍스트 계열인데 UTF-8 아님. 아무것도 보내지 않았다). 실패 뒤에는 `finishHttpWorkWithClose` 만 부른다.
- 텍스트 계열은 chunk 마다 UTF-8 을 보므로 문자 하나를 두 chunk 에 나눠 쓰지 않는다. SSE 이벤트는 줄 단위라 그럴 일이 없다.
- 스트림 body 는 Work 안에서만 된다. `onRequest` 에서 `setHttpBodyStream` 을 부르고 `startHttpWork` 없이 return 하면 라이브러리가 500 을 보내고 닫는다.
- 끝을 알리는 콜백은 없다. 이벤트가 없는 동안 끊김을 빨리 보려면 worker 가 주기적으로 `: ping\n\n` 을 쓰거나 `isHttpWorkConnected` 를 본다.
- 스트림 하나가 worker thread 하나를 쓴다. 여러 클라이언트에 같은 것을 실시간으로 뿌리는 일은 WebSocket broadcast 의 몫이다.
- HEAD 가 스트림 라우트에 오면: `onRequest` 에서 `setHttpBodyStream` 과 `startHttpWork` 를 부른 라우트는 Work 를 열지 않는다 (`onWorkStarted` 없음). 헤더만 보내고 같은 연결로 다음 요청을 읽는다. 그래서 스트림 라우트는 status 와 body 종류를 `onRequest` 에서 정하는 것을 권한다. worker 가 스트림을 연 경우는 첫 write 가 헤더만 보내고 `KIOTTY_NET_CODE_CLOSED` 를 돌려준다.

C++ 은 `KiottyNet_HttpWork::write(data, timeout_milliseconds)` 가 `writeHttpWorkChunk` 다.

### 1.6 정적 서빙

GET / HEAD 에만 적용되고 라우트와 같은 표에 들어간다. 다른 메서드로 오면 405 + `Allow: GET, HEAD, OPTIONS`.

| 함수 | 매핑 | content type |
|---|---|---|
| `KiottyNet_serveHttpDirectory(s, "/static/", "./www/")` | `/static/a/b.js` → `./www/a/b.js`. `url_prefix` 는 `/` 로 끝나야 한다. `..` 가 들어오면 403 | 확장자로 정한다. 모르는 확장자는 `application/octet-stream` |
| `KiottyNet_serveHttpFile(s, "/favicon.ico", "./www/favicon.ico")` | 파일 하나 | 확장자 |
| `KiottyNet_serveHttpMemory(s, "/app.wasm", KIOTTY_NET_HTTP_CONTENT_TYPE_WASM, data)` | 메모리 바이너리. **복사하지 않는다.** 서버가 살아 있는 동안 data 가 유효해야 한다 | 인자의 enum. `BY_EXTENSION`, `EVENT_STREAM`, `CUSTOM` 은 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. 텍스트 계열이면 등록 때 UTF-8 을 한 번 본다 |

- 디렉토리와 파일 경로는 `KIOTTY_NET_MAX_PATH_LENGTH` (260) 미만. 등록 때 복사되므로 호출자의 문자열은 버려도 된다.
- 파일은 요청 때 열어 응답 버퍼 조각으로 나눠 보낸다. sendfile / TransmitFile 은 쓰지 않는다. 없으면 404.
- Single Page App 처럼 `/` 전체를 디렉토리로 주고 그 위에 API 라우트를 두려면 API 라우트를 먼저, `serveHttpDirectory("/", ...)` 를 마지막에 등록한다.

### 1.7 수명과 thread 규칙

| 함수 | 하는 일 |
|---|---|
| `KiottyNet_runHttpServer(s, address, tls, app_context)` | (tls 가 있으면 SSL_CTX 를 만들고) listen 을 열고 IO loop 를 돈다. **blocking.** `close` 가 불릴 때까지 return 하지 않는다. 열기에 실패하면 바로 그 코드. tls 가 `NULL` 이면 평문. `app_context` 는 이 run 동안의 모든 콜백 (라우트, Work, 끊김, 오류) 의 첫 인자다. worker 가 run 뒤까지 들고 있을 수 있으므로 수명은 앱이 지킨다 |
| `KiottyNet_closeHttpServer(s)` | loop 를 멈추라고 알린다. 기다리지 않는다. Work 중인 연결은 FIN 을 보내고 worker 의 finish 뒤에 닫힌다 (1.5) |
| `KiottyNet_awaitHttpServer(s, timeout_milliseconds)` | loop 가 돌기 시작할 때까지 기다린다. 시작했으면 `KIOTTY_NET_CODE_SUCCESS`, run 이 이미 끝났으면 run 의 반환값, 시간 안에 못 봤으면 `KIOTTY_NET_CODE_TIMEOUT` |
| `KiottyNet_destroyHttpServer(context, s)` | run 이 return 한 뒤에 부른다 |

- `onRequest`, `onWorkStarted` 는 IO Thread 에서 순서대로 불린다. 콜백 안에서 block 하지 않는다. 블로킹이 필요하면 Work 다.
- 응답 setter 는 `onRequest` 안 (IO Thread) 에서, 또는 `onWorkStarted` 뒤 Work 를 들고 있는 thread 에서 부른다. 같은 응답을 두 thread 가 동시에 만지는 순간은 없다.
- 다른 thread 가 불러도 되는 것은 `finishHttpWork*`, `isHttpWorkConnected`, `closeHttpServer`, `awaitHttpServer` 다. `writeHttpWorkChunk` 는 Work 를 든 worker 가 부른다.

**끊김과 오류.** 연결 단위의 알림 둘은 라우트처럼 콜백 하나씩 run 전에 등록한다. run 이 시작된 뒤의 등록은 `KIOTTY_NET_CODE_BUSY`, 다시 부르면 바꾸고 `NULL` 이면 지운다. 등록하지 않으면 알리지 않는다. IO Thread 에서 불리고, `app_context` 는 run 에 준 것이다.

```c
typedef void (*KiottyNetHttpDisconnectedAction)(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode reason);
typedef void (*KiottyNetHttpErrorAction)(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message);
KiottyNetCode KiottyNet_onHttpDisconnected(KiottyNetHttpServerHandle server, KiottyNetHttpDisconnectedAction on_disconnected);
KiottyNetCode KiottyNet_onHttpError(KiottyNetHttpServerHandle server, KiottyNetHttpErrorAction on_error);
```

| action | 언제 |
|---|---|
| `on_disconnected(app_context, info, reason)` | 연결마다 한 번. reason: `KIOTTY_NET_CODE_SUCCESS` (keep-alive 아님, 다 보냄), `KIOTTY_NET_CODE_TIMEOUT` (header / keep-alive timeout), `KIOTTY_NET_CODE_PEER_CLOSED`, `KIOTTY_NET_CODE_PEER_NOT_READING` (base 의 send timeout), `KIOTTY_NET_CODE_PROTOCOL_ERROR` (400 / 413 / 414 / 431 / 501 뒤), `KIOTTY_NET_CODE_CLOSED` (서버 close). 연결의 handle 은 오지 않는다 (끊긴 뒤라 쓸 곳이 없다). 상대는 info 로 본다 |
| `on_error(app_context, info, code, message)` | 연결이 생기기 전이나 연결과 따로 생기는 문제: `KIOTTY_NET_CODE_TLS_*` (handshake 실패), `KIOTTY_NET_CODE_TABLE_FULL` (연결 수 초과. 자리가 없으면 서버가 accept 를 멈추고 새 연결은 OS 대기열에서 기다리므로 드물다), accept 실패. 잘못된 요청은 on_disconnected 로 오고 여기로 두 번 오지 않는다 |

C++ 은 `server.onDisconnected(action)`, `server.onError(action)` 다.

### 1.8 C 예제

```c
#include "kiotty_net_http.h"

typedef struct App
{
    WorkerPool* pool;
} App;

static void onHealth(KiottyNetAppContext app, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
{
    (void)app;
    (void)request;
    KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_OK);
}

static void onDevice(KiottyNetAppContext app, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
{
    KiottyNetSpan id = tail_after(request->path, "/api/devices/");
    KiottyNetSpan json = render_device((App*)app, id);
    KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, json);
}

static void onReport(KiottyNetAppContext app, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
{
    (void)app;
    (void)request;
    KiottyNet_startHttpWork(response);
}

static void onReportWork(KiottyNetAppContext app, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
{
    ReportJob* job = acquire_job(((App*)app)->pool);
    job->response  = response;
    job->work      = work;
    copy_span(&job->query, request->query);
    post_job(((App*)app)->pool, job);
}

static void onEventsWork(KiottyNetAppContext app, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
{
    (void)request;
    EventJob* job = acquire_event_job(((App*)app)->pool);
    job->work     = work;
    post_event_job(((App*)app)->pool, job);
}

static void onEvents(KiottyNetAppContext app, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
{
    (void)app;
    (void)request;
    KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_OK);
    KiottyNet_setHttpHeader(response, "Cache-Control", "no-cache");
    KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
    KiottyNet_startHttpWork(response);
}

static void run_event_job(EventJob* job)
{
    for (;;)
    {
        KiottyNetSpan event = wait_next_event(job, 15000);
        if (event.length == 0)
        {
            event = span_of(": ping\n\n");
        }
        KiottyNetResult written = KiottyNet_writeHttpWorkChunk(job->work, event, 5000);
        if (written.code != KIOTTY_NET_CODE_SUCCESS)
        {
            KiottyNet_finishHttpWorkWithClose(job->work, written.code);
            return;
        }
        if (job->done)
        {
            KiottyNet_finishHttpWorkWithResponse(job->work);
            return;
        }
    }
}

static void onDisconnected(KiottyNetAppContext app, KiottyNetConnectionInfo info, KiottyNetCode reason)
{
    audit_disconnect((App*)app, info, reason);
}

static void run_job(ReportJob* job)
{
    KiottyNetSpan json = build_report(job);
    if (KiottyNet_isHttpWorkConnected(job->work) == 0)
    {
        KiottyNet_finishHttpWorkWithClose(job->work, KIOTTY_NET_CODE_PEER_CLOSED);
        return;
    }
    KiottyNet_setHttpBody(job->response, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, json);
    KiottyNet_finishHttpWorkWithResponse(job->work);
}

int main(void)
{
    KiottyNetContextResult initialized = KiottyNet_initializeContext();
    if (initialized.code != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }
    KiottyNetContext context = initialized.handle;

    KiottyNetServerConfiguration configuration;
    KiottyNet_initializeServerConfiguration(&configuration);
    KiottyNet_configureServerMaxClients(&configuration, 16);
    KiottyNetAddress address;
    KiottyNet_initializeTcpAddress(&address, "0.0.0.0", 8080);

    KiottyNetHttpLimits limits;
    KiottyNet_initializeHttpLimits(&limits);
    KiottyNet_configureHttpBodyCapacity(&limits, 16 * 1024);

    App app = { create_pool(4) };
    KiottyNetHttpServerResult created = KiottyNet_createHttpServer(context, &configuration, &limits);
    if (created.code != KIOTTY_NET_CODE_SUCCESS)
    {
        KiottyNet_releaseContext(context);
        return 1;
    }
    KiottyNetHttpServerHandle server = created.handle;

    KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "/health",         KiottyNet_makeHttpRoute(onHealth, NULL));
    KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "/api/devices/*", KiottyNet_makeHttpRoute(onDevice, NULL));
    KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "/api/report",    KiottyNet_makeHttpRoute(onReport, onReportWork));
    KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "/api/events",    KiottyNet_makeHttpRoute(onEvents, onEventsWork));
    KiottyNet_serveHttpMemory(server, "/app.wasm", KIOTTY_NET_HTTP_CONTENT_TYPE_WASM, wasm_span());
    KiottyNet_serveHttpDirectory(server, "/", "./www/");

    KiottyNet_onHttpDisconnected(server, onDisconnected);
    KiottyNet_runHttpServer(server, &address, NULL, &app);

    KiottyNet_destroyHttpServer(context, server);
    KiottyNet_releaseContext(context);
    return 0;
}
```

TLS 는 `KiottyNetTlsServerConfiguration tls; KiottyNet_initializeTlsServerConfiguration(&tls, "server.crt", "server.key", NULL);` 를 만들어 run 의 세 번째 인자로 넘기면 된다. 인증서 오류 (`KIOTTY_NET_CODE_TLS_*`) 는 create 가 아니라 run 이 돌려준다. close 뒤 같은 server 를 다른 주소, 다른 TLS 설정으로 다시 run 해도 된다.

### 1.9 C++ 예제

```cpp
#include "kiotty_net_http.h"

struct App
{
    WorkerPool pool;
};

int main()
{
    KiottyNet_Context context;

    KiottyNetServerConfiguration configuration;
    KiottyNet_ServerConfigurationBuilder(configuration).maxClients(16).build();
    KiottyNetAddress address;
    KiottyNet_initializeTcpAddress(&address, "0.0.0.0", 8080);

    KiottyNetHttpLimits limits;
    KiottyNet_HttpLimitsBuilder(limits).bodyCapacity(16 * 1024).build();

    App                  app;
    KiottyNet_HttpServer server(context.handle(), configuration, limits);
    if (!server)
    {
        return static_cast<int>(server.code());
    }

    server.get("/health", [](KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r)
    {
        KiottyNet_HttpResponse(r).status(KIOTTY_NET_HTTP_STATUS_OK);
    });

    server.get("/api/devices/*", KiottyNet_makeHttpRoute(
        [](KiottyNetAppContext ctx, const KiottyNetHttpRequest* q, KiottyNetHttpResponseHandle r)
        {
            KiottyNet_HttpResponse(r).body(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, static_cast<App*>(ctx)->render(q->path));
        },
        nullptr));

    server.get("/api/report", KiottyNet_makeHttpRoute(
        [](KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r)
        {
            KiottyNet_HttpResponse(r).startWork();
        },
        [](KiottyNetAppContext ctx, const KiottyNetHttpRequest* q, KiottyNetHttpResponseHandle r, KiottyNetHttpWorkHandle w)
        {
            std::string        query(q->query.data, q->query.length);
            KiottyNet_HttpWork work(w);
            static_cast<App*>(ctx)->pool.post([query, r, work = std::move(work)]() mutable
            {
                std::string json = buildReport(query);
                if (!work.connected())
                {
                    return;
                }
                KiottyNet_HttpResponse(r).body(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, { json.data(), json.size() });
                work.finishWithResponse();
            });
        }));

    server.get("/api/events", KiottyNet_makeHttpRoute(
        [](KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r)
        {
            KiottyNet_HttpResponse response(r);
            response.header("Cache-Control", "no-cache");
            response.bodyStream(KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
            response.startWork();
        },
        [](KiottyNetAppContext ctx, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle, KiottyNetHttpWorkHandle w)
        {
            KiottyNet_HttpWork work(w);
            static_cast<App*>(ctx)->pool.post([ctx, work = std::move(work)]() mutable
            {
                for (std::string event : static_cast<App*>(ctx)->events())
                {
                    if (work.write({ event.data(), event.size() }, 5000).code != KIOTTY_NET_CODE_SUCCESS)
                    {
                        return;
                    }
                }
                work.finishWithResponse();
            });
        }));

    server.serveMemory("/app.wasm", KIOTTY_NET_HTTP_CONTENT_TYPE_WASM, wasmSpan());
    server.serveDirectory("/", "./www/");

    server.onError([](KiottyNetAppContext, KiottyNetConnectionInfo, KiottyNetCode code, const char* message)
    {
        std::fprintf(stderr, "http error %d: %s\n", static_cast<int>(code), message);
    });
    std::thread io([&] { server.run(address, &app); });                         // TLS 면 server.run(address, tls, &app)
    if (server.await(3000) != KIOTTY_NET_CODE_SUCCESS)
    {
        server.close();
    }
    waitForShutdownSignal();
    server.close();
    io.join();
    return 0;
}
```

- capture 없는 람다는 함수 포인터로 바뀌므로 그대로 들어간다. capture 가 필요하면 run 의 `app_context` 로 넘긴다.
- 스트림 worker 가 write 실패로 return 하면 `KiottyNet_HttpWork` 의 소멸자가 `finishWithClose()` 를 부른다.
- `KiottyNet_HttpServer` 는 소멸자가 `KiottyNet_destroyHttpServer` 를 부른다. `run` 이 return 한 뒤 (`io.join()` 뒤) 소멸되어야 한다.

### 1.10 반환 코드 요약

| 함수 | 코드 | 뜻 |
|---|---|---|
| `addHttpRoute`, `serveHttp*` | `KIOTTY_NET_CODE_TABLE_FULL` | `max_routes` 초과 (`setHttpNotFound` 는 표를 쓰지 않으므로 이 코드가 없다) |
| `onHttpDisconnected`, `onHttpError` | `KIOTTY_NET_CODE_BUSY` | run 이 시작된 뒤 |
| | `KIOTTY_NET_CODE_DUPLICATE_RULE` | 같은 (method, path) |
| | `KIOTTY_NET_CODE_INVALID_ARGUMENT` | path 형식 / 길이, `onRequest` NULL, 지원하지 않는 method, 끝에 `/` 없는 url_prefix |
| | `KIOTTY_NET_CODE_BUSY` | run 이 시작된 뒤 |
| `createHttpServer` | `KIOTTY_NET_CODE_INVALID_ARGUMENT`, `KIOTTY_NET_CODE_TABLE_FULL` | 1.1. 메모리를 잡지 못하면 process 가 끝난다 (base 와 같다) |
| `setHttpHeader`, `setHttpBody` | `KIOTTY_NET_CODE_BUFFER_FULL` | `response_bytes` 초과 |
| `setHttpHeader` | `KIOTTY_NET_CODE_INVALID_ARGUMENT` | `Content-Type`, 라이브러리 헤더, CR / LF |
| body setter, `writeHttpWorkChunk` | `KIOTTY_NET_CODE_INVALID_ENCODING` | 텍스트 계열인데 UTF-8 아님 |
| body setter | `KIOTTY_NET_CODE_INVALID_ARGUMENT` | 이름 없는 `CUSTOM`, 자리에 맞지 않는 type (`BY_EXTENSION` 은 파일만, `EVENT_STREAM` 은 스트림만) |
| setter 전부 | `KIOTTY_NET_CODE_BUSY` | 헤더가 이미 나감 |
| `setHttpFile` | `KIOTTY_NET_CODE_NOT_FOUND` | 파일 없음. 파일을 type 검사보다 먼저 열므로 없는 파일 + 잘못된 type 도 `NOT_FOUND` 다 |
| `writeHttpWorkChunk` | `KIOTTY_NET_CODE_INVALID_ARGUMENT` | body 가 스트림이 아님, 빈 data. 인자 검사가 남아 있는 실패보다 먼저다 (앞의 write 가 실패한 뒤에도 빈 data 는 `INVALID_ARGUMENT`) |
| | `KIOTTY_NET_CODE_TIMEOUT`, `KIOTTY_NET_CODE_PEER_CLOSED`, `KIOTTY_NET_CODE_CLOSED` | 그 뒤는 `finishHttpWorkWithClose` 만 |
| `startHttpWork` | `KIOTTY_NET_CODE_INVALID_ARGUMENT` | 라우트에 `onWorkStarted` 가 없음 |
| | `KIOTTY_NET_CODE_BUSY` | 이미 startWork 했음 |
| `finishHttpWork*` | `KIOTTY_NET_CODE_BUSY` | 두 번째 finish. keep-alive 로 다음 요청이 열린 뒤에는 handle 의 generation 이 바뀌어 `KIOTTY_NET_CODE_PEER_CLOSED` 다 |
| 응답 / Work 함수 공통 | `KIOTTY_NET_CODE_PEER_CLOSED` | 닫힌 handle |
| `runHttpServer` | `KIOTTY_NET_CODE_SOCKET_ERROR`, `KIOTTY_NET_CODE_INVALID_ARGUMENT`, `KIOTTY_NET_CODE_TLS_*` | listen 실패, handle / 주소 / TLS 설정 잘못, 인증서 |
| 끊김 action 의 reason | `KIOTTY_NET_CODE_PEER_NOT_READING` | 상대가 읽지 않아 send timeout |
| `awaitHttpServer` | `KIOTTY_NET_CODE_TIMEOUT` | 시간 안에 loop 가 시작되지 않음 |

### 1.11 아직 없는 것

- WebSocket Upgrade 경로는 `kiotty_net_websocket.h` 에 있다 (`KiottyNet_addWebSocket(http_server, "/chat")` 과 `KiottyNet_onWebSocket*`, C++ `KiottyNet_WebSocketRoute(server, "/chat")`). 이 헤더는 그것을 모른다. 사용법은 [user_guide_websocket.md](user_guide_websocket.md).
- HttpClient 는 2절이다 ([design_http_client_v0.html](../design/design_http_client_v0.html)).
- JSON 은 라이브러리 밖이다. body 는 바이트이고 텍스트 계열이면 UTF-8 만 본다.
- 간단한 CORS 설정, `:id` 파라미터 파싱, 응답 압축, Range 요청, 요청 body 스트리밍, sendfile 은 지원하지 않는다.

## 2. HTTP 클라이언트

`KiottyNetHttpClientHandle` 은 동기 HTTP/1.1 클라이언트다. base 의 StreamClient 위에 있고 IO Thread 도 콜백도 없다. 부른 thread 가 보내고 기다린다. 선언은 서버와 같은 `kiotty_net_http.h` 에 있다. 설계는 [design_http_client_v0.html](../design/design_http_client_v0.html), 사이클 C1 ~ C5.

| 것 | 무엇 |
|---|---|
| 모양 | 요청 함수는 상태줄과 헤더까지 받고 돌아온다. body 는 언제나 `readHttpReplyBody` 로 받은 만큼 꺼낸다. 작은 JSON 도 SSE 도 같은 길 |
| 연결 | client 하나 = 연결 하나 = keep-alive. 끊긴 연결은 다음 요청이 다시 연다 |
| 설정 | `KiottyNetClientConfiguration` (버퍼, connect timeout, 재시도) + `KiottyNetHttpLimits` (머리 상한). 클라이언트 전용 설정 struct 는 없다 |
| 헤더 | 공통 헤더는 client 에 등록. 요청마다의 헤더는 앱의 stack 배열을 요청 함수의 인자로 |
| thread | client 하나는 한 번에 한 thread. 다른 thread 는 `closeHttpClient` 로 걸린 호출을 깨운다 |

### 2.1 쓰는 순서

1. **만들기.** `KiottyNet_createHttpClient(context, &configuration, &limits)` 는 자원만 잡는다 (`limits` NULL = 기본값).
2. **연결.** `KiottyNet_connectHttpClient(client, &address, &tls)` 가 대상을 기억하고 연다. tls NULL 이면 http.
3. **공통 헤더 등록.** `KiottyNet_setHttpClientHeader(client, "Authorization", "Bearer abc")`. 이 client 의 모든 요청에 붙는다.
4. **요청마다의 헤더 만들기.** `KiottyNetHttpHeader headers[2]` 에 `KiottyNet_makeHttpHeader(name, value)`.
5. **요청.** `KiottyNet_getHttp(client, path, headers, 2, timeout)` 등. 헤더는 path 바로 뒤의 인자, 없으면 `NULL, 0`.
6. **body 읽기.** `KiottyNet_readHttpReplyBody(client, buffer, length, timeout)` 를 `{ 0, SUCCESS }` 까지.
7. **(필요하면) 깨우기.** 다른 thread 에서 `KiottyNet_closeHttpClient(client)`. 걸린 호출이 `CLOSED` 로 돌아온다.
8. **공통 헤더 지우기.** 하나는 `setHttpClientHeader(client, name, NULL)`, 전부는 `KiottyNet_clearHttpClientHeaders(client)`.
9. **없애기.** `KiottyNet_destroyHttpClient(context, client)`. 연결도 닫힌다.

### 2.2 함수

```c
KiottyNetHttpClientResult  KiottyNet_createHttpClient(KiottyNetContext context, const KiottyNetClientConfiguration* configuration, const KiottyNetHttpLimits* limits /* NULL = 기본값 */);
void                       KiottyNet_destroyHttpClient(KiottyNetContext context, KiottyNetHttpClientHandle client);
KiottyNetCode              KiottyNet_connectHttpClient(KiottyNetHttpClientHandle client, const KiottyNetAddress* address, const KiottyNetTlsClientConfiguration* tls /* NULL = http */);
void                       KiottyNet_disconnectHttpClient(KiottyNetHttpClientHandle client);
void                       KiottyNet_closeHttpClient(KiottyNetHttpClientHandle client);                 /* 아무 thread */
KiottyNetCode              KiottyNet_setHttpClientHeader(KiottyNetHttpClientHandle client, const char* name, const char* value /* NULL = 지움 */);
KiottyNetCode              KiottyNet_clearHttpClientHeaders(KiottyNetHttpClientHandle client);

KiottyNetHttpHeader        KiottyNet_makeHttpHeader(const char* name, const char* value);
KiottyNetHttpClientRequest KiottyNet_makeHttpClientRequest(KiottyNetHttpMethod method, const char* path);   /* type OCTET_STREAM, body 없음 */

KiottyNetHttpReply KiottyNet_sendHttpRequest(client, const KiottyNetHttpClientRequest* request, headers, header_count, timeout_milliseconds);
KiottyNetHttpReply KiottyNet_getHttp    (client, path, headers, header_count, timeout_milliseconds);
KiottyNetHttpReply KiottyNet_postHttp   (client, path, headers, header_count, type, body, timeout_milliseconds);
KiottyNetHttpReply KiottyNet_putHttp    (client, path, headers, header_count, type, body, timeout_milliseconds);
KiottyNetHttpReply KiottyNet_deleteHttp (client, path, headers, header_count, timeout_milliseconds);
KiottyNetHttpReply KiottyNet_patchHttp  (client, path, headers, header_count, type, body, timeout_milliseconds);
KiottyNetHttpReply KiottyNet_headHttp   (client, path, headers, header_count, timeout_milliseconds);
KiottyNetHttpReply KiottyNet_optionsHttp(client, path, headers, header_count, timeout_milliseconds);   /* path "*" 는 서버 전체 */

KiottyNetResult    KiottyNet_readHttpReplyBody(KiottyNetHttpClientHandle client, void* buffer, size_t length, uint32_t timeout_milliseconds);
KiottyNetSpan      KiottyNet_findHttpReplyHeader(const KiottyNetHttpReply* reply, const char* name);   /* 대소문자 무시. 없으면 { NULL, 0 } */
uint32_t           KiottyNet_getHttpAllowedMethods(const KiottyNetHttpReply* reply);                   /* Allow 를 KIOTTY_NET_HTTP_METHOD_BIT 의 합으로. 없으면 0 */
```

| 함수 | 하는 일 |
|---|---|
| `createHttpClient` | 버퍼를 잡는다 (2.9). `{ code, handle }`. NULL context / 설정, 초기화하지 않은 설정, 범위 밖 limits 는 `{ INVALID_ARGUMENT, NULL }`. 쓰는 limits 필드는 `max_header_bytes`, `max_headers` 둘이다 |
| `connectHttpClient` | 대상 (address, tls) 을 기억하고 연결한다. 연결 시도와 재시도는 설정의 `connect_timeout_milliseconds`, `max_trial`, `trial_interval_milliseconds` 를 따른다. 연결만 실패하면 (서버가 없음, 거절, timeout) 대상은 남아 다음 요청이 다시 연다. 인자가 틀리면 (주소, Host 가 될 수 없는 host, TLS 설정) `INVALID_ARGUMENT` 이고 **이전 대상도 지운다**. 그 뒤의 요청은 connect 한 적 없을 때처럼 `INVALID_ARGUMENT` 다. 다시 부르면 대상을 바꾼다 |
| `disconnectHttpClient` | 연결만 닫는다. 대상과 공통 헤더는 남는다. 다른 호출이 걸려 있으면 아무것도 하지 않는다 |
| `closeHttpClient` | 걸린 호출 (요청, read, 연결) 을 `CLOSED` 로 돌아오게 하고 연결을 닫는다. 걸린 호출이 없으면 그 자리에서 연결을 닫는다. 대상과 공통 헤더는 남고 다음 요청이 다시 연다 |
| `setHttpClientHeader` | 공통 헤더를 더하거나 같은 이름 (대소문자 무시) 을 바꾼다. value NULL 은 그 이름을 지운다 (없어도 `SUCCESS`) |
| `clearHttpClientHeaders` | 공통 헤더를 전부 지운다 |
| `makeHttpHeader` | strlen 으로 Span 둘. name / value 가 NULL 이거나 1 MiB 를 넘으면 빈 헤더를 돌려주고, 그 헤더를 넘긴 요청이 `INVALID_ARGUMENT` 다 |
| `makeHttpClientRequest` | `{ method, path, OCTET_STREAM, { NULL, 0 } }`. type 과 body 는 앱이 채운다 |
| `sendHttpRequest` | struct 하나로 요청. 메서드별 일곱은 안에서 이것을 부른다. 메서드별 함수에 없는 조합 (body 있는 DELETE 등) 은 이것으로 |
| `readHttpReplyBody` | 마지막 요청의 body 를 받은 만큼 (2.5) |
| `findHttpReplyHeader` | 응답 헤더 찾기. 같은 이름 여럿이면 첫 것 |
| `getHttpAllowedMethods` | OPTIONS 응답의 `Allow`. 여러 줄은 합치고 모르는 이름은 버린다 |

`headHttp`, `optionsHttp` 는 HTTP 메서드 이름을 동사로 쓴 것이다 (naming 규약 1번의 예외).

### 2.3 헤더 : 공통과 요청마다

| 것 | 어디 | 수명 | 지우기 |
|---|---|---|---|
| 공통 헤더 | client 안의 저장소 (복사) | connect / disconnect / close 를 지나 destroy 까지 | `setHttpClientHeader(client, name, NULL)`, `clearHttpClientHeaders(client)` |
| 요청마다의 헤더 | 앱의 배열 (`KiottyNetHttpHeader[]`) | 요청 호출 동안만 읽는다 | 앱이 버린다 |

- 같은 이름이 공통과 요청 둘 다에 있으면 그 요청에서는 요청 쪽만 나간다.
- 공통 헤더의 text 합은 `max_header_bytes`, 개수는 `max_headers` 안이다. 넘으면 `BUFFER_FULL` 이고 저장소는 그대로다.
- 다른 thread 의 호출이 걸려 있는 동안의 set / clear 는 `BUSY`.
- 지운 공통 헤더의 자리는 0 으로 채운다 (토큰이 남지 않는다). 보낸 요청 머리도 보낸 뒤 0 으로 지운다.

글자 규칙 (RFC 9110 5.1, 5.5. 서버의 헤더 파서와 같다). 공통 헤더와 요청 헤더 모두.

| 것 | 허용 | `INVALID_ARGUMENT` |
|---|---|---|
| name | token 글자 (영숫자, ``! # $ % & ' * + - . ^ _ ` \| ~``) | 빈 이름, 공백, `:`, 비 ASCII |
| value | 보이는 ASCII, 공백, 탭, UTF-8. 앞뒤 공백은 잘라 보낸다 | CR, LF, NUL (헤더 주입), 깨진 UTF-8 |
| path | `/` 로 시작하는 보이는 ASCII (query 포함). OPTIONS 만 `*` | 공백, CR, LF, 상대 경로, OPTIONS 아닌 메서드의 `*` |

`Host`, `Content-Length`, `Transfer-Encoding`, `Connection` 은 라이브러리가 쓴다. 공통 헤더나 요청 헤더에 넣으면 `INVALID_ARGUMENT`.

C++ 은 `KiottyNet_HttpHeaders<N>` (기본 N = 8) 이 stack 배열이다. `add(name, value)` 를 사슬로 부르고, N 을 넘으면 그 헤더를 버리고 `TABLE_FULL` 을 기록한다. 그 헤더 목록을 받은 요청은 보내지 않고 `TABLE_FULL` 이다. 요청 함수는 템플릿이 아닌 기반 `KiottyNet_HttpHeaderList` 를 받으므로 N 이 달라도 된다. 복사와 이동은 막혀 있다 (가리키는 배열이 어긋난다).

### 2.4 요청

```c
typedef struct KiottyNetHttpClientRequest {
    KiottyNetHttpMethod      method;
    const char*              path;    /* "/a/b?x=1". OPTIONS 만 "*" */
    KiottyNetHttpContentType type;    /* body 의 Content-Type */
    KiottyNetSpan            body;    /* length 0 이면 body 없음 */
} KiottyNetHttpClientRequest;
```

**라이브러리가 쓰는 머리.** 이 순서로 나간다.

1. `METHOD path HTTP/1.1`
2. `Host`: TCP 는 address 의 host. 포트가 기본 (http 80, https 443) 이 아니면 `:port`, IPv6 literal 은 `[...]`. pipe 연결은 `localhost`
3. 공통 헤더 (요청 헤더에 같은 이름이 있는 것은 빼고)
4. 요청 헤더 (value 는 앞뒤 공백을 잘라서)
5. body 가 있으면 `Content-Type` 과 `Content-Length`. POST / PUT / PATCH 는 body 가 없어도 `Content-Length: 0`
6. 빈 줄

`Content-Type` 의 규칙.

| 경우 | 나가는 것 |
|---|---|
| type 이 `CUSTOM` 이 아니고 요청 헤더에 `Content-Type` 이 없다 | type 의 문자열 (텍스트 계열은 `; charset=utf-8`). 공통 헤더의 `Content-Type` 은 뺀다 |
| 요청 헤더에 `Content-Type` 이 있다 | 요청 헤더의 것 하나 |
| type 이 `CUSTOM` | 라이브러리는 쓰지 않는다. 앱이 요청 헤더에 넣는다 (없으면 공통 헤더의 것, 둘 다 없으면 없음) |
| body 가 없다 | 라이브러리는 쓰지 않는다 |

- type `BY_EXTENSION` 은 `INVALID_ARGUMENT`. 텍스트 계열 type 의 body 가 UTF-8 이 아니면 `INVALID_ENCODING` (서버의 body setter 와 같다).
- body 는 복사하지 않고 앱 버퍼에서 바로 보낸다. 요청 호출 동안만 살아 있으면 된다. 요청을 chunked 로 올리는 길은 없다. 언제나 `Content-Length` 다.
- 머리 전체 (요청줄, Host, 헤더) 는 `max_header_bytes` 안이어야 하고 넘으면 `REQUEST_TOO_LARGE`. 헤더 줄 수 (Host 포함) 가 `max_headers` 를 넘으면 `TABLE_FULL`. 둘 다 아무것도 보내지 않는다.
- 인자 검사에서 실패한 요청 (`INVALID_ARGUMENT`, `INVALID_ENCODING`, `REQUEST_TOO_LARGE`, `TABLE_FULL`) 은 이전 응답과 읽지 않은 body 를 건드리지 않는다.

**Reply.** 요청 함수는 `KiottyNetHttpReply { code, status, headers, header_count }` 를 돌려준다.

| code | 뜻 |
|---|---|
| `SUCCESS` | 상태줄과 헤더를 받았다. 4xx / 5xx 도 `SUCCESS` 다 (HTTP 의 실패는 status 로 본다). body 는 `readHttpReplyBody` |
| `TIMEOUT` | timeout 안에 머리까지 오지 않았다 (다시 연결 포함). 연결은 닫는다 |
| `PEER_CLOSED` | 응답 머리 전에 서버가 끊었다. 다시 보내지 않은 POST / PATCH 도 (2.6) |
| `PROTOCOL_ERROR` | 상태줄 / 헤더가 깨졌다. 101. 1xx 가 16 번을 넘었다. `Transfer-Encoding` 이 `chunked` 하나가 아니다. `Content-Length` 가 여럿이다 (값이 같아도). 연결은 닫는다 |
| `BUFFER_FULL` | 응답 머리가 `max_header_bytes` / `max_headers` 를 넘었다. `max_header_bytes` 가 64 KiB 를 넘게 설정되었을 때 응답의 한 줄이 64 KiB 를 넘는다 |
| `REQUEST_TOO_LARGE` | 보낼 머리가 `max_header_bytes` 를 넘는다 |
| `TABLE_FULL` | 보낼 헤더 줄 수가 `max_headers` 를 넘는다. C++ 헤더 목록이 넘쳤다 |
| `INVALID_ARGUMENT` | NULL, 잘못된 path / 헤더 / 메서드, 라이브러리가 쓰는 헤더, `BY_EXTENSION`, connect 한 적 없음 (인자가 틀린 connect 가 대상을 지운 뒤도) |
| `INVALID_ENCODING` | 텍스트 계열 type 의 body 가 UTF-8 아님 |
| `BUSY` | 다른 thread 의 호출이 걸려 있다 |
| `CLOSED` | 다른 thread 의 `closeHttpClient` |
| `SOCKET_ERROR`, `TLS_*` | (다시) 연결 실패. connect 와 같다 |

- code 가 `SUCCESS` 가 아니면 status, headers, header_count 는 0 이다.
- headers 는 client 안의 응답 머리 저장소를 가리킨다. 다음 요청 (인자 검사를 지난 것), connect, destroy 까지 유효하다. body 를 읽어도, disconnect / close 를 해도 무효가 되지 않는다. 보관할 것은 복사한다.
- 같은 이름의 헤더 여럿은 배열에 그대로 있다. `findHttpReplyHeader` 는 첫 것을 준다.

### 2.5 응답 body 읽기

```c
for (;;)
{
    KiottyNetResult got = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), 3000);
    if (got.code != KIOTTY_NET_CODE_SUCCESS || got.bytes == 0) { break; }
    consume(buffer, got.bytes);
}
```

- 마지막 요청의 body 를 chunk 를 벗긴 바이트로, 받은 만큼 (최대 length) 준다. 머리와 같이 온 body 앞부분을 먼저 주고, 없으면 socket 에서 한 바이트 이상 올 때까지 기다린다.
- body 가 끝나면 `{ 0, SUCCESS }` 이고 그 뒤로도 같다. body 가 없는 응답 (HEAD, 204, 304), 요청을 보낸 적이 없을 때, 검사에서 실패한 요청 뒤 (이전 body 가 다 읽혔으면) 도 `{ 0, SUCCESS }`.
- body 에는 상한이 없다. 앱 버퍼보다 큰 body 는 여러 번 읽는다. 텍스트 계열이어도 UTF-8 을 보지 않는다 (글자 하나가 read 둘에 나뉠 수 있다).
- `TIMEOUT` 은 연결을 닫지 않는다. 다시 read 하면 이어서 받는다 (SSE 는 조용한 시간이 길다).
- 그 밖의 실패는 연결을 닫는다: `PROTOCOL_ERROR` (깨진 chunk), `PEER_CLOSED` (body 가 끝나기 전에 서버가 끊음. 그 뒤의 read 도 계속 `PEER_CLOSED`), `CLOSED` (다른 thread 의 close, 또는 body 중간의 disconnect).
- buffer NULL, length 0 은 `INVALID_ARGUMENT`. 다른 thread 의 호출이 걸려 있으면 `BUSY`.
- **body 를 다 읽지 않고 다음 요청을 보내면 그 연결을 닫고 다시 연다.** 남은 body 를 읽어 버리지 않는다 (끝이 없는 SSE 에서 돌아오지 않으므로). 스트림을 끝내는 길이 이것이다.

body 의 끝을 정하는 규칙.

| 응답 | body |
|---|---|
| HEAD 의 응답, 204, 304 | 없음. 헤더가 무엇이든 |
| `Transfer-Encoding: chunked` | chunk 를 벗긴다. extension 과 trailer 는 버린다 |
| `Content-Length: n` | n 바이트 |
| 둘 다 없음 | 서버가 닫을 때까지. 닫히면 `{ 0, SUCCESS }`. 그 연결은 keep-alive 가 아니다 |
| 1xx (100 Continue 등) | 건너뛰고 다음 응답을 읽는다 (16 번까지) |

- keep-alive 는 서버와 같다. HTTP/1.1 이고 `Connection: close` 가 없으면 남긴다. HTTP/1.0 은 `Connection: keep-alive` 가 있을 때만. 남기지 않는 응답은 body 를 다 읽은 뒤 닫고 다음 요청이 다시 연다.
- SSE 를 이벤트로 나누는 일 (`data:` 줄, 빈 줄, `id:`, `retry:`) 은 앱이다. JSON 이 라이브러리 밖인 것과 같다.

### 2.6 다시 연결과 재시도

- 연결이 없으면 (처음, 끊긴 뒤, close 뒤) 요청이 그 자리에서 연결한다. 연결은 설정의 재시도를 따르고 호출의 timeout 이 그것을 자른다 (2.7). connect 를 한 번도 하지 않았거나 인자가 틀린 connect 가 대상을 지웠으면 `INVALID_ARGUMENT`.
- **keep-alive 가 서버에서 닫혀 있었으면 한 번 다시 보낸다.** 조건은 넷 다:

| 조건 | 뜻 |
|---|---|
| 이미 열려 있던 연결 | 이 요청이 연 연결이 아니다. 앞 요청이 쓴 연결과 `connectHttpClient` 로 열어 두고 쉬던 연결이 모두 든다 |
| 응답 바이트 없음 | 이 요청에서 줄을 하나도 받지 못했다 |
| 끊김 | 실패가 `PEER_CLOSED` 또는 `SOCKET_ERROR` (EOF, reset, broken pipe). `TIMEOUT`, `CLOSED`, `PROTOCOL_ERROR`, TLS 오류는 다시 보내지 않는다 |
| 멱등 메서드 | GET, HEAD, PUT, DELETE, OPTIONS |

- POST / PATCH 는 다시 보내지 않고 `PEER_CLOSED` 다 (서버가 이미 처리했을 수 있다. RFC 9110 9.2.2). 다시 보낼지는 앱이 정한다.
- 다시 보내기는 새 연결로 한 번뿐이다. 그 새 연결의 실패는 그대로 돌려준다.

### 2.7 timeout

- **호출마다 하나다.** 요청의 timeout 은 (다시) 연결 + 송신 + 머리 수신 전체이고, 다시 보내기도 같은 시간 안이다. 다시 연결의 connect 시도, TLS handshake, 재시도 사이의 쉼을 남은 시간으로 자른다. 시간이 다 되면 마지막 시도의 code 다 (`TIMEOUT`, 포트가 거절하면 `SOCKET_ERROR`, handshake 중이면 `TLS_HANDSHAKE_TIMEOUT` 일 수 있다).
- read 의 timeout 은 read 한 번 (한 바이트 이상 올 때까지) 이다.
- **0 은 제한 없음** (라이브러리 전체와 같다). 요청 timeout 0 이면 다시 연결은 설정의 connect 값을 그대로 쓴다.
- `connectHttpClient` 는 timeout 인자가 없다. 설정의 connect timeout 과 재시도 그대로다.

### 2.8 thread

- client 하나는 한 번에 한 thread 가 쓴다. 다른 thread 가 동시에 부르면 `BUSY` 이고 아무것도 하지 않는다 (connect, 요청, read, 공통 헤더 set / clear. disconnect 는 `void` 라 조용히 아무것도 하지 않는다). `make*`, `findHttpReplyHeader`, `getHttpAllowedMethods` 는 client 를 만지지 않는다.
- 요청을 동시에 여럿 보내려면 client 를 여럿 만든다. MCP 처럼 스트림 하나를 열어 두고 요청도 하면 client 둘 (요청용, 알림용).
- **`closeHttpClient` 만 아무 thread 에서나 부른다.** 걸린 호출 (요청, read, 다시 연결, connect) 이 `CLOSED` 로 돌아오고 연결은 닫힌다. 닫는 일은 걸린 호출의 thread 가 한다. 호출 사이의 close 는 다음 호출을 막지 않는다 (그 호출은 연결을 다시 연다).
- **깨우지 못하는 것: connect 안의 이름 풀기 (`getaddrinfo`).** host 가 이름이면 DNS 응답까지 기다린다.
- destroy 는 다른 thread 의 호출이 끝난 뒤에 부른다. destroy 와 경쟁하는 close 는 다른 handle 처럼 쓰지 않는다.

### 2.9 메모리와 상한

create 때 다 잡고 요청 중의 할당은 없다.

| 것 | 크기 |
|---|---|
| 머리 버퍼 | `2 × max_header_bytes`. 앞 반은 공통 헤더, 뒤 반은 요청마다 만드는 머리 |
| 응답 머리 저장소 | `max_header_bytes` + 헤더 표 `max_headers` 칸. 공통 헤더 표도 `max_headers` 칸 |
| StreamClient | 수신 Stage `max(receive_capacity, max_header_bytes + 2)` (상한 64 KiB. 응답의 줄이 여기서 잘린다), `send_capacity`. TLS 면 TLS Stage |
| 깨우는 handle | StreamClient 마다 하나: POSIX 는 pipe (fd 2 개), Windows 는 UDP socket 1 개. 평문 StreamClient 도 가진다. pool slot 이 처음 쓰일 때 열고 context 를 놓을 때까지 둔다. 열지 못하면 (fd / socket 이 모자람: `EMFILE`, `ENFILE`, `WSAEMFILE`) create 가 `{ SOCKET_ERROR, NULL }` 이다 |

- 쓰는 limits 필드는 `max_header_bytes` (기본 8 KiB) 와 `max_headers` (기본 32) 둘이다. 응답 머리, 보내는 머리, 공통 헤더 저장소에 같은 값이 쓰인다. 서버 전용 필드 (`max_routes`, `max_body_bytes`, `response_bytes`, timeout, WebSocket 타이머) 는 읽지 않는다.
- 응답의 헤더 한 줄은 `max_header_bytes` 까지 받는다 (최대 64 KiB). client 가 create 때 수신 Stage 를 `max(receive_capacity, max_header_bytes + 2)` 로 키우므로 앱이 `receive_capacity` 를 맞출 일은 없다. `max_header_bytes` 를 64 KiB 보다 크게 잡으면 한 줄만 64 KiB 로 묶인다 (머리 전체는 `max_header_bytes`).
- 공통 헤더가 앞 반을 다 써도 요청 머리의 상한은 줄지 않는다.
- body 버퍼는 없다. body 는 수신 Stage 에서 앱 버퍼로 바로 간다.

### 2.10 C 예제

장비가 상태를 보고하고, 다른 thread 가 이벤트 스트림 (SSE) 을 받는다.

```c
#include "kiotty_net_http.h"

typedef struct Events
{
    KiottyNetHttpClientHandle client;
    const char*               session;
} Events;

static void report(KiottyNetHttpClientHandle client, KiottyNetSpan status)
{
    KiottyNetHttpHeader headers[1];
    headers[0] = KiottyNet_makeHttpHeader("Accept", "application/json");
    KiottyNetHttpReply reply = KiottyNet_postHttp(client, "/v1/devices/7/status", headers, 1, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, status, 3000);
    if (reply.code != KIOTTY_NET_CODE_SUCCESS)
    {
        return;                                                            /* POST 의 PEER_CLOSED 는 다시 보내지 않았다. 다음 주기에 */
    }
    if (reply.status != KIOTTY_NET_HTTP_STATUS_OK)
    {
        log_status(reply.status);
    }
    char buffer[1024];
    for (;;)
    {
        KiottyNetResult got = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), 3000);
        if (got.code != KIOTTY_NET_CODE_SUCCESS || got.bytes == 0)
        {
            break;
        }
        append_reply(buffer, (size_t)got.bytes);
    }
}

static void run_events(Events* events)                                     /* 알림 thread */
{
    KiottyNetHttpHeader headers[1];
    headers[0] = KiottyNet_makeHttpHeader("Accept", "text/event-stream");
    for (;;)
    {
        KiottyNetHttpReply reply = KiottyNet_getHttp(events->client, "/v1/events", headers, 1, 5000);
        if (reply.code == KIOTTY_NET_CODE_CLOSED)
        {
            return;
        }
        if (reply.code != KIOTTY_NET_CODE_SUCCESS)
        {
            continue;
        }
        char buffer[4096];
        for (;;)
        {
            KiottyNetResult got = KiottyNet_readHttpReplyBody(events->client, buffer, sizeof(buffer), 30000);
            if (got.code == KIOTTY_NET_CODE_TIMEOUT)
            {
                continue;                                                  /* 조용한 시간. 스트림은 그대로 */
            }
            if (got.code == KIOTTY_NET_CODE_CLOSED)
            {
                return;
            }
            if (got.code != KIOTTY_NET_CODE_SUCCESS || got.bytes == 0)
            {
                break;                                                     /* 끝났거나 끊김. 다시 GET */
            }
            feed_sse_parser(buffer, (size_t)got.bytes);
        }
    }
}

int main(void)
{
    KiottyNetContextResult initialized = KiottyNet_initializeContext();
    if (initialized.code != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }
    KiottyNetContext context = initialized.handle;

    KiottyNetClientConfiguration configuration;
    KiottyNet_initializeClientConfiguration(&configuration);
    KiottyNet_configureClientRetry(&configuration, 3, 1000);
    KiottyNetTlsClientConfiguration tls;
    KiottyNet_initializeTlsClientConfiguration(&tls);
    KiottyNet_configureTlsClientTrustStore(&tls, "ca.pem");
    KiottyNetAddress address;
    KiottyNet_initializeTcpAddress(&address, "cloud.example.com", 443);

    KiottyNetHttpClientResult made_requests = KiottyNet_createHttpClient(context, &configuration, NULL);
    KiottyNetHttpClientResult made_events   = KiottyNet_createHttpClient(context, &configuration, NULL);
    if (made_requests.code != KIOTTY_NET_CODE_SUCCESS || made_events.code != KIOTTY_NET_CODE_SUCCESS)
    {
        KiottyNet_destroyHttpClient(context, made_requests.handle);
        KiottyNet_destroyHttpClient(context, made_events.handle);
        KiottyNet_releaseContext(context);
        return 1;
    }
    KiottyNetHttpClientHandle requests = made_requests.handle;
    Events events = { made_events.handle, NULL };

    KiottyNet_connectHttpClient(requests, &address, &tls);                 /* 실패해도 대상은 남는다. 요청이 다시 연다 */
    KiottyNet_connectHttpClient(events.client, &address, &tls);
    KiottyNet_setHttpClientHeader(requests, "Authorization", token());
    KiottyNet_setHttpClientHeader(requests, "User-Agent", "device/1.0");
    KiottyNet_setHttpClientHeader(events.client, "Authorization", token());

    Thread worker = start_thread(run_events, &events);
    while (!should_stop())
    {
        report(requests, collect_status());
        sleep_seconds(60);
    }

    KiottyNet_closeHttpClient(events.client);                              /* 알림 thread 의 걸린 read 가 CLOSED */
    join_thread(worker);

    KiottyNet_clearHttpClientHeaders(requests);
    KiottyNet_destroyHttpClient(context, events.client);
    KiottyNet_destroyHttpClient(context, requests);
    KiottyNet_releaseContext(context);
    return 0;
}
```

- `report` 는 body 를 끝까지 읽는다. 읽지 않고 다음 요청을 보내도 되지만 그러면 연결을 닫고 다시 연다.
- `run_events` 의 GET 은 멱등이라 서버가 닫아 둔 keep-alive 위에서도 한 번 다시 간다.
- 토큰이 바뀌면 `setHttpClientHeader(requests, "Authorization", new_token)` (같은 이름은 바꾼다).

### 2.11 C++ 예제

```cpp
#include "kiotty_net_http.h"

int main()
{
    KiottyNet_Context context;

    KiottyNetClientConfiguration configuration;
    KiottyNet_ClientConfigurationBuilder(configuration).retry(3, 1000).build();
    KiottyNetHttpLimits limits;
    KiottyNet_HttpLimitsBuilder(limits).headerCapacity(16 * 1024, 64).build();
    KiottyNetAddress address;
    KiottyNet_initializeTcpAddress(&address, "127.0.0.1", 8080);

    KiottyNet_HttpClient client(context.handle(), configuration, &limits);   // limits nullptr = 기본값
    if (!client)
    {
        return static_cast<int>(client.code());
    }
    client.connect(address);                                                // TLS 면 client.connect(address, tls)
    client.header("Authorization", "Bearer abc");                           // 공통 헤더

    KiottyNet_HttpHeaders<> headers;                                        // 요청마다의 헤더. stack, heap 없음
    headers.add("Accept", "application/json").add("X-Request-Id", "42");

    KiottyNetHttpReply reply = client.get("/api/status", headers, 3000);
    if (reply.code == KIOTTY_NET_CODE_SUCCESS)
    {
        char            buffer[1024];
        KiottyNetResult got = client.readBody(buffer, sizeof(buffer), 3000);
        while (got.code == KIOTTY_NET_CODE_SUCCESS && got.bytes > 0)
        {
            consume(buffer, got.bytes);
            got = client.readBody(buffer, sizeof(buffer), 3000);
        }
    }

    reply = client.post("/api/report", KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, report(), 3000);   // 헤더 없는 판
    reply = client.del("/api/devices/7", 3000);

    KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_DELETE, "/api/devices");
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    request.body = ids();                                                   // body 있는 DELETE 는 send 로
    reply = client.send(request, headers, 3000);

    KiottyNetHttpReply allow = client.options("/api/devices", 3000);
    if ((KiottyNet_getHttpAllowedMethods(&allow) & KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_PATCH)) != 0)
    {
        // PATCH 지원
    }

    client.header("Authorization", nullptr);                                // 하나 지우기
    client.clearHeaders();                                                  // 전부 지우기
    return 0;
}
```

| C++ | C |
|---|---|
| `KiottyNet_HttpClient(context, configuration, limits)` | `createHttpClient`. `explicit operator bool`, `code()`, `handle()`. 소멸자가 `destroyHttpClient` |
| `connect(address)`, `connect(address, tls)` | `connectHttpClient` (tls NULL, tls) |
| `disconnect()`, `close()` | `disconnectHttpClient`, `closeHttpClient`. `close()` 만 아무 thread |
| `header(name, value)`, `clearHeaders()` | `setHttpClientHeader` (value `nullptr` 이면 지움), `clearHttpClientHeaders` |
| `send(request[, headers], timeout)` | `sendHttpRequest` |
| `get / head / del / options(path[, headers], timeout)` | `getHttp`, `headHttp`, `deleteHttp`, `optionsHttp` |
| `post / put / patch(path[, headers], type, body, timeout)` | `postHttp`, `putHttp`, `patchHttp` |
| `readBody(buffer, length, timeout)` | `readHttpReplyBody` |
| `KiottyNet_HttpHeaders<N>` : `KiottyNet_HttpHeaderList` | 헤더 배열. `add(name, value)` 사슬, `data()`, `count()`, `code()` (넘쳤으면 `TABLE_FULL`) |

- 요청 함수마다 헤더 없는 판과 `const KiottyNet_HttpHeaderList&` 를 받는 판이 있다.
- 응답은 C 의 `KiottyNetHttpReply` 그대로다. 헤더 찾기는 `KiottyNet_findHttpReplyHeader(&reply, name)`.
- `KiottyNet_HttpClient` 는 이동만 된다. 소멸 전에 다른 thread 의 호출이 끝나 있어야 한다 (`close()` 뒤 join).

### 2.12 반환 코드 요약

| 함수 | 코드 | 뜻 |
|---|---|---|
| `createHttpClient` | `INVALID_ARGUMENT` | NULL context / 설정, 초기화하지 않은 설정, 범위 밖 limits |
| | `TABLE_FULL` | context 의 client 자리가 다 찼다 |
| | `SOCKET_ERROR` | 깨우는 handle (pipe / UDP socket) 을 열지 못했다. fd / socket 이 모자람 (2.9) |
| `connectHttpClient` | `INVALID_ARGUMENT` | NULL, 잘못된 주소 / TLS 설정, host 가 Host 헤더가 될 수 없음. 이전 대상도 지운다 |
| | `SOCKET_ERROR`, `TIMEOUT`, `TLS_*` | 연결 실패. 대상은 남는다 |
| | `CLOSED` | 다른 thread 의 close |
| `setHttpClientHeader`, `clearHttpClientHeaders` | `INVALID_ARGUMENT` | NULL, 글자 규칙, 라이브러리가 쓰는 헤더 |
| | `BUFFER_FULL` | 공통 헤더 저장소 (text `max_header_bytes`, 개수 `max_headers`) 가 찼다. 저장소는 그대로 |
| 요청 함수 | 2.4 의 표 | |
| `readHttpReplyBody` | `TIMEOUT` | 연결은 그대로. 다시 read 하면 이어서 |
| | `PROTOCOL_ERROR`, `PEER_CLOSED`, `CLOSED` | 연결은 닫혔다 |
| | `INVALID_ARGUMENT` | NULL client / buffer, length 0 |
| connect, 요청, read, 공통 헤더 | `BUSY` | 다른 thread 의 호출이 걸려 있다 |

### 2.13 하지 않는 것

- redirect 따라가기 (3xx 는 그대로 돌려준다), 쿠키, 압축 (gzip), 인증 흐름, 프록시, HTTP/2. 필요한 헤더는 앱이 넣는다.
- `Transfer-Encoding` 이 `chunked` 하나가 아닌 응답 (`gzip, chunked` 등) 은 `PROTOCOL_ERROR`. 압축을 풀지 않으므로 읽을 수 없다.
- `Content-Length` 가 여럿인 응답은 값이 같아도 `PROTOCOL_ERROR` (서버의 요청 파서와 같다).
- 비동기 판 (콜백으로 응답) 은 없다. 동시 요청은 client 여럿과 thread 로 한다.
- 요청 body 를 조각으로 올리기 (chunked 요청, 큰 파일 업로드) 는 없다. body 는 한 Span 이다.
- SSE 를 이벤트로 나누는 도우미, body 를 한 번에 모으는 함수는 없다.
- `Expect: 100-continue` 를 보내지 않는다 (서버가 보낸 1xx 는 건너뛴다).

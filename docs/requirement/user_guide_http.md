# Kiotty-Net 사용 가이드

사용자가 헤더를 어떻게 쓰는지 적는 메모다. 인터페이스의 기준은 [requirement_v0.md](requirement_v0.md) 이고, 여기서는 "무엇을 어떤 순서로 부르는가" 만 다룬다. 내부 동작은 [implementation_guide_v0.md](implementation_guide_v0.md) 에 있다.

## 헤더 구성

| 헤더 | 내용 | 누가 include 하나 |
|---|---|---|
| `kiotty_net_base.h` | 공통 타입 (`KiottyNetCode`, `KiottyNetSpan`), 설정 struct, Context, Connection / Work, StreamClient, EventClient, Server | 직접 TCP / Pipe 프로토콜을 짜는 사용자 |
| `kiotty_net_statemachine.h` | Connection 위에 얹는 표 기반 StateMachine | 자기 프로토콜을 표로 쓰려는 사용자. HTTP / WebSocket 사용자는 필요 없다 |
| `kiotty_net_http.h` | HTTP/1.1 서버. Rest API 라우트와 정적 서빙 | HTTP 서버를 쓰는 사용자. **이 헤더 하나만 include 하면 된다** |
| `kiotty_net_websocket.h` | WebSocket 서버 / 클라이언트, HttpServer 에 Upgrade 경로 추가 | WebSocket 사용자 |

- 각 헤더는 위에 C 인터페이스, 아래에 (`__cplusplus` 일 때만) 그 C 인터페이스를 부르는 C++ 래퍼가 있다. C 로 빌드하면 아래 절반은 보이지 않는다.
- `kiotty_net_http.h` 는 `kiotty_net_base.h` 를 include 한다. 공통 타입과 `KiottyNetServerConfig`, `KiottyNetContext` 가 거기 있기 때문이다. StateMachine 은 HttpServer 의 내부 구현이라 `kiotty_net_statemachine.h` 는 include 하지 않는다.
- C++ 소유 handle 래퍼는 `kiotty::net::OwnedHandle<Handle, Destroy>` 로 RAII 를 한다. 이 template 은 `KIOTTY_NET_OWNED_HANDLE_DEFINED` 매크로로 감싸 `kiotty_net_base.h` 와 `kiotty_net_http.h` 양쪽에 같은 정의가 있다. 먼저 include 된 쪽이 정의하고 나머지는 건너뛴다. 두 정의는 글자까지 같아야 하고, 고칠 때는 둘을 함께 고친다.

## 1. HTTP 서버

### 1.1 준비 : Context, ServerConfig, HttpLimits

HttpServer 는 소유 handle 이다. `KiottyNet_createHttpServer` 가 만들고 `KiottyNet_destroyHttpServer` 가 없앤다. 만들 때 세 가지를 넘긴다.

1. `KiottyNetContext` : `KiottyNet_initializeContext()` 로 만든 라이브러리 전역 컨텍스트. 소유 handle 은 모두 같은 context 를 받고, destroy 때도 같은 context 를 넘긴다.
2. `KiottyNetServerConfig` : 주소와 `max_clients`, 버퍼 크기. `KiottyNet_initializeTcpServerConfig(&config, host, port)` 또는 `KiottyNet_initializePipeServerConfig(&config, name)` 로 채우고 `KiottyNet_configureServer*` 로 바꾼다. TLS 를 쓰면 `KiottyNetTlsServerConfig` 를 따로 만들어 넘기고, 평문이면 `NULL`.
3. `KiottyNetHttpLimits` : HTTP 층의 고정 크기 버퍼와 timeout. 아래 표.

`KiottyNet_initializeHttpLimits(&limits)` 가 모든 필드를 기본값으로 채우고, `KiottyNet_configureHttp*` 가 하나씩 바꾼다. 다른 설정 struct 와 같은 규칙이다. 함수가 값을 검사하고 `KiottyNetCode` 를 돌려주며, 잘못된 값이면 struct 를 바꾸지 않는다.

| 필드 | 기본값 | 뜻 | configure 함수 |
|---|---|---|---|
| `max_routes` | 64 | 라우트 + 정적 경로 + Upgrade 경로의 합. 표는 create 때 이 크기로 잡힌다 | `KiottyNet_configureHttpMaxRoutes(limits, max_routes)` |
| `max_header_bytes` | 8 KiB | request line + 헤더 전체. 넘으면 431 로 거절하고 닫는다 | `KiottyNet_configureHttpHeaderCapacity(limits, max_header_bytes, max_headers)` |
| `max_headers` | 32 | 요청 헤더 개수. `request.header_count` 의 상한 | 위와 같음 |
| `max_body_bytes` | 64 KiB | 요청 body. 넘으면 413 | `KiottyNet_configureHttpBodyCapacity(limits, max_body_bytes)` |
| `response_bytes` | 64 KiB | 응답 버퍼 (status line + 헤더 + 복사된 body). 넘치면 setter 가 `KIOTTY_BUFFER_FULL` | `KiottyNet_configureHttpResponseCapacity(limits, response_bytes)` |
| `header_timeout_ms` | 10000 | 헤더 수신 완료까지. 지나면 408 없이 닫는다 | `KiottyNet_configureHttpTimeouts(limits, header_timeout_ms, keep_alive_timeout_ms)` |
| `keep_alive_timeout_ms` | 15000 | 응답 뒤 다음 요청을 기다리는 시간 | 위와 같음 |

버퍼는 Connection 마다 (`max_clients` 개) 잡히므로 메모리는 `max_clients × (max_header_bytes + max_body_bytes + response_bytes)` 정도다. 임베디드 타깃에서는 `max_clients` 와 함께 줄인다.

C++ 은 `KiottyNet_HttpLimitsBuilder` 가 같은 함수를 부른다. 처음 실패한 코드를 `build()` 가 돌려준다.

```cpp
KiottyNetHttpLimits limits;
KiottyNetCode code = KiottyNet_HttpLimitsBuilder(limits).bodyCapacity(16 * 1024).timeouts(5000, 10000).build();
```

### 1.2 라우트 등록

라우트는 `run` 전에 등록한다. run 이 시작된 뒤의 등록은 `KIOTTY_BUSY` 다.

```c
KiottyNetCode KiottyNet_addHttpRoute(KiottyNetHttpServerHandle server, KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
KiottyNetCode KiottyNet_setHttpNotFound(KiottyNetHttpServerHandle server, KiottyNetHttpRoute route);
```

- `path` 는 `/` 로 시작하는 URL 경로다. 정확 일치, 또는 `/*` 로 끝나면 접두 wildcard 다 (`/api/devices/*`). `:id` 같은 파라미터 파싱은 없다. `request.path` 를 사용자가 자른다. 길이는 `KIOTTY_MAX_URL_LENGTH` (256) 미만이어야 하며 넘으면 `KIOTTY_INVALID_ARGUMENT`.
- 라우트, 정적 경로, Upgrade 경로는 한 표에 **등록한 순서대로** 매칭한다. 좁은 경로를 먼저, `/*` 나 `/` 디렉토리 매핑을 마지막에 등록한다.
- 같은 (method, path) 를 두 번 등록하면 `KIOTTY_DUPLICATE_RULE`. 표가 가득 차면 `KIOTTY_TABLE_FULL`.
- `KiottyNetHttpRoute` 는 세 필드다. `KiottyNet_makeHttpRoute(app_context, onRequest, onWorkStarted)` 로 만들면 C 에서도 한 줄이다.

| 필드 | 뜻 |
|---|---|
| `app_context` | `onRequest` / `onWorkStarted` 의 첫 인자로 그대로 돌아오는 사용자 포인터. 라이브러리는 읽지 않는다 |
| `onRequest(app_context, request, response)` | 요청이 다 들어오면 IO Thread 에서 한 번. 여기서 응답을 채우거나 `KiottyNet_startHttpWork` 를 부른다. `NULL` 이면 `KIOTTY_INVALID_ARGUMENT` |
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
| `method` | `KIOTTY_HTTP_GET` 등 7개 중 하나 |
| `path` | `?` 앞. percent-decoding 하지 않는다 |
| `query` | `?` 뒤. 없으면 길이 0 |
| `headers`, `header_count` | 이름 / 값 span 배열. `KiottyNet_findHttpHeader(request, "Content-Type")` 가 대소문자 무시로 찾고 없으면 `{ NULL, 0 }` |
| `body` | `Content-Length` 또는 chunked 를 합친 것. 없으면 길이 0 |
| `keep_alive` | 응답 뒤 연결을 유지할지. 라이브러리가 처리하므로 보통 볼 일이 없다 |

### 1.4 응답 채우기

응답은 pool handle `KiottyNetHttpResponseHandle` 로 채운다. Connection 마다 `response_bytes` 크기의 버퍼가 있고 setter 는 거기에 쓴다.

| 함수 | 하는 일 | 실패 |
|---|---|---|
| `KiottyNet_setHttpStatus(r, status)` | status code. 100..599 | 범위 밖 `KIOTTY_INVALID_ARGUMENT` |
| `KiottyNet_setHttpHeader(r, name, value)` | 헤더 추가. `Content-Length`, `Connection`, `Date` 는 라이브러리가 채우므로 넣지 않는다 | `KIOTTY_BUFFER_FULL` |
| `KiottyNet_setHttpBody(r, data, content_type)` | body 를 응답 버퍼로 **복사**. return 뒤 data 를 버려도 된다. `content_type` 이 NULL 이면 `application/octet-stream` | `KIOTTY_BUFFER_FULL` |
| `KiottyNet_setHttpBodyRef(r, data, content_type)` | 복사하지 않고 가리킨다. 송신 완료까지 data 가 살아 있어야 한다 (static 데이터, 메모리 바이너리). 버퍼 크기와 무관 | 없음 |
| `KiottyNet_setHttpFile(r, path, content_type)` | 파일을 chunk 로 스트리밍. `content_type` 이 NULL 이면 확장자로 정한다 | 없는 파일 `KIOTTY_NOT_FOUND`, `..` 포함 경로 `KIOTTY_INVALID_ARGUMENT` |
| `KiottyNet_startHttpWork(r)` | 응답을 지금 보내지 않고 연결을 IO Thread 에서 떼어낸다 (1.5) | 1.5 |

- body 는 셋 중 하나만 둔다. 다시 부르면 이전 것을 대체한다.
- 응답은 **`onRequest` 가 return 하는 순간** 보내진다. status 를 부르지 않았으면 200 이고, 아무 setter 도 부르지 않고 return 했으면 404 다.
- 모든 setter 는 닫힌 handle 이면 `KIOTTY_PEER_CLOSED` 를 돌려주고 아무 일도 하지 않는다.
- HEAD 요청에 GET 라우트가 실행될 때도 setter 는 그대로 동작한다. 라이브러리가 body 를 떼고 `Content-Length` 만 같게 보낸다.

C++ 은 `KiottyNet_HttpResponse(r)` 가 같은 함수를 `status / header / body / bodyRef / file / startWork` 로 부른다. 비소유 값 타입이라 콜백 안에서 handle 로 만들어 쓰고 버린다.

### 1.5 긴 작업 : Work

DB 조회, 리포트 생성처럼 IO Thread 를 붙잡으면 안 되는 일은 base 의 Work 모델을 HTTP 에 맞게 줄인 것으로 처리한다. 흐름은 세 단계다.

1. `onRequest` 안에서 `KiottyNet_startHttpWork(response)` 를 부르고 return 한다. 응답은 아직 보내지지 않는다. 라우트의 `onWorkStarted` 가 `NULL` 이면 `KIOTTY_INVALID_ARGUMENT`, 이미 불렀으면 `KIOTTY_BUSY`.
2. IO Thread 가 그 연결을 떼어내고 **다음 loop 에서** `onWorkStarted(app_context, request, response, work)` 를 한 번 낸다. 여기서 `response` 와 `work` 를 worker 에게 넘긴다. 둘 다 정수 handle 이라 복사해 두어도 dangling 이 없다. `request` 가 가리키는 데이터도 finish 까지 유효하다.
3. worker 가 결과를 만들어 setter 로 응답을 채우고 (`KiottyNet_setHttpStatus`, `KiottyNet_setHttpBody` 등. 어느 thread 에서든 된다. 연결이 떼어져 있는 동안 IO Thread 는 응답 버퍼를 건드리지 않는다) `KiottyNet_finishHttpWorkWithResponse(work)` 로 보낸다. 보내지 않고 끊으려면 `KiottyNet_finishHttpWorkWithClose(work, reason)`.

규칙은 base 의 Work 와 같다.

- **Work 를 받았으면 반드시 finish 한다.** finish 는 한 번만 유효하고 두 번째는 `KIOTTY_BUSY`. 소유자가 destroy 된 뒤에 불러도 `KIOTTY_PEER_CLOSED` 만 돌아온다.
- finish 전까지 그 연결에는 어떤 콜백도 없다. deadline 도 멈춘다. 라이브러리는 Work 에 timeout 을 두지 않는다. 오래 걸릴 것 같으면 `KiottyNet_isHttpWorkConnected(work)` 로 peer 가 살아 있는지 확인한다. 끊겼으면 finish 는 `KIOTTY_PEER_CLOSED` 다.
- 소유자의 `close()` 는 Work 중인 연결도 바로 닫는다. 그 뒤의 finish 는 `KIOTTY_PEER_CLOSED` 다.
- `finishHttpWorkWithResponse` 는 응답 버퍼의 내용을 그대로 보낸다. body 가 `response_bytes` 보다 커서 setter 가 `KIOTTY_BUFFER_FULL` 이었다면 `setHttpBodyRef` 나 `setHttpFile` 로 바꾼다.

C++ 은 `KiottyNet_HttpWork` 가 finish 의무를 소유한다. 이동만 되고, finish 하지 않은 채 소멸되면 `finishWithClose()` 를 불러 연결을 닫는다. 그래서 worker 로 `std::move` 해서 넘긴다 (C++14 init capture, C++11 은 `shared_ptr`).

### 1.6 정적 서빙

GET / HEAD 에만 적용되고 라우트와 같은 표에 들어간다. 다른 메서드로 오면 405 + `Allow: GET, HEAD, OPTIONS`.

| 함수 | 매핑 | content type |
|---|---|---|
| `KiottyNet_serveHttpDirectory(s, "/static/", "./www/")` | `/static/a/b.js` → `./www/a/b.js`. `url_prefix` 는 `/` 로 끝나야 한다. `..` 가 들어오면 403 | 확장자로 정한다. 모르는 확장자는 `application/octet-stream` |
| `KiottyNet_serveHttpFile(s, "/favicon.ico", "./www/favicon.ico")` | 파일 하나 | 확장자 |
| `KiottyNet_serveHttpMemory(s, "/app.wasm", data, "application/wasm")` | 메모리 바이너리. **복사하지 않는다.** 서버가 살아 있는 동안 data 가 유효해야 한다 | 인자. `KIOTTY_MAX_CONTENT_TYPE_LENGTH` (64) 미만 |

- 디렉토리와 파일 경로는 `KIOTTY_MAX_PATH_LENGTH` (260) 미만. 등록 때 복사되므로 호출자의 문자열은 버려도 된다.
- 파일은 요청 때 열어 chunk 로 스트리밍한다 (가능하면 sendfile / TransmitFile). 없으면 404.
- Single Page App 처럼 `/` 전체를 디렉토리로 주고 그 위에 API 라우트를 두려면 API 라우트를 먼저, `serveHttpDirectory("/", ...)` 를 마지막에 등록한다.

### 1.7 수명과 thread 규칙

| 함수 | 하는 일 |
|---|---|
| `KiottyNet_runHttpServer(s)` | listen 을 열고 IO loop 를 돈다. **blocking.** `close` 가 불릴 때까지 return 하지 않는다. 열기에 실패하면 바로 그 코드 |
| `KiottyNet_closeHttpServer(s)` | loop 를 멈추라고 알린다. 기다리지 않는다. Work 중인 연결도 바로 닫힌다 |
| `KiottyNet_awaitHttpServer(s, timeout_ms)` | loop 가 돌기 시작할 때까지 기다린다. 시작했으면 `KIOTTY_SUCCESS`, run 이 이미 끝났으면 run 의 반환값, 시간 안에 못 봤으면 `KIOTTY_TIMEOUT` |
| `KiottyNet_destroyHttpServer(context, s)` | run 이 return 한 뒤에 부른다 |

- `onRequest`, `onWorkStarted` 는 IO Thread 에서 순서대로 불린다. 콜백 안에서 block 하지 않는다. 블로킹이 필요하면 Work 다.
- 응답 setter 는 `onRequest` 안 (IO Thread) 에서, 또는 `onWorkStarted` 뒤 Work 를 들고 있는 thread 에서 부른다. 같은 응답을 두 thread 가 동시에 만지는 순간은 없다.
- 다른 thread 가 불러도 되는 것은 `finishHttpWork*`, `isHttpWorkConnected`, `closeHttpServer`, `awaitHttpServer` 다.

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
    KiottyNet_setHttpStatus(response, 200);
}

static void onDevice(KiottyNetAppContext app, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
{
    KiottyNetSpan id = tail_after(request->path, "/api/devices/");
    KiottyNetSpan json = render_device((App*)app, id);
    KiottyNet_setHttpBody(response, json, "application/json");
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

static void run_job(ReportJob* job)
{
    KiottyNetSpan json = build_report(job);
    if (!KiottyNet_isHttpWorkConnected(job->work))
    {
        KiottyNet_finishHttpWorkWithClose(job->work, KIOTTY_PEER_CLOSED);
        return;
    }
    KiottyNet_setHttpBody(job->response, json, "application/json");
    KiottyNet_finishHttpWorkWithResponse(job->work);
}

int main(void)
{
    KiottyNetContext context = KiottyNet_initializeContext();

    KiottyNetServerConfig config;
    KiottyNet_initializeTcpServerConfig(&config, "0.0.0.0", 8080);
    KiottyNet_configureServerMaxClients(&config, 16);

    KiottyNetHttpLimits limits;
    KiottyNet_initializeHttpLimits(&limits);
    KiottyNet_configureHttpBodyCapacity(&limits, 16 * 1024);

    App app = { create_pool(4) };
    KiottyNetHttpServerHandle server = KiottyNet_createHttpServer(context, &config, NULL, &limits);

    KiottyNet_addHttpRoute(server, KIOTTY_HTTP_GET, "/health",         KiottyNet_makeHttpRoute(&app, onHealth, NULL));
    KiottyNet_addHttpRoute(server, KIOTTY_HTTP_GET, "/api/devices/*", KiottyNet_makeHttpRoute(&app, onDevice, NULL));
    KiottyNet_addHttpRoute(server, KIOTTY_HTTP_GET, "/api/report",    KiottyNet_makeHttpRoute(&app, onReport, onReportWork));
    KiottyNet_serveHttpMemory(server, "/app.wasm", wasm_span(), "application/wasm");
    KiottyNet_serveHttpDirectory(server, "/", "./www/");

    KiottyNet_runHttpServer(server);

    KiottyNet_destroyHttpServer(context, server);
    KiottyNet_releaseContext(context);
    return 0;
}
```

TLS 는 `KiottyNetTlsServerConfig tls; KiottyNet_initializeTlsServerConfig(&tls, "server.crt", "server.key", NULL);` 를 만들어 create 의 세 번째 인자로 넘기면 된다.

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

    KiottyNetServerConfig config;
    KiottyNet_ServerConfigBuilder::tcp(config, "0.0.0.0", 8080).maxClients(16).build();

    KiottyNetHttpLimits limits;
    KiottyNet_HttpLimitsBuilder(limits).bodyCapacity(16 * 1024).build();

    App                  app;
    KiottyNet_HttpServer server(context.handle(), config, limits);

    server.get("/health", [](KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle r)
    {
        KiottyNet_HttpResponse(r).status(200);
    });

    server.get("/api/devices/*", [](KiottyNetAppContext ctx, const KiottyNetHttpRequest* q, KiottyNetHttpResponseHandle r)
    {
        KiottyNet_HttpResponse(r).body(static_cast<App*>(ctx)->render(q->path), "application/json");
    }, nullptr, &app);

    server.get("/api/report",
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
                KiottyNet_HttpResponse(r).body({ json.data(), json.size() }, "application/json");
                work.finishWithResponse();
            });
        },
        &app);

    server.serveMemory("/app.wasm", wasmSpan(), "application/wasm");
    server.serveDirectory("/", "./www/");

    std::thread io([&] { server.run(); });
    if (server.await(3000) != KIOTTY_SUCCESS)
    {
        server.close();
    }
    waitForShutdownSignal();
    server.close();
    io.join();
    return 0;
}
```

- capture 없는 람다는 함수 포인터로 바뀌므로 그대로 들어간다. capture 가 필요하면 `app_context` 로 넘긴다.
- `KiottyNet_HttpServer` 는 소멸자가 `KiottyNet_destroyHttpServer` 를 부른다. `run` 이 return 한 뒤 (`io.join()` 뒤) 소멸되어야 한다.

### 1.10 반환 코드 요약

| 함수 | 코드 | 뜻 |
|---|---|---|
| `addHttpRoute`, `setHttpNotFound`, `serveHttp*` | `KIOTTY_TABLE_FULL` | `max_routes` 초과 |
| | `KIOTTY_DUPLICATE_RULE` | 같은 (method, path) |
| | `KIOTTY_INVALID_ARGUMENT` | path 형식 / 길이, `onRequest` NULL, 지원하지 않는 method, 끝에 `/` 없는 url_prefix |
| | `KIOTTY_BUSY` | run 이 시작된 뒤 |
| `setHttpHeader`, `setHttpBody` | `KIOTTY_BUFFER_FULL` | `response_bytes` 초과 |
| `setHttpFile` | `KIOTTY_NOT_FOUND` | 파일 없음 |
| `startHttpWork` | `KIOTTY_INVALID_ARGUMENT` | 라우트에 `onWorkStarted` 가 없음 |
| | `KIOTTY_BUSY` | 이미 startWork 했음 |
| `finishHttpWork*` | `KIOTTY_BUSY` | 두 번째 finish |
| 응답 / Work 함수 공통 | `KIOTTY_PEER_CLOSED` | 닫힌 handle |
| `runHttpServer` | `KIOTTY_SOCKET_ERROR`, `KIOTTY_INVALID_ARGUMENT` | listen 실패, handle NULL |
| `awaitHttpServer` | `KIOTTY_TIMEOUT` | 시간 안에 loop 가 시작되지 않음 |

### 1.11 아직 없는 것

- WebSocket Upgrade 경로 (`KiottyNet_addWebSocketUpgrade(http_server, ...)`) 는 `kiotty_net_websocket.h` 에 있다.
- 간단한 CORS 설정, `:id` 파라미터 파싱, 응답 압축, Range 요청, 요청 body 스트리밍은 지원하지 않는다.

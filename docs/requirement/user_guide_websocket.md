# WebSocket 사용 가이드 (kiotty_net_websocket.h)

`include/kiotty_net_websocket.h` 를 쓰는 방법이다. requirement_v0.md 10, 11절의 WebSocket 을 1~8절의 지금 모양 (run / close / await, 두 context, write lane / push lane, notify, StateMachine) 에 맞춰 다시 쓴 것이고, WebSocket 에 대해서는 이 문서가 10, 11절보다 우선한다. 달라진 점은 9절에 모아 두었다.

헤더는 `kiotty_net_statemachine.h` 를 포함하고, 그것이 `kiotty_net_base.h` 를 포함한다. 사용자는 `kiotty_net_websocket.h` 하나만 포함하면 된다. 위는 C 인터페이스, 아래는 C 함수를 하나씩 부르는 C++ 래퍼다. C++ 래퍼가 쓰는 `kiotty::net::OwnedHandle` 은 `KIOTTY_NET_OWNED_HANDLE_DEFINED` 로 묶여 있어서, 먼저 정의한 헤더가 있으면 그것을 쓰고 없으면 이 헤더가 정의한다. 프로토콜 헤더 하나만 떼어 쓰거나 single header 로 합쳐도 같은 코드가 된다.

## 1. 한눈에

| 것 | 무엇 | C | C++ |
|---|---|---|---|
| 설정 | 경로, subprotocol, 메시지 상한, 타이머 | `KiottyNetWebSocketConfig` + `KiottyNet_initializeWebSocketConfig` / `KiottyNet_configureWebSocket*` | `KiottyNet_WebSocketConfigBuilder` |
| 서버 | 7절 Server 위에 라이브러리의 WebSocket 표를 얹은 것. 경로 하나 | `KiottyNetWebSocketServerHandle`, `KiottyNet_*WebSocketServer` | `KiottyNet_WebSocketServer` |
| 클라이언트 | 6절 EventClient 위에 같은 표를 얹은 것 | `KiottyNetWebSocketClientHandle`, `KiottyNet_*WebSocketClient` | `KiottyNet_WebSocketClient` |
| 소켓 하나 | 연결 하나의 WebSocket. pool handle | `KiottyNetWebSocketHandle`, `KiottyNet_*WebSocket` | `KiottyNet_WebSocket` (비소유 값 타입) |
| 콜백 | 연결마다 넷 | `KiottyNetWebSocketHandler` | 같은 struct |

서버와 클라이언트는 같은 `KiottyNetWebSocketHandler` 와 같은 `KiottyNetWebSocketConfig` 를 쓴다. 다른 것은 소켓 수 (서버 `max_clients`, 클라이언트 1) 와 masking (클라이언트만) 뿐이고 둘 다 라이브러리 안의 일이다.

thread 규칙은 base 와 같다.

| 어디서든 | IO Thread (콜백 안) 에서만 |
|---|---|
| `KiottyNet_sendWebSocket`, `KiottyNet_sendWebSocketClient`, `KiottyNet_broadcastWebSocketServer` | `KiottyNet_getWebSocketInfo` |
| `KiottyNet_closeWebSocket` | `KiottyNet_findWebSocketUpgradeHeader`, `KiottyNet_selectWebSocketProtocol` (`onUpgrade` 안에서만) |
| 소유자의 `close`, `await`, `KiottyNet_getWebSocketClientSocket` | |

## 2. 머릿속 모델

```
Connection ──[HTTP Upgrade 표]──101 송신 완료──[WebSocket frame 표]──> onOpen / onMessage / onClose
                 │                                      │
              onUpgrade                        ping / pong / close frame 은 라이브러리가 처리
```

- 서버는 accept 된 Connection 에서 HTTP Upgrade 요청 하나를 읽고 (`recv_capacity` 안에 들어와야 한다), 경로가 맞고 `onUpgrade` 가 허락하면 101 을 write lane 으로 보낸다. 101 의 `onWritten` 이 오면 같은 Connection 이 WebSocket frame 표로 넘어가고 `onOpen` 이 온다. 그 뒤 이 Connection 에 HTTP 는 없다.
- 클라이언트는 연결되면 Upgrade 요청을 보내고 101 을 읽은 뒤 `onOpen` 이다. `onUpgrade` 는 클라이언트에서 호출되지 않는다 (NULL 로 둔다).
- **`KiottyNetWebSocketHandle` 은 pool handle 이다** (규약 8). `int64_t` 이고 0 은 무효다. 닫히면 generation 이 바뀌어 옛 handle 로 부른 함수는 `KIOTTY_PEER_CLOSED` 를 돌려준다. 다른 thread 에 넘겨 두어도 dangling 이 없다.
- **context 는 둘이다.** base 와 같다. `KiottyNetAppContext` 는 `run` 에 넘기는 앱의 것이고 `onUpgrade`, `onOpen`, `onClose` 가 받는다. `KiottyNetWebSocketContext` 는 소켓마다의 데이터다. `onOpen` 이 만들어 return 하면 라이브러리가 그 소켓에 묶어 두고 `onMessage` 의 첫 인자로 돌려주며, `onClose` 가 `app_context` 와 함께 돌려받아 해제한다. 둘 다 `void*` 의 typedef 이고 라이브러리는 읽지 않는다.
- **state 는 라이브러리가 든다.** frame 파싱 상태, 조각 합치기 버퍼, ping 타이머는 모두 라이브러리의 per-connection 객체에 있고 `createWebSocketServer` 가 `max_clients` 분량을 잡는다. 사용자의 `ws_context` 에는 앱 데이터만 둔다.
- **모든 송신은 push lane 이다.** WebSocket 메시지는 수신 흐름의 한 단계가 아니라 아무 때나 나가는 것이므로 (4절 "어느 lane 으로 보낼까") `send` 는 `KiottyNet_notifyConnection` 과 같은 길로 간다. 그래서 어느 thread 에서나 부를 수 있고 완료 콜백이 없다. 이전 요구사항의 `resume` / `onResumed` 는 없어졌다.
- **Work 는 없다.** 긴 작업은 `onMessage` 에서 데이터를 `ws_context` 로 복사해 worker 에 넘기고, worker 가 끝나면 `KiottyNet_sendWebSocket` 으로 보낸다. 연결을 IO Thread 에서 떼어낼 이유가 없다 (10절 열린 질문 1).

## 3. 설정 : KiottyNetWebSocketConfig

base 3절의 규칙 그대로다. initialize 한 번, configure 여러 번, 반환 코드 확인. 잘못된 값이면 struct 를 바꾸지 않고 `KIOTTY_INVALID_ARGUMENT`. 서버와 클라이언트가 같은 struct 를 쓰고 모든 필드의 뜻이 양쪽에서 같다.

| 필드 | 기본값 | 함수 | 뜻 |
|---|---|---|---|
| `path` | initialize 인자 (필수) | `KiottyNet_initializeWebSocketConfig(config, path)` | 서버: 승격을 받는 URL 경로 하나. 다른 경로는 404. 클라이언트: 요청 경로. `/` 로 시작해야 하고 `KIOTTY_MAX_URL_PATH_LENGTH` (256) 미만 |
| `protocols` | 빈 문자열 | `KiottyNet_configureWebSocketProtocols(config, "chat, superchat")` | 이 쪽이 지원하는 subprotocol 을 선호 순서대로. 서버: 클라이언트가 제시한 것 중 이 목록에 있는 첫 번째를 고른다. 클라이언트: `Sec-WebSocket-Protocol` 로 보낸다. 비어 있으면 subprotocol 없음 |
| `max_message_bytes` | 64 KiB | `KiottyNet_configureWebSocketMaxMessageBytes` | 조각을 합친 메시지 하나의 최대 크기. 넘으면 1009 로 닫는다. 연결마다 이 크기의 버퍼가 생성 시점에 잡힌다 (`max_clients` × 이 값) |
| `handshake_timeout_ms` | 10000 | `KiottyNet_configureWebSocketHandshakeTimeout` | 연결부터 101 까지. 지나면 끊는다 (`onOpen` 전이므로 콜백 없음) |
| `ping_interval_ms`, `pong_timeout_ms` | 30000, 10000 | `KiottyNet_configureWebSocketPing(config, interval_ms, pong_timeout_ms)` | interval 마다 ping 을 보내고 pong 이 timeout 안에 없으면 1001 로 닫는다. interval 0 이면 ping 을 보내지 않는다 (들어온 ping 에 대한 pong 은 언제나 보낸다) |
| `close_timeout_ms` | 5000 | `KiottyNet_configureWebSocketCloseTimeout` | close frame 을 보낸 뒤 상대의 close frame 을 기다리는 시간 |

base 의 버퍼 설정과 이렇게 맞물린다.

| ServerConfig / ClientConfig | WebSocket 에서의 뜻 |
|---|---|
| `recv_capacity` | Upgrade 요청 (request line + headers) 의 상한. frame 은 이 크기 단위로 나눠 읽어 메시지 버퍼에 붙이므로 메시지보다 작아도 된다 |
| `send_capacity` | 101 응답 (서버) / Upgrade 요청 (클라이언트) 이 들어가는 write lane. 그 뒤로는 쓰지 않는다 |
| `push_capacity` | **`send` 와 `broadcast` 가 들어가는 곳.** 메시지 하나는 frame 헤더 (최대 14 바이트) 를 더해 이 값 이하여야 하고, 상대가 느리면 여러 개가 쌓인다. 큰 메시지를 보내는 앱은 이 값을 올린다 |

```cpp
KiottyNetWebSocketConfig websocket;
KiottyNetCode code = KiottyNet_WebSocketConfigBuilder(websocket, "/chat")
    .protocols("chat")
    .maxMessageBytes(16 * 1024)
    .ping(30000, 10000)
    .build();
```

```c
KiottyNetWebSocketConfig websocket;
if (KiottyNet_initializeWebSocketConfig(&websocket, "/chat") != KIOTTY_SUCCESS) { }
if (KiottyNet_configureWebSocketProtocols(&websocket, "chat") != KIOTTY_SUCCESS) { }
```

## 4. 콜백 : KiottyNetWebSocketHandler

함수 표만 담으므로 `static const` 로 두고 여러 객체가 같이 쓴다. `run` 에 `app_context` 와 따로 넘기고, 둘 다 `run` 이 return 할 때까지만 살아 있으면 된다 (7절).

| 콜백 | 언제 | 받는 것 | 돌려주는 것 |
|---|---|---|---|
| `onUpgrade(app_context, ws, upgrade)` | 서버. 경로가 맞는 Upgrade 요청을 다 읽었을 때. NULL 이면 모두 허용 | `KiottyNetWebSocketUpgrade` : `path`, `query`, `host`, `origin`, `protocols` (요청의 `Sec-WebSocket-Protocol` 원문). 모두 수신 Stage 를 가리키며 return 하면 무효 | `true` 면 101. `false` 면 403 을 보내고 끊는다. 이 연결에 더 이상 콜백은 없다 |
| `onOpen(app_context, ws)` | 101 송신 완료 (서버) / 101 수신 (클라이언트) | | 이 소켓의 `ws_context`. NULL 이어도 된다 |
| `onMessage(ws_context, ws, message)` | 메시지 하나가 완성됐을 때. 조각은 합쳐져 있다 | `KiottyNetWebSocketMessage { opcode, data }`. `data` 는 메시지 버퍼를 가리키며 return 하면 무효. TEXT 는 UTF-8 검사를 통과한 것 | |
| `onClose(app_context, ws_context, ws, close)` | 마지막 콜백. `onOpen` 이 나간 소켓에만, 한 번 | `KiottyNetWebSocketClose { code, status, reason }` (아래 표). `reason` 은 return 하면 무효 | |

`onUpgrade` 안에서만 할 수 있는 일 둘.

- `KiottyNet_findWebSocketUpgradeHeader(ws, "Authorization")` : 요청의 다른 헤더. 대소문자 무시. 없으면 `{ NULL, 0 }`.
- `KiottyNet_selectWebSocketProtocol(ws, "chat")` : 라이브러리가 `protocols` 로 고른 것을 덮어쓴다. 요청에 없던 값이면 `KIOTTY_INVALID_ARGUMENT` 이고 바뀌지 않는다.

`onClose` 의 `KiottyNetWebSocketClose`.

| `code` (KiottyNetCode) | `status` (close frame) | 뜻 |
|---|---|---|
| `KIOTTY_SUCCESS` | 상대가 보낸 값 (1000 등). 상대가 코드 없이 보냈으면 1005 | close handshake 가 끝났다. 누가 시작했든 |
| `KIOTTY_PEER_CLOSED` | 1006 | close frame 없이 TCP 가 끊겼다 |
| `KIOTTY_TIMEOUT` | 1001 (pong 없음) / 우리가 보낸 값 (close 응답 없음) | pong 이 `pong_timeout_ms` 안에 없거나, close frame 응답이 `close_timeout_ms` 안에 없다 |
| `KIOTTY_PROTOCOL_ERROR` | 1002 / 1007 | frame 이 잘못됐다 (서버가 받은 frame 에 mask 가 없음, 예약 비트, 잘못된 opcode, TEXT 가 UTF-8 이 아님). 우리가 그 status 로 close frame 을 보냈다 |
| `KIOTTY_REQUEST_TOO_LARGE` | 1009 | 메시지가 `max_message_bytes` 를 넘었다 |
| `KIOTTY_CLOSED` | 1001 | 소유자의 `close()` |
| `KIOTTY_SOCKET_ERROR` | 1006 | OS 소켓 에러 |

`status` 와 `reason` 은 **상대의** close frame 에서 온다. 우리가 먼저 보냈고 상대가 같은 값을 돌려주는 것이 보통이다. 우리가 닫는 쪽일 때 어떤 status 를 보내는지는 위 표의 "우리가 보낸 값" 이다.

## 5. 보내기, 닫기

```c
KiottyNetCode KiottyNet_sendWebSocket(KiottyNetWebSocketHandle ws, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
KiottyNetCode KiottyNet_broadcastWebSocketServer(KiottyNetWebSocketServerHandle server, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
KiottyNetCode KiottyNet_sendWebSocketClient(KiottyNetWebSocketClientHandle client, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
KiottyNetCode KiottyNet_closeWebSocket(KiottyNetWebSocketHandle ws, uint16_t status, const char* reason);
```

**send.** 아무 thread 에서나. frame 하나로 감싸 (클라이언트는 masking 해서) 그 연결의 push lane 으로 복사하고 돌아온다. 돌아온 뒤 `data` 는 버려도 된다. 완료 콜백은 없고 socket 으로 나가는 것은 IO Thread 의 drain 이 한다. 규칙과 코드는 `KiottyNet_notifyConnection` 과 같다.

| 코드 | 뜻 |
|---|---|
| `KIOTTY_SUCCESS` | push lane 에 들어갔다 |
| `KIOTTY_BUFFER_FULL` | push lane 에 여유가 없다. 아무것도 넣지 않았고 연결은 그대로다. 끊을지는 호출자가 정한다 |
| `KIOTTY_INVALID_ARGUMENT` | `data` 가 비었거나 frame 헤더를 더한 길이가 `push_capacity` 보다 크다. 모르는 opcode |
| `KIOTTY_PEER_CLOSED` | 닫힌 handle. `onOpen` 전이거나 `onClose` 가 나간 뒤 |
| `KIOTTY_CLOSED` | `closeWebSocket` 을 부른 뒤라 데이터 frame 을 보낼 수 없다 |
| `KIOTTY_BUSY` | 다른 thread 의 대기 slot 이 모두 사용 중. 잠시 뒤 다시 |

IO Thread 콜백 안에서 부르면 그 자리에서 복사한다. 다른 thread 에서 부르면 IO Thread 가 복사할 때까지 기다린다. 조각으로 나눠 보내지 않으므로 `push_capacity` 가 메시지 하나의 상한이다.

**broadcast.** `onOpen` 이 나간 모든 소켓에 `send` 한 것과 같다. 여유가 없는 소켓은 건너뛰고 끊지 않으며, 하나라도 건너뛰면 `KIOTTY_BUFFER_FULL`. 특정 소켓들에게만 보내려면 앱이 `onOpen` / `onClose` 에서 handle 목록을 관리하고 각각 `send` 한다. 목록은 IO Thread (콜백) 와 앱 thread 가 같이 만지므로 앱이 잠근다. 닫힌 handle 에 `send` 해도 `KIOTTY_PEER_CLOSED` 일 뿐이므로 목록이 잠깐 낡아도 안전하다.

**close.** 아무 thread 에서나. close frame 을 push lane 으로 보내고 close handshake 를 시작한다. `reason` 은 NULL 이거나 `KIOTTY_MAX_WEBSOCKET_REASON_LENGTH` (124, NUL 포함) 미만의 UTF-8 이다. 그 뒤 `send` 는 `KIOTTY_CLOSED` 다. 상대의 close frame 이 오거나 `close_timeout_ms` 가 지나면 `onClose`. 두 번째 호출은 `KIOTTY_BUSY`.

```c
typedef enum KiottyNetWebSocketStatus { KIOTTY_WS_NORMAL_CLOSURE = 1000, KIOTTY_WS_GOING_AWAY = 1001, ... KIOTTY_WS_INTERNAL_ERROR = 1011 } KiottyNetWebSocketStatus;
```

status 는 `uint16_t` 다. 이름 있는 값은 enum 으로 두었고 앱 정의 값 (3000~4999) 도 그대로 넣는다. C++ 의 `KiottyNet_WebSocket::close()` 는 기본값이 `KIOTTY_WS_NORMAL_CLOSURE`, `nullptr` 다.

**소유자의 close.** `KiottyNet_closeWebSocketServer` / `KiottyNet_closeWebSocketClient` 는 7절과 같다. close handshake 없이 모든 연결을 끊고 (`onClose(KIOTTY_CLOSED, 1001)`) loop 를 끝낸다. 정상 종료를 원하면 먼저 각 소켓에 `closeWebSocket` 을 보내고 `onClose` 를 기다린 뒤 소유자를 닫는다.

## 6. 서버 예제

```cpp
#include "kiotty_net_websocket.h"

struct ChatSession
{
    struct ChatApp* app;
    char            name[32];
};

struct ChatApp
{
    std::mutex                            mutex;
    std::vector<KiottyNetWebSocketHandle> clients;
    SessionPool<ChatSession, 16>          sessions;    // 구현 가이드 3.2 의 고정 배열 저장소. acquire() 는 비면 nullptr
};

static bool onUpgrade(KiottyNetAppContext, KiottyNetWebSocketHandle ws, KiottyNetWebSocketUpgrade upgrade)
{
    KiottyNetSpan token = KiottyNet_WebSocket(ws).upgradeHeader("Authorization");   // 요청 헤더. return 하면 무효
    return token.length != 0 && upgrade.origin.length != 0;                         // false 면 403
}

static KiottyNetWebSocketContext onOpen(KiottyNetAppContext app_context, KiottyNetWebSocketHandle ws)
{
    ChatApp*     app     = static_cast<ChatApp*>(app_context);
    ChatSession* session = app->sessions.acquire();                                  // 저장소는 앱 것. 고정 배열이면 된다
    if (session == nullptr)
    {
        KiottyNet_WebSocket(ws).close(KIOTTY_WS_GOING_AWAY, "full");
        return nullptr;
    }
    session->app = app;
    std::lock_guard<std::mutex> lock(app->mutex);
    app->clients.push_back(ws);
    return session;                                                                  // 이 소켓의 ws_context
}

static void onMessage(KiottyNetWebSocketContext, KiottyNetWebSocketHandle ws, KiottyNetWebSocketMessage message)
{
    if (message.opcode != KIOTTY_WS_TEXT)
    {
        KiottyNet_WebSocket(ws).close(KIOTTY_WS_UNSUPPORTED_DATA, "text only");
        return;
    }
    KiottyNet_WebSocket(ws).send(KIOTTY_WS_TEXT, message.data);                      // echo. push lane 으로 복사되므로 data 는 그대로 넘겨도 된다
}

static void onClose(KiottyNetAppContext app_context, KiottyNetWebSocketContext ws_context, KiottyNetWebSocketHandle ws, KiottyNetWebSocketClose)
{
    ChatApp* app = static_cast<ChatApp*>(app_context);
    {
        std::lock_guard<std::mutex> lock(app->mutex);
        app->clients.erase(std::find(app->clients.begin(), app->clients.end(), ws));
    }
    if (ws_context != nullptr)
    {
        app->sessions.release(static_cast<ChatSession*>(ws_context));
    }
}

static const KiottyNetWebSocketHandler CHAT_HANDLER = { &onUpgrade, &onOpen, &onMessage, &onClose };

int runChatServer(uint16_t port)
{
    ChatApp app;

    KiottyNetServerConfig config;
    if (KiottyNet_ServerConfigBuilder::tcp(config, "0.0.0.0", port).maxClients(16).bufferCapacity(4096, 4096, 65536).build() != KIOTTY_SUCCESS)
    {
        return 1;
    }
    KiottyNetWebSocketConfig websocket;
    if (KiottyNet_WebSocketConfigBuilder(websocket, "/chat").protocols("chat").maxMessageBytes(16384).build() != KIOTTY_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Context         context;
    KiottyNet_WebSocketServer server(context.handle(), config, websocket);           // TLS 면 (context.handle(), config, tls, websocket)
    if (!context.valid() || !server.valid())
    {
        return 1;
    }

    std::thread io([&]() { server.run(CHAT_HANDLER, &app); });                      // 이 thread 가 IO Thread. app 과 CHAT_HANDLER 는 run 이 끝날 때까지 살아 있다
    if (server.await(3000) != KIOTTY_SUCCESS)
    {
        io.join();
        return 1;
    }

    // 앱 thread : 센서 값이 바뀌면 모두에게. 아무 thread 에서나
    server.broadcast(KIOTTY_WS_TEXT, reading.span());

    // 앱 thread : 한 명에게. 닫힌 handle 이면 KIOTTY_PEER_CLOSED 일 뿐이다
    {
        std::lock_guard<std::mutex> lock(app.mutex);
        for (KiottyNetWebSocketHandle ws : app.clients)
        {
            KiottyNet_WebSocket(ws).send(KIOTTY_WS_TEXT, reading.span());
        }
    }

    server.close();
    io.join();
    return 0;
}
```

같은 것을 C 로 쓰면 콜백과 표는 그대로이고 조립만 다르다.

```c
static const KiottyNetWebSocketHandler ECHO_HANDLER = { on_upgrade, on_open, on_message, on_close };

int run_echo_server(uint16_t port)
{
    KiottyNetContext               context;
    KiottyNetServerConfig          config;
    KiottyNetWebSocketConfig       websocket;
    KiottyNetWebSocketServerHandle server;
    KiottyNetCode                  code;

    if (KiottyNet_initializeTcpServerConfig(&config, "0.0.0.0", port) != KIOTTY_SUCCESS) { return 1; }
    if (KiottyNet_initializeWebSocketConfig(&websocket, "/echo") != KIOTTY_SUCCESS)      { return 1; }
    context = KiottyNet_initializeContext();
    if (context == NULL) { return 1; }
    server = KiottyNet_createWebSocketServer(context, &config, NULL, &websocket);
    if (server == NULL) { KiottyNet_releaseContext(context); return 1; }
    code = KiottyNet_runWebSocketServer(server, &ECHO_HANDLER, NULL);                /* 이 thread 가 IO Thread. 다른 thread 의 KiottyNet_closeWebSocketServer 로 끝난다 */
    KiottyNet_destroyWebSocketServer(context, server);
    KiottyNet_releaseContext(context);
    return code == KIOTTY_CLOSED ? 0 : 1;
}
```

## 7. 클라이언트 예제

서버와 같은 handler 모양이다. 소켓이 하나라 `app_context` 가 곧 session 이 되는 것이 보통이다 (`onOpen` 이 `app_context` 를 그대로 돌려준다).

```cpp
static KiottyNetWebSocketContext onOpen(KiottyNetAppContext app_context, KiottyNetWebSocketHandle ws)
{
    KiottyNetWebSocketInfo info = KiottyNet_WebSocket(ws).info();                    // info.protocol : 서버가 고른 subprotocol
    return app_context;
}

static const KiottyNetWebSocketHandler CLIENT_HANDLER = { nullptr, &onOpen, &onMessage, &onClose };   // onUpgrade 는 클라이언트에서 부르지 않는다

int runClient(const char* host, uint16_t port)
{
    KiottyNetClientConfig    config;
    KiottyNetTlsClientConfig tls;
    KiottyNetWebSocketConfig websocket;
    if (KiottyNet_ClientConfigBuilder::tcp(config, host, port).retry(3, 1000).build() != KIOTTY_SUCCESS
        || KiottyNet_TlsClientConfigBuilder(tls).build() != KIOTTY_SUCCESS                                  // 기본값 : OS 저장소로 서버 + hostname 검증
        || KiottyNet_WebSocketConfigBuilder(websocket, "/chat").protocols("chat, superchat").build() != KIOTTY_SUCCESS)
    {
        return 1;
    }

    ClientSession             session;
    KiottyNet_Context         context;
    KiottyNet_WebSocketClient client(context.handle(), config, tls, websocket);     // wss. 평문이면 tls 를 뺀다
    if (!context.valid() || !client.valid())
    {
        return 1;
    }

    std::thread io([&]() { client.run(CLIENT_HANDLER, &session); });                // 연결 (재시도) + Upgrade + loop
    if (client.await(5000) != KIOTTY_SUCCESS)                                        // 101 을 받아 onOpen 이 나가면 SUCCESS
    {
        io.join();
        return 1;
    }
    client.send(KIOTTY_WS_TEXT, hello);                                              // 아무 thread 에서나. = KiottyNet_WebSocket(client.socket()).send(...)
    io.join();                                                                       // 서버가 닫거나 onMessage 안에서 close 하면 run 이 return 한다
    return 0;
}
```

- `run` 의 반환값은 끊어진 이유다. `close` 로 끝났으면 `KIOTTY_CLOSED`, 서버가 끊었으면 `KIOTTY_PEER_CLOSED`, 101 이 아닌 응답이나 핸드셰이크 timeout 이면 `KIOTTY_PROTOCOL_ERROR` / `KIOTTY_TIMEOUT`.
- `KiottyNet_getWebSocketClientSocket` 은 `onOpen` 전이면 0 이다. `KiottyNet_sendWebSocketClient` 는 그 소켓의 `sendWebSocket` 이고 (`notifyEventClient` 와 같은 관계) 연결 전이면 `KIOTTY_PEER_CLOSED` 다.

## 8. 안에서 어떻게 도는가

`KiottyNet_runWebSocketServer` 는 구현 가이드 2.6 의 HTTP 와 같은 방법으로 들어간다. 자기 stack 에 `KiottyNetStatefulAppContext` 를 만들어 machine 에는 라이브러리의 WebSocket 표, `app_context` 에는 서버의 내부 struct (사용자 handler, 사용자 app_context, WebSocket session pool), create / release 에는 그 pool 의 acquire / release 를 넣고 `KiottyNet_runStatefulServer` 를 부른다. 클라이언트는 `KiottyNet_runStatefulEventClient` 다. 사용자는 이 표를 보지 않지만 모양은 이렇다.

```
Handshaking ──READ_UNTIL("\r\n\r\n")──> (경로, 헤더 검사, onUpgrade) ──WRITE(101)──> Opening ──WRITTEN──> onOpen
                                                                                                    │
     ┌──────────────────────────────────────────────────────────────────────────────────────────────┘
     v
ReadingHeader(2) ──READ──> ReadingExtendedLength(2 | 8) ──READ──> ReadingMask(4) ──READ──> ReadingPayload(min(남은 길이, recv_capacity)) ──READ──> ...
     ^                                                                                                     │
     │            control frame (ping → push lane 으로 pong, pong → 타이머 리셋, close → Closing)             │
     └───────────────────────── 데이터 frame 은 메시지 버퍼에 붙이고, FIN 이면 onMessage ──────────────────────┘

Closing ──READ(상대 close frame) 또는 TIMEOUT──> close(KIOTTY_SUCCESS / KIOTTY_TIMEOUT) ──CLOSED──> onClose, release
```

- 수신 frame 이 `recv_capacity` 보다 커도 payload 를 그 크기 단위로 나눠 읽어 메시지 버퍼에 붙인다. 메시지 버퍼가 `max_message_bytes` 다.
- 송신 control frame (pong, 우리가 보내는 ping 과 close) 은 IO Thread 가 `Connection::push` 로 push lane 에 직접 넣는다 (구현 가이드 1.6). 사용자의 `send` 와 같은 lane 이므로 순서가 섞이지 않는다.
- ping 타이머는 `setConnectionTimeout` 하나를 쓴다. 상태마다 entry action 에 `ping_interval_ms` 또는 `pong_timeout_ms` 를 붙이고 TIMEOUT 행이 "ping 보낼 차례인가, pong 이 안 왔는가" 를 가른다.
- 서버와 클라이언트의 표는 Handshaking 두 행 (요청을 읽는가, 응답을 읽는가) 과 mask 유무만 다르다.

## 9. requirement 10, 11절과 달라진 점

| 10, 11절 | 지금 | 왜 |
|---|---|---|
| `KiottyNetWebSocketLimits` 를 사용자가 직접 채움 | `KiottyNetWebSocketConfig` + initialize / configure, `initialization_mark` | 3절 4번. 설정 struct 는 한 가지 방법으로만 채운다 |
| `url_path` 가 create 인자 | `KiottyNet_initializeWebSocketConfig(config, path)` 의 필수 인자 | 문자열은 설정 struct 안 `char[]` 에 값으로 (규약 "문자열 수명") |
| `handler` 가 create 인자, `user_context` 가 handler 안 | `run(handler, app_context)` | 7절 3번. create 는 handler 를 받지 않는다 |
| `onUpgrade(ctx, const KiottyNetHttpRequest*, ws)` | `onUpgrade(app_context, ws, KiottyNetWebSocketUpgrade)` + `KiottyNet_findWebSocketUpgradeHeader` | `kiotty_net_http.h` 에 의존하지 않는다. http.h 만 쓰는 사용자와 websocket.h 만 쓰는 사용자가 서로의 헤더를 끌어오지 않는다 |
| `KiottyNet_getWebSocketContext` / `setWebSocketContext` slot | `onOpen` 이 `ws_context` 를 return, `onClose` 가 `app_context` 와 함께 돌려받음 | base 5절의 두 context 모델과 같은 모양 |
| `onResumed` + `KiottyNet_resumeWebSocket(ws, payload)` 로 다른 thread 의 push | `KiottyNet_sendWebSocket` 이 아무 thread 에서나. push lane (notify) | 4절. 흐름과 무관한 송신은 notify 다. Work 도 resume 도 필요 없다 |
| `send` 가 write lane | push lane | 위와 같다. write lane 은 핸드셰이크에만 쓴다 |
| `onClose(ctx, ws, uint16_t code, KiottyNetCode reason)` | `onClose(app_context, ws_context, ws, KiottyNetWebSocketClose { code, status, reason })` | 상대의 reason 문자열도 준다. `code` 가 KiottyNetCode 인 것은 `KiottyNetResult.code` 와 같다 |
| `KiottyNet_setWebSocketProtocol` | `KiottyNet_selectWebSocketProtocol` + 설정의 `protocols` 로 자동 선택 | 보통은 설정만으로 끝나고 `onUpgrade` 에서 고르는 것은 예외다 |
| `KiottyNet_getWebSocketInfo` → `KiottyNetConnectionInfo` | `KiottyNetWebSocketInfo { connection, protocol }` | 고른 subprotocol 을 값으로 돌려준다 |
| 서버 / 클라이언트 Limits 가 따로 | 하나의 `KiottyNetWebSocketConfig` | 모든 필드의 뜻이 양쪽에서 같다. base 가 Server / Client 설정을 나눈 이유 (TLS 역할, 재시도) 가 여기에는 없다 |
| `KiottyNet_HttpServer::upgrade` | 이 헤더에 없음 | `kiotty_net_http.h` 가 생길 때 그쪽에서 이 헤더를 포함해 `KiottyNet_addWebSocketUpgrade(http_server, &websocket)` 를 둔다. handler 모양은 그대로 쓴다 |

## 10. 열린 질문

1. **Work 를 둘지.** 지금은 없다. "메시지 하나를 처리하는 동안 다음 메시지를 받지 않기" (backpressure) 가 필요해지면 `KiottyNet_startWebSocketWork(ws)` + `onWorkStarted` + `KiottyNet_finishWebSocketWork(work)` 를 base 의 Work 위에 얹는다. 그러면 Work 중에는 ping / pong 도 멈춘다는 점 (base 는 WORKING 중 onRead 를 내지 않는다) 을 받아들여야 한다.
2. **`closeWebSocket` 을 아무 thread 에서나 되게 하는 구현.** close frame 은 push lane (notify) 으로 가고, "그 뒤 데이터 frame 거부 + close 타이머" 는 IO Thread 가 push slot 을 처리할 때 소켓의 atomic 플래그를 보고 전이한다. 구현 가이드 1.6 의 slot 에 "close" 종류를 더하는 셈이다.
3. **큰 메시지 송신.** `push_capacity` 가 메시지 하나의 상한이다. 조각으로 나눠 보내는 streaming 송신 (`sendWebSocketFragment`) 은 두지 않았다.
4. **동기 WebSocketClient** (11절 열린 질문 1). 두지 않았다. 서버 push 를 받으려면 결국 loop 가 필요하다.
5. **`KiottyNetWebSocketUpgrade` 에 무엇을 더 둘지.** 지금은 `path`, `query`, `host`, `origin`, `protocols`. 나머지는 `findWebSocketUpgradeHeader`.
6. **서버가 받는 Upgrade 요청의 상한이 `recv_capacity`.** 쿠키가 큰 브라우저 요청은 4 KiB 를 넘을 수 있다. 그런 앱은 `recv_capacity` 를 올린다. 별도 limit 을 두지 않았다.

# WebSocket 사용 가이드 (kiotty_net_websocket.h)

`include/kiotty_net_websocket.h` 를 쓰는 방법이다. requirement_v0.md 10, 11절의 WebSocket 을 1~8절의 지금 모양 (run / close / await, 두 context, write lane / push lane, notify, StateMachine) 과 HttpServer 에 맞춰 다시 쓴 것이고, WebSocket 에 대해서는 이 문서가 10, 11절보다 우선한다.

이 문서는 헤더 판 v2 를 따른다. 설계는 [design_websocket_v0.html](../design/design_websocket_v0.html) 의 결정 1 ~ 13 이다. 판 v1 과 달라진 점은 9절에 모아 두었다.

헤더는 `kiotty_net_http.h` 를 포함하고, 그것이 `kiotty_net_base.h` 를 포함한다. 서버 쪽 WebSocket 은 HttpServer 의 경로이므로 의존은 WebSocket → HTTP 한 방향이다. `kiotty_net_http.h` 는 `kiotty_net_websocket.h` 를 모른다. 사용자는 `kiotty_net_websocket.h` 하나만 포함하면 HTTP 와 WebSocket 을 같이 쓸 수 있다. `kiotty_net_statemachine.h` 는 포함하지 않는다 (사용자는 표를 보지 않는다). 위는 C 인터페이스, 아래는 C 함수를 하나씩 부르는 C++ 래퍼다. `kiotty::net::OwnedHandle` 은 `kiotty_net_base.h` 의 것을 쓴다.

## 1. 한눈에

| 것 | 무엇 | C | C++ |
|---|---|---|---|
| 서버 | HttpServer 의 Upgrade 경로. 따로 선 WebSocketServer 는 없다. 한 서버에 경로 여럿 | `KiottyNet_addWebSocket(http_server, "/chat", handler, app_context)`, `KiottyNet_broadcastWebSocket(http_server, "/chat", opcode, data)`. run / close / await 는 HttpServer 의 것 | `KiottyNet_HttpServer` + `KiottyNet_addWebSocket(server.handle(), ...)` |
| 클라이언트 | 6절 EventClient 위에 Upgrade 행과 WebSocket 행을 얹은 것 | `KiottyNetWebSocketClientHandle`, `KiottyNet_*WebSocketClient`. create 는 `KiottyNetWebSocketClientResult { code, handle }` | `KiottyNet_WebSocketClient` (`explicit operator bool`, `code()`) |
| 설정 | WebSocket 전용 설정 struct 는 없다 | `KiottyNetHttpLimits` 한 벌. 메시지 상한 `max_body_bytes`, 핸드셰이크 `max_header_bytes` / `header_timeout_milliseconds`, 타이머 셋 `websocket_*` | `KiottyNet_HttpLimitsBuilder` |
| 소켓 하나 | 연결 하나의 WebSocket. pool handle | `KiottyNetWebSocketHandle { owner, id }`, `KiottyNet_*WebSocket` | `KiottyNet_WebSocket` (비소유 값 타입. `==`, `!=`, `<`) |
| 콜백 | 연결마다 넷 | `KiottyNetWebSocketHandler` | 같은 struct |

서버 경로와 클라이언트는 같은 `KiottyNetWebSocketHandler` 와 같은 `KiottyNetHttpLimits` 를 쓴다. 다른 것은 소켓 수 (서버 `max_clients`, 클라이언트 1), masking (클라이언트만 씌운다), subprotocol (서버만 고른다) 이고 앞의 둘은 라이브러리 안의 일이다.

thread 규칙은 base 와 같다.

| 어디서든 | IO Thread (콜백 안) 에서만 | run 전에만 |
|---|---|---|
| `KiottyNet_sendWebSocket`, `KiottyNet_sendWebSocketClient`, `KiottyNet_broadcastWebSocket` | `KiottyNet_getWebSocketInfo` | `KiottyNet_addWebSocket` (run 뒤 `KIOTTY_NET_CODE_BUSY`) |
| `KiottyNet_closeWebSocket` | `KiottyNet_findWebSocketUpgradeHeader`, `KiottyNet_selectWebSocketProtocol` (`onUpgrade` 안에서만) | |
| 소유자의 `close`, `await`, `KiottyNet_getWebSocketClientSocket` | | |

## 2. 머릿속 모델

```
HttpSession ──[HTTP 행]── GET /chat + Upgrade ──onUpgrade──101 송신 완료──[WebSocket 행]──> onOpen / onMessage / onClose
                                                                                    │
                                                            ping / pong / close frame 은 라이브러리가 처리
```

- **WebSocket 은 HTTP 연결에서 시작한다.** 따로 여는 소켓이 아니라 HTTP 요청이 오가던 TCP 연결 하나를 그 자리에서 바꿔 쓰는 것이다 (RFC 6455 1.3). 그래서 서버는 한 포트에서 웹 페이지 (`GET /index.html`) 와 WebSocket (`GET /chat` + Upgrade) 을 같이 받는다. 장비의 웹 UI 와 실시간 채널이 한 서버에 있는 것이 흔한 모양이다.
- **경로가 endpoint 다.** 서버는 HttpServer 에 `KiottyNet_addWebSocket` 으로 경로를 등록한다. 경로마다 handler 와 app_context 가 따로다 (`/chat`, `/telemetry`). broadcast 는 등록에 쓴 같은 경로 문자열로 그 경로의 소켓 전부에 보낸다.
- 서버는 등록된 경로의 요청을 HTTP 헤더처럼 읽고 (`max_header_bytes`, `header_timeout_milliseconds`), `onUpgrade` 가 허락하면 101 을 HttpSession 의 응답 버퍼에서 write lane 으로 보낸다. 101 의 송신이 끝나면 같은 Session 이 WebSocket 행으로 넘어가고 `onOpen` 이 온다. 그 뒤 이 연결에 HTTP 는 없다.
- 클라이언트는 연결되면 Upgrade 요청을 write lane 으로 보내고 101 을 읽은 뒤 `onOpen` 이다. `onUpgrade` 는 클라이언트에서 호출되지 않는다 (NULL 로 둔다).
- **`KiottyNetWebSocketHandle` 은 pool handle 이다** (규약 8). `{ KiottyNetWebSocketEndpoint* owner; int64_t id; }` 로 base 의 `KiottyNetConnectionHandle` 과 같은 모양이다. owner 는 소켓을 가진 쪽 (HttpServer 또는 WebSocketClient) 이고 id 는 generation + Session index 다. 무효는 `{ NULL, 0 }`. **`onUpgrade` 가 받는 handle 과 `onOpen` 이 받는 handle 은 다르다** (101 을 만들 때 generation 을 올린다). `onUpgrade` 의 handle 은 `onUpgrade` 안에서만 쓰고, 연결이 끝날 때까지 쓰는 것은 `onOpen` 의 handle 이다. 닫히면 generation 이 바뀌어 옛 handle 로 부른 함수는 `KIOTTY_NET_CODE_PEER_CLOSED` 를 돌려준다. 다른 thread 에 넘겨 두어도 dangling 이 없다. Upgrade 전의 HTTP 응답 handle 과는 generation 이 달라서 HTTP 쪽 handle 로 WebSocket 을 만질 수 없다.
- **context 는 둘이다.** base 와 같다. `KiottyNetAppContext` 는 앱의 것이고 `onUpgrade`, `onOpen`, `onClose` 가 받는다. 서버는 `addWebSocket` 에 경로마다 넘기고, 클라이언트는 `run` 에 넘긴다. `KiottyNetWebSocketContext` 는 소켓마다의 데이터다. `onOpen` 이 만들어 return 하면 라이브러리가 그 소켓에 묶어 두고 `onMessage` 의 첫 인자로 돌려주며, `onClose` 가 `app_context` 와 함께 돌려받아 해제한다. 둘 다 `void*` 의 typedef 이고 라이브러리는 읽지 않는다.
- **state 는 라이브러리가 든다.** frame 파싱 상태, 메시지 버퍼, ping 타이머, 닫힘 플래그는 모두 HttpSession 에 있다. 새 Session 종류도 새 버퍼도 없다. 메시지는 HTTP body 의 Stage 에 모인다. 사용자의 `websocket_context` 에는 앱 데이터만 둔다.
- **사용자의 송신은 모두 push lane 이다.** 사용자의 `send`, `broadcast`, `closeWebSocket` 의 close frame, 라이브러리의 ping / pong 과 pong 늦음의 close frame 은 `KiottyNet_notifyConnection` 과 같은 길로 간다. 그래서 어느 thread 에서나 부를 수 있고 완료 콜백이 없다. write lane 은 핸드셰이크 (101 / 403 / 426 / 405 / 400, 클라이언트의 Upgrade 요청) 와, 받은 frame 때문에 라이브러리가 닫을 때의 close frame (1002 / 1007 / 1009, 상대 close 에 대한 답) 에 쓴다. 뒤의 것은 close frame 을 다 보낸 뒤 연결을 닫아야 해서다 (8절).
- **Work 는 없다.** 긴 작업은 `onMessage` 에서 데이터를 `websocket_context` 로 복사해 worker 에 넘기고, worker 가 끝나면 `KiottyNet_sendWebSocket` 으로 보낸다. 연결을 IO Thread 에서 떼어낼 이유가 없다.

## 3. 설정과 경로 등록

WebSocket 전용 설정 struct 는 없다. 서버는 HttpServer 의 `KiottyNetHttpLimits` 를, 클라이언트는 create 에 넘긴 `KiottyNetHttpLimits` (NULL 이면 기본값) 를 그대로 쓴다. 규칙은 `user_guide_http.md` 1.1 과 같다. `KiottyNet_initializeHttpLimits` 한 번, `KiottyNet_configureHttp*` 여러 번, 반환 코드 확인.

| `KiottyNetHttpLimits` 필드 | 기본값 | WebSocket 에서의 뜻 |
|---|---|---|
| `max_body_bytes` | 64 KiB | 조각을 합친 메시지 하나의 최대 크기. 넘으면 1009 로 닫는다. 메시지는 body 의 Stage 에 모이므로 버퍼가 새로 생기지 않는다 |
| `max_header_bytes`, `max_headers` | 8 KiB, 32 | 서버: Upgrade 요청 (request line + 헤더) 의 상한. 넘으면 431. 클라이언트: 101 응답 헤더의 상한 |
| `header_timeout_milliseconds` | 10000 | 핸드셰이크 timeout. 서버: Upgrade 요청을 다 받을 때까지. 클라이언트: 101 을 다 받을 때까지. 지나면 끊는다 (`onOpen` 전이므로 WebSocket 콜백 없음) |
| `response_bytes` | 64 KiB | 서버의 101 / 403 / 426 이 들어가는 응답 버퍼. 그 뒤로는 쓰지 않는다 |
| `max_routes` | 64 | HTTP 라우트 + 정적 경로 + WebSocket 경로의 합 |
| `websocket_ping_interval_milliseconds` | 30000 | interval 마다 ping 을 보낸다. 0 이면 보내지 않는다 (들어온 ping 에 대한 pong 은 언제나 보낸다) |
| `websocket_pong_timeout_milliseconds` | 10000 | ping 뒤 pong 이 이 시간 안에 없으면 1001 close frame 을 보내고 닫는다 (4절의 `TIMEOUT`). 0 이면 ping 은 보내되 pong 을 검사하지 않는다 |
| `websocket_close_timeout_milliseconds` | 5000 | close frame 을 보낸 뒤 상대의 close frame 을 기다리는 시간. 0 이면 끝없이 기다린다 |

타이머 셋은 `KiottyNet_configureHttpWebSocketTimers(limits, ping_interval_milliseconds, pong_timeout_milliseconds, close_timeout_milliseconds)` 한 번으로 바꾼다. C++ 은 `KiottyNet_HttpLimitsBuilder::webSocketTimers(ping, pong, close)` 다. `keep_alive_timeout_milliseconds` 는 101 뒤에는 쓰지 않는다.

설정은 서버 하나에 한 벌이다. 경로마다 다른 메시지 상한이나 타이머를 둘 수 없다 (결정 11).

base 의 버퍼 설정과 이렇게 맞물린다.

| ServerConfig / ClientConfig | WebSocket 에서의 뜻 |
|---|---|
| `receive_capacity` | frame payload 를 이 크기 단위로 나눠 읽어 Stage 에 붙인다. 메시지보다 작아도 된다. Upgrade 요청의 상한은 이것이 아니라 `max_header_bytes` 다 |
| `send_capacity` | 101 / 403 / 426 (서버), Upgrade 요청 (클라이언트) 이 나가는 write lane. 그 뒤로는 쓰지 않는다 |
| `push_capacity` | **`send`, `broadcast`, ping / pong / close frame 이 들어가는 곳.** 메시지 하나는 frame 머리 (최대 14 바이트) 를 더해 이 값 이하여야 하고, 상대가 느리면 여러 개가 쌓인다. 큰 메시지를 보내는 앱은 이 값을 올린다 |

메모리는 HttpServer 와 같은 `max_clients × (max_header_bytes + max_body_bytes + response_bytes)` 에 base 의 세 버퍼다. WebSocket 경로를 더해도 늘지 않는다.

```cpp
KiottyNetHttpLimits limits;
KiottyNetCode code = KiottyNet_HttpLimitsBuilder(limits)
    .bodyCapacity(16 * 1024)                 // 메시지 상한 16 KiB
    .webSocketTimers(30000, 10000, 5000)     // ping 간격, pong timeout, close timeout
    .build();
```

```c
KiottyNetHttpLimits limits;
if (KiottyNet_initializeHttpLimits(&limits) != KIOTTY_NET_CODE_SUCCESS) { }
if (KiottyNet_configureHttpWebSocketTimers(&limits, 30000, 10000, 5000) != KIOTTY_NET_CODE_SUCCESS) { }
```

**경로 등록 (서버).** run 전에 라우트처럼 부른다.

```c
KiottyNetCode KiottyNet_addWebSocket(KiottyNetHttpServerHandle server, const char* path, KiottyNetWebSocketHandler handler, KiottyNetAppContext app_context);
```

- `handler` 는 값으로 복사해 고정한다. 등록 뒤 사용자의 struct 는 버려도 된다. `app_context` 는 run 이 return 할 때까지 살아 있어야 한다.
- 한 HttpServer 에 경로를 여럿 둘 수 있고 경로마다 handler 와 app_context 가 따로다. C++ 의 `KiottyNet_HttpServer` 에는 메서드가 없다 (`kiotty_net_http.h` 는 이 헤더를 모른다). `KiottyNet_addWebSocket(server.handle(), "/chat", handler, &app)` 로 부른다.

| 코드 | 뜻 |
|---|---|
| `KIOTTY_NET_CODE_SUCCESS` | 등록됐다 |
| `KIOTTY_NET_CODE_BUSY` | run 이 시작된 뒤다 |
| `KIOTTY_NET_CODE_TABLE_FULL` | `max_routes` 가 찼다 |
| `KIOTTY_NET_CODE_DUPLICATE_RULE` | 같은 path 가 이미 있다. HTTP 라우트든 WebSocket 경로든 |
| `KIOTTY_NET_CODE_INVALID_ARGUMENT` | path 가 NULL, `/` 로 시작하지 않음, 상한 초과. handler 의 `onOpen` 또는 `onMessage` 가 NULL |

등록된 경로로 온 요청은 이렇게 갈린다.

| 요청 | 응답 |
|---|---|
| OPTIONS | HTTP 라우트 표의 자동 204 (`Allow: GET, HEAD, OPTIONS`) |
| GET / HEAD / OPTIONS 밖의 메서드 | HTTP 라우트 표의 자동 405 (`Allow: GET, HEAD, OPTIONS`) |
| HEAD | 405 (`Allow: GET`) |
| body 가 있는 GET | 400 |
| Upgrade 헤더가 없는 GET (또는 `Connection: Upgrade`, `Sec-WebSocket-Key`, `Sec-WebSocket-Version: 13` 이 없음) | 426 (`Upgrade: websocket`, `Sec-WebSocket-Version: 13` 헤더와 함께). `onUpgrade` 는 부르지 않는다 |
| `Sec-WebSocket-Key` 가 16 바이트의 base64 (24 글자) 가 아님 | 400 |
| `onUpgrade` 가 0 | 403 을 보내고 끊는다. 이 연결에 더 이상 WebSocket 콜백은 없다 |
| `onUpgrade` 가 1 이거나 NULL | 101. 송신이 끝나면 `onOpen` |

HEAD 와 GET 의 거절 (405 / 400 / 426 / 403) 은 `Connection: close` 를 붙이고 닫는다. 이것들은 HTTP 응답이므로 HttpServer 의 끊김 action (`onHttpDisconnected`) 에도 잡힌다.

## 4. 콜백 : KiottyNetWebSocketHandler

함수 표만 담는다. 서버는 `addWebSocket` 이 값으로 복사하고, 클라이언트는 `run` 에 포인터로 넘기므로 `run` 이 return 할 때까지 살아 있어야 한다. `static const` 로 두면 둘 다 된다.

| 콜백 | 언제 | 받는 것 | 돌려주는 것 |
|---|---|---|---|
| `onUpgrade(app_context, websocket, upgrade)` | 서버. 등록된 경로의 올바른 Upgrade 요청을 다 읽었을 때. NULL 이면 모두 허용 | `KiottyNetWebSocketUpgrade` : `path`, `query`, `host`, `origin`, `protocols` (요청의 `Sec-WebSocket-Protocol` 원문, 쉼표로 이은 것). 모두 요청 버퍼를 가리키며 return 하면 무효 | `KiottyNetBool`. 1 이면 101. 0 이면 403 |
| `onOpen(app_context, websocket)` | 101 송신 완료 (서버) / 101 수신 (클라이언트) | | 이 소켓의 `websocket_context`. NULL 이어도 된다 |
| `onMessage(websocket_context, websocket, message)` | 메시지 하나가 완성됐을 때. 조각은 합쳐져 있다 | `KiottyNetWebSocketMessage { opcode, data }`. `data` 는 Stage 를 가리키며 return 하면 무효. TEXT 는 UTF-8 검사를 통과한 것 | |
| `onClose(app_context, websocket_context, websocket, close)` | 마지막 콜백. `onOpen` 이 나간 소켓에만, 한 번. NULL 이어도 된다 | `KiottyNetWebSocketClose { code, status, reason }` (아래 표). `reason` 은 return 하면 무효 | |

`onUpgrade` 안에서만 할 수 있는 일 둘.

- `KiottyNet_findWebSocketUpgradeHeader(websocket, "Authorization")` : 요청의 다른 헤더. 대소문자 무시. 없으면 `{ NULL, 0 }`.
- `KiottyNet_selectWebSocketProtocol(websocket, "mqtt")` : subprotocol 을 고른다. `upgrade.protocols` 에 없던 값이면 `KIOTTY_NET_CODE_INVALID_ARGUMENT` 이고 바뀌지 않는다. `onUpgrade` 밖이면 `KIOTTY_NET_CODE_BUSY`. 두 번째 호출은 앞의 것을 바꾼다. 고른 값은 101 의 `Sec-WebSocket-Protocol` 로 나가고, 연결 뒤에는 `KiottyNet_getWebSocketInfo` 의 `protocol` 로 본다. C++ 은 `KiottyNet_WebSocket::selectProtocol`.

**subprotocol 은 서버만 고른다** (결정 12). 일반 앱 (웹 UI, 채팅, 게임) 은 경로와 메시지 형식으로 충분해 쓰지 않는다. 고르지 않으면 101 에 그 헤더가 없다. 다만 MQTT (`mqtt`), OCPP (`ocpp1.6`, `ocpp2.0.1`), GraphQL 구독 (`graphql-transport-ws`), STOMP (`v12.stomp`) 같은 외부 표준 클라이언트는 후보를 보내고, 서버가 하나도 고르지 않으면 연결을 실패로 처리한다. 그런 클라이언트를 받는 경로는 `onUpgrade` 에서 고른다. WebSocketClient 는 subprotocol 을 보내지 않는다.

`onClose` 의 `KiottyNetWebSocketClose`.

| `code` (KiottyNetCode) | `status` (close frame) | 뜻 |
|---|---|---|
| `KIOTTY_NET_CODE_SUCCESS` | 상대가 보낸 값 (1000 등). 상대가 코드 없이 보냈으면 1005 | close handshake 가 끝났다. 누가 시작했든 |
| `KIOTTY_NET_CODE_PEER_CLOSED` | 1006 | close frame 없이 TCP 가 끊겼다 |
| `KIOTTY_NET_CODE_TIMEOUT` | 우리가 보낸 값. pong 늦음이면 1001 | 우리가 보낸 close frame 에 상대가 답하지 않았다. pong 이 `websocket_pong_timeout_milliseconds` 안에 없으면 라이브러리가 1001 close frame 을 보내고 `websocket_close_timeout_milliseconds` 를 기다리므로, 상대가 그 사이 답하면 `SUCCESS` 다 (5절 close) |
| `KIOTTY_NET_CODE_PROTOCOL_ERROR` | 1002 / 1007 | frame 이 잘못됐다 (서버가 받은 frame 에 mask 가 없음, 클라이언트가 받은 frame 에 mask 가 있음, 예약 비트, 잘못된 opcode, TEXT 가 UTF-8 이 아님). 우리가 그 status 로 close frame 을 보냈다 |
| `KIOTTY_NET_CODE_REQUEST_TOO_LARGE` | 1009 | 메시지가 `max_body_bytes` 를 넘었다 |
| `KIOTTY_NET_CODE_CLOSED` | 1001 | 소유자의 `close()` (HttpServer 또는 WebSocketClient) |
| `KIOTTY_NET_CODE_SOCKET_ERROR` | 1006 | OS 소켓 에러 |

`status` 와 `reason` 은 **상대의** close frame 에서 온다. 상대가 close frame 을 보냈으면 `code` 가 무엇이든 그 값이다. 우리가 먼저 보냈고 상대가 같은 status 를 돌려주는 것이 보통이다. 상대의 close frame 이 없을 때의 status 는 위 표의 "우리가 보낸 값" 또는 1006 이다. 라이브러리가 상대의 close 에 답할 때는 받은 status 만 돌려보내고 reason 은 붙이지 않는다.

## 5. 보내기, 닫기, broadcast

```c
KiottyNetCode KiottyNet_sendWebSocket(KiottyNetWebSocketHandle websocket, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
KiottyNetCode KiottyNet_broadcastWebSocket(KiottyNetHttpServerHandle server, const char* path, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
KiottyNetCode KiottyNet_sendWebSocketClient(KiottyNetWebSocketClientHandle client, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
KiottyNetCode KiottyNet_closeWebSocket(KiottyNetWebSocketHandle websocket, uint16_t status, const char* reason);
```

**send.** 아무 thread 에서나. frame 머리와 payload 를 (클라이언트는 masking 해서) 그 연결의 push lane 에 한 번에 복사하고 돌아온다. 전부 들어가지 않으면 하나도 넣지 않는다. 돌아온 뒤 `data` 는 버려도 된다. 완료 콜백은 없고 socket 으로 나가는 것은 IO Thread 의 drain 이 한다. 규칙과 코드는 `KiottyNet_notifyConnection` 과 같다.

| 코드 | 뜻 |
|---|---|
| `KIOTTY_NET_CODE_SUCCESS` | push lane 에 들어갔다 |
| `KIOTTY_NET_CODE_BUFFER_FULL` | push lane 에 여유가 없다. 아무것도 넣지 않았고 연결은 그대로다. 끊을지는 호출자가 정한다 |
| `KIOTTY_NET_CODE_INVALID_ARGUMENT` | `data` 가 비었거나 frame 머리를 더한 길이가 `push_capacity` 보다 크다. 모르는 opcode |
| `KIOTTY_NET_CODE_PEER_CLOSED` | 닫힌 handle 또는 `{ NULL, 0 }`. `onOpen` 전이거나 `onClose` 가 나간 뒤 |
| `KIOTTY_NET_CODE_CLOSED` | `closeWebSocket` 을 부른 뒤라 데이터 frame 을 보낼 수 없다 |
| `KIOTTY_NET_CODE_BUSY` | 다른 thread 의 대기 slot 이 모두 사용 중. 잠시 뒤 다시 |

IO Thread 콜백 안에서 부르면 그 자리에서 복사한다. 다른 thread 에서 부르면 IO Thread 가 복사할 때까지 기다린다. 조각으로 나눠 보내지 않으므로 `push_capacity` 가 메시지 하나의 상한이다.

**broadcast.** 아무 thread 에서나. `path` 는 `addWebSocket` 에 준 것과 글자 그대로 비교한다. 그 경로로 열린 (`onOpen` 이 나간) 모든 소켓에 `send` 한 것과 같다. `path` 가 NULL 이면 그 서버의 모든 WebSocket 소켓이다. 등록되지 않은 path 는 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. 여유가 없는 소켓은 건너뛰고 끊지 않으며, 하나라도 건너뛰면 `KIOTTY_NET_CODE_BUFFER_FULL`.

특정 소켓들에게만 보내려면 앱이 `onOpen` / `onClose` 에서 handle 목록을 관리하고 각각 `send` 한다. C++ 의 `KiottyNet_WebSocket` 은 `==`, `!=`, `<` 가 있어 `std::vector` 의 `std::find` 나 `std::set` 에 그대로 든다. 목록은 IO Thread (콜백) 와 앱 thread 가 같이 만지므로 앱이 잠근다. 닫힌 handle 에 `send` 해도 `KIOTTY_NET_CODE_PEER_CLOSED` 일 뿐이므로 목록이 잠깐 낡아도 안전하다.

**close.** 아무 thread 에서나. 소켓의 닫힘 플래그 (atomic) 를 세우고 close frame 을 push lane 에 넣은 뒤 돌아온다. `reason` 은 NULL 이거나 `KIOTTY_NET_MAX_WEB_SOCKET_REASON_LENGTH` (124, NUL 포함) 미만의 UTF-8 이다. 그 뒤 `send` 는 `KIOTTY_NET_CODE_CLOSED` 이고, 그 사이에 상대가 보낸 데이터 frame 은 `onMessage` 로 넘기지 않고 버린다. 두 번째 호출은 `KIOTTY_NET_CODE_BUSY`.

| 어디서 불렀나 | 언제 `onClose` 가 오나 |
|---|---|
| IO Thread (`onMessage` 안) | 콜백이 돌아온 뒤 거는 다음 read 부터 timeout 이 `websocket_close_timeout_milliseconds` 다. 상대의 close frame 이 오면 `SUCCESS`, 그 시간이 지나면 `TIMEOUT` |
| 다른 thread | IO Thread 는 따로 깨어나지 않고, 그 연결의 다음 READ / TIMEOUT 행이 플래그를 본다. 상대는 close frame 을 받으면 곧 답하므로 (RFC 6455 5.5.1) 보통 그 READ 에서 `SUCCESS` 로 끝난다. 상대가 답 대신 다른 frame 을 보내면 그 뒤의 read 부터 `websocket_close_timeout_milliseconds` 가 걸린다 |

**상대가 답하지 않으면.** 다른 thread 에서 부른 close 에 상대가 끝까지 답하지 않고 아무것도 보내지 않으면, `websocket_close_timeout_milliseconds` 가 아니라 걸려 있던 read 의 남은 시간이 지나야 `TIMEOUT` 으로 끊긴다. **최대 `websocket_ping_interval_milliseconds` 다** (결정 9). 빨리 끊어야 하는 앱은 ping 간격을 줄인다. ping 간격이 0 이면 상대가 무언가를 보내거나 TCP 가 끊길 때까지 기다린다.

```c
typedef enum KiottyNetWebSocketStatus { KIOTTY_NET_WEB_SOCKET_STATUS_NORMAL_CLOSURE = 1000, KIOTTY_NET_WEB_SOCKET_STATUS_GOING_AWAY = 1001, ... KIOTTY_NET_WEB_SOCKET_STATUS_INTERNAL_ERROR = 1011 } KiottyNetWebSocketStatus;
```

status 는 `uint16_t` 다. 이름 있는 값은 enum 으로 두었고 앱 정의 값 (3000~4999) 도 그대로 넣는다. C++ 의 `KiottyNet_WebSocket::close()` 는 기본값이 `KIOTTY_NET_WEB_SOCKET_STATUS_NORMAL_CLOSURE`, `nullptr` 다.

**소유자의 close.** `KiottyNet_closeHttpServer` / `KiottyNet_closeWebSocketClient` 는 base 7절과 같다. WebSocket 연결을 close handshake 없이 끊고 (`onClose(KIOTTY_NET_CODE_CLOSED, 1001)`) loop 를 끝낸다. HttpServer 에서는 WebSocket 이었던 연결도 연결이므로 그 뒤 HttpServer 의 끊김 action (`onHttpDisconnected`) 도 불린다. 정상 종료를 원하면 먼저 각 소켓에 `closeWebSocket` 을 보내고 `onClose` 를 기다린 뒤 소유자를 닫는다.

## 6. 서버 예제

장비의 웹 UI (`/`), 채팅 (`/chat`), 센서 값 (`/telemetry`) 을 한 HttpServer, 한 포트에 둔다.

```cpp
#include "kiotty_net_websocket.h"                                                    // kiotty_net_http.h 도 같이 들어온다

struct ChatSession
{
    struct ChatApp* app;
    char            name[32];
};

struct ChatApp
{
    std::mutex                    mutex;
    std::set<KiottyNet_WebSocket> clients;                                           // operator< 로 set 에 든다
    SessionPool<ChatSession, 16>  sessions;                                          // 구현 가이드 3.2 의 고정 배열 저장소. acquire() 는 비면 nullptr
};

static KiottyNetBool onChatUpgrade(KiottyNetAppContext, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketUpgrade upgrade)
{
    KiottyNetSpan token = KiottyNet_WebSocket(websocket).upgradeHeader("Authorization");   // 요청 헤더. return 하면 무효
    if (token.length == 0 || upgrade.origin.length == 0)
    {
        return 0;                                                                    // 403
    }
    return 1;
}

static KiottyNetWebSocketContext onChatOpen(KiottyNetAppContext app_context, KiottyNetWebSocketHandle websocket)
{
    ChatApp*     app     = static_cast<ChatApp*>(app_context);
    ChatSession* session = app->sessions.acquire();                                  // 저장소는 앱 것. 고정 배열이면 된다
    if (session == nullptr)
    {
        KiottyNet_WebSocket(websocket).close(KIOTTY_NET_WEB_SOCKET_STATUS_GOING_AWAY, "full");
        return nullptr;
    }
    session->app = app;
    std::lock_guard<std::mutex> lock(app->mutex);
    app->clients.insert(KiottyNet_WebSocket(websocket));
    return session;                                                                  // 이 소켓의 websocket_context
}

static void onChatMessage(KiottyNetWebSocketContext, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketMessage message)
{
    if (message.opcode != KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT)
    {
        KiottyNet_WebSocket(websocket).close(KIOTTY_NET_WEB_SOCKET_STATUS_UNSUPPORTED_DATA, "text only");
        return;
    }
    KiottyNet_WebSocket(websocket).send(KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, message.data);   // echo. push lane 으로 복사되므로 data 는 그대로 넘겨도 된다
}

static void onChatClose(KiottyNetAppContext app_context, KiottyNetWebSocketContext websocket_context, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketClose)
{
    ChatApp* app = static_cast<ChatApp*>(app_context);
    {
        std::lock_guard<std::mutex> lock(app->mutex);
        app->clients.erase(KiottyNet_WebSocket(websocket));
    }
    if (websocket_context != nullptr)
    {
        app->sessions.release(static_cast<ChatSession*>(websocket_context));
    }
}

static KiottyNetWebSocketContext onTelemetryOpen(KiottyNetAppContext, KiottyNetWebSocketHandle)
{
    return nullptr;                                                                  // 받기만 하는 경로. 보내는 것은 broadcast
}

static void onTelemetryMessage(KiottyNetWebSocketContext, KiottyNetWebSocketHandle, KiottyNetWebSocketMessage)
{
}

static const KiottyNetWebSocketHandler CHAT_HANDLER      = { &onChatUpgrade, &onChatOpen, &onChatMessage, &onChatClose };
static const KiottyNetWebSocketHandler TELEMETRY_HANDLER = { nullptr, &onTelemetryOpen, &onTelemetryMessage, nullptr };   // onUpgrade NULL = 모두 허용

int runDevice(uint16_t port)
{
    ChatApp app;

    KiottyNetServerConfiguration configuration;
    KiottyNetHttpLimits          limits;
    KiottyNetAddress             address;
    if (KiottyNet_ServerConfigurationBuilder(configuration).maxClients(16).bufferCapacity(4096, 4096, 65536).build() != KIOTTY_NET_CODE_SUCCESS
        || KiottyNet_HttpLimitsBuilder(limits).bodyCapacity(16 * 1024).webSocketTimers(30000, 10000, 5000).build() != KIOTTY_NET_CODE_SUCCESS
        || KiottyNet_initializeTcpAddress(&address, "0.0.0.0", port) != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Context    context;
    KiottyNet_HttpServer server(context.handle(), configuration, limits);           // 주소와 TLS 는 run 이 받는다
    if (!context || !server)
    {
        return 1;
    }
    if (server.serveDirectory("/", "./www/") != KIOTTY_NET_CODE_SUCCESS                                     // 장비의 웹 UI
        || KiottyNet_addWebSocket(server.handle(), "/chat", CHAT_HANDLER, &app) != KIOTTY_NET_CODE_SUCCESS   // 같은 포트의 /chat 이 WebSocket
        || KiottyNet_addWebSocket(server.handle(), "/telemetry", TELEMETRY_HANDLER, &app) != KIOTTY_NET_CODE_SUCCESS)   // 경로마다 handler 가 따로
    {
        return 1;
    }

    std::thread io([&]() { server.run(address); });                                  // 이 thread 가 IO Thread. app 은 run 이 끝날 때까지 살아 있다. TLS 면 run(address, tls)
    if (server.await(3000) != KIOTTY_NET_CODE_SUCCESS)
    {
        io.join();
        return 1;
    }

    // 앱 thread : 센서 값이 바뀌면 /telemetry 의 모두에게. 아무 thread 에서나
    KiottyNet_broadcastWebSocket(server.handle(), "/telemetry", KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, reading.span());

    // 앱 thread : 채팅 참가자에게만. 닫힌 handle 이면 KIOTTY_NET_CODE_PEER_CLOSED 일 뿐이다
    {
        std::lock_guard<std::mutex> lock(app.mutex);
        for (KiottyNet_WebSocket websocket : app.clients)
        {
            websocket.send(KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, notice.span());
        }
    }

    server.close();                                                                  // WebSocket 은 onClose(CLOSED, 1001)
    io.join();
    return 0;
}
```

같은 것을 C 로 쓰면 콜백과 표는 그대로이고 조립만 다르다.

```c
static const KiottyNetWebSocketHandler ECHO_HANDLER = { on_upgrade, on_open, on_message, on_close };

int run_echo_server(uint16_t port)
{
    KiottyNetContextResult       initialized;
    KiottyNetContext             context;
    KiottyNetServerConfiguration configuration;
    KiottyNetHttpLimits          limits;
    KiottyNetAddress             address;
    KiottyNetHttpServerResult    created;
    KiottyNetHttpServerHandle    server;
    KiottyNetCode                code;

    if (KiottyNet_initializeServerConfiguration(&configuration) != KIOTTY_NET_CODE_SUCCESS)   { return 1; }
    if (KiottyNet_initializeHttpLimits(&limits) != KIOTTY_NET_CODE_SUCCESS)                   { return 1; }
    if (KiottyNet_initializeTcpAddress(&address, "0.0.0.0", port) != KIOTTY_NET_CODE_SUCCESS) { return 1; }
    initialized = KiottyNet_initializeContext();
    if (initialized.code != KIOTTY_NET_CODE_SUCCESS) { return 1; }
    context = initialized.handle;
    created = KiottyNet_createHttpServer(context, &configuration, &limits);
    if (created.code != KIOTTY_NET_CODE_SUCCESS) { KiottyNet_releaseContext(context); return 1; }
    server = created.handle;
    code = KiottyNet_addWebSocket(server, "/echo", ECHO_HANDLER, NULL);
    if (code == KIOTTY_NET_CODE_SUCCESS)
    {
        code = KiottyNet_runHttpServer(server, &address, NULL);   /* 이 thread 가 IO Thread. 다른 thread 의 KiottyNet_closeHttpServer 로 끝난다 */
    }
    KiottyNet_destroyHttpServer(context, server);
    KiottyNet_releaseContext(context);
    return code == KIOTTY_NET_CODE_CLOSED ? 0 : 1;
}
```

## 7. 클라이언트 예제

서버와 같은 handler 모양이다. 소켓이 하나라 `app_context` 가 곧 session 이 되는 것이 보통이다 (`onOpen` 이 `app_context` 를 그대로 돌려준다).

```c
KiottyNetWebSocketClientResult KiottyNet_createWebSocketClient(KiottyNetContext context, const KiottyNetClientConfiguration* configuration, const KiottyNetHttpLimits* limits /* NULL = 기본값 */, const char* path);
KiottyNetCode KiottyNet_runWebSocketClient(KiottyNetWebSocketClientHandle client, const KiottyNetAddress* address, const KiottyNetTlsClientConfiguration* tls /* NULL = ws */, const KiottyNetWebSocketHandler* handler, KiottyNetAppContext app_context);
```

- create 는 `{ code, handle }` 을 돌려준다. 실패하면 handle 이 `NULL` 이고 code 가 이유다. `path` 는 요청 경로 (`/chat?room=1` 처럼 query 를 붙여도 된다) 이고 `/` 로 시작하는 보이는 ASCII 글자여야 한다 (공백 안 됨).
- `limits` 에서 쓰는 것은 `max_body_bytes` (메시지 상한), `max_header_bytes` / `header_timeout_milliseconds` (101 응답), 타이머 셋이다.
- 주소와 TLS 는 run 이 받는다 (결정 13). `tls` 가 NULL 이면 ws, 아니면 wss 다. 한 클라이언트로 ws 와 wss 에 차례로 붙을 수 있다.
- **Host 헤더와 TLS SNI 는 run 의 `address` 의 host 에서 온다** (결정 6). 연결하는 주소와 이름이 하나여야 인증서 검증이 맞는다.
- Host 는 IPv6 literal 이면 `[...]` 로 감싸고, 포트가 ws 의 80 / wss 의 443 이 아니면 `:port` 를 붙인다. `Sec-WebSocket-Key` 는 run 마다 새 난수다.
- Upgrade 요청에 `Sec-WebSocket-Protocol` 은 없다 (결정 12).
- 보내는 frame 은 모두 frame 마다 새 mask key 로 mask 한다. 서버가 mask 한 frame 을 보내면 1002 로 닫는다 (`onClose(PROTOCOL_ERROR, 1002)`).

```cpp
static KiottyNetWebSocketContext onOpen(KiottyNetAppContext app_context, KiottyNetWebSocketHandle websocket)
{
    KiottyNetWebSocketInfo info = KiottyNet_WebSocket(websocket).info();            // info.connection : 상대 주소. protocol 은 언제나 빈 문자열
    return app_context;
}

static const KiottyNetWebSocketHandler CLIENT_HANDLER = { nullptr, &onOpen, &onMessage, &onClose };   // onUpgrade 는 클라이언트에서 부르지 않는다

int runClient(const char* host, uint16_t port)
{
    KiottyNetClientConfiguration    configuration;
    KiottyNetHttpLimits             limits;
    KiottyNetAddress                address;
    KiottyNetTlsClientConfiguration tls;
    if (KiottyNet_ClientConfigurationBuilder(configuration).retry(3, 1000).build() != KIOTTY_NET_CODE_SUCCESS
        || KiottyNet_HttpLimitsBuilder(limits).bodyCapacity(16 * 1024).build() != KIOTTY_NET_CODE_SUCCESS
        || KiottyNet_initializeTcpAddress(&address, host, port) != KIOTTY_NET_CODE_SUCCESS                  // host 가 Host 헤더와 SNI
        || KiottyNet_TlsClientConfigurationBuilder(tls).build() != KIOTTY_NET_CODE_SUCCESS)                  // 기본값 : OS 저장소로 서버 + hostname 검증
    {
        return 1;
    }

    ClientSession             session;
    KiottyNet_Context         context;
    KiottyNet_WebSocketClient client(context.handle(), configuration, &limits, "/chat");   // limits 가 nullptr 이면 기본값
    if (!context || !client)
    {
        return 1;                                                                    // 이유는 client.code()
    }

    std::thread io([&]() { client.run(address, tls, CLIENT_HANDLER, &session); });  // wss. 평문 (ws) 이면 run(address, CLIENT_HANDLER, &session). 연결 (재시도) + Upgrade + loop
    if (client.await(5000) != KIOTTY_NET_CODE_SUCCESS)                                // 101 을 받아 onOpen 이 나가면 SUCCESS
    {
        io.join();
        return 1;
    }
    client.send(KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, hello);                           // 아무 thread 에서나. = KiottyNet_WebSocket(client.socket()).send(...)
    io.join();                                                                       // 서버가 닫거나 onMessage 안에서 close 하면 run 이 return 한다
    return 0;
}
```

`run` 의 반환값은 끊어진 이유다.

| 코드 | 뜻 |
|---|---|
| `KIOTTY_NET_CODE_CLOSED` | `close` 로 끝났다 |
| `KIOTTY_NET_CODE_PEER_CLOSED` | 서버가 끊었다. close handshake 를 마친 정상 종료도 이것이다 (어떻게 끝났는지는 `onClose` 의 `code` 를 본다) |
| `KIOTTY_NET_CODE_PROTOCOL_ERROR` | 101 이 아닌 응답, `Sec-WebSocket-Accept` 가 틀림, 요청하지 않은 `Sec-WebSocket-Protocol` 이 101 에 있음 (RFC 6455 4.1). `onOpen` / `onClose` 는 없다 |
| `KIOTTY_NET_CODE_TIMEOUT` | 101 이 `header_timeout_milliseconds` 안에 오지 않았다 |
| `KIOTTY_NET_CODE_INVALID_ARGUMENT` | handler / address 가 NULL, `onOpen` / `onMessage` 가 NULL, 이미 돌고 있다, Upgrade 요청 (path + host) 이 768 바이트를 넘는다 |
| 그 밖 | EventClient 의 run 과 같다 (연결 재시도 실패, `SOCKET_ERROR`, TLS 실패) |

- **await** 는 `onOpen` 이 나가면 `KIOTTY_NET_CODE_SUCCESS`, 그 전에 run 이 끝났으면 run 의 반환값 (위 표), 시간이 지나면 `KIOTTY_NET_CODE_TIMEOUT` 이다. base 의 await 가 "loop 가 돌기 시작" 을 기다리는 것과 달리 "소켓이 열림" 을 기다린다. 끝난 run 의 결과는 다음 run 이 시작할 때까지 남으므로, 같은 클라이언트로 다시 run 한 직후의 await 는 앞 run 의 결과를 볼 수 있다. 다시 run 한 뒤에는 `onOpen` 에서 앱이 신호를 주거나 `SUCCESS` 가 될 때까지 짧게 다시 묻는다.
- `KiottyNet_getWebSocketClientSocket` 은 `onOpen` 전과 `onClose` 뒤에 `{ NULL, 0 }` 이다. `KiottyNet_sendWebSocketClient` 는 그 소켓의 `sendWebSocket` 이고 (`notifyEventClient` 와 같은 관계) 그때는 `KIOTTY_NET_CODE_PEER_CLOSED` 다.
- 정상 종료는 `KiottyNet_WebSocket(client.socket()).close()` 로 close handshake 를 하는 것이다. 서버가 답하고 끊으면 `onClose(SUCCESS, ...)` 뒤 run 이 `PEER_CLOSED` 로 돌아온다. `client.close()` 는 handshake 없이 끊는다 (`onClose(CLOSED, 1001)`, run 은 `CLOSED`).

## 8. 안에서 어떻게 도는가

WebSocket 소켓을 가질 수 있는 Endpoint 는 둘이고 (HttpServer, WebSocketClient), 둘 다 "HttpSession 배열 + 표" 다. 이것이 `KiottyNetWebSocketEndpoint` 이고 handle 의 `owner` 가 가리키는 것이다. 한 연결이 HTTP 로 시작해 WebSocket 으로 바뀌므로 표를 바꿔 타지 않고 한 표에 HTTP 행과 WebSocket 행을 합친다. WebSocket 행은 한 벌만 정의하고 두 표가 같이 쓴다 (결정 8). Session 도 같은 HttpSession 이고, 서버 / 클라이언트의 차이 (mask 를 검사하나 씌우나) 는 Session 의 역할 값 하나다. 사용자는 이 표를 보지 않지만 모양은 이렇다.

**HttpServer 의 연결 하나** (base 의 Server. 역할 값 = 서버 : 받은 frame 의 mask 를 검사하고 벗긴다, 보내는 frame 에 mask 없음)

| 상태 | 사건 | 하는 일 | 다음 |
|---|---|---|---|
| ReadingRequestLine, ReadingHeaderLine | READ | HTTP 와 같다. `max_header_bytes`, `header_timeout_milliseconds` | afterHeaders |
| afterHeaders | 라우트 표가 WebSocket 경로 | GET 이 아니면 405. Upgrade / Connection / Sec-WebSocket-Key / Version 13 이 없으면 426. body 가 있으면 400 | HTTP 응답을 보내고 닫는다 |
| afterHeaders | 올바른 Upgrade 요청 | `onUpgrade(app_context, websocket, upgrade)`. 0 이면 403 (keep-alive 아님). 1 이면 응답 버퍼에 101 + `Sec-WebSocket-Accept` + (골랐으면) `Sec-WebSocket-Protocol`, generation 을 올린다 | WebSocketOpening, write lane 으로 101 |
| WebSocketOpening | WRITTEN | 101 이 다 나갔으면 `onOpen(app_context, websocket)`. 돌려준 `websocket_context` 를 Session 에 | WebSocketReadingHeader, read 2 (timeout `ping_interval`) |
| WebSocketReadingHeader | READ (2) | FIN / opcode / mask 비트 / 길이 7 비트 | 126 / 127 이면 ReadingExtendedLength, mask 면 ReadingMask, 아니면 ReadingPayload |
| WebSocketReadingExtendedLength | READ (2 또는 8) | 길이 | ReadingMask 또는 ReadingPayload |
| WebSocketReadingMask | READ (4) | mask key | ReadingPayload |
| WebSocketReadingPayload | READ (min(남은 길이, `receive_capacity`)) | unmask 하며 Stage 에 붙인다. `max_body_bytes` 를 넘으면 1009 | 남으면 같은 상태 |
| WebSocketReadingPayload | frame 끝, control frame | ping → push lane 에 pong (닫는 중이면 보내지 않는다). pong → 타이머 리셋. close → 플래그가 서 있으면 우리가 시작한 close 의 답이라 close(`SUCCESS`). 아니면 플래그를 세우고 받은 status 의 close frame 을 보낸 뒤 close(`SUCCESS`) (`thenCloseWith`) | ReadingHeader 또는 CLOSED |
| WebSocketReadingPayload | frame 끝, 데이터 frame | FIN 이 아니면 다음 frame. FIN 이면 TEXT 의 UTF-8 검사 (아니면 1007), 닫힘 플래그가 서 있으면 버리고 아니면 `onMessage(websocket_context, websocket, { opcode, Stage })`, Stage 를 비운다 | ReadingHeader. 닫힘 플래그가 서 있으면 (`onMessage` 안의 close 포함) timeout `close_timeout`, 아니면 `ping_interval` (pong 을 기다리는 중이면 남은 `pong_timeout`) |
| WebSocket* | READ, 잘못된 frame | 1002 / 1007 / 1009 close frame 을 보낸 뒤 close (`thenCloseWith`. 플래그가 이미 서 있으면 보내지 않고 바로 close) | CLOSED |
| WebSocket* | TIMEOUT | 닫힘 플래그가 서 있으면 close(`TIMEOUT`). pong 이 안 왔으면 push lane 에 1001 close frame, 플래그를 세우고 같은 read 를 `close_timeout` 으로 다시. ping 차례면 push lane 에 ping 하고 같은 read 를 `pong_timeout` 으로 다시 | 같은 상태 |
| CLOSED | | `onClose(app_context, websocket_context, websocket, { code, status, reason })`, HttpServer 의 `onHttpDisconnected` | release |

**WebSocketClient 의 연결 하나** (base 의 EventClient. 역할 값 = 클라이언트 : 받은 frame 에 mask 가 있으면 1002, 보내는 frame 에 mask 를 씌운다)

| 상태 | 사건 | 하는 일 | 다음 |
|---|---|---|---|
| SendingUpgrade | CONNECTED | GET path + Host (run 의 address) + Upgrade / Connection / Sec-WebSocket-Key / Version 13 | write lane 으로 요청 |
| SendingUpgrade | WRITTEN | | ReadingStatusLine (timeout `header_timeout`) |
| ReadingStatusLine | READ | 101 이 아니면 close(`PROTOCOL_ERROR`) | ReadingResponseHeaderLine |
| ReadingResponseHeaderLine | READ | 헤더 줄을 요청 버퍼에 (`max_header_bytes`). 끝나면 `Sec-WebSocket-Accept` 검사, `Sec-WebSocket-Protocol` 이 있으면 close(`PROTOCOL_ERROR`) | |
| ReadingResponseHeaderLine | 헤더 끝 | `onOpen(app_context, websocket)` | WebSocketReadingHeader |
| WebSocketReadingHeader ~ CLOSED | | 위 표의 WebSocket 행 그대로. mask 처리만 역할 값을 따른다 | |

- HTTP body 와 WebSocket 메시지가 같은 Stage 를 쓴다. Upgrade 요청에는 body 가 없고 101 뒤에는 HTTP 가 없으므로 한 연결에서 둘이 겹치는 순간이 없다.
- 수신 frame 이 `receive_capacity` 보다 커도 payload 를 그 크기 단위로 나눠 읽어 Stage 에 붙인다. 조각마다 unmask 의 key offset 을 Session 이 든다.
- 송신 frame (pong, ping, `closeWebSocket` 과 pong 늦음의 close, 사용자의 send 와 broadcast) 은 push lane 이다. 서버는 frame 머리와 payload 를 조각 둘로 넘겨 한 lock 안에서 이어 복사하고, 전부 들어가지 않으면 하나도 넣지 않는다. 클라이언트는 `push_capacity` 크기의 scratch 버퍼 (create 때 한 번 잡는다) 를 mutex 로 지키며 거기에 mask 한 frame 을 만들어 한 조각으로 넣는다. mask key 는 frame 마다 새것이다. 같은 lane 이므로 순서가 섞이지 않는다.
- 받은 frame 때문에 라이브러리가 닫을 때 (1002 / 1007 / 1009, 상대 close 에 대한 답) 의 close frame 만 write lane 이다. base 의 `thenCloseWith` 로 Session 의 작은 버퍼 (131 바이트) 의 close frame 만 보내고 닫는다. push lane 에 남은 데이터 frame 은 버리므로 close frame 뒤에 데이터가 나가지 않는다. pong 늦음의 close 가 push lane 인 것은 그때 read 가 걸려 있어 write 를 걸 수 없어서다.
- ping 타이머는 entry action 의 `timeout_milliseconds` 하나를 쓴다. 상태마다 read 에 `ping_interval` / `pong_timeout` / `close_timeout` 을 붙이고 TIMEOUT 행이 "닫는 중인가, ping 보낼 차례인가, pong 이 안 왔는가" 를 가른다.
- 다른 thread 의 close 는 플래그와 push lane 의 close frame 뿐이다. drain 뒤 상태를 옮기는 훅은 없다 (결정 3). 그래서 5절의 "최대 ping_interval" 이 생긴다.

## 9. v1 헤더와 달라진 점

| 것 | 판 v1 | 판 v2 | 왜 |
|---|---|---|---|
| 서버 | `KiottyNet_WebSocketServer` (create / destroy / run / close / await / broadcast) | HttpServer + `KiottyNet_addWebSocket(server, "/chat", handler, app_context)`. run / close / await 는 HttpServer 의 것 | 결정 1. WebSocket 은 언제나 HTTP 요청으로 시작한다. WebSocketServer 는 "Upgrade 경로 하나만 등록한 HttpServer" 였다 |
| broadcast | `broadcastWebSocketServer(server, opcode, data)` | `broadcastWebSocket(http_server, "/chat", opcode, data)`. NULL path 는 서버의 모든 WebSocket 소켓 | 결정 10. 경로가 여럿일 수 있다 |
| handler, app_context (서버) | `run` 의 인자 | `addWebSocket` 이 경로마다 값으로 복사해 고정 | 결정 4. HTTP 라우트와 같은 모델 |
| 설정 | `KiottyNetWebSocketConfiguration` + initialize / configure 여섯 + `KiottyNet_WebSocketConfigurationBuilder` | 없음. `KiottyNetHttpLimits` 한 벌 (+ `websocket_*` 타이머 셋, `KiottyNet_configureHttpWebSocketTimers`, `webSocketTimers`) | 결정 11. 필드 대부분이 HttpServer 에 이미 있다. 대가는 경로마다 다른 상한 / 타이머를 둘 수 없다는 것 |
| 메시지 상한 | `max_message_bytes` | `max_body_bytes` | 메시지는 body 의 Stage 에 모인다 |
| 핸드셰이크 | `handshake_timeout_milliseconds`, Upgrade 요청 상한은 `receive_capacity` | `header_timeout_milliseconds`, `max_header_bytes` | Upgrade 요청은 HTTP 헤더다. 줄 단위로 읽어 요청 버퍼에 복사한다 |
| subprotocol | 설정의 `protocols` 로 자동 선택 (양쪽), `selectWebSocketProtocol` 로 덮어쓰기 | 서버만. `onUpgrade` 에서 `selectWebSocketProtocol` 로 고른다. 클라이언트는 보내지 않고, 101 에 있으면 `PROTOCOL_ERROR` | 결정 12. 일반 앱은 쓰지 않고, 외부 표준 클라이언트를 받는 서버만 고르면 된다 |
| 클라이언트 create | `(context, configuration, tls, websocket)` → handle | `(context, configuration, limits, path)` → `{ code, handle }` | 결정 11, 12, 13. base 판 v1 과 같은 Result |
| 클라이언트 run | `(client, handler, app_context)` | `(client, address, tls, handler, app_context)`. tls NULL 이면 ws | 결정 13. 상대의 주소와 TLS 는 실행 중에 정해진다 |
| Host 헤더와 SNI | 설정의 주소 | run 의 `address` 의 host | 결정 6 |
| `KiottyNetWebSocketHandle` | `int64_t`, 0 이 무효 | `{ KiottyNetWebSocketEndpoint* owner; int64_t id; }`, `{ NULL, 0 }` 이 무효 | 결정 7. 소유자를 handle 이 든다. base / HTTP 와 같은 모양 |
| include | `kiotty_net_statemachine.h`, OwnedHandle 가드 복사 | `kiotty_net_http.h` (그 안에 base.h) | 결정 2. 서버 쪽 WebSocket 은 HttpServer 없이 쓸 수 없다 |
| 101 / 403 응답 | write lane | 같다. HttpSession 의 응답 버퍼에서 나간다. 426 / 405 가 더해졌다 | Upgrade 는 HTTP 응답이다 |
| 다른 thread 의 close | 열린 질문 (atomic 플래그) | 플래그 + push lane 의 close frame. 훅 없음. 상대가 답하지 않으면 최대 `ping_interval` | 결정 3, 9 |
| 서버 close | `closeWebSocketServer` | `closeHttpServer`. `onClose(CLOSED, 1001)` 뒤 `onHttpDisconnected` 도 | 연결은 HttpServer 의 것이다 |
| C++ `KiottyNet_WebSocket` | 6 메서드 | + `==`, `!=`, `<` | 목록 / 집합에 들기 위해 |
| C++ `KiottyNet_WebSocketClient` | `operator bool` | `explicit operator bool`, `code()` | create 가 이유를 돌려준다 |

## 10. 정할 것

모두 정했다 (2026-11-11, 설계 결정 1 ~ 13).

두지 않은 것은 이렇다. 필요해지면 그때 더한다.

| 것 | 지금 | 대신 |
|---|---|---|
| Work | 없다 | `onMessage` 에서 복사해 worker 로, 결과는 `sendWebSocket`. Work 를 두면 Work 중에 ping / pong 도 멈춘다 |
| 큰 메시지의 조각 송신 | 없다. `push_capacity` 가 메시지 하나의 상한 | `push_capacity` 를 올린다 |
| 동기 WebSocketClient | 없다 | 서버 push 를 받으려면 결국 loop 가 필요하다 |
| 경로마다 다른 메시지 상한 / 타이머 | 없다. 서버 하나에 한 벌 | 다른 값이 필요한 경로는 다른 HttpServer 에 둔다 |
| 클라이언트의 subprotocol | 보내지 않는다 | 경로와 메시지 형식으로 나눈다 |

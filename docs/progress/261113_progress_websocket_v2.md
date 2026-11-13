# 2026-11-13 진행 정리 : WebSocket 설계 v1 의 사이클 V2 (서버 WebSocketRoute)

기준 문서: `docs/design/design_websocket_v1.html` 4절, 6절, 7절, 9절, 11절의 V2 행.

## 바뀐 API

```c
typedef KiottyNetBool             (*KiottyNetWebSocketUpgradeAction)(KiottyNetAppContext app_context, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketUpgrade upgrade);
typedef KiottyNetWebSocketContext (*KiottyNetWebSocketOpenAction)(KiottyNetAppContext app_context, KiottyNetWebSocketHandle websocket);
typedef void                      (*KiottyNetWebSocketMessageAction)(KiottyNetWebSocketContext websocket_context, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketMessage message);
typedef void                      (*KiottyNetWebSocketCloseAction)(KiottyNetAppContext app_context, KiottyNetWebSocketContext websocket_context, KiottyNetWebSocketClose close);

typedef struct KiottyNetWebSocketRoute { KiottyNetCode code; KiottyNetHttpServerHandle server; int32_t index; } KiottyNetWebSocketRoute;

KiottyNetWebSocketRoute KiottyNet_addWebSocket(KiottyNetHttpServerHandle server, const char* path);
KiottyNetWebSocketRoute KiottyNet_onWebSocketUpgrade(KiottyNetWebSocketRoute route, KiottyNetWebSocketUpgradeAction on_upgrade);
KiottyNetWebSocketRoute KiottyNet_onWebSocketOpen(KiottyNetWebSocketRoute route, KiottyNetWebSocketOpenAction on_open);
KiottyNetWebSocketRoute KiottyNet_onWebSocketMessage(KiottyNetWebSocketRoute route, KiottyNetWebSocketMessageAction on_message);
KiottyNetWebSocketRoute KiottyNet_onWebSocketClose(KiottyNetWebSocketRoute route, KiottyNetWebSocketCloseAction on_close);
```

C++: `KiottyNet_WebSocketRoute(server, "/chat").onUpgrade(a).onOpen(b).onMessage(c).onClose(d).build()`. 생성자는 `KiottyNet_HttpServer&` 와 `KiottyNetHttpServerHandle` 둘, `build()` 는 code, `route()` 는 C 값. websocket.h 에만 있고 http.h 는 바뀌지 않는다.

## 정한 것 (구현에서)

- **route.code** 는 처음 실패. `addWebSocket` 의 실패 (`DUPLICATE_RULE`, `TABLE_FULL`, `BUSY`, `INVALID_ARGUMENT`) 또는 `on*` 의 실패 (run 중 `BUSY`, 콜백 NULL / server NULL / 없는 index 는 `INVALID_ARGUMENT`). 실패한 route 를 받은 `on*` 는 아무것도 하지 않고 그 code 를 그대로 돌려준다. 같은 이벤트를 다시 등록하면 바꾼다.
- **필수는 onMessage 하나.** Upgrade 가 없으면 모두 허용, Open 이 없으면 websocket_context 는 NULL, Close 가 없으면 부르지 않는다. onMessage 없는 경로가 하나라도 있으면 `runHttpServer` 가 열기 전에 `INVALID_ARGUMENT`.
- **콜백의 app_context 는 run 의 것** (`HttpSite::app_context`, V1). 경로마다 다른 데이터는 Open 이 돌려주는 websocket_context 에 둔다.
- **Close 에 handle 이 없다.** 끊긴 뒤라 쓸 곳이 없다 (http 결정 19 의 규약). 목록에서 빼려면 Open 에서 handle 을 websocket_context 에 담아 둔다.

## 안

- `WebSocketTarget` 이 `{ on_upgrade, on_open, on_message, on_close, path }` 가 됐다 (app_context 를 들지 않는다). frame 행 (`openSocket`, `endSocket`, `deliver`) 과 `callUpgrade` 는 `session.site.app_context` 를 넘긴다.
- `HttpRegistry::addRoute(path)` 가 `SocketIndexResult` (index) 를 돌려주고, `find(int32_t index)` 가 그 칸 (`SocketTargetResult`) 을 준다. `find(path)` 가 index 를 주는 것과 짝이다. 규칙 검사의 public 함수 수를 늘리지 않으려고 새 이름 대신 오버로드로 뒀다.
- onMessage 검사는 `HttpServer::run` 의 첫 줄 (`hasEverySocketMessage`, 익명 namespace).
- **클라이언트는 V3 전까지 임시.** `KiottyNetWebSocketHandler` 는 클라이언트 run 에만 남고, 필드 넷이 위 typedef 다. 그래서 클라이언트의 onClose 도 handle 을 받지 않는다. 클라이언트 run 이 handler 를 target 으로 옮기고 run 의 app_context 를 `site.app_context` 에 넣는다.

## 테스트

테스트 계획 23절. 서버 integration 의 등록을 사슬로 바꾸고, `RunRefusesASocketPathWithoutOnMessageAndOnlyMessageIsNeeded` 를 더했다. C / C++ 헤더 layout 에 `KiottyNetWebSocketRoute` 를 더했다. 전에는 onClose 안에서 handle 로 send 해 `PEER_CLOSED` 를 봤는데, handle 이 없어졌으므로 닫힌 뒤 테스트 thread 에서 그 handle 로 send 해 본다.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 663 / 663 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 664 / 664 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 664 / 664, 664 / 664 |
| WSL clang Release TLS ON | 663 / 663 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 문서

requirement, user guide, 구현 가이드의 WebSocket 절은 V4 에서 한 번에 고친다 (설계 v1 11절). 이 사이클은 테스트 계획 23절과 이 progress 만.

## 다음

V3: 클라이언트 (on* 등록, run 의 path 와 app_context, subprotocol 후보, bye, onError, close frame 의 mask key).

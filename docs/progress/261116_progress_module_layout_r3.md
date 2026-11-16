# 2026-11-16 진행 정리 : 모듈 정리 R3 (upgrade 를 websocket 으로)

기준 문서: `docs/design/design_module_layout_v0.html` 4절, 정할 것 4 (병렬 배열). 구현은 설계를 받은 C++ 구현 에이전트가 하고, 검토와 전체 빌드는 이 세션에서 했다.

## 결과

http 가 websocket 을 include 하지 않는다. 수칙 검사 `LAYER` 9 → 0 (FILE 1, LINE 4, MACRO 18, PUBLIC 3 그대로). 공개 헤더 (`include/`) 는 바뀌지 않았다.

## 모양

| 것 | 자리 | 내용 |
|---|---|---|
| `HttpUpgradeHandler` | `http/data/repository/kiotty_http_upgrade_handler.h` (새) | `answer(session, upgrade_route)`, `step(session, connection, event)`, `end(session, reason)`. 소멸자는 protected (PushLane 과 같음) |
| `HttpSite::upgrade` | http | `HttpUpgradeHandler*` 하나. websocket 칸 (`socket_owner`, `lane`, client 의 upgrade, `role`, `mask_state`) 은 모두 빠짐 |
| `HttpSession` | http | `SocketState socket`, `SocketLink& link` 가 빠짐. 풀의 `_links` / `links()` 도 빠짐 |
| 라우트 | http registry | `MatchKind::SOCKET` → `UPGRADE`, `RouteMatch.upgrade` 는 정수. 대상 (콜백 넷과 path) 은 websocket 이 가진다. http 의 표지 `NO_UPGRADE_ROUTE` |
| `appendBody` | http request store | 붙인 바이트를 `Result<Span<char>, Status>` 로 돌려준다. mask 풀기는 websocket 이 그 span 에 한다 (http 는 `MaskKey` 를 모른다) |
| 상태 기계 | http machine | `SOCKET_OPENING`, `FRAME_*` 상태 이름은 그대로. 그 상태의 열 줄이 모두 `onUpgraded` → `site.upgrade->step(...)`. `.to(...)` 목록 그대로. `dispatch` 는 `answer`, `onClosed` 는 `end` |
| `WebSocketHandler` | `websocket/data/repository/kiotty_websocket_handler.*` (새) | `HttpUpgradeHandler` 구현. 서버와 클라이언트가 함께 쓴다. 라우트 대상, `SocketState[]`, `SocketLink[]` (initialize 때 크기), `owner`, `lane`, `client`, `role`, `mask_state`. `step` 은 WRITTEN → `onSocketWritten`, TIMEOUT → `onSocketTimeout`, READ → `socket.read.state` 로 고른 row. session 에서 찾기: `handlerOf(session)` (`site.upgrade` 의 static_cast), `socketOf / linkOf` (session index) |
| `WebSocketServer` | `websocket/data/repository/kiotty_websocket_server.*` (새) | `HttpServer + WebSocketHandler + PushLane + KiottyNetWebSocketEndpoint`. 지금까지 HttpServer 가 하던 websocket 일 (push, endpoint 다섯, run 전의 on_message 검사, `site.upgrade` 설정) |
| 공개 handle | `context/kiotty_handles.h` | `KiottyNetHttpServer final : WebSocketServer` |
| `HttpServer` | http | 순수 HTTP 서버 |
| WebSocketClient | websocket | 자기 HttpSite 의 `upgrade` 를 자기 `WebSocketHandler` 로. 배열은 한 칸 |

## 설계와 다른 점

- 설계 4절의 `UPGRADED` 한 상태는 하지 않았다. 동작을 그대로 두려고 상태 이름을 남기고 row 만 `step` 으로 넘겼다. R4w 에서 websocket 이 자기 frame 상태 enum 을 가질 때 줄인다.
- 설계는 `reserve(count)` 를 인터페이스에 두었지만, 서버가 `WebSocketServer` 로 HttpServer 를 상속하게 되어 `WebSocketServer::initialize` 가 `max_clients` 로 직접 배열을 만든다. 인터페이스는 셋 (`answer`, `step`, `end`).
- registry 의 upgrade 라우트 추가는 이름 `addRoute(path)` 그대로 (새 이름이면 공개 메서드 8). C 번역이 `registry.addRoute(path)` 뒤 `server->addTarget(index)` 로 짝을 맞춘다. 둘 다 initialize 때 비고 websocket 만 부르므로 번호가 맞는다.
- 핸들러가 없는 서버에 upgrade 라우트가 오면 404 (지금은 생길 수 없음).
- `WebSocketHandler::initialize` 가 SocketState 를 모두 비운다 (예전에는 session 을 다시 만들며 비워졌다).

## 남은 것 (R4w 에서)

- `kiotty_websocket_rows.cpp` 393 줄 (400 한도 가까이).
- `WebSocketHandler` 가 공개 필드와 private 배열을 섞는다. state 로 나눌 때 정리.
- 클라이언트 콜백 등록이 `handlerOf(site).client.events...` 로 길다.

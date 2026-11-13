# 2026-11-13 진행 정리 : WebSocket 설계 v1 의 사이클 V1 (HTTP 의 app_context 는 run 에 한 번)

기준 문서: `docs/design/design_websocket_v1.html` 3절, 11절의 V1 행.

## 왜

라우트마다, 끊김 / 오류 action 마다 app_context 를 따로 받았다. 실제 앱은 app 하나를 모든 곳에 넘기고 있었고 (H6 MCP 예제, 테스트 전부), WebSocket 의 새 등록 (V2, V3) 이 app_context 를 run 에서 한 번 받기로 하면서 HTTP 도 같은 모양으로 맞췄다 (사용자 결정).

## 바뀐 API

| 전 | 후 |
|---|---|
| `KiottyNetHttpRoute { app_context, onRequest, onWorkStarted }` | `{ onRequest, onWorkStarted }` |
| `KiottyNet_makeHttpRoute(app_context, onRequest, onWorkStarted)` | `KiottyNet_makeHttpRoute(onRequest, onWorkStarted)` |
| `KiottyNet_onHttpDisconnected(server, action, app_context)` | `KiottyNet_onHttpDisconnected(server, action)` |
| `KiottyNet_onHttpError(server, action, app_context)` | `KiottyNet_onHttpError(server, action)` |
| `KiottyNet_runHttpServer(server, address, tls)` | `KiottyNet_runHttpServer(server, address, tls, app_context)` |
| C++ `onDisconnected(action, app_context)`, `onError(action, app_context)` | `onDisconnected(action)`, `onError(action)` |
| C++ `run(address)`, `run(address, tls)` | `run(address, app_context)`, `run(address, tls, app_context)` (base 의 `KiottyNet_Server::run` 처럼 app_context 는 필수) |

콜백 typedef 넷의 첫 인자 `app_context` 는 그대로다. 값이 run 의 것으로 바뀌었을 뿐이다.

## 안

- `HttpSite::app_context` 를 두고 `HttpServer::run` 이 시작할 때 넣는다. 라우트 (`callRoute`), Work (`startWork`), 끊김 (CLOSED 행의 `onClosed`), 오류 (`reportError`) 가 여기서 읽는다.
- `HttpNotices` 는 action 둘만 든다. `reportDisconnected` / `reportError` 가 app_context 를 인자로 받는다.
- WebSocket 클라이언트의 `reportError` 도 같은 함수를 지나므로 `site.app_context` (클라이언트 site 에서는 아직 NULL. V3 에서 run 의 것이 된다) 를 넘긴다.
- WebSocket 서버의 `KiottyNet_addWebSocket(server, path, handler, app_context)` 는 이 사이클에서 그대로다 (V2 에서 WebSocketRoute 로 바뀐다).

## 테스트

새 테스트는 없다. 19절의 테스트가 콜백의 app_context 를 앱 객체로 써서 기록하므로 run 의 포인터가 라우트, Work, 끊김, 오류에 닿는 것을 그대로 본다 (테스트 계획 22절). 고친 곳: HttpServer / HttpStatic / HttpWork / HttpMcp / WebSocketServer / WebSocketClient integration 의 등록과 run, HTTP 벤치마크.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 662 / 662 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 663 / 663 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 663 / 663, 663 / 663 |
| WSL clang Release TLS ON | 662 / 662 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 문서

requirement 9절 (끊김과 오류 표, 헤더 초안, C++ wrapper, 사용 예, 미결의 template 오버로드), user guide HTTP (라우트, run, 끊김과 오류, C / C++ 예제), 구현 가이드 4절 (HttpSite, callRoute, startWork, run), 테스트 계획 22절, user guide WebSocket 의 run 예제 한 줄.

## 다음

V2: 서버 WebSocketRoute (설계 v1 4절).

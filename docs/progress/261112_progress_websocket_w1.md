# 2026-11-12 진행 정리 : WebSocket 사이클 W1 (공개 API 판 v2)

기준 문서: `docs/design/design_websocket_v0.html` 4절 (헤더 초안), 5절 (C++ wrapper), 결정 1 ~ 13.

## 한 것

### `include/kiotty_net_websocket.h`

- include 를 `kiotty_net_statemachine.h` 에서 `kiotty_net_http.h` 로 (결정 2). `OwnedHandle` 의 가드 복사를 지웠다 (base.h 의 것).
- 추가: `KiottyNetWebSocketEndpoint` (불투명), `KiottyNetWebSocketHandle { owner, id }` (무효는 `{ NULL, 0 }`), `KiottyNet_addWebSocket(server, path, handler, app_context)`, `KiottyNet_broadcastWebSocket(server, path, opcode, data)`, `KiottyNetWebSocketClientResult`.
- 바뀜: `KiottyNet_createWebSocketClient(context, configuration, limits, path)` 가 `{ code, handle }` 을 돌려준다. run 은 그대로 `(client, address, tls, handler, app_context)`.
- 제거: `KiottyNetWebSocketConfiguration` 과 initialize / configure 여섯, `KiottyNet_WebSocketConfigurationBuilder`, `KiottyNetWebSocketServerHandle` 과 서버 함수 여섯, `KiottyNet_WebSocketServer` (결정 1, 11).
- C++: `KiottyNet_WebSocket` 에 `==`, `!=`, `<` (`std::less` 로 owner, 그다음 id). `KiottyNet_WebSocketClient` 는 `(context, configuration, limits*, path)` 생성자, `explicit operator bool`, `code()`.
- 상수 셋, enum 둘, Message / Close / Upgrade / Info / Handler 구조, 소켓 함수 다섯, 클라이언트 함수의 나머지는 그대로 (handle 타입만 바뀜).

`src/kiotty_net_websocket.cpp` 는 아직 비어 있다 (W3, W4).

### 테스트

`tests/unit/kiotty_test_headers*` 에 `KiottyNetWebSocketHandle` (크기, id offset), `KiottyNetWebSocketClientResult`, `KiottyNetWebSocketHandler` 의 C / C++ 크기 비교, `KiottyNet_WebSocketClient` 가 explicit bool 인 것, `KiottyNet_WebSocket` 의 순서 연산자. 지운 `KiottyNet_WebSocketServer` 의 trait 검사를 뺐다.

### `docs/requirement/user_guide_websocket.md`

판 v2 로 다시 썼다. 서버는 HttpServer 의 Upgrade 경로, 설정은 `KiottyNetHttpLimits` 한 벌, subprotocol 은 서버만, handle struct, 모든 송신은 push lane, 다른 thread 의 close 는 플래그 + close frame (상대가 답하지 않으면 최대 ping 간격 뒤에 끊긴다, 결정 9), 서버 / 클라이언트 예제 (C, C++), 안에서 도는 표, v1 과 달라진 점.

## 설계 문서에서 본 것 (고치지 않음)

- 설계 3.4 의 `runHttpServer(http, &address, tls, &http_handler, app_context)` 와 5절의 `server.run(address, events, &app)` 는 옛 모양이다. 지금 http.h 는 `KiottyNet_runHttpServer(server, address, tls)` 와 C++ `run(address)` / `run(address, tls)` 다 (http 결정 19). user guide 는 헤더를 따랐다.
- 설계 9절은 "결정 1 ~ 12" 라 적지만 결정 13 이 있다.
- websocket.h 의 `KIOTTY_NET_MAX_URL_PATH_LENGTH` 와 http.h 의 `KIOTTY_NET_MAX_URL_LENGTH` 가 둘 다 256 이다. `addWebSocket` 의 path 는 W3 에서 HTTP 라우트 표의 규칙 (`KIOTTY_NET_MAX_URL_LENGTH`) 을 따른다.
- 열린 것 둘 (W3 에서 정한다): onUpgrade 가 받는 handle 과 onOpen 이 받는 handle 이 같은지 (101 을 보낼 때 generation 을 올린다면 다르다), HttpServer close 뒤 run 의 반환값 (HTTP 와 같이 `CLOSED`).

## 매트릭스

W2 와 같이 돌렸다 (`261112_progress_websocket_w2.md`).

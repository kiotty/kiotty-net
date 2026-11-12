# 2026-11-12 진행 정리 : WebSocket 사이클 W3 (서버)

기준 문서: `docs/design/design_websocket_v0.html` 3.2, 6절, 7절, 10절의 W3 행. `docs/requirement/user_guide_websocket.md` 3 ~ 5절, 8절.

## 정한 것

- **onUpgrade 의 handle 과 onOpen 의 handle 은 다르다.** 설계 3.1 그대로 101 을 만들 때 generation 을 올린다. onUpgrade 의 handle 은 onUpgrade 안에서만 쓴다 (`findWebSocketUpgradeHeader`, `selectWebSocketProtocol`). onOpen 이 받는 handle 이 연결이 끝날 때까지 쓰는 것이다.
- **pong 이 늦으면 close frame 은 push lane 으로 간다.** TIMEOUT 행 안에서는 read 가 걸려 있어서 write lane 을 쓸 수 없다 (base 가 `BUSY` 로 거절한다). 그래서 1001 close frame 을 push lane 에 넣고 닫힘 플래그를 세운 뒤 `close_timeout` 동안 상대의 close frame 을 기다린다. 답이 없으면 `onClose(TIMEOUT, 1001)`, 답하면 `onClose(SUCCESS, 상대 status)`.
- **READ 행에서 우리가 닫을 때 (1002 / 1007 / 1009, 상대 close 에 대한 답) 는 write lane 이다.** close 를 하면 base 가 소켓을 바로 shutdown 하고 push lane 의 남은 것은 나가지 않는다. 그래서 close frame 을 Session 의 작은 버퍼 (127 바이트) 에서 write lane 으로 보내고 WRITTEN 뒤에 close 한다.
- **WebSocket 경로의 405 는 HTTP 라우트 표의 자동 응답과 같은 길이다.** POST 등은 표가 405 를 만들고 `Allow` 는 표의 규칙대로 `GET, HEAD, OPTIONS` 다. HEAD 는 Upgrade 검사에서 405 (`Allow: GET`) 다. 설계의 "GET 만" 과 Allow 목록이 조금 다르다 (고치지 않음).

## 만든 것

| 층 | 파일 | 하는 일 |
|---|---|---|
| repository/websocket | `kiotty_websocket_socket` | `WebSocketTarget` (handler, app_context, 경로 번호), `SocketLink` (다른 thread 가 보는 atomic 넷: socket id, connection id, 경로, 닫힘 status), `SocketState` (IO thread 의 frame 상태, 메시지 opcode, ping / close deadline, control payload, close frame 버퍼, 고른 subprotocol), `PushLane` 인터페이스 |
| repository/http | session, pool, registry, request store, response store | Session 에 `SocketState` / `SocketLink&` / `has_body`, pool 의 `links()`, registry 의 `addRoute(path, handler, app_context)` 와 `find(path)` (WebSocket 경로는 모든 메서드를 차지해 같은 path 의 라우트와 겹치면 `DUPLICATE_RULE`), `appendBody(data, key, offset)` (복사하며 unmask), `Persistence::UPGRADE` (`Connection: Upgrade`) |
| usecases/http | exchange, machine | `HttpState` 에 `SOCKET_OPENING`, `FRAME_START / LENGTH / MASK / PAYLOAD`, `SOCKET_CLOSING`. dispatch 가 `MatchKind::SOCKET` 을 `answerUpgrade` 로. 표에 행 열한 개, CLOSED 행이 `endSocket` |
| usecases/websocket | `kiotty_websocket_upgrade` | 405 / 400 (body) / 426 (`Upgrade: websocket`, `Sec-WebSocket-Version: 13`) / 400 (키) / 403, onUpgrade, 101 (`Sec-WebSocket-Accept`, 고른 `Sec-WebSocket-Protocol`) |
| | `kiotty_websocket_rows` | 101 송신 뒤 onOpen, frame 읽기 (mask 필수, 조각 순서, 길이 → `max_body_bytes` 넘으면 1009), payload 를 `receive_capacity` 단위로, control (ping → pong, pong → 타이머 해제, close → 답하거나 끝), 데이터 (TEXT 의 UTF-8, 닫는 중이면 버림, onMessage), TIMEOUT (ping / pong 늦음 / 닫는 중), onClose |
| | `kiotty_websocket_timing` | 다음 read 의 timeout (닫는 중이면 close deadline, ping 이 나갔으면 pong deadline, 아니면 ping 간격), onClose 의 `{ code, status, reason }` |
| | `kiotty_websocket_lane` | push lane 으로 frame 하나 (`pushFrame`), 닫힘 플래그 CAS + close frame (`pushClose`) |
| | `kiotty_websocket_access` | 공개 함수의 몸: info, 헤더, subprotocol, send, close, broadcast |
| context | `KiottyNetWebSocketEndpoint`, HttpServer | 소켓 함수 다섯의 가상 함수. HttpServer 가 이것과 `PushLane` 을 상속. `work()` 를 `site()` 로 바꾸고 C API 가 `HttpWork(site)` 를 만든다 (공개 메서드 7 개 규칙) |
| base | Endpoint, ConnectionAccess, Server / EventClient / StreamClient | 여러 조각 notify (`notify(id, Span<const KiottyNetSpan>)`). desk 에 있던 것을 위로 연결 |
| C API | `src/kiotty_net_websocket.cpp` | 서버 쪽 일곱 (`getWebSocketInfo`, `findWebSocketUpgradeHeader`, `selectWebSocketProtocol`, `sendWebSocket`, `closeWebSocket`, `addWebSocket`, `broadcastWebSocket`). 클라이언트는 W4 |

## 테스트

`tests/integration/kiotty_test_websocket_server.cpp` 10 개 (raw socket 클라이언트가 mask 한 frame 을 보낸다):

- 텍스트 / 바이너리 echo, 조각 둘 사이의 ping 에 pong 먼저, 12000 바이트 (126 길이, `receive_capacity` 4096 조각 읽기), 클라이언트 close 에 답하고 `onClose(SUCCESS, 1000, "done")`.
- 조각 합 1200 > `max_body_bytes` 1024 → 1009, `REQUEST_TOO_LARGE`.
- mask 없는 frame → 1002, 깨진 UTF-8 → 1007, 앞 조각 없는 continuation → 1002.
- ping 100 ms / pong 300 ms: ping, pong, ping, 답 없음 → close 1001, `onClose(TIMEOUT, 1001)`.
- 다른 thread 의 close (4000, "bye"): 두 번째 close `BUSY`, 그 뒤 send `CLOSED`, 1005 `INVALID_ARGUMENT`, 그 사이 온 데이터는 버림, 상대 답 → `SUCCESS`, 닫힌 뒤 send `PEER_CLOSED`.
- onMessage 안의 close → close frame, 답 없음 → close_timeout 200 ms 뒤 `TIMEOUT`.
- 405 (POST, HEAD), 426 (헤더 없음, Version 8), 400 (키, body), 403 (onUpgrade 0, `Connection: close`), 허용.
- subprotocol: 후보에 없는 값 `INVALID_ARGUMENT`, `mqtt` 선택 → 101 헤더, onOpen 의 info, onOpen 안의 select `BUSY`.
- HTTP 라우트 + `/chat` + `/telemetry` 한 서버, 경로별 broadcast, NULL 은 전부, 모르는 경로 `INVALID_ARGUMENT`, 메시지가 경로의 context 로.
- 등록 규칙 (같은 path, 같은 path 의 라우트, onMessage NULL, 상대 경로, NULL 서버, run 뒤 `BUSY`), `{ NULL, 0 }` handle, 빈 data, 서버 close → `onClose(CLOSED, 1001)`, onClose 안의 send `PEER_CLOSED`.

외부 클라이언트 (Python websockets) 확인은 못 했다. WSL 에 `websockets` 와 pip 가 없다.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 654 / 654 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 655 / 655 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 655 / 655, 655 / 655 |
| WSL clang Release TLS ON | 654 / 654 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 남은 것

- 설계 6 / 7절, user guide 5 / 8절에 "pong 늦음은 push lane close + close_timeout", "우리가 닫는 close frame 은 write lane" 을 적는다 (W5).
- send 의 TEXT 는 UTF-8 을 검사하지 않는다 (user guide 표대로). 상대가 1007 로 닫는다.
- W4 (클라이언트): 같은 행에 역할 값 (mask 를 씌우고, 받은 frame 의 mask 는 1002).

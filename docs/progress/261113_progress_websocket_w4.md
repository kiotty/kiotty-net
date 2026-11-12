# 2026-11-13 진행 정리 : WebSocket 사이클 W4 (클라이언트)

기준 문서: `docs/design/design_websocket_v0.html` 3.3, 6절 (WebSocketClient 의 연결 하나), 10절의 W4 행. `docs/requirement/user_guide_websocket.md` 7절.

## 정한 것

- **mask 는 클라이언트의 push lane 안에서 씌운다.** base 의 NotificationDesk 는 조각을 그대로 복사한다. 그래서 `WebSocketClient` 가 `push_capacity` 크기의 scratch 버퍼 (initialize 때 한 번 잡는다) 를 mutex 하나로 지키며 masked 머리 + XOR 한 payload 를 만들고 `notify` 한 조각으로 넣는다. base 는 바뀌지 않는다. `PushLane::push` 가 frame 하나 (opcode, payload) 를 받도록 바꾸어 서버는 평문 머리 + 조각 둘, 클라이언트는 mask 한 한 조각으로 갈린다.
- **mask key** 는 splitmix64 (seed 는 `std::random_device`) 로 frame 마다 새로. 우리가 write lane 으로 보내는 close frame (1002 등) 은 시계에서 만든 key 다.
- **Sec-WebSocket-Key** 는 run 마다 `std::random_device` 16 바이트의 base64.
- **Host 헤더** 는 run 의 address 의 host (IPv6 literal 이면 `[...]`) 에, ws 의 80 / wss 의 443 이 아니면 `:port`. SNI 는 base 그대로 address 의 host (설계 결정 6).
- **Session generation 은 연결마다 올린다** (서버의 beginExchange 와 같이). 처음 연결에서 generation 0 이면 handle id 가 0 (무효) 이 되었다.
- **역할 값** 은 Session 이 아니라 `HttpSite::role` (서버 / 클라이언트) 이다. 사이트 하나가 한 역할이다.
- **await** 는 onOpen 이 나가면 `SUCCESS`, 그 전에 run 이 끝나면 run 의 반환값. 끝난 run 의 결과는 다음 run 이 시작할 때까지 남아 있으므로, 같은 클라이언트로 다시 run 한 직후의 await 는 앞 run 의 결과를 볼 수 있다 (test 는 onOpen 횟수로 기다린다).
- **run 의 반환값**: 핸드셰이크 실패 (101 아님, Accept 틀림, 요청하지 않은 `Sec-WebSocket-Protocol`) 는 `PROTOCOL_ERROR`, 101 이 `header_timeout` 안에 없으면 `TIMEOUT`, `close` 는 `CLOSED`, 그 밖에 끊기면 `PEER_CLOSED` (close handshake 를 마친 정상 종료도 이것이다).
- `Context` 의 공개 메서드 7 개 규칙 때문에 `createHttpServer` 를 `create(HttpServerConfiguration)` 로 바꾸고 `create(WebSocketClientConfiguration)` 을 오버로드로 더했다.

## 만든 것

| 층 | 파일 | 하는 일 |
|---|---|---|
| repository/websocket | `kiotty_websocket_socket` | `SocketRole`, `ClientUpgrade` (target, 요청 텍스트, 기대하는 Accept, onOpen 을 알리는 Parker, 핸드셰이크 실패 코드), close frame 버퍼에 mask key 자리 |
| usecases/websocket | `kiotty_websocket_client_machine` | CONNECTED → Upgrade 요청 write → 상태줄 (`HTTP/1.1 101`) → 헤더 줄 (`max_header_bytes`) → `isUpgradeResponse` → `openSocket`. 그 뒤는 서버와 같은 frame 행 |
| | `kiotty_websocket_upgrade` | `isUpgradeResponse` (Upgrade, Connection, Accept 같음, Protocol 없음) |
| | `kiotty_websocket_rows` | `openSocket` 공개, 받은 frame 의 mask 검사가 역할을 따른다, 클라이언트의 close frame 은 mask |
| context | `kiotty_websocket_client` | `WebSocketClient : EventClient, PushLane, KiottyNetWebSocketEndpoint`. initialize (limits 검사, Session 하나), run (`ClientRun` 하나로 인자 넷), await, socket, push (mask), 소켓 함수 다섯 |
| context | Context, handles | `KiottyNetWebSocketClient`, 풀, release 의 close / join |
| C API | `src/kiotty_net_websocket.cpp` | create (path 는 `/` 로 시작하는 보이는 글자, limits NULL 이면 기본값), destroy, run, close, await, getWebSocketClientSocket, sendWebSocketClient |

## 테스트

`tests/integration/kiotty_test_websocket_client.cpp` 7 개:

- W3 서버와 왕복: 연결 전 socket `{ NULL, 0 }` / send `PEER_CLOSED`, await `SUCCESS`, info 의 port, 텍스트와 12000 바이트 echo, 클라이언트의 close 1000 "bye" → 양쪽 onClose, run `PEER_CLOSED`.
- 한 클라이언트가 ws 뒤 wss (CA 로 검증, SNI 127.0.0.1): close → `CLOSED`, `onClose(CLOSED, 1001)`.
- 가짜 서버로 요청 모양 (`GET /chat?room=1`, Host `127.0.0.1:port`, Upgrade, Connection, Version 13, 24 글자 키, Protocol 없음) 과 거절 셋 (200, 틀린 Accept, Protocol 있음) → run 과 await 모두 `PROTOCOL_ERROR`, onOpen / onClose 없음.
- 가짜 서버로 mask: 보낸 TEXT 의 MASK 비트와 unmask 결과, mask 된 서버 frame → mask 된 close 1002, `onClose(PROTOCOL_ERROR, 1002)`.
- 101 이 오지 않음 (`header_timeout` 200 ms) → `TIMEOUT`.
- 서버가 300 ms 늦게 뜸 → 재시도 뒤 왕복.
- create 의 잘못된 path (상대, 공백), NULL 설정, run 의 handler 없음 / onMessage 없음, run 전 await `TIMEOUT`.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 661 / 661 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 662 / 662 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 662 / 662, 662 / 662 |
| WSL clang Release TLS ON | 661 / 661 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 다음

W5 (문서): requirement 10 / 11절, 구현 가이드 5절, 테스트 계획 18절, user guide / 설계에 W3 / W4 에서 정한 것 (pong 늦음의 push lane close, 우리가 닫는 close frame 의 write lane, 클라이언트 mask 의 scratch, await 의 의미, run 의 반환값).

# 2026-11-13 진행 정리 : WebSocket 사이클 W5 (문서)

기준 문서: `docs/design/design_websocket_v0.html` 10절의 W5 행. W3 / W4 의 progress 에 "W5 에 적는다" 로 남긴 것.

## 한 것

| 문서 | 바꾼 것 |
|---|---|
| `requirement_v0.md` 10절 | "WebSocket / WebSocketServer" 를 "WebSocket (HttpServer 의 Upgrade 경로)" 로 다시 썼다. 판 v2 의 타입과 함수, 등록 규칙, Upgrade 요청의 갈래 표 (204 / 405 / 400 / 426 / 403 / 101), 101 뒤 handle 이 바뀌는 것, 수신 규칙과 닫는 status, ping / pong, 송신의 두 길 (push lane / write lane), close, `onClose` 의 표, broadcast, subprotocol, 타이머의 0 |
| `requirement_v0.md` 11절 | 제목을 "HttpClient (초안) / WebSocketClient" 로. WebSocketClient 를 W4 의 구현으로 (create 의 Result 와 path 규칙, run, Upgrade 요청 모양, Host / SNI, 101 검사, mask, await, close, run 의 반환값 표). 열린 질문 1 과 미결 셋 (Work, 소유자, WebSocketServer) 을 닫았다 |
| `requirement_v0.md` 앞쪽 | 규약의 소유 handle 목록에서 WebSocketServer 를 빼고, WebSocket handle 을 `{ owner, id }` 로, create 의 예외 문장 (HttpServer / WebSocketClient 는 아직 handle 만) 을 Result 로, push lane 설명의 "WebSocket control frame" 을 송신 frame 전부로, 7절 수명 표의 WebSocketServer 를 지웠다 |
| `implementation_guide_v0.md` | 5절 "WebSocket : HttpSession 위의 frame 행" 을 새로 (5.1 ~ 5.9). 옛 5절 미결은 6절이 되고 닫힌 것 둘을 더했다 |
| `test_base_v0.html` | 20절 "케이스 : WebSocket (W1 ~ W4)". 설계는 18절이라 했지만 17 ~ 19절이 차 있다 |
| `user_guide_websocket.md` | onUpgrade / onOpen 의 handle 이 다른 것, 송신의 두 길, 타이머 0, Upgrade 갈래 표 (OPTIONS 204, HEAD 405, 키 400, 426 의 Version 헤더), onClose 의 TIMEOUT 행 (pong 늦음은 close frame 뒤 close_timeout), onMessage 안의 close, 클라이언트의 Host / mask / await / run 반환값 / 정상 종료, 8절 표를 실제 행 (SocketClosing, 잘못된 frame, TIMEOUT 의 셋) 으로 |
| `design_websocket_v0.html` | 6절 끝에 "구현에서 정한 것 (W3)", "(W4)". 3.4 / 5절의 옛 `runHttpServer` / `run(address, events, &app)` 모양, 9절의 "결정 1 ~ 12", 7절 note 의 "9절의 정할 것 1", 10절 W5 행 |

코드는 바꾸지 않았다. 그래서 빌드와 ctest 는 돌리지 않았다.

## 문서를 쓰며 본 것 (고치지 않음)

- **write lane 의 close frame 이 push lane 의 앞선 frame 보다 먼저 나갈 수 있다.** base 는 한 WRITE 에 send Stage 다음 notify Stage 를 고른다 (구현 가이드 1.6, B1). frame 행이 write lane 으로 close frame 을 낼 때 push lane 에 앞서 넣은 데이터 frame 이 아직 나가지 않았으면, 같은 WRITE 에서 close frame 뒤에 그 데이터 frame 이 나간다. RFC 6455 5.5.1 은 close frame 을 보낸 뒤 데이터 frame 을 보내지 않아야 한다고 적는다. 송신 규칙으로 본 것이고 테스트로 재현하지는 않았다. 고치는 길은 close frame 을 낼 때 push lane 이 빌 때까지 기다리거나, 그 상태에서 notify 를 고르지 않게 하는 것이다.
- **write lane close frame 의 mask key 는 시계에서 온다** (`closeMaskOf`). RFC 6455 10.3 은 상대가 key 를 예측할 수 없어야 한다고 적는다. push lane 의 key (splitmix64, random seed) 와 달리 단조 시계 × 상수라 예측할 수 있다. 행이 클라이언트의 mask 상태를 보지 못해서 이렇게 됐다 (`PushLane` 에 key 를 묻는 함수를 두면 된다).
- pong timeout 0 은 "pong 을 검사하지 않음" 이 된다 (`ping_deadline` 이 0 으로 남는다). 문서에 이 뜻으로 적었다. 의도와 다르면 configure 가 0 을 거절해야 한다.
- 상대 close 에 답하는 close frame 은 status 만 돌려보내고 reason 은 붙이지 않는다. 닫는 중에 온 ping 에는 pong 을 보내지 않는다. 둘 다 RFC 가 허락하는 것이고 문서에 적었다.
- `KIOTTY_NET_MAX_URL_PATH_LENGTH` 는 헤더에 남아 있지만 쓰는 곳이 없다 (W1 에 적은 것). requirement 에 "지금은 쓰지 않는다" 로 적었다.
- `send` 의 TEXT 는 UTF-8 을 검사하지 않는다 (W3 에 적은 것). 상대가 1007 로 닫는다.
- 클라이언트 run 이 이미 돌고 있을 때는 `BUSY` 가 아니라 `INVALID_ARGUMENT` 다 (requirement 7절의 base 규칙과 같다).

## 다음

설계 10절의 WebSocket 행은 이것으로 끝이다. 위의 첫 둘 (close frame 의 순서, close frame 의 mask key) 을 고칠지, HTTP H6 에 남긴 어긋난 곳을 정리할지, 다음 판 (HttpClient 등) 으로 갈지.

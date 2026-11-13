# 2026-11-13 진행 정리 : base 사이클 B3 (보내고 닫기, thenCloseWith)

기준 문서: `docs/design/design_websocket_v1.html` 결정 14, 11절의 B3 행. W5 progress 의 "write lane 의 close frame 이 push lane 의 앞선 frame 보다 먼저 나갈 수 있다".

## 왜

WebSocket 의 frame 행이 1002 / 1007 / 1009 나 상대 close 의 답을 `thenWrite` 로 보내고 WRITTEN 뒤에 닫았다. base 의 WRITE 는 send Stage 다음 notify Stage 를 한 번에 고르므로 (B1), push lane 에 남은 데이터 frame 이 close frame 뒤에 붙어 나갈 수 있었다 (RFC 6455 5.5.1 위반). 사용자 제안으로 "보내고 닫기" 를 base 의 action 하나로 만들었다.

## 정한 것

- **`KiottyNet_thenCloseWith(data, reason)`.** data 를 write lane 으로만 보낸 뒤 reason 으로 닫는다. push lane 의 남은 것은 붙이지 않고 버린다. onWritten 은 오지 않고 CLOSED 가 마지막 한 번 온다. data 가 비면 `thenClose` 와 같다.
- **IoActionKind 를 늘리지 않았다.** CLOSE 에 `is_after_send` 를 둔다 (`IoAction::closeAfterSend`). backend 넷의 switch 는 그대로다.
- **Connection 의 `Phase::FLUSHING`.** `isAlive` 가 아니라서 notify / broadcast 는 `PEER_CLOSED` 로 거절된다. 새로 둔 `isOpen` (alive 또는 FLUSHING) 이라서 dispatch, close, `SendFlow::pick` 은 돈다. `SendFlow::select` 는 FLUSHING 에서 notify Stage 를 고르지 않는다.
- **FLUSHING 의 완료** 는 `Connection::onFlushing` 이 받는다. SEND 는 release 뒤 `SendFlow::isDrained` (send Stage 와 TLS cipher 가 비었다) 면 `close(reason)`. RECEIVE 는 실패면 닫고 아니면 버린다. SEND_TIMEOUT 은 `PEER_NOT_READING`. TIMEOUT 은 deadline 을 지웠으므로 오지 않는다.
- **TIMEOUT 행에서 read 가 걸린 채 돌려준 CLOSE** (`Connection::extend`) 도 `caller.admit` 을 지나게 해서 thenCloseWith 를 받는다.
- **HTTP.** `Connection: close` 응답의 마지막 조각을 `thenCloseWith(piece, close_reason)` 으로 보내 `CLOSING` 으로 간다 (`sendPiece`, `isLastPiece`). 마지막 WRITTEN 한 번이 줄었다. 그래서 `SENDING` 으로 가던 READ 행 여덟 (요청줄, 헤더줄, body, chunk 셋, trailer, Work 중 요청줄) 의 다음 상태에 `CLOSING` 을 더했다.
- **WebSocket.** `writeClose` 가 `thenCloseWith(close_frame, code)` 로 `CLOSING` 에 간다. `HttpState::SOCKET_CLOSING`, 서버 / 클라이언트 표의 WRITTEN 행, `onCloseWritten`, `SocketState::close_code` / `outgoing_length` 를 지웠다. close frame 버퍼 (`outgoing`, 131 바이트) 는 남는다 (action 이 Connection 에 복사되기 전까지 살아 있어야 한다).

## 달라진 동작

- WebSocket close frame 의 송신 한도가 `websocket_close_timeout_milliseconds` 에서 base 의 `send_timeout_milliseconds` (기본 30000) 로 바뀌었다. 지나면 끊김 reason 이 `PEER_NOT_READING` 이다.
- 닫기로 정한 순간 push lane 에 남아 있던 데이터는 나가지 않는다 (사용자 결정. 닫는 중이므로 버린다).

## 테스트

- `BaseApiTest.CloseWithSendsOnlyItsDataThenClosesWithItsReason`: `thenCloseWith` 의 필드, onReady 에서 push lane 에 "late" 를 넣고 `thenCloseWith("bye!", PROTOCOL_ERROR)` → 상대는 "bye!" 만 받고 EOF, onWritten 없음, 끊김 reason `PROTOCOL_ERROR`.
- HTTP 의 close 응답, WebSocket 의 1002 / 1007 / 1009 와 close 답의 기존 테스트가 바뀐 길로 그대로 지난다 (테스트 계획 21절).
- 하지 않은 것: TLS 위의 thenCloseWith 를 따로 재현하지 않았다. push lane 에 데이터가 남은 채 WebSocket 이 1002 로 닫히는 장면도 따로 만들지 않았다.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 662 / 662 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 663 / 663 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 663 / 663, 663 / 663 |
| WSL clang Release TLS ON | 662 / 662 |

줄 길이를 고친 뒤 MSVC, WSL gcc, clang TLS 를 다시 돌려 같은 결과다. `tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 문서

requirement 5절 (action 일곱 가지, 거부 규칙), 구현 가이드 1.6 (보내고 닫기), 5.1 / 5.4 / 5.6 / 5.7, user guide WebSocket 8절, 테스트 계획 21절, 설계 v1 의 결정 14 / 15 와 B3 사이클.

## 다음

V1: HTTP 의 app_context 를 run 한 번으로 (설계 v1 3절).

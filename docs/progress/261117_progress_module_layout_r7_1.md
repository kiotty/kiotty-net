# 2026-11-17 진행 정리 : 모듈 정리 R7-1 UpgradeSession

기준 문서: `docs/design/design_http_websocket_boundary_v0.html` (결정: 넘기는 값은 `UpgradeSession`, 101 은 http 가 보냄, message 버퍼는 http 요청 body 버퍼를 빌림, 클라이언트는 자기 state). 계획: `docs/plan/plan_base_261116.md`. 동작 바뀜 없음.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `HttpUpgradeHandler::answer / step / end` 가 `HttpSession&` 를 받았다 | `UpgradeSession` 값 (요청 view, 응답 store, message 버퍼, limits, receive_capacity, app_context, id, has_body) 을 받는다. http 가 부를 때마다 `upgradeSessionOf(session)` 로 만든다 |
| websocket 의 answer 가 persistence / phase 를 쓰고 `advanceSession`, `startSending` 을 불렀다 | answer 는 응답 store 에 101 이나 거절을 쓰고 `UpgradeAnswer { ACCEPT, REFUSE, DROP }` 을 돌려준다. http 의 `dispatchUpgrade` 가 phase UPGRADING, persistence, generation, 보내기를 한다 |
| websocket 의 `sendOpening` 이 `pieceAt` 과 `session.state.sent` 로 101 을 보냈다 | http 의 `stepUpgraded` 가 보내고, 다 보냈으면 phase SOCKET 뒤 새 `open(upgrade)` 를 부른다 |
| message 조각을 `session.request.appendBody` 로 모았다 | `upgrade.message` (같은 body 버퍼) 에 `SocketState::message_length` 만큼 모은다. 상한은 버퍼 길이 (= `max_body_bytes`) |
| frame / message / control / timing / handshake 의 함수 스무 개 남짓이 `HttpSession&` 를 받았다 | `(WebSocketHandler&, const UpgradeSession&)`. 이 폴더들은 http 의 session / exchange 를 include 하지 않는다 (`closeWith` 선언을 새 `machine/kiotty_http_close.h` 로. 상태 이름 상수가 있는 `kiotty_http_rule_names.h` 에 두면 gcc 의 `-Wshadow` 가 `SocketCode::CLOSED` / `TIMEOUT` 에 걸린다) |

- 새 파일: `http/data/repository/upgrade/kiotty_http_upgrade_flow.{h,cpp}`. `HttpRequestStore::bodyArea()` (공개 7).
- 클라이언트 기계와 `WebSocketAccess` 는 아직 http session 위에 있다 (R7-2, R7-3). 그 둘은 `upgradeSessionOf` 로 값을 만들어 websocket 함수를 부른다. 클라이언트는 열 때 요청 저장소를 비우고 phase SOCKET 을 직접 적는다 (`openClientSocket`).
- 구현 가이드 5절의 표, answer 흐름, `openSocket` 조각, `readFramePiece` 설명. 설계 문서의 R7-1 note, 계획의 R7 행.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로).

## 다음

R7-2 access: send / close / info / header / select 가 http 의 session 풀 대신 websocket 의 `SocketLink` / `SocketState` 를 본다.

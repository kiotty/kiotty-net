# 2026-11-17 진행 정리 : 모듈 정리 R7-4 WebSocketHandler 의 공개 필드

기준 문서: `docs/design/design_http_websocket_boundary_v0.html` 4절. 계획: `docs/plan/plan_base_261116.md`. 동작 바뀜 없음.

## 바뀐 것

| 전 (공개 필드) | 뒤 |
|---|---|
| `owner`, `lane`, `role` | 값 `SocketSide { owner, lane, client, role }` 하나. `initialize(sockets, routes, side)` 가 받아 private 에 두고 `side()` 로 읽는다 |
| `client` (`ClientUpgrade`) | `WebSocketClientState::upgrade`. handler 는 `side().client` 포인터로 본다 (서버는 NULL) |
| `mask_state` | `ClientUpgrade::mask_state` (클라이언트만 mask 를 쓴다) |

- 공유 단계 (opening, message, control) 는 `handler.role == CLIENT` 대신 `handler.side().client != nullptr` 로 클라이언트를 가르고 그 포인터로 콜백을 부른다. frame 머리 검사와 close frame 의 mask 여부는 `side().role`.
- 공개 메서드 7 을 지키려고 `linkAt` 를 지웠다. `linkOf(handler, upgrade)` 는 `links()[index]`.
- 구현 가이드 5절의 `openSocket` / `callClose` 조각, mask key, 파일 표. 설계 문서의 R7-4 note.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로).

## R7 정리

R7-1 `UpgradeSession` (`4828405`), R7-2 access (`1c2e99e`), R7-3 클라이언트 자기 state (`cd92ede`), R7-4 handler 공개 필드. websocket 이 http 의 session / host / 풀을 쓰는 곳은 `WebSocketServer : HttpServer` (R3 의 상속) 가 access 와 broadcast 에 `host().registry` / `host().push_capacity` 를 넘기는 곳만 남았다.

## 다음

계획의 다음 사이클은 U2 (HttpClient v1 의 연결과 취소).

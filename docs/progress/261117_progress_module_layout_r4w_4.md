# 2026-11-17 진행 정리 : 모듈 정리 R4w-4 HttpSessionState

기준 문서: `docs/design/design_module_layout_v0.html` 5.1절 (R4 행의 `HttpSessionState`). 계획: `docs/plan/plan_base_261116.md`. 동작 바뀜 없음.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `HttpSession` 이 요청 하나의 진행 칸 아홉 (`framing`, `body`, `header_deadline`, `sent`, `close_reason`, `stream_failure`, `phase`, `persistence`, `is_work_requested`) 을 바로 가졌다 | entity `http/domain/entity/kiotty_http_session_state.h` 의 값 `HttpSessionState` 하나. `session.state.phase` 처럼 읽는다 |
| `beginExchange` 가 아홉 칸을 한 줄씩 되돌렸다 | `session.state = idleSessionState()` 한 번 (`state/kiotty_http_session`). 생성자도 같은 값으로 시작한다 |
| `SessionPhase` 가 `state/kiotty_http_session.h` 에 있었다 | entity 로 |

- `HttpSession` 에 남는 것: store 둘 (request, response), 빌린 것 (host, file, published), 연결과 요청에 밖에서 받은 것 (info, route, stream), id, worker 와의 소유 (is_worker_owned, is_orphaned). 소유 플래그는 교환이 아니라 session 의 수명에 걸린 것이라 state 에 넣지 않았다.
- `header_deadline` 은 전에는 `beginExchange` 가 되돌리지 않았다. 이제 0 으로 되돌리지만 `startHeaderClock` 이 언제나 `headerTimeLeft` 보다 먼저 불리므로 같다.
- 구현 가이드 4.2 의 struct 와 `beginExchange` 조각.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로).

## R4w 정리

R4w-1 자리 옮기기 (`b1bb486`), R4w-2 frame 읽기를 값으로 (`ad3e91b`), R4w-3 `HttpSessionHost` (`69f2c9d`), R4w-4 `HttpSessionState`. websocket 이 http 의 session / host 를 include 하지 않는 것과 클라이언트가 host 를 통째로 빌리지 않는 것은 R7 로.

## 다음

R5 client State.

# 2026-11-16 진행 정리 : 모듈 정리 R4b-4 보내기

기준 문서: `docs/design/design_module_layout_v0.html` 6절. 계획: `docs/plan/plan_base_261116.md`.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `sendPiece(session, chunk)` 가 `session.sent`, `persistence` 를 읽고 `isLastPiece(session, end)` 로 응답을 다시 꺼내 마지막 조각인지 보았다 | `SendStep sendStepOf(const ResponseFrame&, size_t sent, Span chunk, Persistence)` (`response/kiotty_http_send_rules`) 가 `WRITE / FAREWELL / DONE` 을 고른다. `sendPiece(session, step)` 는 고른 대로 보낸다 |
| `sendNext` 와 work 의 `nextExchangeOf` 가 각자 `persistence == KEEP_ALIVE` 를 보았다 | `AfterResponse afterResponseOf(Persistence)` 하나 (`NEXT_EXCHANGE / CLOSE`) |
| 기계 (`startSending`) 와 work (`sealFor`) 가 같은 seal 두 줄을 따로 가졌다 | `sealResponse(session)` (`machine/kiotty_http_exchange`) 하나 |

- 새 값 `http/domain/entity/kiotty_http_send_step.h`: `SendStep { chunk, kind }`, `SendKind`, `AfterResponse`.
- 흐름: `ResponseFrame + sent + chunk -> SendStep -> 동작`, `Persistence -> AfterResponse -> 다음 교환 또는 닫기`.
- `session.sent` 는 보낸 길이를 세는 상태로 남는다 (`onSent` 가 더한다). websocket 의 101 보내기 (`onSocketWritten`) 는 R4w 에서 본다.

## R4b 정리

R4b-1 `RequestFraming`, R4b-2 `BodyPlan`, R4b-3 `Answer` / `RouteOutcome`, R4b-4 `SendStep` / `AfterResponse`. 기계의 갈래는 값을 고르는 규칙 (session 을 받지 않는다) 과 그 값을 session 에 한 번 쓰는 곳으로 나뉜다. 남은 숨은 쓰기는 C ABI 를 거치는 앱의 응답 쓰기와 `is_work_requested` (앱이 쓰는 길) 뿐이다.

## 확인

동작 바뀜 없음. MSVC 847, WSL 848 × 6, release-tls 847. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3 (그대로).

## 다음

R4w websocket.

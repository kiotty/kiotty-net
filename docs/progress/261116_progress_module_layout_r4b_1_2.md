# 2026-11-16 진행 정리 : 모듈 정리 R4b-1, R4b-2 (요청 머리와 body 를 값으로)

기준 문서: `docs/design/design_module_layout_v0.html` 6절 (값으로 잇는 흐름). 동작 바뀜 없음.

## R4b-1 요청 머리

| 전 | 후 |
|---|---|
| `finishHeaders()` 가 `BodyFraming` 만 돌려주고 `keep_alive` 를 몰래 썼다. machine 이 그 `keep_alive` 를 다시 읽어 `session.persistence` 를 만들고 `has_body` 를 따로 계산했다 | `RequestFramingResult finishHeaders()` → `RequestFraming { BodyFraming body; Persistence persistence; bool expects_continue; }` (entity `kiotty_http_request_framing.h`, 새). store 는 앱에 보이는 `request.keep_alive` 를 자기 기록으로 계속 쓴다 |
| `HttpRequestStore::expectsContinue()` 가 body 를 읽기 전에 헤더를 다시 훑었다 | 지움. `RequestFraming::expects_continue` 를 쓴다 |
| `session.has_body` (machine 이 쓰고 websocket upgrade 가 읽음) | 지움. `session.framing` 에 값 하나로 두고 `hasBody(const BodyFraming&)` (body/message_framing_parser) 로 묻는다 |

`afterHeaders` 의 흐름: `RequestFramingResult framing = request.finishHeaders()` → `session.framing = framing.value` (한 번) → `framing.value.body.kind` 로 갈래.

## R4b-2 body

| 전 | 후 |
|---|---|
| `session.body_remaining` 을 `afterLength`, `onChunkSize`, `takeBody` 셋이 쓰고 `readBody`, `readFirstBody` 둘이 읽었다. 크기 검사가 `afterLength` (`> max_body_bytes`) 와 `onChunkSize` (`> room`) 에 따로 있었다 | `BodyPlan { remaining }` (entity `kiotty_http_body_plan.h`, 새) 과 규칙 (`body/kiotty_http_body_plan_rules.*`, 새): `BodyPlanResult planBody(length, room)` (넘으면 413), `BodyPlan takeBodyPiece(plan, n)`, `size_t nextReadOf(plan, capacity)`, `bool hasMoreBody(plan)`. upload 규칙과 같은 모양 (값을 받아 새 값) |

`takeBody` 의 흐름: `appendBody(piece)` → `session.body = takeBodyPiece(session.body, n)` → `hasMoreBody(session.body)` 로 갈래.

## 남은 숨은 쓰기 (R4b-3, R4b-4)

`session.persistence` (reject / answerWithoutWork / 보내기가 바꿈), `is_work_requested` (콜백 → C ABI → HttpWork), `response.seal`, `sent`, `close_reason`, `pieceAt` 의 파일 읽기.

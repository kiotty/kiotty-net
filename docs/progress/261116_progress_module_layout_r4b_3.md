# 2026-11-16 진행 정리 : 모듈 정리 R4b-3 답 고르기

기준 문서: `docs/design/design_module_layout_v0.html` 6절. 계획: `docs/plan/plan_base_261116.md`.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `answerAutomatically(session, match)` 가 match 를 보며 바로 응답에 썼다. 정적 경로는 `answerStatic` 이 디렉토리 경로를 붙이면서 403 / 404 / 파일을 그 자리에서 썼다 | `Answer answerOf(const RouteMatch&, Span path)` (`response/kiotty_http_answer_rules`) 가 값을 고르고 `writeAnswer(session, answer)` (`machine/kiotty_http_answer_writer`, 전 `kiotty_http_static` 을 `git mv`) 가 한 번 쓴다 |
| `afterRoute` 가 응답 frame, 요청 method, `is_work_requested` 를 읽으며 바로 갈래를 탔다 | 콜백이 끝난 뒤 `RouteOutcome { is_stream, is_head, is_work_requested }` 로 한 번 모으고 `AfterRoute afterRouteOf(const RouteOutcome&)` 가 `SEND / AWAIT_WORK / REFUSE_STREAM` 을 고른다 |

- 새 값 `http/domain/entity/kiotty_http_answer.h`: `Answer { location (FixedString<KIOTTY_NET_MAX_PATH_LENGTH>), memory, type, allowed, status, kind }`, `AnswerKind { NONE, STATUS, ALLOW, MEMORY, FILE }`, `RouteOutcome`, `AfterRoute`.
- 흐름: `RouteMatch -> Answer -> 응답`, `콜백 -> RouteOutcome -> AfterRoute`. 고르는 함수는 session 을 받지 않는다.
- 콜백이 C ABI handle 로 응답과 `is_work_requested` 를 쓰는 것은 그대로다 (앱이 쓰는 길). 기계는 콜백 뒤에 그것을 한 번만 읽는다.
- 404 (라우트 없음, upgrade 처리기 없음) 는 이제 `response.begin()` 뒤 상태를 쓴다. 콜백 전이라 응답이 비어 있으므로 결과는 같다.
- 단일 헤더는 번역 단위 하나라 익명 이름을 새로 지었다 (`ANSWER_FORBIDDEN`, `ALLOW_HEADER_NAME`, `write*Answer`).
- 구현 가이드의 `kiotty_http_static` 두 줄을 새 파일로.

## 확인

동작 바뀜 없음. MSVC 847, WSL 848 × 6, release-tls 847. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3 (그대로).

## 다음

R4b-4 보내기: `seal`, `sent`, keep-alive 판단을 `ResponseFrame` 과 `AfterResponse` 값으로.

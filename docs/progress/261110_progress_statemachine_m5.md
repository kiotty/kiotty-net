# 2026-11-10 진행 정리 : StateMachine 사이클 M5 (문서)

기준 문서: `docs/design/design_statemachine_v0.md` 13절의 M5. 코드 변경은 없다. M2 ~ M4 가 바꾼 것을 문서가 따라가게 하고 StateMachine 층을 닫는다.

## 고친 것

| 문서 | 내용 |
|---|---|
| `docs/deviations.md` | DV6 (machine state 의 getter / setter 쌍) 과 DV12 (`const char*` 가 내부 경계를 지남) 를 "닫힘" 으로. 닫은 모양 (Connection 의 private `int32_t` 와 `Caller` 의 `int32_t&`, `core/` 의 `FixedString` / `parseText` / `parseUnsigned` / `parseHexadecimal` 과 번역 층의 한 번 지나는 길) 을 상태 열에 적었다. DV13 은 "열림 (유지)" 그대로 |
| `docs/requirement/requirement_v0.md` 1절 | `ILLEGAL_TRANSITION` 주석의 "(debug)" 를 "release 빌드에서도 검사한다" 로. `SESSION_EXHAUSTED` 주석을 "max_clients 초과" 에서 "createConnectionContext 가 NULL 을 돌려줌" 으로 (max_clients 초과는 backlog 에서 기다리는 일이지 코드가 아니다) |
| 같은 문서 2절 | C++ 래퍼의 `valid()` 를 `explicit operator bool` 로 (판 v1) |
| 같은 문서 8절 | `onError` 규칙 한 줄 (SESSION_EXHAUSTED, UNHANDLED_EVENT, ILLEGAL_TRANSITION, WORK_STARTED 의 잘못된 action. NULL 이면 부르지 않는다). create 가 NULL 을 돌려주면 `onError(SESSION_EXHAUSTED)` 뒤 ignore. WORK_STARTED 의 잘못된 action 은 report 뒤 Stream 이 남고, 행이 없으면 UNHANDLED_EVENT 로 닫힘. CLOSED 행이 없어도 release 는 하고 report 는 없음. UNHANDLED_EVENT 와 ILLEGAL_TRANSITION 은 `onError` 뒤 `thenClose`, release 에서도. `KiottyNetRule` 은 `KiottyNetNextStates next` 하나, `KiottyNetStatefulAppContext` 에 `onError`, `KIOTTY_NET_RULE` 매크로는 헤더의 두 겹 중괄호 (C 에서만 정의), 예제 둘의 초기화에 다섯째 필드 |
| `docs/test/test_base_v0.html` | 16절 "케이스 : StateMachine". 16.1 core text (M2 의 FixedString 7, ParseText 11, ParseUnsigned 7, ParseHexadecimal 6 과 공개 API 쪽 셋), 16.2 Handler (M3 의 Connection 넷과 Endpoint 의 `runOn`), 16.3 RuleTable 8 + StatefulHandler 11, 16.4 공개 C API 의 Stateful 6 + `EchoRoundtripStateful`. 하지 않은 `try_compile` 검사를 끝에 |
| `docs/design/design_statemachine_v0.md` 13절 | M5 행을 끝으로 |
| `docs/plan/plan_base_v2.md` | StateMachine 층 끝, DV6 / DV12 닫힘, 다음은 HTTP 층의 재설계 (Streamable HTTP) |

구현 가이드 2절 (Handler 기반 StateMachine) 과 3절의 예제는 11-09 의 커밋 535de66 이 먼저 헤더에 맞췄다.

## 하지 않은 것

- `user_guide_http.md` / `user_guide_websocket.md`: M1 (103c260) 이 v1 이름으로 바꿨다. 두 문서가 쓰는 함수 가운데 헤더에 없는 것 (`KiottyNet_addWebSocketUpgrade`, `KiottyNet_startWebSocketWork`, `KiottyNet_finishWebSocketWork`, `KiottyNet_resumeWebSocket`, `KiottyNet_setWebSocketProtocol`, `KiottyNet_getWebSocketContext`) 과 `KiottyNetWebSocketHandle` 의 `int64_t` 모양은 HTTP / WebSocket 재설계 (`design_http_v0.html`, `design_websocket_v0.html`) 가 정한다. 그 전에 고치면 두 번 고친다.
- M4 의 남은 것 둘 (가이드 3절의 도우미, C++ 중복 행의 `try_compile`) 가운데 첫째는 535de66 이 가이드를 헤더에 맞춰 닫았다. 둘째는 그대로 남는다 (테스트 계획 16.4 끝).
- 테스트는 다시 돌리지 않았다. `src/`, `include/`, `tests/` 에 변경이 없다 (`git status` 가 `docs/` 만).

## 다음

HTTP 층의 재설계. MCP 의 Streamable HTTP (POST 에 대한 `text/event-stream` 응답, GET 으로 연 긴 SSE 스트림, 202 빈 응답) 가 되도록 `include/kiotty_net_http.h` 와 `kiotty_net_websocket.h` 를 공개 인터페이스부터 다시 짠다. base 의 두 모양 (Work 로 chunk 를 동기 write, Hold 뒤 notify 로 push) 을 HTTP 이름으로 드러내는 것이 골자이고 새 개념은 두지 않는다. 설계 문서는 `docs/design/design_http_v0.html` 과 `design_websocket_v0.html`.

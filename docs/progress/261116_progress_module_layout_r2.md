# 2026-11-16 진행 정리 : 모듈 정리 R2 (statemachine)

기준 문서: `docs/design/design_module_layout_v0.html` 3절, 정할 것 1 / 2 (repository, 앱의 배열을 가리킴).

## 바뀐 자리

| 층 | 파일 | 내용 |
|---|---|---|
| entity | `statemachine/domain/entity/kiotty_state_machine.h` (새) | `struct StateMachine { Span<const KiottyNetRule> rules; int32_t initial; }`. 앱의 규칙 배열을 가리키는 값. 복사 없음. 규칙 하나는 공개 구조체 `KiottyNetRule` 그대로 |
| repository | `statemachine/data/repository/kiotty_rule_table.*` (entity 에서 옮김) | `stateMachineOf(const KiottyNetStateMachine*) → StateMachine` (null 이면 빈 것), `findRule(const StateMachine&, state, event)`, `isAllowed`, `checkRules` |
| repository | `statemachine/data/repository/kiotty_stateful_handler.*` (entity 에서 옮김) | base 의 `Handler` 구현. `initialState` / `dispatch` 가 부를 때마다 `stateMachineOf(_stateful.machine)` 로 값을 만든다 (지금처럼 앱이 넘긴 구조체를 그때그때 읽는다) |
| usecases | `statemachine/domain/usecases/kiotty_statemachine_usecases.*` (새) | `runStatefulServer(Server&, address, tls, stateful)`, `runStatefulEventClient(EventClient&, ...)`: handler 를 만들어 run |
| adapter | `src/kiotty_net_statemachine.cpp` | 인자 검사와 주소 복사 뒤 usecases 를 부른다 |

http 서버와 websocket 클라이언트는 repository 의 `StatefulHandler` 를 그대로 쓴다 (include 경로만 바뀜).

검사 순서와 code 는 그대로다 (`Server::run` 안의 `handler.validate()` 가 `checkRules`).

처음에는 handler 가 생성 때 `StateMachine` 을 멤버로 들게 했는데 `StatefulHandlerTest.DispatchClosedWithoutARowStillReleases` 가 실패했다. 그 테스트는 handler 를 만든 뒤 `stateful.machine` 을 바꾼다. 지금까지의 동작 (부를 때마다 읽음) 을 지키려고 멤버를 두지 않았다.

## 테스트

`kiotty_test_rule_table.cpp` 의 `findRule` 세 곳이 `stateMachineOf(&machine)` 을 거친다. 개수는 그대로.

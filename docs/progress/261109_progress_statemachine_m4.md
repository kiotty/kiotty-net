# 2026-11-09 진행 정리 : StateMachine 사이클 M4 (StatefulHandler)

기준 문서: `docs/design/design_statemachine_v0.md` 4절, 6절, 10절, 13절의 M4. M3 의 `Handler` 에 두 번째 구현을 얹어 `KiottyNet_runStatefulServer / runStatefulEventClient` 를 연다.

## 더한 것

| 파일 | 내용 |
|---|---|
| `domain/usecases/statemachine/kiotty_rule_table.h / .cpp` | `findRule` (선형 탐색. 정확 일치가 먼저, 없으면 ANY 행, 둘 다 없으면 UNHANDLED_EVENT), `isAllowed` (`next.count` 0 이면 true), `checkRules` (4.3 의 표 그대로. NULL machine / rules, count 0, NULL action, 범위 밖 event, `next.count > KIOTTY_NET_MAX_NEXT`, `KIOTTY_NET_ANY_STATE` 보다 작은 state / next 는 INVALID_ARGUMENT, 같은 (state, event) 는 DUPLICATE_RULE. 중복은 O(n²)) |
| `domain/usecases/statemachine/kiotty_stateful_handler.h / .cpp` | `StatefulHandler final : Handler`. 멤버는 사용자 `KiottyNetStatefulAppContext` 참조 하나. `validate` = `checkRules`, `initialState` = `machine->initial`, `admit` 는 create 가 없으면 NULL Session 으로 받고 create 가 NULL 을 돌려주면 `report(SESSION_EXHAUSTED)` 뒤 ignore. `dispatch` 는 4.1 그대로: 행이 없으면 `report(UNHANDLED_EVENT)` + `thenClose`, `.to()` 밖이면 `report(ILLEGAL_TRANSITION)` + `thenClose` (release 빌드에서도. 14절 3), 아니면 `caller.machine_state` 를 옮기고 entry action. CLOSED 는 행이 있으면 action 을 부르고 Transition 은 버린다. 행이 없어도 release 는 한다 (UNHANDLED_EVENT 를 report 하지 않는 유일한 자리). `report` 는 `onError` 가 NULL 이면 부르지 않는다 |
| `src/kiotty_net_statemachine.cpp` | `KiottyNet_runStatefulServer / runStatefulEventClient`. NULL 을 거르고 stack 에 `StatefulHandler` 를 만들어 `run(handler)` (설계 6절) |

`StatefulHandler` 는 `context/kiotty_action.h` 를 쓰지 않는다 (domain 이 context 에 기대지 않게). HOLD / CLOSE action 과 accept / ignore decision 은 파일 안의 작은 함수로 만든다. `CallbackHandler` 와 같은 방식이다.

## 테스트

| 층 | 케이스 |
|---|---|
| unit `RuleTable` (8) | `FindRulePrefersExactStateOverAny`, `FindRuleFallsBackToAny`, `FindRuleWithoutARowIsUnhandledEvent`, `IsAllowedWithoutNextListIsTrue`, `CheckRulesRejectsNullMachineRulesAndZeroCount`, `CheckRulesRejectsDuplicateStateEvent`, `CheckRulesRejectsNullActionUnknownEventAndBadStates`, `CheckRulesAcceptsTheSortedCppTable` |
| unit `StatefulHandlerTest` (11). 실제 `Connection` (`ConnectionBench`) 에 StatefulHandler 를 넘긴다 | `ValidateChecksTheTableAndInitialStateIsTheMachineInitial`, `DispatchMovesStateAndReturnsEntryAction`, `DispatchClosedIgnoresTransitionAndReleases`, `DispatchClosedWithoutARowStillReleases`, `IllegalTransitionReportsAndCloses`, `UnhandledEventReportsAndCloses`, `AdmitWithoutCreateAcceptsNullSession`, `AdmitWithNullSessionReportsSessionExhaustedAndIgnores`, `OnErrorNullIsAllowed`, `WorkStartedReturningReadIsAppliedToTheStream`, `WorkStartedWithoutARowClosesTheStreamWithUnhandledEvent` |
| integration `StatefulTest` (6). 공개 C API 만 | `StatefulEchoRoundTrip` (구현 가이드 3절의 서버 표 + EventClient 표, 연결 둘), `StatefulEchoRoundTripInC` (서버 표가 C 파일 `kiotty_test_stateful_c.c` 의 `KIOTTY_NET_RULE` 표), `RunStatefulRejectsNullAndDuplicateRules` (C 표의 중복 → DUPLICATE_RULE, 서버와 EventClient 둘 다. 그 뒤 같은 서버가 정상 run), `RunStatefulClosesOnUnhandledTimeout`, `SessionNullIgnoresTheConnectionAndReportsOnError`, `StatefulWorkStartedRowHandsTheStreamToAWorker` (WORK_STARTED 행이 stream 을 Session 에 넣고 `thenWork()`, 테스트 thread 가 worker 로 읽고 `closeConnectionStream(thenWrite)`, `(Working, WRITTEN)` 행이 닫는다) |
| benchmark | `EchoRoundtripStateful` (callbacks 판 `EchoRoundtrip` 과 같은 일을 표 다섯 줄로) |

| 환경 | 결과 |
|---|---|
| MSVC TLS ON | 488 / 488 |
| MSVC TLS OFF | 488 / 488 |
| WSL gcc TLS ON | 489 / 489 (ring 테스트 skip). `StatefulTest.*` 20 회 반복 통과 |
| WSL clang ASan + UBSan TLS ON | 489 / 489. UBSan 보고는 googletest 안의 vptr 뿐 |
| WSL clang TSan TLS ON | 489 / 489. 경고 0 |
| WSL benchmark target | 빌드 |
| `check_rules.py src/implementation src/kiotty_net_statemachine.cpp` | LINE 0, MACRO 18, PUBLIC 2 (전과 같다) |

## 벤치마크 (WSL2, Release, TLS ON, io_uring)

같은 바이너리에서 번갈아 세 번 (`--benchmark_min_time=2s`) 의 평균. 차이는 잡음 안이다 (같은 쪽 회차 사이가 10% 안팎).

| 벤치 | callbacks | stateful |
|---|---|---|
| EchoRoundtrip clients 1, max 16 | 220 µs | 220 µs |
| EchoRoundtrip clients 1, max 1024 | 207 µs | 215 µs |
| EchoRoundtrip clients 128, max 1024 | 2.19 ms | 2.17 ms |

## 남은 것 (M5 문서로)

- 구현 가이드 3절의 echo 예제는 `KiottyNet_EntryAction::read(...)`, 인자 하나짜리 `moveToNextState(state)`, `createConnectionContext(app, handle)` 를 쓴다. 공개 헤더에 없거나 모양이 다르다 (헤더는 `KiottyNet_thenRead`, 인자 둘, `createConnectionContext(app, info)`). 테스트는 헤더대로 썼다. 가이드를 헤더에 맞추거나 헤더에 그 도우미를 더하는 것은 사용자가 정한다.
- 설계 10절의 C++ 중복 행 compile-fail 검사 (`try_compile`) 는 하지 않았다. `KiottyNet_rules` 의 중복은 지금도 정의 없는 `duplicate_rule_in_table` 을 부르게 되어, 표를 `constexpr` 로 두면 compile 이, 아니면 link 가 실패한다. 그 실패를 CI 가 확인하는 검사가 없을 뿐이다.

## 다음

M5: 문서 (deviations 의 DV6 / DV12 / DV13 닫힘, requirement 8절의 `onError` 와 ILLEGAL_TRANSITION 검사, 구현 가이드 2절, user_guide 의 이름, 테스트 계획 16절).

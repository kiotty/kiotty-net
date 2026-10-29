# 2026-10-29 진행 정리 : base 사이클 1 (base.h 와 statemachine.h)

기준 문서: `docs/design/design_base_v2.md` 3절, `docs/test/test_base_v0.html` 15절 ①, `docs/plan/plan_base_v0.md` 3절.

## 바뀐 것

- `include/kiotty_net_base.h`
  - `KiottyNetCode` 에 TLS 코드 8 개 (`KIOTTY_TLS_NOT_SUPPORTED` ~ `KIOTTY_TLS_RECORD_ERROR`).
  - Server / Client config 에 `bool no_delay`, `KiottyNet_configureServerNoDelay` / `KiottyNet_configureClientNoDelay`, Builder 의 `noDelay()`.
  - `KiottyNetActionType`, `KiottyNetConnectionAction`, action 함수 여섯 (`holdCommunication`, `readConnection`, `readConnectionUntil`, `writeConnection`, `startWork`, `closeConnection`). 전부 handle 없이 값을 돌려준다.
  - `KiottyNetConnectionDecision` + `acceptConnection` / `ignoreConnection`.
  - `KiottyNetConnectionHandler`: onConnected 는 `(app, info) → Decision`, onReady / onRead / onWritten / onTimeout 은 action 을 return, onWorkStarted 는 handle 없이 `(ctx, work)`, onError 추가.
  - `KiottyNet_finishWork(work, action)` 하나가 `finishWorkWith*` 넷을 대신한다.
  - `KiottyNetContextResult`, `KiottyNetServerResult`, `KiottyNetEventClientResult`, `KiottyNetStreamClientResult` 와 그것을 돌려주는 create 넷.
  - 지운 것: `setConnectionTimeout`, handle 받는 action 함수, `startWork(handle)`, `finishWorkWith*` 넷.
- C++ 래퍼
  - `KiottyNet_Connection` 은 `handle()`, `info()`, `notify()` 만 남았다.
  - `KiottyNet_Work` 는 `connected()` 와 `finish(action)`. `BUFFER_FULL` / `INVALID_ARGUMENT` 면 Work 를 들고 있고, 그 외에는 비운다. 소멸자는 `finish(closeConnection(SUCCESS))`.
  - `KiottyNet_Context`, `KiottyNet_Server`, `KiottyNet_EventClient`, `KiottyNet_StreamClient` 에 `code()`. create 의 Result 를 받는 private 위임 생성자로 만든다. `OwnedHandle` 은 그대로라 http.h / websocket.h 의 사본도 그대로다.
- `include/kiotty_net_statemachine.h`, `src/kiotty_net_statemachine.cpp`
  - `KiottyNetEntryAction` 을 지우고 `KiottyNetTransition::entry_action` 이 `KiottyNetConnectionAction` 이다.
  - `KiottyNet_make*EntryAction` 여섯, `KiottyNet_setEntryActionTimeout`, C++ `KiottyNet_EntryAction` 래퍼를 지웠다. timeout 은 action 함수의 인자다. C++ 도 base.h 의 action 함수를 그대로 쓴다.
  - `KiottyNet_Transition<State>::moveToNextState` 의 기본 인자를 없앴다. 머무는 것은 `KiottyNet_holdCommunication(0)` 을 명시한다.
- http.h / websocket.h 는 바뀐 base 타입을 쓰지 않아 고칠 것이 없었다.
- `docs/requirement/requirement_v0.md`: 1절 (코드), 2절 (create 의 Result), 3절 (no_delay), 4절 (write lane), 5절 (다시 씀), 6 ~ 7절 (create 시그니처, close 의 reason, broadcast), 8절 (entry action).

## 테스트

- `tests/unit/kiotty_test_headers_cpp.cpp` 와 `tests/unit/kiotty_test_headers_c.c`: 네 공개 헤더를 C++ 와 C 로 각각 컴파일하고, C 쪽이 잰 공개 struct 크기 15 개와 enum 값 둘을 C++ 쪽과 비교한다 (`Headers.*` 3 개).
- smoke 와 benchmark smoke 는 `KiottyNet_moveToNextState` 를 쓰도록 바꿨다.
- 로컬: MSVC (VS 18, C++14 / C11), WSL gcc 13, clang 18 에서 unit 5/5 초록. 헤더만 따로: g++ / clang++ `-std=c++11`, gcc / clang `-std=c99`, MSVC `/Zs /std:c++14 /permissive-` 와 `/TC /std:c11` 경고 0.

## 이 사이클에서 내가 정한 것 (확인 필요)

- `KiottyNet_Work::finish` 가 Work 를 들고 있는 코드는 `BUFFER_FULL` 과 `INVALID_ARGUMENT` 둘. 설계 9.5 에서 이 둘만 `_work.finish` 전에 return 한다.
- 소유 래퍼의 `code()` 추가. 설계 3절에는 C++ 래퍼가 적혀 있지 않다.
- StateMachine 의 `WORK_STARTED` 행이 Work 를 worker 에게 넘길 때의 entry action 은 이제 `START_WORK` 다 (예전의 NONE 자리). `finishWork(START_WORK)` 가 INVALID_ARGUMENT 라 겹치지 않는다. HOLD 는 `finishWork(HOLD)` 로 쓰인다.
- `KiottyNet_EntryAction` C++ 래퍼를 없앴다.

## 미결

- `KiottyNetStatefulAppContext::createConnectionContext(app, connection)` 는 handle 을 받는데, onConnected 에는 이제 handle 이 없다. stateful 계층을 짤 때 `(app, info)` 로 바꿀지, onReady 에서 만들지 정해야 한다. base 범위 밖이라 그대로 두었다.
- requirement 의 9 ~ 11절 (HTTP, WebSocket) 은 아직 옛 Work 모델이다 (기존 미결 그대로).

## 다음 할 일

push → CI 8 job 초록 확인. 그 뒤 사이클 2: `core/` (Stage, StageSet, Span, PoolHandle, Pool, Clock) 와 테스트 v0 5절. 시작 전에 테스트 v0 14절 3 (Stage::commit 초과, Pool::release 남의 포인터) 계약을 확정한다.

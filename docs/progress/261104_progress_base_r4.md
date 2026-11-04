# 2026-11-04 진행 정리 : plan v2, R4 (문서)

기준 문서: `docs/design/design_base_v4.md` (11-03 의 19절 판), `docs/design/design_base_v8_feedback.html`.

## plan_base_v2.md

- v1 의 R1 ~ R3, 3c, 4 가 끝났고 설계 19절 (메모리 할당) 이 끝난 층의 모양을 바꿨으므로 계획을 다시 썼다. 1부 (R4 ~ R8: 문서, base.h 의 handle struct, core/pool 과 Stage 와 Parker, IoEvent 구체 타입, TlsEngine 과 TlsMemory) 와 2부 (4b, 5a, 5b, 6, 7, 8).
- 사이클 4 노트의 "R4 (handle 함수가 Context 를 첫 인자로)" 는 v8 피드백 11.12 의 결정 C (handle 이 owner 를 든다) 로 없어졌다. plan v2 의 R4 는 문서 사이클이다.
- R6 ~ R8 사이에는 사이클 4 의 Context 테스트와 tls_pool_tests 가 깨진 채이므로 push 는 R8 끝에 한 번.
- R5 첫 줄에서 확정할 것 하나: `initializeContext` 의 시그니처 (설계 3절은 `(void)`, 13절과 19.9 는 `KiottyNetContextConfig`). config 쪽을 권했다.

## R4. 테스트 계획과 requirement

- `test_base_v0.html`
  - 5절: `Stage<N>` + `limit` / `clear` (Pool 에서 받던 케이스 삭제, 셋 추가), `PoolHandle` 은 generation · slot 둘, `Parker` 넷, Holder 삭제. 5.1 은 `ReusablePool` 8, `LazyReusablePool` 6, `Pool` 둘 (arena 멤버, PoolBase), TLS Layout 셋. `ContextPoolLayout` / `TlsPoolLayout` / 사이클 4 의 `Context` 셋은 지웠다 (하나는 TlsMemory 로).
  - 8절: "정적 TLS Pool" → `TlsMemory` 여덟 (installed, 서브풀 Scope, 전역 꼬리표 격리, 서브풀 바닥, 등록표, lateFrees, detachThread, 중첩 Scope).
  - 9절: `NotificationContext` 줄을 `ConnectionHost` 셋 (lock 안의 reset, 지난 generation, acquire 실패) 으로, handle 케이스 둘을 struct 모양으로, Sender 의 clear 를 `StageBase::clear` 로. 평문 96.
  - 10절: 여섯 프로세스 → 다섯 (`stepLocalEvents` 반복), 성질 하나 더 (local completion 의 step 이 만든 Work 도 같은 바퀴), 케이스 셋 추가 (`WorkRequestedByATimeoutHandlerStartsInTheSameRunOnce`, `StepLocalEventsTerminatesWithinThreeStepsPerSlot`, `IsIoThreadIsTrueOnlyOnTheRunningThread`). IoLoop 조립은 전부 지역 변수 + `run` 의 참조 인자. 35.
  - 11절: Context 케이스를 설계 13절 / 19.9 로 (두 번 initialize, release 한 번, 타입별 TABLE_FULL, config 의 미리 만들기, 은퇴 Owner 재사용, 옛 handle 은 PEER_CLOSED, lateFrees 0, releaseContext 없이 종료, 서브풀 바닥). `g_contexts` 케이스 삭제.
  - 13절, 14절 (handle 군을 `{ owner, id }` 로), 15절 (plan v2 의 사이클 번호).
- `requirement_v0.md`
  - 규약 8 과 공통 타입: pool handle 이 `{ owner, id }` struct. 비교는 둘 다, 무효는 `owner == NULL` 또는 `id == 0`, destroy 된 소유자의 포인터는 releaseContext 전까지 유효. HttpResponse / WebSocket 의 handle 은 그 절 그대로 두고 base 뒤에 맞춘다.
  - 2절: 메모리가 보장되는 시점은 `create*`, `KIOTTY_OUT_OF_MEMORY` 는 create 가, Context 는 process 에 하나 (두 번 initialize 는 같은 handle, release 한 번), Windows 커밋 크기와 `KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER`.
  - 5절 Stream 오용 표, `KiottyNetConnectionStream` struct, C++ `KiottyNet_Connection` 의 `operator==` / `operator<`.
- `user_guide_http.md`, `implementation_guide_v0.md` 는 HTTP 층의 int64 handle 을 말하므로 두었다.

## 결정 (같은 날) : initializeContext 는 인자 없이

- `KiottyNet_initializeContext(void)` 를 그대로 둔다. 공개 `KiottyNetContextConfig` 는 만들지 않는다.
- `Context::open` 이 받는 `ContextConfiguration` 은 라이브러리 안의 struct 이고, base.cpp 가 기본값 (타입별 미리 만들 개수 0) 으로 넘긴다. 필요해지면 그때 base.h 에 공개한다.
- 설계 v4 3, 13, 19.2, 19.3, 19.9 와 plan v2 R5 / 7 / 8, requirement 2절, 테스트 계획 11절 (`ReservedOwnersFromTheConfig...` 삭제) 을 맞췄다. `KIOTTY_OUT_OF_MEMORY` 는 `create*` 만 돌려준다.

## R5. base.h 의 handle 을 struct 로

- `include/kiotty_net_base.h`:
  - `KiottyNetConnectionOwner` (불투명).
  - `KiottyNetConnectionHandle { owner, id }`, `KiottyNetConnectionStream { owner, id }`. 같은 필드의 다른 타입이라 서로 넘길 수 없다.
  - `KiottyNet_serverMemorySize()` / `KiottyNet_clientMemorySize()` 선언 (구현은 사이클 8).
  - C++ `KiottyNet_Connection` 에 `operator==` / `!=` / `<` (owner 는 `std::less`, 그 다음 id). `KiottyNet_ConnectionStream` 은 owner 가 NULL 이면 빈 것으로 본다.
- statemachine.h, http.h, websocket.h 는 타입 이름만 쓰므로 바뀐 줄이 없다. HTTP / WebSocket 의 `int64_t` handle 은 그 층에서 맞춘다.
- 테스트: 헤더 layout 비교에 handle 둘의 크기와 `id` offset, `ConnectionHandleAndStreamAreDistinctTypesWithTheSameFields`, `ConnectionWrappersCompareByOwnerThenId` (map key).
- 로컬:
  - MSVC Debug TLS ON 185/185. 헤더만 `/Zs /std:c++14 /permissive- /W4 /WX`, `/TC /std:c11` 경고 0.
  - WSL gcc / clang TLS ON 186/186, OFF 184/184. 헤더만 clang `-std=c++11`, `-std=c99` 경고 0.
- 환경 메모: 이 PC 는 MSVC toolset 이 14.52 로 올라 옛 `build/msvc` 의 cache 가 깨져 `build/msvc-tls` 를 새로 만들었다. vcvars64 가 vswhere 를 찾도록 Installer 경로를 PATH 앞에 둔다. WSL 배포판 이름은 `Ubuntu` 다.

## 다음 할 일

R6. core/pool (ReusablePool, LazyReusablePool, PoolBase, Pool<Layout>), StageBase / Stage<N>, Parker, PoolHandle 에서 owner 비트 제거, memory limits 와 TLS Layout 셋. 사이클 4 의 Context 코드와 테스트를 지운다. R8 까지 push 하지 않는다.

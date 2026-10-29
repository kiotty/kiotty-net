# 2026-10-29 진행 정리 : base 사이클 2 (core)

기준 문서: `docs/design/design_base_v2.md` 4절, `docs/test/test_base_v0.html` 5절, `docs/plan/plan_base_v0.md` 4절.

## 사이클 1 뒤처리

- `KiottyNetStatefulAppContext::createConnectionContext` 는 `(app, info)` 를 받는다 (commit `6241bf1`). NULL 이면 그 연결은 ignore 다.

## 확정한 계약 (테스트 v0 14절 3)

- `Stage` 의 Provider / Consumer `commit` 이 writable / readable 을 넘기면 debug 는 `assert`, release 는 clamp.
- `Pool::release` / `reallocate` 에 남의 포인터가 들어오면 debug 는 `assert`, release 는 무시한다 (`reallocate` 는 nullptr 를 돌려준다).
- heap fallback 블록은 앞에 16 바이트 header (magic + 크기) 를 둔다. 남의 포인터를 판별할 때와 `reallocate` 가 옛 크기를 알 때 쓴다. 판별은 최선 노력이다 (포인터 앞 16 바이트를 읽는다).
- 설계 v2 4절에 세 줄로 적었다.

## 만든 것

- `src/implementation/core/`
  - `kiotty_stage.h/.cpp`: Stage. Provider / Consumer 는 Stage 를 가리키는 값이다. Consumer 는 `prepare` 대신 설계의 `peek()` 을 가진다. 복사와 이동은 없다.
  - `kiotty_stage_set.h`: StageSet.
  - `kiotty_span.h`: Span.
  - `kiotty_pool_handle.h`: PoolHandle, `toRaw` / `fromRaw`, `OwnerIndex` + `makeIndex` / `ownerOf` / `slotOf`.
  - `kiotty_pool.h/.cpp`: reference 의 BlockPool 에서 가져왔다. 등급 여덟 개가 `bytes / 8` 씩 나눠 갖고, 16 정렬이며, mutex 하나로 지킨다. reallocate 를 더했다.
  - `kiotty_clock.h/.cpp`: `monotonicMs()` (steady_clock).
- requirement 4절의 Stage 선언을 코드에 맞췄다 (namespace kiotty, `peek`, `valid`).

## 테스트

- 새 테스트 28 개: Stage 11, StageSet 2, Span 2, PoolHandle 4, Pool 8, Clock 1. 그리고 Holder, Result (+ `Assign`), ResultRef (+ `RefFail`, `ResultRefDeath`) 42 개 (파라미터 포함) 는 reference 의 테스트를 주석만 빼고 옮겼다 (헤더가 reference 와 같다).
- Stage 와 Pool 의 오용 테스트는 `EXPECT_DEBUG_DEATH` 다. 테스트 v0 에서 둘로 적었던 Stage commit 은 Provider 와 Consumer 로 나눴다. Pool 의 fallback 블록 reallocate 테스트를 하나 더했다.
- 벤치마크: `kiotty_bench_core.cpp` 의 `PoolAllocateRelease`, `StageProvideConsume`. smoke 벤치마크는 지웠다.
- 로컬: MSVC Debug 74/74, WSL gcc 75/75, clang 75/75, clang Release 74/74, clang ASan+UBSan 75/75. 개수 차이는 reference 의 `ResultRefDeath` 가 `!_WIN32 && !NDEBUG` 에서만 컴파일되기 때문이다.

## 미결

- Pool 은 맞는 등급이 비어 있으면 큰 등급에 자리가 있어도 heap 으로 간다 (reference 와 같다). 사이클 4 의 OpenSSL 할당 크기를 보고 등급과 비율을 다시 정한다.
- 벤치마크 수치는 사이클 6 에서 기준치로 적는다.

## 다음 할 일

CI 초록 확인. 그 뒤 사이클 3a: error + socket (테스트 v0 6절).

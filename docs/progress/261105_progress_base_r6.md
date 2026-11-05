# 2026-11-05 진행 정리 : R6 (core/pool, Stage<N>, Parker, memory limits)

기준 문서: `docs/plan/plan_base_v2.md` R6, `docs/design/design_base_v4.md` 4절, 19.2 ~ 19.4.

## 범위 정정

- Stage 를 바꾸면 TlsEngine 이 바로 깨진다. 그래서 TlsEngine 의 `Stage<N>` 멤버와 `limit()` 을 R8 에서 R6 으로 당겼다.
- `kiotty_holder.h` 는 event listener 가 쓰므로 R7 에서 지운다.
- OpenSSL 의 정적 Pool 은 R8 까지 `ProcessTlsLayout` 하나로 둔다 (전역 + Server 몫 × MAX_SERVERS + client 몫 × 나머지).
- 그래서 R6 의 commit 도 매트릭스가 초록이다. plan v2 의 R6 머리에 적었다.

## 코드

- `core/pool/` 로 옮기고 다시 썼다:
  - `kiotty_pool_layout.h`: `PoolLayout::ARENA_BYTES`. BlockClass 와 합의 overflow, 등급 32 개 상한을 static_assert 로 막는다.
  - `kiotty_pool.h/.cpp`: `PoolBase` (가상 인터페이스), `ArenaPool` (등급 표 로직, 비 template), `Pool<Layout>` (arena 가 멤버, default-init 이라 0 으로 채우지 않는다).
  - `kiotty_static_pool.h`: 이사.
  - `kiotty_pool_handle.h`: `PoolHandle { generation, index }` 의 `value()` / `fromValue()` 둘. owner / context 비트를 지웠다.
  - 새로: `kiotty_reusable.h` (`is_reusable`, `SlotState`), `kiotty_reusable_pool.h` (`ReusablePool<T, N>`), `kiotty_lazy_reusable_pool.h` (`LazyReusablePool<T, N>`).
- MISRA C++:2023 쪽 정리 (allocator):
  - 포인터 ↔ 정수 변환 (`uintptr_t`, `reinterpret_cast`) 을 Pool 과 StaticPool 에서 지웠다. 범위 검사는 `std::less<const unsigned char*>`, offset 은 같은 배열 안의 포인터 차이다.
  - free list 는 블록 안에 `void*` 를 쓰는 대신 index + 1 을 `memcpy` 한다 (StaticPool 과 같은 방식).
  - `const_cast` 를 쓰지 않는다. const 조회는 index 를 돌려주는 helper 하나로 모았다.
  - 풀의 객체는 default-init (`std::array<T, N>` 를 mem-init 에 넣지 않는다, `new (std::nothrow) T`). T 의 생성자가 user-provided 가 아니어도 수 MB 를 0 으로 채우지 않는다 (19.1 #5).
- 풀 계약에 상수 둘을 더했다 (설계 19.2): `T::TABLE_FULL`, `T::OUT_OF_MEMORY` (Lazy 만). core 가 `KiottyNetCode` 를 모르기 때문이다.
- `ReusablePool` 은 `all()` 대신 `at(i)` / `active(i)` / `capacity()` 를 둔다. `LazyReusablePool` 은 `clear()` (releaseContext 용) 와 `created()` 를 둔다.
- `core/kiotty_parker.h`: `park(ready, timeout_ms)` / `unpark(change)`. 0 은 무한. 긴 timeout 은 하루씩 나눠 기다린다 (`steady_clock::now() + d` 의 overflow 방지).
- `core/kiotty_stage.h`: `StageBase` (provider / consumer / compact / `clear` / `limit`) 와 `Stage<N>` (멤버 배열). `StageSet<R, S, N>`.
- `base/pool/kiotty_memory_limits.h`: `MAX_SERVERS` (4) / `MAX_STREAM_CLIENTS` (8) / `MAX_EVENT_CLIENTS` (4). `MAX_OWNERS`, `MAX_TLS_CONTEXTS` 는 지웠다. CMake cache 변수도 같이 바꿨다.
- `base/pool/kiotty_memory_layout.h`: `CIPHER_RECEIVE_BYTES` / `CIPHER_SEND_BYTES`, `GlobalTlsLayout` / `ServerTlsLayout` / `ClientTlsLayout` / `ProcessTlsLayout`. 모두 `TlsLayoutOf<Blocks>` 하나의 표에서 나온다.
- TlsEngine: 기본 생성자, `limit(receive, send)`, `StageBase&` 를 받는 `decode`.
- 사이클 4 의 `base/context/kiotty_context.*` 와 테스트를 지웠다. Context 는 사이클 7 에서 13절대로 새로 만든다.
- class template 의 static const 멤버에 out-of-class 정의를 두었다. gtest 의 `EXPECT_EQ` 가 참조로 받아 ODR-use 하면 gcc / clang 에서 링크가 깨지기 때문이다.

## 테스트

- 새 파일: ReusablePool 8, LazyReusablePool 7, Parker 4. support 에 `kiotty_test_reusable.h`.
- 다시 쓴 것:
  - Stage: Pool 케이스를 지우고 limit / clamp / 비지 않은 limit 의 assert / clear / StageBase 참조를 더했다.
  - StageSet 3, PoolHandle 3, Pool (arena 멤버, PoolBase 참조), MemoryLimits / TlsLayout 4.
- TLS 테스트의 Stage 와 engine 은 compile time 상한 아래에서 `limit()` 으로 줄인다. 상한 (16 KiB) 을 넘던 셋은 상한에 맞췄다:
  - 40 KiB 를 한 번에 encode 하던 다중 레코드 테스트는 `MAX_SEND_CAPACITY` 가 한 레코드 (16 KiB) 이하인 build 에서 skip 한다. 기본 build 에서는 한 메시지가 늘 레코드 하나다.
  - SSL 안에 남은 평문을 꺼내는 테스트는 16 KiB 대신 12 KiB 를 4 KiB 씩 세 번 꺼낸다. cipherReceive 가 16 KiB 라 16 KiB 평문의 레코드가 한 번에 들어가지 않기 때문이다.
  - capacity 군 { 4096, 16384, 65536 } 은 { 4096, MAX_SEND_CAPACITY } 가 됐다.
- `kiotty_net_tls_pool_tests` 는 Context 대신 `useTlsStaticPool()` 을 직접 부른다 (R8 에서 TlsMemory 로).
- 계측 도구와 benchmark 는 `Stage<N>` 과 `Pool<Layout>` 으로 바꿨다. 계측의 stage 크기는 `MAX_RECV_CAPACITY` 가 상한이다.

## 로컬

- MSVC Debug: TLS ON 206/206, OFF 204/204.
- WSL TLS ON 은 207/207, OFF 는 205/205:
  - gcc (benchmark 포함 build)
  - clang
  - clang ASan+UBSan
  - clang TSan

## 추가 (같은 날) : Result 를 MISRA C++:2023 에 맞춤

- `core/kiotty_result.h`:
  - union 과 placement new, 소멸자 직접 호출을 뺐다 (Rule 12.3.1, 21.6.3). 멤버는 `ErrorCode _code` 와 `Value _value` 둘이고, 실패는 기본 생성한 Value 를 든다. 그래서 Value 는 기본 생성 가능해야 한다 (static_assert). 지금의 Value (`ActiveSocket`, `size_t`, `Holder<IoEvent>`) 는 모두 된다.
  - copy / move / 대입은 컴파일러가 만든다. 손으로 짠 `assign` 이 없어졌다.
  - `ok()` / `okRef()` / `error()` 의 암시 변환 연산자를 지우고 (Rule 15.1.3) static factory `success(value)` / `failure(code)` 로 바꿨다. 호출부는 typedef 를 쓴다 (`ActiveSocketResult::failure(code)`, `AcquireResult::success(object)`).
  - 실패일 때 true 이던 `explicit operator bool` 을 지웠다. 검사는 `isOk()` 하나다.
  - `Result<E, T&>` 는 포인터 멤버 그대로, factory 만 같은 모양으로 바꿨다.
- 테스트: operator bool 케이스 둘을 지우고 bool 로 만들 수 없다는 static_assert 로 바꿨다. 실패 결과가 기본 Value 를 든다는 확인과 move 전용 Value (`unique_ptr`) 의 static_assert 를 더했다.
- 남은 것: 설계 v4 의 `Result<KiottyNetCode, ConnectionStream>` 은 ConnectionStream 이 참조 멤버를 들면 기본 생성이 안 된다. 사이클 5 에서 포인터 멤버로 두거나 다른 모양으로 정한다.
- 로컬: MSVC Debug TLS ON 204/204, OFF 202/202. WSL gcc / clang / clang ASan+UBSan / clang TSan TLS ON 205/205, gcc / clang TLS OFF 203/203, clang Release TLS 204/204.

## 추가 (같은 날) : 등급 풀 이름

- 저장 위치가 이름에 보이도록 바꿨다. `Pool<Layout>` → `MemberPool<Layout>` (저장 공간이 그 객체의 멤버, heap 에 따로 잡지 않는다), `StaticPool` → `GlobalPool` (멤버가 전부 static, .bss). 딸린 이름도 같이: `GlobalPoolUsage`, `GlobalClassStorage`, `GlobalPoolLevel`, `useTlsGlobalPool`.
- 파일: `core/pool/kiotty_member_pool.h/.cpp`, `core/pool/kiotty_global_pool.h`, 테스트 `kiotty_test_member_pool.cpp`, `kiotty_test_global_pool.cpp`, `kiotty_test_tls_global_pool.cpp`. 테스트 이름은 `MemberPool.*`, `GlobalPool.*`, `TlsGlobalPool.*`.
- CMake option `KIOTTY_NET_TLS_STATIC_POOL` 은 `KIOTTY_NET_USES_TLS_STATIC_LIBRARY` 가 됐다 (뜻: OpenSSL 을 kiotty 가 정적 라이브러리로 혼자 쓴다. 그래서 그 할당을 GlobalPool / MemberPool 로 받는다). 실행 파일 `kiotty_net_tls_pool_tests` 는 그대로다.
- 설계 v4, plan v2 (앞으로 할 일의 줄), 테스트 계획 5.1 / 15절, build 스킬을 맞췄다. 지난 진행 노트와 feedback 문서는 기록이라 두었다.

## 다음 할 일

R7. IoEvent 를 backend 의 구체 타입으로, `attach` 가 제자리에서 채운다. `kiotty_holder.h` 와 그 테스트를 지운다.

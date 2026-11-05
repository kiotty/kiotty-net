# 2026-11-05 진행 정리 : R8 (TlsMemory)

기준 문서: `docs/plan/plan_base_v2.md` R8, `docs/design/design_base_v4.md` 19.5, 19.6.

## 코드

- `base/tls/kiotty_tls_memory.h/.cpp` (OpenSSL 헤더를 모른다):
  - `TlsMemoryFunctions` 와 backend 함수 다섯 (`installTlsMemory`, `warmTlsMemory`, `attachTlsThread`, `detachTlsThread`, `tlsThreadStateFile`). OpenSSL backend 가 정의하고 stub 은 false / 빈 함수다.
  - `TlsMemory`: static `functions`, `installGlobal`, `installed`, `detachThread`, `lateFrees`, `globalClassCount`, `globalUsage`, `globalLiveBlocks`. 인스턴스 `Scope`, `published`, `failures`, `liveBlocks`. 전역 arena 는 `GlobalPool<GlobalTlsTag, GlobalTlsLayout>` 을 감싼 `GlobalArena : PoolBase` 하나 (.bss, 파괴자 없음). 등록표 `s_ranges[Limits::OWNERS]` 는 atomic 셋 (begin, end, pool) 이고 쓰기만 `atomic_flag` 안이다. thread 상태는 `thread()` 의 thread_local 하나 (`current`, `last_hit` 은 등록표 칸의 포인터, `attached`).
  - `OwnerTlsMemory<Layout> : private OwnerTlsPool<Layout>, public TlsMemory`. 서브풀이 먼저 만들어지는 base 라서 `TlsMemory` 의 생성자가 받을 때 이미 있다. 생성자가 발행하고 소멸자가 칸을 비운다.
- `kiotty_openssl_tls.cpp`:
  - `installTlsMemory`: 기본 malloc 일 때만 `CRYPTO_set_mem_functions`. 우리 것이면 true, 다른 hook 이면 false 이고 덮지 않는다. 늘 `OPENSSL_init_ssl(OPENSSL_INIT_NO_ATEXIT)`.
  - `warmTlsMemory`: RSA 1024 와 P-256 키로 자기 서명 인증서를 만든다. PEM 으로 내보냈다가 다시 읽는다 (평문 키, aes-256-cbc 로 암호화한 키). 상호 인증 handshake 를 TLS 1.3 / 1.2 로 BIO pair 위에서 한 번씩 하고 1 바이트 왕복한다.
  - `attachTlsThread` (`ERR_clear_error`, `RAND_bytes`, `RAND_priv_bytes`), `detachTlsThread` (`OPENSSL_thread_stop`).
  - `tlsThreadStateFile`: 할당 자리가 `crypto/err/` 이면 true.
  - `configureProtocol` 에 `SSL_CTX_set_num_tickets(0)`, `SSL_CTX_set_max_cert_list(16 KiB)`.
- `PoolBase::liveCount()` 를 더했다 (`ArenaPool` 이 센다).
- `ProcessTlsLayout` 과 free 함수 셋 (`useTlsGlobalPool`, `tlsPoolClassCount`, `tlsPoolUsage`) 을 지웠다.
- `kiotty_memory_layout.h`: `Blocks::count(global, thread, profile, connection)`. 전역은 `global + thread × OWNERS`. 값은 다시 계측했다 (아래).
- `tests/measure`: 시작할 때 라이브러리 예열을 "warm" 꼬리표로 돌린다. 새 thread 의 attach 와 오류 16 개를 "thread" 꼬리표로 잰다. 출력 제목에 세 Layout 이름을 적는다.

## 설계와 달라진 것 (설계 19.5 에 반영)

1. **`TlsMemory(_pool)` 은 UB 였다.** 아직 생성이 시작되지 않은 `MemberPool` 을 `PoolBase&` 로 바꾸는 것은 묶기만 해도 정의되지 않은 동작이다 ([class.cdtor]/3). 그래서 base-from-member 로 바꿨고, `publish()` 는 따로 없다.
2. **등록표의 칸은 `TlsMemory` 의 소멸자가 비운다.** 처음에는 `Context::release` 가 지우기로 했었다. Owner 가 지워진 뒤 주소가 표에 남으면 같은 주소에 새로 잡힌 heap 메모리의 free 가 지워진 서브풀로 간다.
3. **error queue 는 전역으로 보낸다.** 첫 테스트에서 Scope 를 닫은 뒤 서브풀에 `err_blocks.c:84` (1024 B) 와 `err.c:846` (81 B) 가 남았다. thread 의 ERR 칸 data 버퍼는 오류가 처음 날 때의 arena 에 잡히고 계속 재사용된다. Owner 가 지워지면 OpenSSL 이 지워진 메모리에 쓴다. attach 때 미리 채우는 것으로는 `ERR_add_error_vdata` 의 새 할당을 막지 못해서 file 로 가른다.
4. **profile 을 Owner 사이에 나누면 안 된다.** 다른 Owner 의 Scope 에서 handshake 하면 OpenSSL 이 인증서 안에 provider 키 사본을 그 Owner 의 서브풀로 캐시한다. 그 Owner 가 먼저 지워지면 `SSL_CTX_free` 가 지워진 메모리를 읽는다. 첫 exhaustion 테스트가 이렇게 0xc0000005 로 죽었다 (cdb 스택: `SSL_CTX_free → X509_STORE_free → X509_free → EVP_PKEY_free → evp_keymgmt_freedata`). 설계 19.3 은 원래 profile 과 연결이 한 Owner 이므로 테스트를 그 모양으로 고쳤다.
5. **전역 arena 는 연결을 담지 않는다.** Scope 밖의 handshake 는 전역 arena 를 바닥낸다. R6 의 "Scope 밖 handshake" 테스트는 지웠다.
6. **ticket 이 없다.** `num_tickets(0)` 이라 TLS 1.3 서버는 클라이언트 Finished 를 받으면 바로 DONE 이다. `Tls13ServerSendsNewSessionTicketAsOneMoreSendBeforeDone` 을 `Tls13ServerSendsNoSessionTicketSoItIsDoneRightAfterTheClientFinished` 로 바꿨다. plan 5b 의 ticket 문구, 테스트 계획의 IoLoop `TlsEchoWithTicketAndNotify...` 설명, 설계 `flush` 주석을 고쳤다. 그 케이스의 이름과 설계 10절의 바퀴 표는 사이클 6 에서 고친다.

## 계측 (OpenSSL 3.6.1, 연결 8 개, Stage 4 / 16 KiB)

- R6 표, MSVC, gcc 의 항목별 최대를 `TlsLayoutOf` 에 넣었다. 예열이 R6 의 계측보다 전역 상태를 더 만든다 (32 B 5096 → 5633, 96 B 56 → 540, 128 B 1933 → 2336). R6 의 표로는 예열 중에 전역 arena 가 바닥났다.
- thread 하나는 약 31 KB 다 (1 KiB × 18 은 ERR 칸).
- 64 KiB 는 R6 부터 Stage 가 compile time N (기본 16 KiB) 이라 이 build 에서 돌지 않는다 (encode 의 assert).
- gcc (Linux) 는 몇 등급이 더 컸다: 32 B thread 22, 64 B 전역 101, 96 B thread 16, 128 B 전역 2719.

## 테스트

- `kiotty_net_tls_pool_tests` (`kiotty_test_tls_memory.cpp`, 이전 `kiotty_test_tls_global_pool.cpp`): 테스트 계획 8절의 여덟 개. 이름은 테스트 계획을 따랐다. plan v2 R8 줄의 이름과 대응은 이렇다: `InstalledIsFalse...` → unit 실행 파일, `SubpoolExhaustionFailsOnlyThatHandshake` → `...AndCountsAFailure`, `ReleaseAfterPublishIsNotALateFree` → `LateFreesAreZeroAfterEveryObjectIsClosed`, `DetachThreadReturnsCurrentToGlobal` → `...ToTheGlobalArena`. 둘을 더했다: `ReleaseOfABlockFromAnotherOwnersSubpoolFindsItThroughTheRegistry`, `NestedScopesRestoreThePreviousArenaOnExit`.
- unit: `TlsMemory.InstalledIsFalseWhenAnotherAllocatorIsAlreadyThere` (세는 hook 이 먼저 있다. TLS OFF 에서도 돈다). TlsEngine 의 ticket 케이스를 위처럼 바꿨다. `TlsLayout` 케이스 이름과 값 (thread 열). `MemberPool` 에 liveCount 확인.

## 로컬

- MSVC Debug: TLS ON 203/203, OFF 195/195.
- WSL TLS ON 204/204 (gcc, clang, clang ASan+UBSan, clang TSan), TLS OFF 196/196 (gcc, clang), clang Release TLS 203/203 (Debug 전용 death 테스트 하나 빠짐).
- 늘어난 수: unit 에 InstalledIsFalse 하나, tls_pool_tests 가 2 → 8.

## 남은 것

- 사이클 7 의 `Context::open` 이 `TlsMemory::installGlobal()` 을 부른다. 그 전까지는 테스트가 직접 부른다.
- 예열이 덮지 못한 lazy 전역 캐시가 생기면 서브풀에 남는다 (예: 다른 키 형식이나 PKCS#12). `GlobalStateStaysInTheGlobalArenaAfterAttachThread` 와 `HandshakeInsideAScopeTakesBlocksFromThatOwnersSubpoolOnly` 가 지금의 RSA / EC 인증서에서 지키고 있다. 설정 범위가 넓어지면 예열도 같이 넓힌다.
- 사이클 6: `TlsEchoWithTicketAndNotify...` 의 이름과 설계 10절의 ticket 줄.

## R8 보정 (같은 날): thread 상태는 attach 한 Owner 의 것

- 문제: thread 상태 (ERR 상태와 버퍼, thread DRBG) 를 전역 arena 에 두고 `thread × OWNERS` 로 잡았는데, StreamClient 를 부르는 사용자 thread 는 셀 수 없다. Windows 정적 링크에서는 끝난 thread 의 상태가 놓이지도 않는다.
- 바꾼 것:
  - thread 의 가장 바깥 Scope 가 attach 한다. thread 상태를 그 Owner 의 서브풀에 만들고, 그 Scope 가 끝날 때 `OPENSSL_thread_stop` 으로 놓는다.
  - 안쪽 Scope 는 attach 도 detach 도 하지 않는다.
  - `crypto/err/` 할당은 `thread().state` 로 간다.
  - 공개 `detachThread()` 를 지웠다.
  - Layout 의 thread 열을 전역에서 Owner (Server, Client) 쪽으로 옮겼다.
  - `installGlobal` 을 부른 thread 만 thread 상태가 전역에 있고 detach 하지 않는다.
- 값: attach + detach 한 번이 Linux clang Release 9.35 µs, MSVC Debug 38 µs (2000 회 평균, 임시 bench).
- 버린 안: thread_local GlobalPool (모든 thread 가 크기를 내고, 다른 thread 의 free 를 찾을 표가 다시 필요), 넘치면 malloc (원칙 위반).
- 테스트 (tls_pool_tests 10 개): `GlobalStateStays...` → `ThreadStateLivesInTheOwnerWhoseScopeAttachedItAndLeavesWithThatScope`, `DetachThread...` → `TheScopeThatAttachedDetachesAtItsEndAndTheNextScopeAttachesAgain`. 더한 것: `AnInnerScopeOfAnotherOwnerNeitherAttachesNorDetaches`, `ErrorQueueAllocationsOnTheThreadThatInstalledGoToTheGlobalArena`. `LateFrees...` 는 새 thread 에서. `TlsLayout` 케이스는 `OwnerLayoutsHoldOneThreadState...`.
- 기록: `docs/learning/openssl-memory-pitfalls.html` (lazy 할당 세 종류, 함정 넷, 진단 방법, OpenSSL 을 올릴 때의 점검표).
- 로컬: MSVC Debug TLS ON 205/205, OFF 195/195. WSL TLS ON 206/206 (gcc, clang, ASan+UBSan, TSan), TLS OFF 196/196, clang Release TLS 205/205.

## R8 철회 (같은 날): TlsMemory 를 뺐다

- 결정: OpenSSL 은 자기 malloc 을 쓴다. kiotty 는 hook 을 걸지 않는다. 할당의 정확함은 OpenSSL 이 책임지는 것으로 둔다.
- 이유: lazy 할당 (전역 캐시, thread 의 ERR 상태와 버퍼, 인증서 안의 키 사본) 을 수명이 맞는 arena 로 보내려고 더한 규칙이 OpenSSL 내부 동작에 기댔다. 파일 이름으로 가르기, 예열 handshake, thread attach / detach 가 그것이고, 계속 늘었다.
- 지운 코드: `kiotty_tls_memory.h/.cpp`, backend 의 hook / 예열 / attach / detach / `tlsThreadStateFile`, `GlobalTlsLayout` / `ServerTlsLayout` / `ClientTlsLayout`, CMake option `KIOTTY_NET_USES_TLS_STATIC_LIBRARY`, `tests/measure`.
- 지운 테스트: `kiotty_net_tls_pool_tests` 10 개, unit 의 `TlsMemory.InstalledIsFalse...`, `TlsLayout` 둘. 테스트 계획 11절의 Context 두 케이스 (`LateFreesAreZero...`, `ServerSubpoolExhaustion...`) 도 지웠다.
- 남긴 것:
  - `SSL_CTX_set_num_tickets(0)`, `SSL_CTX_set_max_cert_list(16 KiB)`.
  - `CIPHER_*_BYTES`.
  - 세는 hook 의 unit 두 테스트 (새지 않음, 쌍당 128 KiB 미만).
- 같이 지운 것: `MemberPool` / `ArenaPool` / `PoolBase`, `GlobalPool`, `kiotty_pool_layout.h` (TLS 말고 쓰는 곳이 없었다) 와 그 테스트 (MemberPool, GlobalPool 케이스), benchmark 의 `PoolAllocateRelease`. `alignBlock` 은 `kiotty_memory_layout.h` 의 `alignStage` 로 옮겼다 (cipherSend 의 N).
- 남은 일: `OPENSSL_init_ssl(OPENSSL_INIT_NO_ATEXIT)` 은 hook 설치와 같이 있었으므로 지금은 부르는 곳이 없다. 사이클 7 의 `Context::open` 이 부른다 (설계 19.5).
- 기록: `docs/learning/openssl-memory-pitfalls.html` 맨 앞에 "버렸다" 상자를 달았다.
- 로컬 (풀까지 지운 뒤): MSVC Debug TLS ON / OFF 170/170. WSL gcc / clang TLS ON / OFF, ASan+UBSan, TSan 171/171, clang Release TLS 170/170. MSVC Release benchmark 빌드와 실행 (StageProvideConsume) 확인.

## 다음 할 일

push 뒤 4b (ConnectionStream).

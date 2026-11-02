# 2026-11-02 진행 정리 : base 사이클 4 (tls)

기준 문서: `docs/plan/plan_base_v1.md` 사이클 4, `docs/design/design_base_v4.md` 8절, `docs/test/test_base_v0.html` 8절.

## 구현

- `src/implementation/base/tls/`
  - `kiotty_tls_profile.h`: `TlsProfile`, `tlsSupported()`, `TLS_HANDSHAKE_TIMEOUT_MS` (10 s, 설정에 필드가 없으므로 상수).
  - `kiotty_tls_engine.h/.cpp`: `TlsEngine(receive_capacity, send_capacity)`, cipher Stage 둘, `TlsStep`, `tlsCipherBound` / `tlsCipherCapacity` (레코드 16 KiB 마다 64 B, 더해서 512 B).
  - `backend/kiotty_openssl_tls.cpp` (`#if KIOTTY_NET_USE_TLS`), `backend/kiotty_stub_tls.cpp` (`#if !KIOTTY_NET_USE_TLS`, 전부 `TLS_NOT_SUPPORTED`).
- OpenSSL 쪽 세부 (설계 v4 8절에 적었다):
  - memory BIO 둘. profile 은 `SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION`, `SSL_MODE_RELEASE_BUFFERS`, session cache 끔, 버전은 min / max proto, cipher 목록은 bit 에서 만든다.
  - IP 리터럴에는 SNI 를 넣지 않는다. hostname 검사는 `SSL_set1_host`. Windows 는 trust store 가 비면 OS 저장소 (winstore) 를 읽는다.
  - decode 끝에 상대 KeyUpdate 의 답장을 cipherSend 로 꺼낸다.
  - 오류 분류: 검증 실패는 verify result 로 `HOSTNAME_MISMATCH` / `VERIFY_FAILED`, 클라이언트 인증서 없음은 `VERIFY_FAILED`, close_notify 는 `PEER_CLOSED`, 레코드 오류는 `RECORD_ERROR`, 나머지는 `HANDSHAKE_FAILED`.
- **OpenSSL 의 메모리는 OpenSSL 이 관리한다 (할당 hook 없음).** 처음에는 설계 v2 대로 TLS Pool 로 OpenSSL 할당을 받았지만 다음 이유로 걷어냈다.
  - `CRYPTO_set_mem_functions` 는 process 에 하나다. OpenSSL 을 DLL 로 나누면 같은 libcrypto 를 쓰는 다른 library 의 할당까지 받는다.
  - OpenSSL 의 전역 / thread 상태는 lazy 하게 생기고 `atexit` 의 `OPENSSL_cleanup` 때 해제된다. Context 의 Pool 이 그때까지 살아 있어야 해서 전역이 된다. Pool 을 테스트마다 두면 process 종료 때 SegFault 가 난다 (확인함).
  - `OPENSSL_cleanup` 을 직접 부르면 그 뒤 process 에서 TLS 를 다시 쓸 수 없다.
- 사용량은 테스트 실행 파일만 잰다. `tests/unit/support/kiotty_test_openssl_memory.cpp` 가 `CRYPTO_set_mem_functions` 로 live bytes / blocks / peak 를 센다.
- CMake: openssl-cmake prebuilt 를 FetchContent 로. 테스트 타깃은 TLS ON 일 때 `OpenSSL::Crypto` 를 링크하고 `KIOTTY_TEST_OPENSSL=1`.
- CI: test job 이 compiler × tls (ON / OFF), apple-clang 은 OFF 만. sanitizer 는 TLS ON, android-arm64 는 TLS ON, ios 는 OFF.
- 인증서: `tests/certs/generate.sh` 와 ca, other_ca, server, client, server_wrong_host, server_expired, garbage.

## 테스트

- `tests/unit/kiotty_test_tls.cpp` 33 개. `TlsPump` (`tests/unit/support/kiotty_test_tls.h`) 로 server / client engine 을 맞붙인다. stub 빌드는 `StubBuildReturnsNotSupportedForBothOpens` 만 돈다.
- OpenSSL 메모리 (OpenSSL 3.6.1, TLS 1.3, capacity 4 KiB, 연결된 쌍 하나):

| | 보유 | handshake 중 최대 |
|---|---|---|
| WSL gcc | 71812 B | 91422 B |
| MSVC | 71252 B | 90862 B |

  profile 과 연결을 모두 닫으면 live bytes / blocks 가 연 전과 같다.
- 로컬: WSL gcc / clang × TLS ON / OFF, ASan+UBSan TLS, TSan TLS 161/161, clang Release TLS 160/160. MSVC Debug TLS ON / OFF 160/160.

## 미결

- **R4 (base.h).**
  - handle 을 받는 함수가 전부 `KiottyNetContext` 를 첫 인자로 받는다. 대상: `getConnectionInfo`, `notifyConnection`, Stream 함수 다섯, StreamClient connect / disconnect / read / write, EventClient run / close / await / getConnection / notify, Server run / close / await / broadcast, C++ `KiottyNet_Connection` / `KiottyNet_ConnectionStream`.
  - `g_contexts[16]` 를 없앤다. handle 의 context 번호 4 bit 는 다른 Context 의 handle 을 거르는 데만 쓴다.
  - TLS Pool 이 할 일이 없어졌으므로 `KiottyNet_initializeContextWithTlsPool(bytes)` 과 설계 v4 의 TLS Pool 줄 (1, 3, 9, 13, 15, 16절) 을 정리한다.
- IO thread 가 끝날 때 `OPENSSL_thread_stop` (사이클 6). Windows 정적 링크에서는 OpenSSL 이 thread 종료를 스스로 알지 못한다. StreamClient 처럼 호출자 thread 에서 OpenSSL 을 부르는 경로는 그 thread 의 상태가 Linux 에서는 thread 종료 때 풀리고, Windows 정적 링크에서는 남는다.
- 사이클 5 전에 확정할 설계 v4 의 질문 여덟 (R3).

## 다음 할 일

push 뒤 CI (새 tls 축) 확인 → R4.

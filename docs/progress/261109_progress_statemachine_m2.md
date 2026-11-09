# 2026-11-09 진행 정리 : StateMachine 사이클 M2 (core, DV12)

기준 문서: `docs/design/design_statemachine_v0.md` 8절, 13절의 M2. DV12 (`const char*` 가 내부 경계를 지난다) 를 코드에서 닫는다.

## 더한 것 (`core/`)

| 파일 | 내용 |
|---|---|
| `kiotty_fixed_string.h` | `FixedString<N>`. 늘 null 종료. public 일곱 (`assign / append / span / c_str / length / empty / clear`). `assign` 이 실패하면 비우고, `append` 가 실패하면 그대로 둔다. 복사는 `Span::copyFrom` |
| `kiotty_text.h / .cpp` | `parseText(const char*, max_length)`: NULL, `max_length` 초과는 INVALID_ARGUMENT, 잘못된 UTF-8 (overlong, surrogate, U+10FFFF 초과, 잘린 sequence) 은 INVALID_ENCODING. 길이 검사가 먼저다. `equalsIgnoreCase` (ASCII 만). `textOf(const char (&)[N])`: 설정 struct 의 `char[]` 를 `strnlen(field, N)` 으로 읽는다. 종료 문자가 없으면 길이 N 이라 `FixedString<N>::assign` 이 실패한다 |
| `kiotty_parse_integer.h / .cpp` | `parseUnsigned` (10 진, 20 자리), `parseHexadecimal` (16 진, 16 자리). 빈 입력, 자리 초과, 숫자 아닌 글자, 오버플로는 INVALID_ARGUMENT. `checkedMultiply / checkedAdd` |

## 바꾼 자리 (8.3 의 표)

| 자리 | 전 | 지금 |
|---|---|---|
| 번역 층 `src/kiotty_net_base.cpp` | `const char*` 를 안쪽으로 넘긴다 | host, pipe_name 은 `parseText(..., KIOTTY_NET_MAX_HOST_LENGTH - 1)`, 경로는 `..._PATH_LENGTH - 1`, password 는 `..._PASSWORD_LENGTH - 1` 을 한 번 지난다. NULL 이 허용되는 password 와 trust store 는 `parseOptionalText` (NULL 이면 빈 Span). identity 셋은 `parseIdentity` → `TlsIdentityText` |
| `context/kiotty_configuration.*` | `measureText`, `writeText(target, const char*, length)`, `const char* hostnameOf` | `initialize*Configuration(configuration, Span<const char>)`. `measureText` 삭제. `writeText(Span<char> field, Span<const char>)` 는 종료 문자 자리를 남기고 copyFrom, 넘치면 false. `hostnameOf` 는 `Span<const char>` (pipe 는 빈 Span) |
| `context/kiotty_tls_configuration.*` | `measureIdentity / measurePassword / measureTrust` | `TlsIdentityText` 하나를 받는다. 길이를 먼저 보고 (`isIdentityValid`) 설정 struct 에 바로 쓴다. 648 바이트 identity 를 값으로 돌리지 않는다 (수칙 1.7) |
| `ConnectionConfiguration::hostname` | `const char*` | `Span<const char>` (struct 크기 static_assert 를 고쳤다) |
| `Connection::open`, `IoLoop::admit / welcome`, `TlsEngine::open` | `const char* hostname` | `Span<const char>`. Server 의 accept 는 빈 Span |
| OpenSSL session `setHostname` | `void`, `const char*` | `KiottyNetCode`. `FixedString<KIOTTY_NET_MAX_HOST_LENGTH>` 로 복사해 `SSL_set_tlsext_host_name / SSL_set1_host`. 길이를 넘으면 `TlsEngine::open` 이 INVALID_ARGUMENT |
| OpenSSL context identity / trust store | `char[]` 를 그대로 OpenSSL 에, `terminated` 로 종료 검사 | `textOf` → `FixedString` (`IdentityFiles`) → `c_str()`. password callback 은 `FixedString` 의 span 을 복사한다. `terminated`, `identityTerminated`, `TrustStorePath` 삭제 |
| `InputFile::open`, `ConnectionStream::writeFile`, `Endpoint / ConnectionAccess / Server / EventClient / StreamClient::writeStreamFile` | `const char* path` | `Span<const char> path`. POSIX 는 `FixedString<KIOTTY_NET_MAX_PATH_LENGTH>` 로 `open`, Windows 는 길이를 넘겨 `MultiByteToWideChar` (복사 없음) |
| socket address backend | `unixAddressOf(const char*)`, `getaddrinfo(address.host)` | `unixAddressOf(Span<const char>)`. `getaddrinfo` 앞에서 `FixedString` 으로 복사한다. 종료 문자가 없는 host 는 INVALID_ARGUMENT |
| `tools/check_rules.py` | — | `.c_str(` 를 `data/infra/*/backend/` 밖에서 쓰면 BANNED |

`grep "const char\*" src/implementation` 에 남은 것: 메시지 리터럴과 그것을 넘기는 자리 (`TlsFailure::message`, `Caller::reject`, `lastErrorMessage`), `parseText`, `FixedString::c_str`, OpenSSL 을 부르는 줄의 지역 변수 (`certificate`, `getaddrinfo` 의 `host`), cipher 이름 표 (리터럴), 바이트 버퍼를 보는 `receive_flow` 의 `read_until` 검색과 readiness submit 의 payload (문자열이 아니라 길이가 붙은 바이트).

## 동작이 바뀐 것

- 공개 C 함수의 host, pipe_name, 경로, password 가 UTF-8 이 아니면 새 코드 `KIOTTY_NET_CODE_INVALID_ENCODING` 이다 (사용자 결정 2026-11-09). enum 끝에 더해 있던 값 (`OUT_OF_MEMORY` 24 까지) 은 그대로다. 전에는 바이트 그대로 받았다. UTF-8 이 아닌 key password 도 거절된다. 정수 parse 둘은 UTF-8 이 아닌 바이트도 "숫자 아닌 글자" 라 INVALID_ARGUMENT 다.
- `KiottyNet_configureTlsServerClientAuthentication(NONE, "")` 이 SUCCESS 다 (사용자 결정 2026-11-09). 전에는 NONE 에 NULL 이 아니면 INVALID_ARGUMENT 였다. 안쪽이 NULL 과 빈 문자열을 같은 빈 Span 으로 받는다. NONE 에 비지 않은 경로는 그대로 INVALID_ARGUMENT.

## 설계와 다른 것

- 8.3 은 `hostnameOf` 와 `writeText` 의 삭제를 적었다. 둘 다 `Span` 판으로 남겼다. `hostnameOf` 는 "pipe 면 빈 hostname" 을 EventClient 와 StreamClient 가 같이 쓰고, `writeText` 는 설정 struct 의 `char[]` 에 쓰는 자리 (host, 경로 셋, password) 가 같이 쓴다.

## 테스트

- 새 파일 셋: `kiotty_test_fixed_string.cpp` (7), `kiotty_test_text.cpp` (11), `kiotty_test_parse_integer.cpp` (13). 공개 API 쪽은 `Configuration.InitializeRejectsHostAndPipeNameThatAreNotUtf8WithInvalidEncoding`, `TlsConfiguration.PathsAndPasswordThatAreNotUtf8AreInvalidEncoding`, `TlsConfiguration.NoClientAuthenticationAcceptsNullOrEmptyTrustStorePath`. `Headers.CAndCppSeeTheSameEnumeratorValues` 는 마지막 코드를 `INVALID_ENCODING` (25) 으로 본다. 수칙 2.10 의 다섯 입력을 parse 셋 모두에. `parseText` 의 중간 null 바이트는 "그 자리에서 끝난다, 실패가 아니다" 로 테스트 이름 (`MiddleNullByteEndsTheTextThereAndIsNotAFailure`) 에 적었다. 두 정수 parse 는 중간 null 바이트가 숫자 아닌 글자라 INVALID_ARGUMENT.
- 고친 것: 안쪽 설정 함수를 부르던 테스트는 공개 `KiottyNet_*` 를 부른다 (NULL, 빈 문자열, 너무 긴 값의 거절이 번역 층으로 옮겨 갔다). `TlsEngine::open`, `ConnectionConfiguration`, `ConnectionStream::writeFile`, `IoLoop::admit` 를 부르는 자리는 `support/kiotty_test_text.h` 의 `kiotty_test::textOf(const char*)` 로 Span 을 만든다.

| 환경 | 결과 |
|---|---|
| MSVC TLS ON | 460 / 460 |
| MSVC TLS OFF | 460 / 460 |
| WSL gcc TLS ON | 461 / 461 (ring 테스트 skip) |
| WSL clang ASan + UBSan TLS ON | 461 / 461. UBSan 의 `runtime error` 는 googletest 안의 vptr 검사뿐이다 (테스트가 `-fno-rtti`). 라이브러리 소스를 가리키는 줄은 없다 |
| WSL benchmark target | 빌드 |
| `check_rules.py src/implementation` | LINE 0, MACRO 18, PUBLIC 2 (전과 같다) |

## 다음

M3: Handler (3절, 5절, 6절의 `run(const Handler&)`, CallbackHandler, Connection 의 `_machine_state`, onWorkReady). 동작 변화 없이 기존 테스트 그대로.

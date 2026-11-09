# 2026-11-09 진행 정리 : StateMachine 사이클 M1 (공개 API 판 v1)

기준 문서: `docs/design/design_statemachine_v0.md` 2절, 12절, 13절의 M1. 공개 헤더는 사용자가 지정한 모양을 따른다 (`development_rule.md` 0.2). 바꾼 것은 사용자가 고른 것뿐이다 (2026-11-09).

## 바꾼 것

| 자리 | 전 | 판 v1 |
|---|---|---|
| TLS 버전 | `configureTls*Versions(config, bool tls_1_2, bool tls_1_3)`, builder `versions(bool, bool)` | `(configuration, uint32_t versions)`. `KIOTTY_NET_TLS_VERSION_V12 \| V13` 의 비트 합. 0 이나 모르는 비트는 INVALID_ARGUMENT |
| TLS 검증 | `configureTlsClientVerification(config, bool verify_server, bool verify_hostname)`, struct 의 `int skip_server_verification, skip_hostname_verification` | `KiottyNetTlsVerification` (`SERVER_AND_HOSTNAME = 0`, `SERVER`, `NONE`) 하나. struct 필드도 `verification` 하나. 0 이 가장 엄격해서 0 으로 채운 struct 도 검증한다 |
| no delay | `configure*NoDelay(config, bool)`, struct 의 `bool no_delay`, builder `noDelay(bool)` | `KiottyNetNoDelay` (`OFF`, `ON`). 모르는 값은 INVALID_ARGUMENT. 내부의 `enum class NoDelay` 로는 `noDelayOf` 가 바꾼다 |
| 이름 | `config`, `cert_file`, `ws`, `ws_context` | `configuration`, `certificate_file` (`KiottyNetTlsIdentity` 의 필드도), `websocket`, `websocket_context`. `src/implementation` 의 같은 이름도 함께 바꿨다. `App` 은 그대로 (수칙 4.5 예외) |
| `KiottyNetRule` | `int32_t next[4]; uint32_t next_count;` | `KiottyNetNextStates next` (`at[4]`, `count`). C++ `Rule` 의 8 인자 생성자는 `Rule(state, event, action, KiottyNetNextStates)` |
| `KIOTTY_NET_RULE` | C 와 C++ 둘 다 | `#if !defined(__cplusplus)` 안. 복합 리터럴은 C99 다. C++ 은 `kiotty::net::From(...)` 과 `KiottyNet_rules` |
| `KiottyNetStatefulAppContext` | create, release | + `onError(app_context, info, code, message)`. NULL 이면 부르지 않는다. 구현은 M4 |
| HTTP handle | `typedef int64_t KiottyNetHttpResponseHandle / WorkHandle` | `{ KiottyNetHttpServerHandle server; int64_t id; }` 두 struct |
| HttpServer route | `(method, path, on_request, on_work_started = nullptr, app_context = nullptr)` 와 메서드별 같은 overload 여덟 | `(method, path, KiottyNetHttpRequestAction)` 과 `(path, KiottyNetHttpRequestAction)`. context 나 Work 가 있으면 `KiottyNet_makeHttpRoute(context, onRequest, onWorkStarted)` 로 만든 `KiottyNetHttpRoute` 를 넘긴다 |
| 소유 wrapper 의 `valid()` | `bool valid() const` | `explicit operator bool() const`. Context, StreamClient, EventClient, Server, HttpServer, WebSocketServer, WebSocketClient |

그대로 둔 것 (사용자 결정, DV13): 헤더 넷과 그 줄 수, `KiottyNet_HttpServer` 의 메서드 17 개, `KiottyNet_writeConnectionStreamFile` 의 offset / length.

## 테스트

- 고친 것: `TlsConfiguration.ServerDefaultsAndRejectedValues` (버전 0 과 모르는 비트 `0x4`), `ClientDefaultsVerifyServerAndHostnameAndUnknownVerificationIsRejected` (이름과 내용. 전의 "hostname 만 검증" 조합은 enum 에 없다), `TlsEngine.ClientWithVerifyServerOffAcceptsUntrustedCa`.
- 새 테스트 넷: `Configuration.UnknownNoDelayIsInvalidArgument`, `Headers.CRuleMacroAndCppRuleFillTheSameNextStates` (C 파일이 `KIOTTY_NET_RULE` 로 만든 표와 C++ `From().when().then().to()` 가 같은 next 를 채운다), `Headers.ZeroedTlsClientConfigurationVerifiesServerAndHostname`, `Headers.OwningWrappersTestAsBoolOnlyExplicitly`.
- 처음 돌렸을 때 TLS 검증 테스트 여섯이 실패했다. 테스트 helper 가 struct 를 `memset` 0 으로 만들어 쓰는데 enum 의 0 이 `NONE` 이었다. 사용자가 struct 를 0 으로 채워도 같은 일이 생기므로 enum 의 순서를 `SERVER_AND_HOSTNAME` 부터로 바꿨다.

| 환경 | 결과 |
|---|---|
| MSVC TLS ON | 426 / 426 |
| MSVC TLS OFF | 426 / 426 |
| WSL gcc TLS ON | 427 / 427 (ring 테스트 skip) |
| WSL clang ASan + UBSan TLS ON | 427 / 427 |
| WSL benchmark target | 빌드 |
| `check_rules.py src/implementation` | LINE 0, MACRO 18, PUBLIC 2 (전과 같다) |

## 다음

M2: `core/` 의 `FixedString`, `parseText`, `parseUnsigned`, `parseHexadecimal` 과 내부 `const char*` 를 `Span<const char>` 로 (DV12).

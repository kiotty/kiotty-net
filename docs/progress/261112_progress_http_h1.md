# 2026-11-12 진행 정리 : HTTP 사이클 H1 (공개 API 판 v2)

기준 문서: `docs/design/design_http_v0.html` 4절 (C), 5절 (C++), 11절의 H1 행.

## 바꾼 것

### `include/kiotty_net_http.h`

| 자리 | v1 | v2 |
|---|---|---|
| `KiottyNetHttpLimits` | timeout 둘까지 | `websocket_ping_interval / pong_timeout / close_timeout_milliseconds` 셋 추가, `KiottyNet_configureHttpWebSocketTimers` (결정 17) |
| status | `int32_t` 만 | `KiottyNetHttpStatus` enum (101 ~ 503 의 25 개). `setHttpStatus` 의 인자는 정수 그대로 |
| content type | body setter 의 `const char*` | `KiottyNetHttpContentType` enum (45 개 + `BY_EXTENSION`, `CUSTOM`, `COUNT`), `KiottyNet_setHttpCustomContentType` |
| `KiottyNetHttpRequest::keep_alive` | `int` | `KiottyNetBool` (수칙 5.7) |
| body setter | `(response, data, content_type)`, 파일은 `(response, path, content_type)` | `(response, type, data)`, `(response, type, path)` |
| 스트림 | 없음 | `KiottyNet_setHttpBodyStream(response, type)`, `KiottyNet_writeHttpWorkChunk(work, data, timeout) -> KiottyNetResult` |
| `serveHttpMemory` | `(server, url, data, content_type)` | `(server, url, type, data)` |
| create | handle | `KiottyNetHttpServerResult { code, handle }` |
| run | `(server, address, tls)` | `(server, address, tls, handler, app_context)`. `KiottyNetHttpServerHandler { onDisconnected, onError }` (결정 15) |
| C++ `KiottyNet_HttpLimitsBuilder` | | `webSocketTimers(ping, pong, close)` |
| C++ `KiottyNet_HttpResponse` | `body(data, ct)` 등 7 | `customContentType`, `body(type, data)`, `bodyRef(type, data)`, `file(path, type = BY_EXTENSION)`, `bodyStream(type)` 로 9 |
| C++ `KiottyNet_HttpWork` | | `write(data, timeout)` |
| C++ `KiottyNet_HttpServer` | | `code()`, `serveMemory(url, type, data)`, `run(address [, tls], handler, app_context)` 둘 추가 (`run(address [, tls])` 는 handler 없이 그대로) |

server handle typedef 는 가드 없이 그대로다 (설계 4.5. websocket.h 가 http.h 를 include 하는 것은 W1).

### 문서

| 문서 | 내용 |
|---|---|
| `user_guide_http.md` | v2 로. 1.1 의 create 결과와 WebSocket 타이머, 1.4 의 setter 표와 content type, 1.5 의 Work 스트림, 1.6 의 `serveHttpMemory`, 1.7 의 서버 handler, C / C++ 예제 (SSE 라우트, handler), 1.10 반환 코드, 1.11 |
| `requirement_v0.md` 9절 | 머리말만: 이 절은 v1 이고 H6 에서 v2 로 고친다 |
| `plan_base_v2.md` | H1 한 줄 |

`src/kiotty_net_http.cpp` 는 아직 비어 있다. 구현은 H2 ~ H6.

## 테스트

`Headers.*` (unit, C 와 C++ 두 번역 단위가 네 공개 헤더를 include):

- `CAndCppSeeTheSameLayoutForEveryPublicStruct` 에 http struct 일곱 (`HttpLimits`, `HttpRequest`, `HttpResponseHandle`, `HttpWorkHandle`, `HttpRoute`, `HttpServerResult`, `HttpServerHandler`) 과 `keep_alive` 의 offset.
- `CAndCppSeeTheSameEnumeratorValues` 에 `KIOTTY_NET_HTTP_CONTENT_TYPE_COUNT`, `KIOTTY_NET_HTTP_STATUS_HEADER_FIELDS_TOO_LARGE`.
- 새 셋: `HttpRequestKeepAliveIsTheAbiBool`, `HttpContentTypesEndWithByExtensionThenCustom`, `HttpStatusEnumeratorsAreTheWireCodes`.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 514 / 514 (아래 한 번 제외) |
| WSL gcc Debug, TLS OFF / ON | 515 / 515, 515 / 515 |
| WSL clang Debug, TLS OFF / ON | 515 / 515, 515 / 515 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

MSVC 의 두 번째 ctest 에서 `Version/ConnectionTls.TlsReceiveWithCorruptRecordReportsRecordErrorAndCloses/2` 가 한 번 실패로 셌다. TLS OFF 빌드라 SKIP 하는 테스트이고, 바로 뒤의 전체 실행과 `ctest -R ConnectionTls --repeat until-fail:30`, gtest `--gtest_repeat=200` 은 모두 통과했다. 실패 로그는 다음 실행이 덮어써서 남지 않았다. H1 은 이 테스트의 코드를 건드리지 않았다. 다시 보이면 그때 로그를 잡는다.

## 다음

H2 (파서, `domain/usecases/http/`): request line, header line, chunk size, Content-Length, Connection, path 매칭, content type 표 (enum ↔ 문자열, 확장자 → enum), CUSTOM 이름 검사.

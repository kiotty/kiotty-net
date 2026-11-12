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

## 후속 (같은 날, 사용자 피드백)

사용자가 `KiottyNetHttpServerHandler` 를 run 에 넘기는 모양이 라우트 (콜백 하나씩 등록) 와 사용 경험이 다르다고 봤다. 설계 결정 19 로 바꿨다.

- `KiottyNetHttpServerHandler` 와 run 의 `handler, app_context` 인자를 없앴다. `runHttpServer(server, address, tls)` 는 B2 모양으로 돌아갔다.
- 콜백 타입 둘 `KiottyNetHttpDisconnectedAction(app_context, info, reason)`, `KiottyNetHttpErrorAction(app_context, info, code, message)` 과 등록 함수 둘 `KiottyNet_setHttpDisconnectedAction(server, action, app_context)`, `KiottyNet_setHttpErrorAction(server, action, app_context)`. run 전에 등록 (뒤는 BUSY), NULL 이면 지운다. C++ 은 `KiottyNet_HttpServer::onDisconnected / onError(action, app_context)`.
- 끊김 콜백에 `KiottyNetConnectionHandle` 이 없다. 같은 이유로 base 의 `onDisconnected(app_context, connection_context, reason)` 와 StateMachine 의 `releaseConnectionContext(app_context, connection_context)` 에서도 뺐다. 테스트와 benchmark 의 콜백은 모두 `(void)connection` 이었고, unit 의 Recorder 만 handle 을 기록해 `Connection.ReceiveWithClosedCode...` 가 그 id 를 봤다 (그 한 줄을 지웠다).
- 문서: 설계 결정 19 (`design_http_v0.html` 1, 4.9, 5, 6, 9, 11절), `design_websocket_v0.html` 결정 4 와 close 줄, `design_statemachine_v0.md` 의 시그니처, `requirement_v0.md` 5 / 8절, `implementation_guide_v0.md` 의 release 예제 셋, `user_guide_http.md` 1.7 / 예제 / 1.10, `test_base_v0.html` 한 줄.
- 테스트: `Headers.DisconnectCallbacksCarryNoConnectionHandle` (세 콜백의 타입), `Headers` 의 layout 에서 handler struct 를 뺐다.

## 다음

H2 (파서, `domain/usecases/http/`): request line, header line, chunk size, Content-Length, Connection, path 매칭, content type 표 (enum ↔ 문자열, 확장자 → enum), CUSTOM 이름 검사.

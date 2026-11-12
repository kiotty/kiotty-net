# 2026-11-12 진행 정리 : HTTP 사이클 H6 (문서와 MCP 예제)

기준 문서: `docs/design/design_http_v0.html` 6절 (MCP 서버 예제), 11절의 H6 행.

## 한 것

### MCP 서버 골격 테스트

`tests/integration/kiotty_test_http_mcp.cpp` 5 개. 설계 6절의 예제를 C++ wrapper (`KiottyNet_HttpServer`, `KiottyNet_HttpResponse`, `KiottyNet_HttpWork`, builder 둘) 로 옮겼다. JSON 은 문자열 찾기로 대신하고, 세션 표와 이벤트 큐는 테스트의 `McpApp` (mutex + condition_variable) 이다. 라우트는 `POST / GET / DELETE /mcp` 와 그 뒤의 `serveDirectory("/")`.

- initialize 가 JSON 과 `Mcp-Session-Id` 를 주고, 같은 세션의 다음 요청이 JSON 으로 끝난다.
- Accept 에 둘이 없으면 406, 없는 세션 404, 알림 202, JSON 만 받는 GET 405. Work 는 initialize 하나만 열린다.
- `tools/call` 이 SSE 로 진행 알림 둘 뒤 최종 응답, 끝 chunk, 같은 연결의 다음 요청.
- GET 스트림이 앱이 큐에 넣은 이벤트를 받고 (사이에는 `: ping`), 다른 연결의 DELETE 뒤 끝 chunk 가 오고, 같은 연결로 정적 파일을 받는다.
- HEAD 는 Work 없이 헤더만 (`Transfer-Encoding: chunked`) 오고 같은 연결로 다음 요청.

### 고친 것 : HEAD 가 앞의 GET 라우트를 건너뜀

`GET /mcp` 라우트 뒤에 `serveDirectory("/")` 를 등록하면 `HEAD /mcp` 가 디렉토리로 가서 404 였다. `HttpRegistry::find` 가 HEAD 비트만으로 표 전체를 먼저 찾았으므로 HEAD 비트가 있는 정적 항목이 GET 라우트보다 먼저 걸렸다. HEAD 는 이제 등록 순서대로 HEAD 또는 GET 을 가진 첫 항목으로 간다 (사용자 가이드의 "등록 순서 = 매칭 순서" 그대로). 같은 경로에 HEAD 라우트를 GET 라우트보다 뒤에 등록하면 HEAD 라우트는 쓰이지 않는다.

### benchmark

`tests/benchmark/kiotty_bench_http.cpp` 셋 (H4, H5 에서 미룬 것). WSL Release TLS ON, 같은 실행의 `EchoRoundtrip/clients:1/max_clients:16` 이 242 µs 일 때:

| benchmark | 결과 |
|---|---|
| `HttpGetJsonRoundtrip` (keep-alive 연결 하나로 GET → JSON 완성 응답) | 404 µs |
| `HttpWorkChunkLatency` (worker 에게 알림 → 1 KiB chunk 가 상대에 도착, 1000 번) | 145 µs |
| `HttpStaticFile1MiB` (1 MiB 파일, `send_capacity` 조각) | 536 MiB/s |

### 문서

- `requirement_v0.md` 9절을 판 v2 와 H3 ~ H5 의 구현으로 다시 썼다. `deferHttpResponse / resumeConnection / resumedWork` 는 없어졌다고 적었다 (설계 10절은 이것을 "H7" 이라 했지만 H6 에서 했다).
- `implementation_guide_v0.md` 4절 (4.1 ~ 4.8) 을 실제 코드로 고쳐 쓰고 4.9 (Work 스트림), 4.10 (content type) 을 더했다. 5절의 sendfile 미결을 닫았다.
- `test_base_v0.html` 에 19절 "케이스 : HttpServer (H2 ~ H6)" 를 더했다. 17 / 18절이 이미 B1 / B2 라서 설계의 "테스트 계획 17절" 대신 19절이다.

## 문서를 고치며 본, 코드와 설계가 어긋난 곳 (고치지 않음)

- `HttpSessionPool::find` 는 generation 을 mutex 없이 읽는다. 헤더만 보낸 스트림의 finish 는 worker 에서 다음 요청을 열며 generation 을 쓴다. 같은 handle 을 다른 thread 가 동시에 쓰는 규칙 위반일 때만 닿는다.
- 두 번째 finish 는 IO thread 가 다음 요청을 열기 전까지 `BUSY`, 그 뒤는 `PEER_CLOSED` 다 (설계와 user guide 는 BUSY 만 적는다).
- 서버 close 의 끊김 action (`CLOSED`) 은 worker 의 finish 보다 먼저 온다 (H4 에 적은 base 의 순서).
- `writeHttpWorkChunk` 는 실패가 남은 뒤에도 인자 검사를 먼저 하므로 빈 chunk 는 `INVALID_ARGUMENT` 다.
- `setHttpFile` 은 열기를 type 검사보다 먼저 하므로 없는 파일 + 잘못된 type 은 `NOT_FOUND` 다.
- `response_bytes` 의 하한이 `HttpServer::prepare` 는 256 + 2, configure 는 256 + 64 로 다르다.
- user guide 1.10 은 `setHttpNotFound` 가 `TABLE_FULL` 을 줄 수 있다고 적지만 표를 쓰지 않는다.
- 설계 7절은 `setHttpBodyStream` 이 worker 에서만 의미가 있다고 적지만 같은 설계의 MCP 예제와 HEAD 규칙은 onRequest 에서 부른다.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 632 / 632 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 633 / 633 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 633 / 633, 633 / 633 |
| WSL clang Release TLS ON | 632 / 632 |

## 다음

설계 11절의 HTTP 행은 이것으로 끝이다. 위의 어긋난 곳을 정할지, WebSocket (`design_websocket_v0.html` 의 W1 ~ W5) 으로 갈지.

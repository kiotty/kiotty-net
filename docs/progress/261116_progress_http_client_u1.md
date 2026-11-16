# 2026-11-16 진행 정리 : HttpClient v1 사이클 U1 (머리와 조각)

기준 문서: `docs/design/design_http_client_v1.html` 결정 1 ~ 9, 2 ~ 4절, 9절의 U1 행. 넘김은 `261116_handoff_http_client_v1.md`.

## 공개 API (`kiotty_net_http.h`)

| 것 | 내용 |
|---|---|
| `KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN` | `UINT64_MAX`. begin 의 body_length 가 이 값이면 chunked |
| `KiottyNet_beginHttpRequest(client, request, headers, header_count, body_length, timeout)` | 검사 → 머리 송신. `request.body` 가 비어 있지 않으면 `INVALID_ARGUMENT` |
| `KiottyNet_writeHttpRequestBody(client, data, length, timeout)` | `{ length, SUCCESS }` 또는 `{ 0, 오류 }`. 길이 0 은 아무것도 보내지 않고 `{ 0, SUCCESS }` |
| `KiottyNet_finishHttpRequest(client, timeout)` | chunked 면 `0\r\n\r\n`, 응답 머리까지. 응답 body 는 v0 의 `readHttpReplyBody` |

C++ wrapper 는 U3.

## 안에서

- **조립기** (`kiotty_http_request_head.*`): `RequestHead::body_length` 를 `uint64_t` 로. `CHUNKED_BODY_LENGTH` (= UNKNOWN) 이면 `Content-Length` 대신 `Transfer-Encoding: chunked`, Content-Type 은 body 가 있는 것으로 본다. 그 줄도 `max_headers` 에 센다. `TRANSFER_ENCODING` 은 공유 헤더 `kiotty_http_constants.h` 로 옮겼다 (message_framing 과 이름이 겹침, 단일 헤더 규칙).
- **올리기 규칙** (`domain/usecases/http/kiotty_http_upload.*`, 새 파일): `HttpUpload { left, method, mode }` (mode IDLE / LENGTH / CHUNKED) 와 순수 함수 `startUpload`, `checkUploadPiece` (IDLE, `INT32_MAX` 초과, 남은 길이 초과 → `INVALID_ARGUMENT`), `takeUploadPiece`, `checkUploadEnd` (IDLE, 모자람 → `INVALID_ARGUMENT`).
- **`HttpClientExchange`** (`context/kiotty_http_client_exchange.*`, 새 파일): `send`, `readBody`, `begin`, `write`, `finish`. HttpClient 의 공개 메서드가 이미 7 개였고 `kiotty_http_client.cpp` 가 380 줄이라 begin / write / finish 를 더할 자리가 없었다. 요청 / 응답의 오고 감을 세부 클래스로 나눴다 (4.19). HttpClient 의 공개 메서드는 `initialize, reset, connect, exchange, headers, link` 여섯.
  - 4.19 와 다른 점: 세부 클래스가 멤버 reference 묶음이 아니라 `HttpClient&` 를 들고 HttpClient 가 `friend class HttpClientExchange` 를 둔다. 오고 감이 StreamClient 의 protected (`readLine`, `peekReceived`, `reconnect`) 와 HttpClient 의 private 도우미 (머리 조립, 송신, 응답 머리 수신, body pump) 를 거의 다 쓰기 때문이다. `TlsEngine` / `TlsPipe` 와 같은 모양.
  - 상태와 공통 도우미 (`writeHead`, `transmit`, `writeAll`, `receiveReply`, `pumpBody`, `dropUnreadReply`, `replyOf`) 는 HttpClient 에 남았다. v0 의 `exchange / attempt / resend` 는 그대로 옮겼다.
- **올리기 상태** `HttpClient::_upload`. initialize / reset / connect 가 IDLE 로.
  - begin: 검사 (대상, 빈 body, type, 머리 조립) 가 실패하면 아무것도 바꾸지 않는다. 통과하면 올리던 것을 연결과 함께 버리고 (결정 6), 남은 응답 body 가 있으면 v0 처럼 연결을 닫고, 닫혀 있으면 다시 연결해 머리를 보낸다. 송신 실패는 연결을 닫는다.
  - write: chunked 면 `hex\r\n` (기존 `chunkHeadOf`, stack 의 FixedString) / 조각 / `\r\n` 을 차례로 보낸다 (StreamClient::write 를 세 번. TCP_NODELAY 라 Nagle 지연 없음). 조각은 앱 버퍼에서 바로. 송신 오류는 연결을 닫고 IDLE.
  - finish: 모자람이면 연결을 닫고 `INVALID_ARGUMENT`. 아니면 끝 표시를 보내고 v0 의 `receiveReply`.
  - 연결이 닫혀 있으면 (disconnect, close, 송신 오류) 올리기는 IDLE 로 본다 (`settleUpload`). 그래서 `disconnectHttpClient` / `closeHttpClient` 는 바꾸지 않았다.
  - 다른 요청 (`send`) 은 올리던 것을 연결과 함께 버린다. 올리는 중의 `readBody` 는 `INVALID_ARGUMENT`.
- 번역 층: `clientRequestOf` 로 sendHttpRequest 와 beginHttpRequest 의 인자 검사를 나눠 쓴다.

## U2 로 넘긴 것

begin 의 살아 있는지 보기 (peek), 정할 것 1 (write 가 `PEER_CLOSED` / `SOCKET_ERROR` 뒤 finish 가 먼저 온 응답을 돌려줌. 지금은 write 오류 뒤 finish 가 `INVALID_ARGUMENT`), close 로 깨우기와 TLS 의 integration 확인.

## 테스트

- unit `kiotty_test_http_request_head.cpp` +4: chunked 머리 (Content-Type 과 Transfer-Encoding), CUSTOM type 의 chunked, 4 GiB 넘는 Content-Length, chunked 줄이 max_headers 에 셈.
- unit `kiotty_test_http_upload.cpp` 7: 길이 세기와 끝, 넘침은 거절하고 남은 길이 유지, 길이 0, chunked 는 한도 없음, `INT32_MAX` 넘는 조각, IDLE, chunk 머리 hex.
- integration `kiotty_test_http_client_upload.cpp` 14: 우리 HttpServer `/echo` 로 Content-Length 와 chunked 100000 바이트를 7000 바이트 조각으로 (서버가 받은 body 비교), 길이 0 과 빈 write, 넘침 거절 뒤 계속, 모자란 finish 는 연결을 닫고 다음 요청이 다시 연결 (서버 PUT 0 번), begin 없는 write / finish, 인자 검사와 거절된 begin 이 올리기를 지킴, connect 전 begin, 올리는 중의 readBody, 올리는 중의 GET / begin 이 올리기를 버림, 남긴 응답 body 를 begin 이 버림, disconnect 가 올리기를 끝냄, raw peer 로 chunked / Content-Length 의 보낸 바이트 그대로.

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC (`build/msvc`) | 820 | 847 |
| WSL gcc / gcc-tls / clang / clang-tls / asan-tls / tsan-tls | 823 | 848 |
| WSL release-tls | 822 | 847 |

모두 통과. 수칙 검사 기준선 그대로 (FILE 1, LINE 4, MACRO 18, PUBLIC 3). 단일 헤더를 임시 위치에 만들어 번역 단위 하나로 g++ 컴파일 확인 (commit 된 헤더는 소스 commit 뒤 다시 만든다).

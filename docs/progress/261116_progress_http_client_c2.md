# 2026-11-16 진행 정리 : HTTP 클라이언트 사이클 C2 (요청과 read)

기준 문서: `docs/design/design_http_client_v0.html` 3절 (연결과 수명), 4절 (요청), 5절 (body 읽기), 7절 (C 헤더), 9절 (안에서), 11절의 C2 행. C1 은 `261115_progress_http_client_c1.md`.

C3 의 몫 (chunked, 닫힘까지의 body, read 의 TIMEOUT 뒤 이어 읽기, 다 읽지 않은 body 뒤의 요청) 도 같이 들어갔다. C4 의 몫 (멱등 메서드 재시도, `closeHttpClient`) 은 아직이다.

## 정한 것 (사용자 결정)

| 것 | 결정 |
|---|---|
| 줄 읽기 | (a) `ConnectionStream` 에 줄 읽기를 더한다. StreamClient 가 `receive_capacity` 로 잡아 두고 쓰지 않던 수신 Stage 를 줄 버퍼로 쓴다 |
| pipe 연결의 Host | `localhost`. WebSocket 클라이언트도 같은 함수 (`hostFieldOf`) 를 쓴다 |

## 공개 API (`include/kiotty_net_http.h`)

설계 4절, 7절의 모양 그대로: `KiottyNetHttpClientHandle`, `KiottyNetHttpClientResult`, `KiottyNetHttpReply`, `KiottyNetHttpClientRequest`, create / destroy / connect / disconnect, `setHttpClientHeader` / `clearHttpClientHeaders`, `makeHttpHeader` / `makeHttpClientRequest`, `sendHttpRequest` 와 메서드별 일곱, `readHttpReplyBody`, `findHttpReplyHeader`, `getHttpAllowedMethods`.

`closeHttpClient` 는 C4 에서 선언한다 (구현 없는 선언을 두지 않는다). C++ wrapper 는 C5.

## 안에서

| 자리 | 내용 |
|---|---|
| `ConnectionStream::readLine(timeout)` (`kiotty_connection_stream_line.cpp`) | 수신 Stage 를 socket (TLS 면 복호화 결과) 으로 채우고 LF 까지의 줄을 돌려준다 (LF 와 그 앞의 CR 하나를 뺀 Span, 다음 stream 호출까지 유효). LF 까지만 소비하므로 뒤의 바이트 (머리와 같이 온 body 앞부분) 는 Stage 에 남고 `read()` 가 그것부터 준다. 줄이 Stage 에 들어가지 않으면 `BUFFER_FULL`. 기존 `read()` 는 그대로 |
| `StreamClient` | `connect` 를 `prepare` (검사, TLS 프로파일) + `reconnect` (프로파일 재사용) 로 나눴다. 동작은 같다. HttpClient 용 protected 메서드 여섯 (줄 읽기, Stage 들여다보기 / 소비 등). 공개 메서드 수는 그대로 |
| `HttpClient` (`context/kiotty_http_client.*`) | StreamClient 를 private 상속 (public 이면 StreamClient 의 read / write 가 BUSY 를 건너뛴다). 공개 메서드 7. 공통 헤더 set / remove / clear 는 `HttpClientHeaders` (수칙 4.19) |
| 머리 버퍼 | `2 × max_header_bytes` 하나를 반으로: 앞은 공통 헤더 (차면 `BUFFER_FULL`), 뒤는 요청마다의 머리 (넘으면 `REQUEST_TOO_LARGE`). 경계가 움직이는 한 버퍼면 공통 헤더가 두 번 세어지고 요청 상한이 공통 헤더만큼 줄어든다. 응답 머리 저장소는 따로 `max_header_bytes` |
| body 읽기 | 모든 framing 이 같은 길: 수신 Stage 를 들여다보고 `HttpBodyReader::take` 로 앱 버퍼에 옮긴 뒤 옮긴 만큼 소비. chunk 를 앱 버퍼 안에서 벗기면 겹치는 복사가 된다. 평문 body 는 Stage 를 한 번 거친다 |
| BUSY | `core/kiotty_busy_guard.h`: atomic 플래그를 compare-exchange 로 잡고 scope 끝에 놓는다. connect, disconnect, 요청 전부, readBody, 공통 헤더 set / remove / clear 가 잡는다. disconnect 는 `void` 라 BUSY 면 아무것도 하지 않는다. `make*`, `find*`, `getHttpAllowedMethods`, create / destroy 는 잡지 않는다 |
| 민감 데이터 | 요청 머리 영역은 보낸 뒤 (실패도) 0 으로. 공통 헤더는 remove / clear / destroy 때 0 으로 |

## 동작의 세부 (설계에 없던 것)

- 검사에서 실패한 요청 (`INVALID_ARGUMENT`, `REQUEST_TOO_LARGE`, `TABLE_FULL`, `INVALID_ENCODING`, connect 한 적 없음) 은 이전 응답과 읽지 않은 body 를 건드리지 않는다. 검사를 통과한 요청만 이전 응답을 끝낸다.
- 응답 헤더는 disconnect 뒤에도 다음 요청 / connect / destroy 까지 유효하다.
- 1xx 는 16 번까지 건너뛰고 그 뒤는 `PROTOCOL_ERROR`.
- body 중간에 앱이 disconnect 하면 readBody 는 `CLOSED`. 서버가 body 를 끝내기 전에 끊으면 그 뒤의 read 는 계속 `PEER_CLOSED`.
- readBody 의 buffer NULL / length 0 은 `INVALID_ARGUMENT`. `makeHttpHeader` 에 NULL / 너무 긴 / UTF-8 아닌 값을 넣으면 빈 헤더를 돌려주고 요청이 `INVALID_ARGUMENT` 로 거절한다. OPTIONS 가 아닌 메서드의 `*` 는 `INVALID_ARGUMENT`.
- 문자열 입력 상한 `MAX_HTTP_HEADER_BYTES` (1 MiB) 를 `kiotty_http_configuration.h` 에 두고 기존 header capacity 범위도 같은 상수를 쓴다.

## 설계와 다른 것 (C4 에서 다룬다)

- 설계 3절은 "요청의 timeout 이 (다시) 연결 + 송신 + 머리 수신" 이다. 지금 다시 연결은 StreamClient 의 connect 설정 (connect timeout, 재시도) 을 따르고 호출의 timeout 에 들지 않는다. C4 (다시 연결과 재시도) 에서 deadline 을 넘기게 고친다.

## 테스트

34 개 추가.

- `ConnectionStream` 줄 읽기 / 채우기 8 (TLS 하나 포함): 나뉘어 오는 줄, CR 없는 LF, 줄 뒤 바이트가 `read()` 로 나옴, Stage 보다 긴 줄의 `BUFFER_FULL`, timeout
- HttpClient integration 19 (우리 HttpServer 와): 메서드별 왕복, 4xx 의 status 와 body, 작은 버퍼로 여러 번 read, 응답 머리 `BUFFER_FULL`, keep-alive 로 여러 요청 (같은 연결), 공통 헤더 (서버에 보임, 요청 헤더가 덮음, NULL 로 지움, clear), 헤더 주입 거절, `REQUEST_TOO_LARGE`, 두 thread 의 동시 호출 `BUSY`, HEAD, Work 스트림의 chunked 응답을 `{ 0, SUCCESS }` 까지, `Connection: close` 뒤 다시 연결, 읽지 않은 body 뒤의 요청, read 의 TIMEOUT 뒤 이어 읽기, connect 한 적 없음, pipe 연결의 `Host: localhost`, 1xx 건너뛰기와 101 의 `PROTOCOL_ERROR`, 길이 없는 body 가 서버의 닫힘으로 끝남, 서버가 body 중간에 끊으면 `PEER_CLOSED`, OPTIONS 의 Allow
- unit 7: 응답 헤더 찾기 / Allow 분해 5, Host (pipe 의 localhost 등) 2
- 공개 헤더 layout 테스트에 Reply, ClientRequest, ClientResult 추가

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC | 758 | 792 |
| wsl-gcc, gcc-notls, clang, clang-notls, clang-asan, clang-tsan | 759 | 793 |
| wsl-clang-release | 758 | 792 |

`tools/check_rules.py`: FILE 1, LINE 4, MACRO 18, PUBLIC 3 (그대로).

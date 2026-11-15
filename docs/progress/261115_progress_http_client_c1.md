# 2026-11-15 진행 정리 : HTTP 클라이언트 사이클 C1 (파서와 머리)

기준 문서: `docs/design/design_http_client_v0.html` 4절 (요청), 5절 (body 읽기), 6절 (응답을 읽는 규칙), 9절 (안에서), 11절의 C1 행.

공개 헤더 (`include/`) 는 그대로다. 공개 API 와 StreamClient 연결은 C2.

## 새로 만든 것

| 파일 (`src/implementation/`) | 하는 일 |
|---|---|
| `domain/usecases/http/kiotty_http_status_line.*` | `parseStatusLine`: `HTTP/1.1 NNN` / `HTTP/1.0 NNN` + 선택 reason (버림). 세 자리, 100 ~ 599. 아니면 `PROTOCOL_ERROR` |
| `domain/usecases/http/kiotty_http_reply_framing.*` | `selectReplyFraming(method, status line, headers)`: body 종류 `ReplyBodyKind { NONE, LENGTH, CHUNKED, UNTIL_CLOSE }`, keep-alive (`Persistence`), 1xx 이면 `phase = INTERIM`. HEAD 의 응답 / 204 / 304 는 NONE, TE·CL 이 없으면 UNTIL_CLOSE (그때 keep-alive 아님), 101 과 범위 밖은 `PROTOCOL_ERROR`. 서버의 `selectBodyFraming` / `decidePersistence` 를 그대로 쓴다 |
| `domain/usecases/http/kiotty_http_body_reader.*` | `HttpBodyReader::take(input, output) -> { consumed, produced, code, is_finished }`: 받은 바이트를 body 로. chunk 크기 줄은 안의 256 바이트 버퍼 (`CHUNK_LINE_BYTES`) 에 모은다 (넘으면 `PROTOCOL_ERROR`), extension 과 trailer 는 버린다, data 뒤 CRLF (LF 만도 받음). `closeInput()` 은 UNTIL_CLOSE 면 `SUCCESS` (body 끝), 아니면 `PEER_CLOSED`. 실패 뒤로는 같은 code 만 준다 |
| `domain/usecases/http/kiotty_http_request_check.*` | `checkRequestHeader` (name 은 token, value 는 서버의 `isFieldValue`, Host / Content-Length / Transfer-Encoding / Connection 금지), `checkRequestPath` (`/` 로 시작하는 보이는 ASCII, OPTIONS 만 `*`), `checkRequestBody` (`BY_EXTENSION` 금지, 텍스트 계열은 UTF-8 아니면 `INVALID_ENCODING`) |
| `domain/usecases/http/kiotty_http_request_head.*` | `buildHostField(host, port, default_port)` (기본 포트면 포트 없음, IPv6 literal 은 `[...]`, 이미 괄호면 그대로, 길이는 `KIOTTY_NET_MAX_HOST_LENGTH` + 괄호 + `:65535` 의 FixedString), `writeRequestHead(buffer, RequestHead)` |
| `data/repository/http/kiotty_http_reply_store.*` | `HttpReplyStore`: 응답 머리 저장소 (`begin`, `takeStatusLine`, `takeHeader`, `finish(method)`). 상태줄은 `max_header_bytes` 에 세지만 복사하지 않는다. 바이트 / 개수가 차면 `BUFFER_FULL`, 깨지면 `PROTOCOL_ERROR` |
| `data/repository/http/kiotty_http_shared_headers.*` | `HttpSharedHeaders`: 공통 헤더 (`set(name, value)`, `remove(name)`, `clear`, `list`). 같은 이름은 바꾸고 (끝으로 옮겨짐) value 는 앞뒤 공백을 잘라 둔다. 지우면 뒤 text 를 당기고 (겹치는 복사는 앞에서부터 한 바이트씩) 비운 자리를 0 으로 채운다. 찰 때 `BUFFER_FULL` 이고 그때 저장소는 그대로 |

### `writeRequestHead` 가 쓰는 순서

1. `METHOD path HTTP/1.1`
2. `Host`
3. 공통 헤더. 요청 헤더에 같은 이름이 있는 것은 빼고, 라이브러리가 Content-Type 을 쓸 때는 공통 헤더의 Content-Type 도 뺀다 (같은 이름이 둘 나가지 않게. 결정 14)
4. 요청 헤더 (value 는 앞뒤 공백을 잘라서)
5. body 가 있으면 `Content-Type` (type 이 CUSTOM 이거나 요청 헤더에 Content-Type 이 있으면 쓰지 않음) 과 `Content-Length`. POST / PUT / PATCH 는 body 가 없어도 `Content-Length: 0`
6. 빈 줄

검사는 쓰기 전에 한다: path, 요청 헤더의 글자와 금지 이름. 헤더 줄 수 (Host 포함) 가 `max_headers` 를 넘으면 `TABLE_FULL`, buffer 를 넘으면 `REQUEST_TOO_LARGE`.

## 고친 것 (있던 것을 다시 쓰려고)

| 파일 | 고침 |
|---|---|
| `kiotty_http_token.*` | `equalsLiteral` 추가. request line 의 비공개 사본을 이것으로 |
| `kiotty_http_request_line.*` | `parseHttpVersion`, `methodTextOf` 를 공개 (`methodTextOf` 는 있던 메서드 표를 쓴다) |
| `kiotty_http_response_text.*` | `allowListOf` 가 `methodTextOf` 를 쓰고 따로 있던 메서드 이름 표를 지움. `decimalOf` 공개, 타입 이름 `DigitText` → `DecimalText` |
| `kiotty_http_header_line.*` | `isFieldValue`, `isFieldCharacter` 공개 |
| `kiotty_websocket_client_machine.cpp` | `isSwitchingLine` 이 `parseStatusLine` 을 쓴다 (`status == 101 && version == V11`). reason 에 제어 글자가 있으면 이제 `PROTOCOL_ERROR` (전에는 받았다) |

## 설계와 다르게 / 더 엄격하게 한 것

- Content-Length 가 여럿이면 값이 같아도 `PROTOCOL_ERROR` (서버의 `selectBodyFraming` 그대로). 설계 6절은 "다르면" 이었다.
- `Transfer-Encoding` 이 `chunked` 하나가 아니면 (`gzip, chunked` 등) `PROTOCOL_ERROR`. 압축을 하지 않으므로 (결정 9) 읽을 수 없다.
- 요청 헤더 value 의 UTF-8 이 깨져 있으면 `INVALID_ARGUMENT` (서버의 `isFieldValue` 를 같이 쓴다).

## C2 로 넘기는 것

- 줄 읽기를 StreamClient 에 둘지 HttpClient 자기 버퍼로 할지 (9절). C1 의 조각은 줄과 바이트 span 만 받으므로 어느 쪽이든 그대로 쓴다.
- WebSocket 클라이언트의 `appendHost` (`context/kiotty_websocket_client.cpp`) 는 `buildHostField` 와 겹친다. pipe 전송의 host 는 파일 경로라 공백이 있을 수 있어 바꾸지 않았다.

## 테스트

unit 89 개 추가: `kiotty_test_http_{status_line,reply_framing,reply_store,body_reader,request_head,shared_headers}.cpp`.

- 상태줄: 깨진 줄, HTTP/1.0, reason 의 obs-text, 범위 (99 / 100 / 599 / 600), 수칙 2.10 의 다섯 입력
- framing: HEAD / 1xx / 204 / 304 / chunked / length / 닫힘까지 / TE 와 CL 같이 / keep-alive
- 머리 저장소: `BUFFER_FULL` (바이트, 개수), 1xx 뒤 다시 시작
- body reader: chunk 를 한 바이트씩 넣기 (모든 경계), extension, trailer, 깨진 CRLF, 긴 크기 줄, hex 넘침, 작은 출력 버퍼로 LENGTH 나눠 읽기, UNTIL_CLOSE 와 닫힘
- 요청 머리: GET / POST / `OPTIONS *` 의 바이트, Host (기본 포트, 다른 포트, IPv6), 공통 헤더를 요청 헤더가 덮기, value 의 CR / LF / NUL, 잘못된 이름, 금지 이름, body 없는 POST 의 `Content-Length: 0`, CUSTOM, `REQUEST_TOO_LARGE` 경계 (N 이면 들어가고 N-1 이면 실패), `TABLE_FULL` 경계
- 공통 헤더: 바꾸기 / 지우기 / clear / 실패 때 그대로 / 지운 뒤 다른 항목이 온전함

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC | 669 | 758 |
| wsl-gcc, gcc-notls, clang, clang-notls, clang-asan, clang-tsan | 670 | 759 |
| wsl-clang-release | 669 | 758 |

`tools/check_rules.py`: FILE 1, LINE 4, MACRO 18, PUBLIC 3 (그대로).

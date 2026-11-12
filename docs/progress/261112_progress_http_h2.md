# 2026-11-12 진행 정리 : HTTP 사이클 H2 (파서)

기준 문서: `docs/design/design_http_v0.html` 4.3 (content type), 8절, 11절의 H2 행. `docs/requirement/implementation_guide_v0.md` 4.3 (요청 공급).

## 만든 것

`src/implementation/domain/usecases/http/` 에 파일 여섯. 상태도 할당도 없는 함수뿐이다. HttpSession 과 표 (H3) 가 이것을 부른다.

| 파일 | 함수 | 실패 |
|---|---|---|
| `kiotty_http_token` | `isTokenCharacter`, `isVisibleCharacter`, `isWhitespace`, `isToken`, `skipToken`, `skipWhitespace`, `findCharacter`, `trimWhitespace`, `spanOf(KiottyNetSpan)`, `equalsLiteralIgnoreCase` | 없음 (도우미) |
| `kiotty_http_request_line` | `parseRequestLine(line) -> RequestLine { path, query, method, version }` | 400 모양 / 문자 / 버전, 414 target 이 `KIOTTY_NET_MAX_URL_LENGTH` 이상, 501 지원하지 않는 method |
| `kiotty_http_header_line` | `parseHeaderLine(line, max_length) -> KiottyNetHttpHeader` | 431 줄이 남은 헤더 공간보다 김, 400 이름 / 콜론 / 값의 문자 / UTF-8 |
| `kiotty_http_message_framing` | `parseContentLength`, `parseChunkSize`, `selectBodyFraming(headers) -> { length, NONE / LENGTH / CHUNKED }`, `decidePersistence(version, headers) -> CLOSE / KEEP_ALIVE` | 400 숫자 아님 / 넘침 / 모호한 경계, 501 chunked 아닌 Transfer-Encoding |
| `kiotty_http_route_path` | `parseRoutePath(text)`, `parseDirectoryPrefix(text) -> RoutePath { text, EXACT / PREFIX }`, `matchesPath(route, path)` | `INVALID_ARGUMENT`, `INVALID_ENCODING` (등록 API 의 코드) |
| `kiotty_http_content_type` | `contentTypeTextOf(type)`, `textKindOf(type) -> BINARY / UTF8 / UTF8_WITH_CHARSET`, `contentTypeOfPath(path)`, `parseCustomContentType(text)` | `INVALID_ARGUMENT`, `INVALID_ENCODING` |

정한 것:

- 요청을 읽는 파서는 `Result<…, KiottyNetHttpStatus>` 를 돌려준다. 실패의 code 가 그대로 자동 응답의 status 다 (`Code{}` 인 0 은 status 가 아니다). 등록 API 쪽 파서 (라우트 경로, CUSTOM 이름) 는 `KiottyNetCode`.
- request target 은 origin-form (`/` 로 시작) 과 OPTIONS 의 `*` 만 받는다. absolute-form, `#`, 보이는 ASCII 밖의 바이트는 400. method 는 대소문자를 가린다 (`get` 은 token 이라 501).
- 버전은 `HTTP/1.1`, `HTTP/1.0` 만. 그 밖은 400 (505 는 status enum 에 없다).
- header 값은 SP / HTAB 와 보이는 문자, 0x80 이상만 받고 UTF-8 이어야 한다. CR, LF, NUL, DEL 은 400. 줄 앞의 공백 (obs-fold) 은 이름이 비어 400.
- body 경계 (요청 밀반입 방지): Transfer-Encoding 과 Content-Length 가 같이 오거나, 어느 하나가 두 번 오면 400. Transfer-Encoding 은 `chunked` 하나만 (대소문자 무시), 그 밖은 501. Content-Length 는 십진 숫자만 (부호, 공백, 목록은 400).
- keep-alive: `Connection` 의 목록에 `close` 가 하나라도 있으면 닫는다. 없으면 `keep-alive` 가 있으면 유지, 둘 다 없으면 1.1 은 유지, 1.0 은 닫는다.
- 라우트 경로: `/` 로 시작, 보이는 ASCII, `?` `#` 없음, `*` 는 끝의 `/*` 에서만 (`/a*`, `/a/*/b` 는 INVALID_ARGUMENT). `/api/*` 는 접두 `/api/` 로 바뀌고 `/api/` 와 그 아래를 맞춘다 (`/api` 는 아니다). 디렉토리 접두는 `/` 로 끝나야 한다. 비교는 대소문자를 가린다.
- content type 표는 enum 순서의 배열 하나와 `static_assert` 둘 (개수, 순서). `; charset=utf-8` 은 텍스트 묶음 (`TEXT_*`, `JAVASCRIPT`, `EVENT_STREAM`) 에만 붙는다. UTF-8 검사는 그 묶음과 구조화 데이터, DICOM / FHIR 의 JSON / XML, SVG (`textKindOf`). user guide 1.4 의 같은 줄을 이것에 맞춰 고쳤다.
- 확장자는 마지막 `/` 또는 `\` 뒤의 마지막 `.` 뒤다. 모르는 확장자와 확장자 없음은 OCTET_STREAM.
- CUSTOM 이름: `parseText` (UTF-8, 63 바이트) 뒤에 `token/token` 과 `; 이름=값` (값은 token 또는 quoted-string) 이 0 개 이상. 끝의 공백, CR / LF, 닫히지 않은 따옴표는 INVALID_ARGUMENT.
- `core/kiotty_text` 의 `isUtf8(Span)` 를 공개했다 (설계 8절: 텍스트 body 와 chunk 에 같은 함수).

## 테스트

unit 69 개 (`HttpRequestLine` 14, `HttpHeaderLine` 10, `HttpContentLength` 6, `HttpChunkSize` 5, `HttpBodyFraming` 5, `HttpPersistence` 3, `HttpRoutePath` 10, `HttpDirectoryPrefix` 2, `HttpContentTypeTable` 4, `HttpContentTypeOfPath` 2, `HttpCustomContentType` 8). parse 함수마다 수칙 2.10 의 다섯 입력 (빈 입력, 최대 길이, 최대 + 1, 중간 NUL, 잘못된 UTF-8) 이 있다.

- 길이 상한이 없는 파서는 그 파서의 상한으로 본다: request target 은 `KIOTTY_NET_MAX_URL_LENGTH - 1`, header 줄은 인자 `max_length`, Content-Length 는 20 자리, chunk 크기는 16 자리, 라우트 경로는 255, CUSTOM 이름은 63.
- C 문자열을 받는 파서 (라우트 경로, CUSTOM) 는 중간 NUL 에서 끝난다 (`parseText` 와 같다). span 을 받는 파서는 NUL 을 문자로 보고 400.

테스트를 쓰다 같은 실수를 두 번 했다: 문자열 리터럴을 `spanOf(const std::string&)` 에 넘겨 임시 문자열의 span 을 검사했다. 리터럴용 `spanOf(const char*)` 를 두고 만든 문자열은 이름을 붙여 고쳤다.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 584 / 584 |
| WSL gcc / clang Debug, TLS OFF / ON | 585 / 585 넷 |
| WSL asan,undefined TLS ON, thread TLS ON | 585 / 585, 585 / 585 |
| WSL gcc Release TLS ON | 584 / 584 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3). 처음 판의 `strlen` 셋 (수칙 2.3) 은 `std::char_traits<char>::length` 로, 100 자를 넘은 줄 23 개는 짧은 상수 이름과 줄 나누기로 고쳤다.

## 다음

H3 (완성 응답): HttpSession, 라우트 표, HTTP_MACHINE, setter, 자동 응답, keep-alive, `createHttpServer` / `runHttpServer`, `onHttpDisconnected` / `onHttpError`, HttpServer 를 Context 풀에. integration 은 공개 C API 만.

# 2026-11-12 진행 정리 : HTTP 사이클 H5 (정적 서빙과 setHttpFile)

기준 문서: `docs/design/design_http_v0.html` 1절 결정 6 / 13, 4.6 의 `setHttpFile`, 4.9 의 정적 서빙, 7절 (정적 파일), 11절의 H5 행. `docs/requirement/user_guide_http.md` 1.6. 구현 가이드 4.7 (파일 조각).

## 만든 것

| 층 | 파일 | 하는 일 |
|---|---|---|
| data/repository/http | `kiotty_http_registry` | 표의 항목에 `StaticKind` (NONE, DIRECTORY, FILE, MEMORY), 파일 경로 (`KIOTTY_NET_MAX_PATH_LENGTH`), 메모리 span, content type. `addDirectory`, `addFile`, `addMemory`. 정적 항목의 method 는 GET \| HEAD 라 405 / OPTIONS 의 Allow 는 `GET, HEAD, OPTIONS`. 찾으면 `MatchKind::STATIC` 과 `StaticTarget` |
| | `kiotty_http_file_body` (새) | `attachFile` (열고, BY_EXTENSION 이면 확장자로, `BodyStorage::FILE` 로 body), `pieceAt` (송신 조각. 파일 body 면 응답 버퍼에 `send_capacity` 만큼 읽는다), `detachFile` |
| | `kiotty_http_response_store` | `BodyStorage::FILE` 과 `file_length`. Content-Length 는 파일 크기. `ResponseFrame::filePieceAt` 이 다음 조각의 파일 offset 과 staging 자리 (응답 버퍼 앞부터, 헤더는 이미 나갔다) 를 준다 |
| | `kiotty_http_session`, `kiotty_http_session_pool` | 풀이 Session 마다 `InputFile` 하나를 create 때 잡고 Session 은 참조를 든다. 다음 요청과 반납 때 닫는다 |
| domain/usecases/http | `kiotty_http_static` (새) | `answerStatic`: 메모리는 참조 body, 파일은 `attachFile` (없으면 404), 디렉토리는 prefix 뒤를 `isSafeRelativePath` 로 보고 (아니면 403) 디렉토리 경로에 붙여 연다 |
| | `kiotty_http_route_path` | `hasParentSegment` (`/` 와 `\` 로 나눈 `..` 조각), `isSafeRelativePath` (`..` 조각, `\`, `:`, NUL 거부) |
| | `kiotty_http_exchange` | `STATIC` 자동 응답, 송신 조각을 `pieceAt` 으로 (파일을 못 읽으면 `NOT_FOUND` 로 닫는다), 다음 요청에서 파일을 닫는다 |
| C API | `src/kiotty_net_http.cpp` | `setHttpFile`, `serveHttpDirectory`, `serveHttpFile`, `serveHttpMemory` |

### 동작

- 디렉토리: `url_prefix` 는 `/` 로 끝나야 한다. 디렉토리 경로가 `/` 나 `\` 로 끝나지 않으면 등록 때 `/` 를 붙인다. 요청 경로의 prefix 뒤에 `..` 조각, `\`, `:` 가 있으면 403 (Windows 의 구분자와 드라이브 / ADS 를 막는다). 붙인 경로가 `KIOTTY_NET_MAX_PATH_LENGTH` 를 넘거나 파일이 없거나 디렉토리면 404.
- 파일 하나: content type 은 등록 때 확장자로 정한다. 요청 때 열고 없으면 404.
- 메모리: 복사하지 않는다. type 은 목록 안이어야 하고 BY_EXTENSION, EVENT_STREAM, CUSTOM 은 `INVALID_ARGUMENT`. 텍스트 계열이면 등록 때 UTF-8 (아니면 `INVALID_ENCODING`).
- `setHttpFile`: 경로에 `..` 조각이 있으면 `INVALID_ARGUMENT`, 열지 못하면 `NOT_FOUND`. 절대 경로 (`C:\...`) 는 된다. Work 안에서도 쓸 수 있다.
- 파일은 `send_capacity` 조각으로 읽어 write lane 으로 보낸다 (sendfile 없음, 1절 결정 6). HEAD 는 Content-Length 만.
- 등록은 run 전에만 (`BUSY`), 같은 경로 + GET 은 `DUPLICATE_RULE`.

### 설계와 다르게 한 것 / 남긴 것

- 디렉토리 요청의 index 파일 (`/static/` → `index.html`) 은 없다. 디렉토리 경로는 404.
- 경로의 percent-decoding 은 하지 않는다 (설계 그대로). `%2e%2e` 는 그 이름의 파일을 찾는다.
- benchmark (H4 의 chunk latency 와 큰 파일) 는 아직 없다.

## 테스트

- integration `tests/integration/kiotty_test_http_static.cpp` 7 개: 디렉토리 밑 파일과 확장자별 content type, HEAD 의 Content-Length, 없는 파일 / 디렉토리 404, `..` / `\` / `:` 403 과 `index..html` 은 404, 메모리 바이너리 + HEAD + POST 405 Allow, `send_capacity × 5 + 3` 바이트 파일 뒤 keep-alive, 라우트의 `setHttpFile` (`..`, 없음, EVENT_STREAM, 성공), 등록 규칙.
- unit `HttpRelativePath` 둘.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 627 / 627 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 628 / 628 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 628 / 628, 628 / 628 |
| WSL clang Release TLS ON | 627 / 627 |

tsan 에서 처음에 하나가 실패했다 (`MemoryServesBinaryAndOnlyGetAndHead`). 테스트가 `serveHttpMemory` 에 준 데이터를 TestBody 의 지역 변수로 두어 서버보다 먼저 지웠다 (라이브러리는 설계대로 복사하지 않는다). fixture 멤버로 옮겨 서버 close 뒤까지 살게 했고 `--gtest_repeat=3` 과 tsan 전체로 확인했다.

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 다음

H6 (문서와 MCP 예제): requirement 9절을 이 판으로, 구현 가이드 4절에 스트림과 content type, 테스트 계획 17절, 6절의 MCP 서버 골격을 integration 테스트로. H4 / H5 의 benchmark 도 함께.

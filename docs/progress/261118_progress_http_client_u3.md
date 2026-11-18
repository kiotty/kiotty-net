# 2026-11-18 진행 정리 : HttpClient v1 U3 (도우미, C++, 문서)

기준 문서: `docs/design/design_http_client_v1.html` 8절 정할 것 3 (파일 도우미), 5절 (C++), 9절의 U3 행. 앞 사이클은 `261118_progress_write_arrival_a3.md` (먼저 온 응답) 와 `261118_progress_http_client_u2.md`.

## 사용자 결정

- `sendHttpFile` 은 파일을 **조각마다 base writeFile** 로 보낸다 (2026-11-18). 파일 하나를 writeFile 한 번에 보내면 A1 결정 (writeFile 은 시작할 때만 도착을 봄) 때문에 보내는 중에 온 413 / 401 은 연결이 끊긴 뒤에야 보이고 Windows 는 RST 로 그 응답을 잃을 수 있다. 라이브러리가 파일을 읽어 write 로 보내는 길은 복사 버퍼가 더 든다.

## 공개 동작

- `KiottyNet_sendHttpFile(client, request, headers, header_count, path, timeout)`: 파일 크기로 `Content-Length` 를 쓰고 (빈 파일은 0) 조각마다 보낸 뒤 응답 머리까지 받는다. 서버가 먼저 응답하면 (1xx 는 넘김) 그 응답을 돌려준다.
  - 열 수 없는 파일은 `NOT_FOUND` (보낸 것 없음). NULL / 빈 path 는 `INVALID_ARGUMENT`, 깨진 UTF-8 path 는 `INVALID_ENCODING`.
  - 조각 크기는 설정의 `send_capacity` (0 이면 16 KiB, `MemoryLimits::SEND_CAPACITY`). 조각마다 파일을 다시 연다.
  - timeout 은 (다시) 연결부터 응답 머리까지 하나다.
  - 보내는 중에 파일이 줄면 writeFile 의 `INVALID_ARGUMENT` 로 연결을 닫는다.
- C++ `KiottyNet_HttpClient`: `begin(request[, headers], body_length, timeout)`, `write(data, length, timeout)`, `write(KiottyNetSpan, timeout)`, `finish(timeout)`, `sendFile(request[, headers], path, timeout)`. 헤더 목록이 넘친 판은 C 를 부르지 않고 `TABLE_FULL` (begin 은 code, sendFile 은 Reply).
- `DATA_ARRIVED_WHILE_WRITING` 의 HttpClient 뜻 ("서버의 최종 응답이 와 있다. finish 를 부른다") 을 user guide 2.14 와 requirement 11절에 적었다.

## 안에서

- `HttpClientExchange::sendFile` (`kiotty_http_client_exchange_file.cpp`, 새 파일). `fileSizeOf` → `start` → `sendFileBody` → `end`.
- begin / finish 의 몸통을 BusyGuard 없는 private `start(request, body_length, deadline)` / `end(deadline)` 로 뺐다. sendFile 이 BusyGuard 하나 안에서 둘을 부른다.
- `sendFilePart`: `takeEarlyReplies` (A3) → `ConnectionStream::writeFile(path, offset, length, 남은 시간)`. writeFile 이 시작 확인에서 `DATA_ARRIVED_WHILE_WRITING` 을 주면 (보낸 것 없음) 다시 돌아 응답을 읽는다. 최종 응답이면 `stopUpload` 가 `ANSWERED` 로 하고 `end` 가 그 응답을 준다.
- `HttpClientState::file_piece` (initialize 가 `send_capacity` 로 채움).
- C API 의 path 상한 상수는 `MAX_FILE_PATH_TEXT` (`kiotty_net_base.cpp` 의 `MAX_PATH_TEXT` 와 단일 헤더에서 겹치지 않게).

## 테스트

- integration `kiotty_test_http_client_file.cpp` (새, 6 개): 100000 바이트 파일과 작은 파일이 한 연결로, 빈 파일의 `Content-Length: 0` (Transfer-Encoding 없음), 없는 파일의 `NOT_FOUND` 와 인자 오류 (보낸 PUT 없음), 413 을 먼저 보내는 HttpSite, 100 Continue 를 넘기고 201, 보내는 중의 507.
- `kiotty_test_http_client_tls.cpp`: `FileIsSentInPiecesOverTls` (40000 바이트, 조각 셋).
- `kiotty_test_http_client_cpp.cpp`: `UploadInPiecesAndAFileThroughTheWrapper` (begin 두 판, write 두 판, finish, sendFile 두 판).
- 외부 확인 (`build/interop/http_upload/`, gitignore 대상): Python 3.12.3 `http.server` 의 `upload_server.py` (Content-Length 와 chunked 를 읽어 길이와 adler32) 와 C++ wrapper 의 `upload_check.cpp`. http 와 https 에서 46 개, 실패 0. 1 MB 를 16 KB 조각으로 (Content-Length, chunked), 5 MB 파일, 빈 파일, 100 Continue, 머리만 읽고 413 을 보내고 닫는 서버 (write 가 `DATA_ARRIVED_WHILE_WRITING` 로 멈추고 finish 가 413, sendFile 이 413), 없는 파일, 거절 뒤의 다음 sendFile.

| 빌드 | 전 (A3) | 후 |
|---|---|---|
| MSVC (`build/msvc`, TLS OFF) | 871 | 879 |
| WSL gcc / gcc-notls / clang / clang-notls / asan / tsan | 872 | 880 |
| WSL clang-release | 871 | 879 |

수칙 검사 기준선 그대로 (FILE 1, LINE 4, MACRO 18, PUBLIC 3). 단일 헤더 컴파일 확인.

## 문서

- user guide HTTP: 2.14 (올리기, 먼저 온 응답, sendHttpFile, C++), 2.2 / 2.4 / 2.11 / 2.12 / 2.13 고침.
- requirement 11절: 올리기 문단, C 선언, C++ 선언. "하지 않는 것" 에서 올리기를 빼고 `Expect: 100-continue` 를 넣음.
- 구현 가이드 6.9, 테스트 계획 25.6, 설계 `design_http_client_v1.html` 의 U3 메모.

## 남은 것

HttpClient v1 (U1 ~ U3) 과 write 중 도착 (A1 ~ A3) 은 이것으로 끝이다. 계획 문서의 미결은 WebSocket streaming 하나.

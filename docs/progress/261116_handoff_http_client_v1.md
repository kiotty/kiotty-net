# 2026-11-16 넘김 : HttpClient v1 (요청 body 조각 올리기)

다른 세션에서 이어서 한다. 이 문서 하나로 시작할 수 있게 적었다.

## 1. 어디까지 왔나

| 것 | 상태 |
|---|---|
| HttpClient v0 (C1 ~ C5) | 끝, push. `design_http_client_v0.html` 의 "모두 끝났다" 문단. C1 `d9a6a09`, C2 `b5ffb04`, C4 `ea0186d`, C5 `c746045` |
| 단일 헤더 | 끝, push (`8367d1c`, 도구 고침 `67b8aa7`, 헤더 `5b8611b`) |
| **v1 설계** | **확정, 구현 전.** `docs/design/design_http_client_v1.html` (8절의 정할 것 넷 모두 닫힘) |
| WebSocket streaming | 메모만 (`requirement_v0.md` 미결). 보내기는 begin / write / finish, 받기는 `onMessage` 가 조각마다 + 마지막 조각 표시. 이름 / 켜는 단위 / 조각 크기는 아직 |

## 2. v1 에서 정한 것 (요약. 원문은 설계 문서)

- API: `KiottyNet_beginHttpRequest(client, request, headers, header_count, body_length, timeout)` → `KiottyNet_writeHttpRequestBody(client, data, length, timeout)` 여러 번 → `KiottyNet_finishHttpRequest(client, timeout)` 이 응답 머리까지. 응답 body 는 v0 의 `readHttpReplyBody`.
- 길이를 알면 `Content-Length`, `KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN` (`UINT64_MAX`) 이면 `Transfer-Encoding: chunked`. chunked 의 write 하나 = chunk 하나. 길이 0 write 는 아무것도 보내지 않고 `{ 0, SUCCESS }`.
- 선언보다 많이 쓰는 write 는 보내지 않고 `INVALID_ARGUMENT` (상태 유지). 적게 쓰고 finish 하면 `INVALID_ARGUMENT` + 연결 닫음.
- 다시 보내기 없음. begin 이 다시 쓰는 연결이 살아 있는지 먼저 본다 (`ConnectionStream::connected()` 의 peek). 닫혀 있으면 머리 전에 다시 연결.
- 올리는 중에 다른 요청 / begin 을 부르면 연결을 닫고 새 요청 (v0 결정 13 과 같은 규칙). 올리는 중의 `readHttpReplyBody` 는 `INVALID_ARGUMENT`.
- timeout 은 호출마다. BUSY / `closeHttpClient` 는 v0 그대로 (걸린 write / finish 가 `CLOSED`).
- request.body 는 비어 있어야 한다. Content-Type 과 헤더 규칙은 v0 그대로. 조각의 UTF-8 은 보지 않는다.
- **정할 것 1 (닫음)**: write 가 `PEER_CLOSED` / `SOCKET_ERROR` 로 끝난 뒤 finish 는 이미 와 있는 응답 머리 (413, 401 등) 를 돌려준다. 없으면 write 의 code.
- **정할 것 2 (닫음)**: `Expect: 100-continue` 는 지금 두지 않는다. 필요해지면 begin 의 선택 인자.
- **정할 것 3 (닫음)**: `KiottyNet_sendHttpFile(client, request, headers, header_count, path, timeout)` 를 둔다. 파일 크기로 Content-Length, `send_capacity` 조각으로 읽어 보내고 응답 머리까지. 열 수 없으면 `NOT_FOUND`. base 의 `writeStreamFile` 을 쓴다.
- **정할 것 4 (닫음)**: C++ RAII 업로드 객체는 두지 않는다. C++ 은 `begin / write / finish` 메서드만.

## 3. 사이클 (설계 9절)

| 사이클 | 하는 것 |
|---|---|
| U1 머리와 조각 | 조립기 (`writeRequestHead` 의 `RequestHead`) 에 body 방식 (Content-Length N / chunked), chunk 머리, 올리기 상태, begin / write / finish, 길이 규칙, 올리는 중 다른 요청 |
| U2 연결과 취소 | begin 의 살아 있는지 보기, close 로 깨우기, 송신 오류 정리, TLS, 정할 것 1 (먼저 온 응답) |
| U3 도우미, C++, 문서 | `sendHttpFile`, C++ wrapper, user guide / requirement / 구현 가이드 / 테스트 계획, Python `http.server` 외부 확인 (`build/interop/http_client/` 를 넓힌다) |

## 4. 코드 위치

- 공개 API: `include/kiotty_net_http.h` (C 와 C++ wrapper 모두. HttpClient 는 301 줄 근처부터), 번역 층 `src/kiotty_net_http_client.cpp`.
- 클라이언트: `src/implementation/context/kiotty_http_client.*` (StreamClient 를 private 상속, 공개 메서드 7 개 한계), `kiotty_http_client_headers.*`, `kiotty_http_client_link.*` (disconnect / close).
- 요청 머리: `domain/usecases/http/kiotty_http_request_head.*`, 검사 `kiotty_http_request_check.*`.
- 응답: `kiotty_http_status_line.*`, `kiotty_http_reply_framing.*`, `kiotty_http_body_reader.*`, `data/repository/http/kiotty_http_reply_store.*`.
- 스트림: `data/repository/stream/kiotty_connection_stream*.cpp` (줄 읽기는 `_line.cpp`), 깨우기 `data/infra/socket/kiotty_stop_signal.*`.
- 테스트: `tests/integration/kiotty_test_http_client*.cpp` (기본, retry, close, tls, cpp), 도우미 `tests/integration/support/kiotty_test_http_site.*` (우리 HttpServer), `support/kiotty_test_peer.*` (raw socket peer). 설명은 구현 가이드 6절, 테스트 계획 25절, progress `261115_progress_http_client_c1.md`, `261116_progress_http_client_c2.md`, `261116_progress_http_client_c4.md`.

## 5. 빌드와 확인

- WSL distro `Ubuntu`. 전체: `MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/repository/private/kiotty-net/build/wsl_matrix.sh` (7 빌드, 요약 `build/wsl-matrix-summary.log`). 빠르게: `build/quick_all.sh` (wsl-gcc + check_rules).
- MSVC: `cmd /c C:\repository\private\kiotty-net\build\run_msvc.bat`. 이 PC 의 Windows 에는 실제 Python 이 없어 단일 헤더는 commit 된 파일로만 확인된다.
- 수칙 검사: WSL 에서 `python3 -I tools/check_rules.py` → 기준선 **FILE 1, LINE 4, MACRO 18, PUBLIC 3**.
- 지금 테스트 수: **MSVC 820, WSL 823 (clang-release 822)**.
- 소스를 바꾸면 단일 헤더를 다시 만든다: `python3 tools/make_single_header.py` (WSL). 소스를 먼저 commit 하고 다시 만들어 헤더를 따로 commit 해야 머리의 버전이 `+modified` 없이 찍힌다. commit 된 헤더와 소스가 같은지는 `fresh` label 테스트 (matrix 에는 없다).

## 6. 지켜야 할 것

- **파일 지역 이름도 라이브러리 전체에서 겹치지 않는다** (단일 헤더는 번역 단위 하나. 구현 가이드 6.8). 같은 helper 는 공유 헤더 (`core/kiotty_codes.h`, `core/kiotty_bytes.h`, `core/kiotty_text.h`, `kiotty_http_constants.h`, `kiotty_http_rule_names.h`) 를 쓴다. Windows 매크로 이름 (`near`, `far`, `min`, `max`, `DELETE`, `ERROR` 등) 을 쓰지 않는다. `tests/single_header` 가 어기면 실패한다.
- 수칙 (`docs/requirement/development_rule.md`): 함수 30 줄, 인자 4 개, 공개 메서드 7 개 (넘으면 4.19 로 나눈다), 주석 없음 (4.9), raw new 없음, 런타임 heap 없음 (create 때만), `Result` 반환.
- docs 는 CRLF. Python 으로 `newline=''` 를 지켜 고친다.
- 사용자 방식: 설계 → 확인 → 사이클별 구현 → 매 사이클 전체 빌드 (WSL 7 + MSVC) → 보고 (한국어) → 사용자 승인 뒤 commit / push. commit 메시지 끝에 `Co-Authored-By` 줄. progress 문서 `docs/progress/YYMMDD_progress_http_client_uN.md`, plan `docs/plan/plan_base_v2.md` 에 한 줄씩.
- `git stash` / `git checkout -- .` / `git reset` 처럼 작업 트리를 바꾸는 명령은 쓰지 않는다 (한 번 사고가 있었다).

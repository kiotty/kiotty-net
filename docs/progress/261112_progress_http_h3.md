# 2026-11-12 진행 정리 : HTTP 사이클 H3 (완성 응답)

기준 문서: `docs/design/design_http_v0.html` 3.1, 4절, 7절, 11절의 H3 행. `docs/requirement/implementation_guide_v0.md` 4.1 ~ 4.5, 4.7, 4.8.

## 만든 것

HttpServer 가 처음으로 돈다. 요청을 읽고, 라우트를 부르고, 완성 응답 (복사 / 참조 body) 과 자동 응답을 보내고, keep-alive 로 다음 요청을 받는다. Work 와 스트림 (H4), 정적 서빙과 `setHttpFile` (H5) 은 아직 없다.

### 구조

| 층 | 파일 | 하는 일 |
|---|---|---|
| context | `kiotty_http_server` | `HttpServer : Server`. base Server 를 상속해 close / await / lifetime / 연결 관리를 그대로 쓴다. `initialize` 가 limits 검사, 라우트 표, Session 풀을 잡고, `run` 이 HTTP 표로 `StatefulHandler` 를 만들어 `Server::run` 을 부른다. `registry()` / `notices()` 는 run 중이면 `BUSY` 를 돌려주는 Result |
| context | `kiotty_http_configuration` | `KiottyNetHttpLimits` 의 initialize / configure 여섯. 기본값은 설계 4.1 |
| context | `kiotty_context`, `kiotty_handles` | `LazyReusablePool<KiottyNetHttpServer>` 하나. create / destroy (은퇴) / release 는 Server 와 같은 길 |
| data/repository/http | `kiotty_http_site` | 서버 하나의 공유 부분: 라우트 표, 끊김 / 오류 action, Session 풀, limits, receive / send capacity, 소유자 handle. Session 이 이것을 참조한다 |
| | `kiotty_http_session`, `kiotty_http_session_pool` | 연결마다 요청 저장소 + 응답 저장소 + 진행 상태. 버퍼는 create 때 블록 하나 (`max_clients × (max_header + max_body + response)`) 와 헤더 span 배열. handle 의 id 는 `{ generation, index }`. generation 은 요청마다, 연결이 끝날 때 오르고, 서버를 다시 create 해도 이어진다 |
| | `kiotty_http_request_store` | 요청 줄과 헤더 줄을 헤더 버퍼로 복사한 뒤 H2 의 파서로 자른다. body 를 이어 붙인다. `KiottyNetHttpRequest` 를 채운다 |
| | `kiotty_http_response_store` | 구현 가이드 4.4 의 배치. 앞의 256 바이트가 상태줄 + 라이브러리 헤더 (오른쪽 정렬), 사용자 헤더는 앞에서 뒤로, 복사 body 는 버퍼 끝에서 앞으로. setter 넷과 `seal` (상태줄, Date, Content-Length, Content-Type, `Connection: close`), `chunkAt` (send_capacity 조각) |
| | `kiotty_http_registry`, `kiotty_http_notices` | 라우트 표 (등록 순서대로 path 매칭, method 비트, HEAD 는 GET 으로, 405 / OPTIONS 의 Allow), not found 라우트. 끊김 / 오류 action |
| domain/usecases/http | `kiotty_http_machine`, `kiotty_http_exchange` | HTTP 표 (StateMachine 의 rule 열하나) 와 transition action. 상태: 요청 줄, 헤더 줄, body, chunk 크기 / 데이터 / 끝, trailer, 송신, 닫음 |
| | `kiotty_http_response_text` | 상태줄과 reason phrase 표, Date (IMF-fixdate), Content-Length, Allow 목록 |
| data/infra/clock | `kiotty_wall_clock` | UTC 달력 시각 (`gmtime_s` / `gmtime_r`). Date 헤더용 |
| C API | `src/kiotty_net_http.cpp` | limits 일곱, `findHttpHeader`, setter 다섯 (status, header, custom content type, body, body ref), `makeHttpRoute`, create / destroy / addRoute / setNotFound / onHttpDisconnected / onHttpError / run / close / await |

### 동작

- 요청 줄은 `header_timeout` (keep-alive 뒤에는 `keep_alive_timeout`) 안에, 헤더는 요청 줄이 온 때부터 `header_timeout` 안에 다 와야 한다. 넘으면 408 없이 닫고 끊김 reason 은 `TIMEOUT`. body 조각은 조각마다 `header_timeout`.
- 줄 하나는 `min(receive_capacity, max_header_bytes + 2)` 까지 읽는다. 넘으면 base 가 `PROTOCOL_ERROR` 로 닫는다 (응답 없음). 헤더 공간이나 개수가 모자라면 431.
- 거절 (400 / 413 / 414 / 431 / 501) 은 status 만 놓은 응답을 보내고 `Connection: close` 로 닫는다. 끊김 reason 은 `PROTOCOL_ERROR`.
- 라우트가 setter 를 하나도 부르지 않으면 404. setter 를 불렀고 status 가 없으면 200.
- 1xx / 204 / 304 는 Content-Length 와 body 를 보내지 않는다. HEAD 는 GET 의 Content-Length 를 적고 body 를 떼어 낸다.
- OPTIONS 에 라우트가 없으면 204 + Allow. `OPTIONS *` 는 등록된 method 전부. Allow 에는 GET 이 있으면 HEAD, 늘 OPTIONS 가 붙는다.
- `setHttpHeader` 는 이름이 token 이고 예약 이름 (Content-Length, Transfer-Encoding, Connection, Date, Content-Type) 이 아니며 값에 제어 문자가 없고 UTF-8 이어야 한다.
- 응답 handle 은 onRequest 안에서만 쓸 수 있다. 송신이 시작된 뒤에는 `BUSY`, 다음 요청이 시작되면 `PEER_CLOSED`.

### 설계와 다르게 한 것

- 연결 수 초과의 `onError(TABLE_FULL)` 는 테스트하지 않았다. base 는 자리가 없으면 accept 를 멈추고 새 연결을 OS backlog 에 두므로 (`IoLoop::rearmAccept`) 경합 때만 TABLE_FULL 이 난다. 대신 자리가 날 때까지 기다린 연결이 응답을 받는 것과, 오류 action 이 TLS handshake 실패를 받는 것을 본다. 설계 4.9 / 11절과 user guide 1.7 의 문장을 이에 맞췄다.
- user guide 1.1 / 1.10 의 `OUT_OF_MEMORY` 를 뺐다. base 와 같이 메모리를 잡지 못하면 process 가 끝난다.
- usecases 가 `context/kiotty_action.h` (action 만들기) 를 include 했다. 의존 방향 (수칙 7.3) 에서 어긋나서 같은 날 `domain/entity/kiotty_action.h/.cpp` 로 옮겼다 (사용자 결정).
- `Expect: 100-continue` 는 처리하지 않는다. curl 은 1 KiB 넘는 body 에 이것을 보내고 1 초 기다린 뒤 body 를 보낸다. 처리 방법은 `261112_01_100-continue.md` 에 적었다.

## 테스트

`tests/integration/kiotty_test_http_server.cpp` 21 개 (공개 C API 만, 테스트 peer 로 날 HTTP):

- GET 라우트의 Content-Length / Content-Type / Date, POST 의 body / query / 헤더 읽기, chunked 요청 body (chunk 확장, trailer).
- keep-alive 로 pipelined 요청 셋, `Connection: close` 뒤 닫힘 (끊김 reason `SUCCESS`), HTTP/1.0 은 `keep-alive` 일 때만 유지.
- 404 와 not found 라우트, setter 없는 라우트는 404, 405 + Allow, OPTIONS 204 + Allow 와 `OPTIONS *`, HEAD.
- 413 (끊김 reason `PROTOCOL_ERROR`), 431, 414 / 501 / 400 (버전, 헤더 이름, Transfer-Encoding 과 Content-Length 같이), 헤더 timeout (reason `TIMEOUT`).
- `max_clients` 1 에서 두 번째 연결이 첫 연결이 끝난 뒤 응답을 받음, TLS 서버에 평문을 보내면 오류 action (TLS 빌드에서만).
- setter 규칙 (이름 없는 CUSTOM, Content-Type 헤더, CR / LF, UTF-8 아닌 JSON, EVENT_STREAM, 범위 밖 status, CUSTOM + 이름), 앞 요청의 handle 은 `PEER_CLOSED`, run 중 등록은 `BUSY` 와 중복 / 잘못된 경로, 초기화하지 않은 limits.

tsan 에서 셋이 처음에 실패했다. 테스트의 App 필드를 IO thread 의 라우트가 쓰고 테스트 thread 가 lock 없이 읽은 것 (라이브러리 쪽 경합은 아니었다). 같은 mutex 로 고쳤고 `--gtest_repeat=3` 으로 확인했다.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 605 / 605 (TLS 하나 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 606 / 606 넷 |
| WSL asan,undefined TLS ON, thread TLS ON | 606 / 606, 606 / 606 |
| WSL gcc Release TLS ON | 605 / 605 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3). 처음 판의 100 자 넘는 줄 31 개와 `c_str()` 하나 (DV12) 를 고쳤다 (`addHeader` 는 검사된 span 을 받고 C 문자열 검사는 C API 층에서).

## 다음

H4 (Work 와 Work 스트림): `startHttpWork`, `onWorkStarted`, `finishHttpWork*`, `isHttpWorkConnected`, `setHttpBodyStream`, `writeHttpWorkChunk`. HEAD 와 스트림 라우트 (결정 16), Work 없는 스트림 body 의 500.

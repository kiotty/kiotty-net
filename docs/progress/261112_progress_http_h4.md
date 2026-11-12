# 2026-11-12 진행 정리 : HTTP 사이클 H4 (Work 와 Work 스트림)

기준 문서: `docs/design/design_http_v0.html` 3.2, 4.6, 4.7, 4.9 (HEAD), 7절, 11절의 H4 행. `docs/requirement/implementation_guide_v0.md` 4.6, 4.7.

## 만든 것

라우트가 `startHttpWork` 로 응답을 worker 에게 넘길 수 있다. worker 는 완성 응답을 채워 `finishHttpWorkWithResponse` 하거나, `setHttpBodyStream` 뒤 `writeHttpWorkChunk` 로 chunked 스트림 (SSE) 을 보낸다.

| 층 | 파일 | 하는 일 |
|---|---|---|
| context | `kiotty_http_work` (새) | `HttpWork` (수칙 4.19 의 세부 클래스, `HttpServer::work()`). `fill` (setter 가 쓸 Session: FILLING 또는 WORKING), `start`, `write`, `finishWithResponse`, `finishWithClose`, `isConnected`. Work stream 은 `stream.owner` (Endpoint) 로 쓴다 |
| context | `kiotty_http_server` | `session()` 을 `work()` 로 바꿨다. `close()` 가 `HttpSite::is_closing` 을 켠 뒤 `Server::close` 를 부른다. run 이 시작할 때 끈다 |
| data/repository/http | `kiotty_http_response_store` | `BodyStorage::STREAM`. 필드를 `ResponseLayout` 으로 묶고 송신 쪽 읽기를 `ResponseFrame` (`frame()`) 으로 나눴다: `chunkAt`, `checkChunk`, `isStream`, `hasSendableBody`, `frameChunk`. 스트림이면 seal 이 Content-Length 대신 `Transfer-Encoding: chunked` 를 적고 body 자리에 끝 chunk `0\r\n\r\n` 을 둔다 |
| | `kiotty_http_session`, `kiotty_http_session_pool` | Session 에 `route`, `stream`, `stream_failure`, `is_work_requested`, `is_worker_owned`, `is_orphaned`. phase 에 `WORKING`, `STREAMING` (헤더가 나감). 풀에 mutex 하나와 `handBack` |
| domain/usecases/http | `kiotty_http_exchange`, `kiotty_http_machine` | 상태 `WORKING`. 행 `(WORKING, WORK_STARTED)`, `(WORKING, WRITTEN) → onSent`, `(WORKING, READ) → onRequestLine`. HEADER_LINE / BODY / TRAILER 의 다음 상태에 WORKING |
| | `kiotty_http_response_text` | `chunkHeadOf` (길이 hex + CRLF) |
| C API | `src/kiotty_net_http.cpp` | `setHttpBodyStream`, `startHttpWork`, `writeHttpWorkChunk`, `finishHttpWorkWithResponse`, `finishHttpWorkWithClose`, `isHttpWorkConnected` |

### 동작

- `startHttpWork` 는 onRequest 안 (FILLING) 에서 한 번만 된다. 두 번째와 worker 에서의 호출은 `BUSY`, 라우트에 `onWorkStarted` 가 없으면 `INVALID_ARGUMENT`.
- onRequest 가 return 하면 `afterRoute` 가 가른다: Work 요청이면 `{ WORKING, thenWork() }`. 단 HEAD 이고 onRequest 가 스트림 body 를 정했으면 Work 를 열지 않고 헤더만 보낸다 (결정 16). Work 없이 스트림 body 를 정했으면 500 + `Connection: close`.
- `WORK_STARTED` 에서 stream 을 Session 에 넣고 `onWorkStarted(request, response, work)` 를 부른다. 그 뒤로 Session 은 건드리지 않는다 (worker 가 바로 finish 할 수 있다).
- setter 는 WORKING 에서도 되고, 첫 `writeHttpWorkChunk` 뒤 (STREAMING) 와 finish 뒤에는 `BUSY`.
- `writeHttpWorkChunk`: 첫 호출이 seal 하고 헤더 블록 + chunk 를 보낸다. 헤더 (첫 호출) + 머리 + data + CRLF 가 `min(send_capacity, 응답 버퍼의 남은 자리)` 안이면 응답 버퍼에서 조립해 write 한 번, 넘으면 넷을 차례로. timeout 은 `Deadline` 하나로 전부에 걸친다. 빈 data / 스트림 아님 `INVALID_ARGUMENT`, 텍스트 계열의 UTF-8 아님 `INVALID_ENCODING` (아무것도 보내지 않는다). 실패하면 그 코드를 Session 에 남기고 다음 write 도 같은 코드를 돌려준다.
- body 를 보내지 않는 응답 (HEAD, 1xx / 204 / 304) 에 worker 가 스트림을 열면 첫 write 가 헤더만 보내고 `CLOSED`. 그 뒤 `finishHttpWorkWithResponse` 는 keep-alive 로 다음 요청을 읽는다 (worker 가 Session 을 다음 요청으로 돌리고 `closeConnectionStream(thenReadUntil)`. 그래서 `(WORKING, READ)` 행이 있다).
- `finishHttpWorkWithResponse`: 아무것도 보내지 않았으면 seal 하고 첫 조각을 `closeConnectionStream(thenWrite)`. 스트림이 열려 있으면 끝 chunk. 실패한 스트림이면 그 코드로 닫는다. 나머지 조각은 `(WORKING, WRITTEN)` → `onSent` 가 H3 그대로 잇는다.
- 서버 close 중의 write 는 `CLOSED` (base 는 FIN 뒤의 EPIPE 를 `PEER_CLOSED` 로 주므로 `is_closing` 을 보고 바꾼다). 상대가 사라진 write 는 `PEER_CLOSED` (base 는 RST 를 `SOCKET_ERROR` 로 주므로 실패한 뒤 `isStreamOpen` 이 거짓이면 바꾼다).

### 고친 것 : Work 중 서버 close 에서 run 이 돌아오지 않음

base 는 서버 close 때 Work 중인 연결에도 `CLOSED` 를 곧바로 dispatch 하고 (`Connection::enterClosing`), stream 은 finish 까지 살려 둔다. HTTP 의 `onClosed` → `releaseSession` 이 그 자리에서 Session 을 풀에 돌려 generation 을 올렸으므로 worker 의 finish 가 `PEER_CLOSED` 로 끝나 `closeConnectionStream` 을 부르지 못했고, base 는 Work 를 영원히 기다렸다.

Session 의 소유를 표시해 풀었다. Work 를 열 때 `is_worker_owned`. IO thread 의 `release` 는 worker 가 들고 있으면 `is_orphaned` 만 켜고 돌려주지 않는다. worker 의 finish 는 `handBack` (소유를 내려놓고 orphan 여부를 받음) → `closeConnectionStream` → orphan 이면 `release`. 셋 다 풀의 mutex 안이다 (풀의 acquire 도). 설계의 "run 은 모든 Work 가 finish 된 뒤 CLOSED 로 돌아온다" 가 이제 성립한다.

### 설계와 다르게 한 것 / 남긴 것

- Work 없는 스트림 body 의 500 뒤 끊김 reason 은 `SUCCESS` 다 (라이브러리가 `Connection: close` 를 정상으로 보냈다).
- 서버 close 의 `onHttpDisconnected(CLOSED)` 는 worker 의 finish 전에 온다 (base 의 순서 그대로).
- benchmark (chunk 1 KiB × 1000 의 latency) 는 아직 없다.
- 공개 헤더와 user guide 는 바꾸지 않았다 (H1 판 그대로).

## 테스트

`tests/integration/kiotty_test_http_work.cpp` 9 개 (공개 C API 만):

- Work 뒤 JSON 완성 응답 + 같은 연결의 다음 요청.
- 이벤트 셋의 스트림 (`Transfer-Encoding: chunked`, Content-Length 없음, Cache-Control, 끝 chunk), 첫 write 뒤 setter 는 `BUSY`, 같은 연결의 다음 요청.
- `send_capacity × 3 + 17` 바이트 chunk 하나 (나눠 쓰는 길).
- HEAD 가 스트림 라우트에 오면 onWorkStarted 없이 헤더만 오고 같은 연결로 다음 요청.
- worker 가 정한 스트림에 HEAD 면 write 가 `CLOSED` 둘, finish 뒤 keep-alive.
- 상대가 끊으면 write 가 `PEER_CLOSED`, `isHttpWorkConnected` 거짓.
- 서버 close 중 write 가 `CLOSED`, run 이 `CLOSED` 로 돌아옴.
- Work 없는 스트림 body 는 500 + 닫힘.
- 규칙: onWorkStarted 없는 라우트의 start, 두 번째 start, worker 의 start, 스트림 아닌 write, BY_EXTENSION 스트림, 빈 data, 깨진 UTF-8.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 618 / 618 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 619 / 619 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 619 / 619, 619 / 619 |
| WSL clang Release TLS ON | 618 / 618 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 다음

H5 (정적 서빙과 `setHttpFile`). H4 의 benchmark 는 H5 와 함께.

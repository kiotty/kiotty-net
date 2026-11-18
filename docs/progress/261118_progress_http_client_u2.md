# 2026-11-18 진행 정리 : HttpClient v1 사이클 U2 (연결과 취소)

기준 문서: `docs/design/design_http_client_v1.html` 결정 5, 8, 4절, 정할 것 1, 9절의 U2 행. 계획: `docs/plan/plan_base_261116.md`. 공개 API 는 바뀌지 않았다.

## 바뀐 것

| 것 | 전 (U1) | 뒤 |
|---|---|---|
| begin 의 다시 쓰는 연결 | 열려 있으면 그대로 머리를 보냄 (닫힌 연결이면 write / finish 에서야 실패) | `ensureIdleConnected`: 수신 Stage 에 바이트, peek 가 바이트를 받음, EOF / reset 이면 닫고 다시 연결. 1 ms 안에 아무것도 없을 때 (`TIMEOUT`) 만 그대로 쓴다 |
| write 의 송신이 `PEER_CLOSED` / `SOCKET_ERROR` | 연결을 닫고 IDLE. finish 는 `INVALID_ARGUMENT` | 연결을 남기고 올리기 상태 `REFUSED`. finish 가 먼저 와 있는 응답 머리를 읽어 돌려준다 (정할 것 1) |
| write 의 그 밖의 오류 (`TIMEOUT`, `CLOSED`) | 연결을 닫고 IDLE | 그대로 |

- **살아 있는지 보기** (`http/data/repository/client/kiotty_http_client_send.*`): 설계 7절은 `ConnectionStream::connected()` 를 쓰라고 했지만 쓰지 않았다. `connected()` 는 소켓에 바이트가 있으면 열린 것으로 보므로 TLS 서버가 idle 연결을 닫을 때 보내는 close_notify 를 놓친다. 그 함수는 서버의 work stream (`isStreamOpen`) 도 쓰므로 뜻을 바꾸지 않고, 공개 메서드도 더하지 않았다 (ConnectionStream 은 이미 7). 대신 기존 `peek(0, 1)` (Stage 에 남은 것) 과 `peek(1, 1)` (1 ms 기다림, TLS 면 레코드를 풀어 봄) 을 쓴다. TLS 1.3 의 NewSessionTicket 은 평문을 내지 않으므로 살아 있음으로 남는다. 다시 쓰는 연결의 begin 은 1 ms 늦어진다. v0 의 요청 (`send`) 은 재시도가 있으므로 바꾸지 않았다.
- **`UploadMode::REFUSED`** (`http/domain/entity/kiotty_http_upload.h`) 와 규칙 `refuseUpload` (`kiotty_http_upload_rules.*`). `checkUploadPiece` / `checkUploadEnd` 는 LENGTH / CHUNKED 가 아니면 `INVALID_ARGUMENT` (`isSending`).
- **`HttpClientExchange`**: write 실패는 `stopUpload(code)` (연결 잃음이면 `REFUSED`, 아니면 `dropUpload`). finish 는 `REFUSED` 면 끝 표시를 보내지 않고 `refusedReply`: `receiveReply` 가 성공하면 `persistence = CLOSE` 로 두어 응답 body 뒤 연결을 닫고, body 가 이미 끝났으면 바로 닫는다. begin / 다른 요청 / disconnect / close 는 U1 처럼 `REFUSED` 도 연결과 함께 버린다. `REFUSED` 에서 readBody 는 `INVALID_ARGUMENT`.

## 설계와 다른 것 (확인 부탁)

1. 정할 것 1 의 "와 있는 것이 없으면 write 의 code": write 의 code 를 따로 담지 않고 finish 의 읽기 code 를 돌려준다. 둘 다 `PEER_CLOSED` 또는 `SOCKET_ERROR` 이고 (write 는 `SOCKET_ERROR`, 읽기는 `PEER_CLOSED` 처럼 갈릴 수 있음), 담으려면 `HttpUpload` 에 칸 하나와 채움 4 바이트가 는다.
2. 살아 있는지 보기는 `connected()` 가 아니라 1 ms peek (위).
3. TLS 에서 송신이 깨진 뒤에는 보내지 못한 암호문이 남아 읽기 전 flush 가 실패할 수 있다. 그때 finish 는 응답 대신 연결 잃음 code 를 돌려준다. 평문 TCP 와 우리 서버로는 확인했고, TLS 의 먼저 온 응답은 테스트하지 않았다.

## Windows 에서 먼저 온 응답을 잃음 (A3 에서 고침)

서버가 413 / 401 을 보내고 읽지 않은 body 를 남긴 채 닫으면 RST 가 온다. Linux 는 RST 앞에 커널 수신 버퍼에 와 있던 바이트를 남기고, Windows 는 버린다. 올리는 동안 kiotty 는 읽지 않으므로 그 응답은 아직 수신 Stage 가 아니라 커널에 있고, Windows 의 finish 는 `SOCKET_ERROR` 를 돌려준다. 설명은 `docs/learning/early-reply-and-rst.html`.

- 두 테스트 (`FinishShowsTheReplyOfAServerThatRefusedTheBodyEarly`, `FinishAfterAWriteThePeerCutOffShowsTheReplyItSentFirst`) 는 Windows 에서만 (`RESET_DROPS_RECEIVED_BYTES`) 연결 잃음 code 도 받아들인다. A3 에서 다시 엄격하게 한다.
- 고치는 길 (2026-11-18 사용자와 정함): write 가 poll(IN | OUT) 로 기다리다 상대 바이트가 오면 멈추고 수신 Stage 로 옮긴 뒤 `KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING` 을 돌려준다. StreamClient 와 서버 work stream 모두. write 를 시작할 때 한 번 (기다리지 않음) 과 막힐 때마다 확인한다. 새 사이클 A1 (base), A2 (HTTP 서버 work), A3 (HttpClient 올리기).

## 테스트

- unit `kiotty_test_http_upload.cpp` +1: `REFUSED` 는 method 를 지키고 조각과 끝을 거절.
- integration `kiotty_test_http_client_upload.cpp` +7:
  - 서버가 keep-alive 시간 뒤 닫은 idle 연결에서 begin 이 새 연결로 (서버 연결 2, POST 1)
  - raw peer 가 reset 한 연결에서 begin 이 새 연결로
  - 요청 전에 바이트가 와 있는 연결을 begin 이 다시 쓰지 않음 (201 은 새 연결에서)
  - 읽지 않는 peer 에 32 MiB write 가 걸린 것을 다른 thread 의 close 가 `CLOSED` 로 깨움 (2 초 안), 그 뒤 write / finish 는 `INVALID_ARGUMENT`
  - 우리 HttpServer 의 413 (body 상한 256 KiB 에 4 MiB 올리기): finish 가 413 을 돌려주고 다음 GET 은 새 연결
  - raw peer 가 머리 뒤 401 을 보내고 닫음: write 가 연결 잃음, finish 가 401 과 body `nope`
  - raw peer 가 응답 없이 reset: write 와 finish 모두 연결 잃음 code, 두 번째 finish 는 `INVALID_ARGUMENT`
- integration `kiotty_test_http_client_tls.cpp` +2: https 의 Content-Length / chunked 올리기가 한 연결로 (NewSessionTicket 을 살아 있음으로 봄), 서버가 닫은 idle TLS 연결에서 begin 이 새 연결로 (close_notify).
- 새 동작 7 개는 begin 을 `ensureConnected` 로, write 실패를 `dropUpload` 로 되돌리면 모두 실패하는 것을 확인했다 (close 로 깨우기는 U1 의 stop 경로로 이미 됨, 이번에 확인만).
- close 로 깨우기 테스트는 1 MiB 를 막힐 때까지 반복해 쓴다. Windows 의 non-blocking send 는 32 MiB 한 번을 버퍼에 다 받아 막히지 않았다.

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC (`build/msvc`) | 847 | 857 |
| WSL gcc / gcc-tls / clang / clang-tls / asan-tls / tsan-tls | 848 | 858 |
| WSL release-tls | 847 | 857 |

수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3 (그대로).

## 다음

설계 문서 (write 중 도착) → A1 → A2 → A3 → U3 (파일 도우미 `sendHttpFile`, C++ begin / write / finish, 문서, Python `http.server` 외부 확인).

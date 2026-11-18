# 2026-11-18 진행 정리 : write 중 도착 사이클 A3 (HttpClient 올리기)

기준 문서: `docs/design/design_write_arrival_v0.html` 4절의 HttpClient 행, 8절 정할 것 1 (code 는 base 와 같은 `DATA_ARRIVED_WHILE_WRITING`), 9절의 A3 행. 앞 사이클은 `261118_progress_write_arrival_a2.md`.

## 공개 동작

- `KiottyNet_writeHttpRequestBody`: 올리는 중에 서버 바이트가 오면 응답 머리 하나를 읽는다. 1xx (100 Continue, 103 등) 면 버리고 이어 보낸다. 최종 응답이면 `{ 0, KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING }` 을 돌려주고 올리기는 끝난다 (그 뒤 write 는 `INVALID_ARGUMENT`). 앱은 finish 를 불러 그 응답을 본다.
- `KiottyNet_finishHttpRequest`: 받아 둔 최종 응답을 돌려준다 (body 는 `readHttpReplyBody`). 요청 body 가 끝나지 않은 연결이므로 body 를 다 읽으면 연결을 닫는다 (U2 의 REFUSED 와 같음).
- **Windows 에서도 먼저 온 응답이 보인다.** 응답 바이트는 RST 전에 수신 Stage 로 옮겨지므로 (A1) Windows 의 RST 가 지우지 못한다. U2 의 두 테스트 (`FinishShowsTheReplyOfAServerThatRefusedTheBodyEarly`, `FinishAfterAWriteThePeerCutOffShowsTheReplyItSentFirst`) 의 Windows 예외 (`RESET_DROPS_RECEIVED_BYTES`) 를 없앴다.
- v0 요청과 begin 의 머리 송신은 도착이 와도 내부에서 이어 보낸다 (A1 의 다리 그대로). 받은 바이트는 Stage 에 남아 응답 읽기가 먼저 쓴다.

## 안에서

- `UploadMode::ANSWERED` 와 `answerUpload` (규칙). 연결이 이미 닫혔을 수 있으므로 (응답 body 가 다 왔고 `Connection: close` 면 응답을 받는 순간 닫힘) `settleUpload` 는 ANSWERED 를 소켓으로 판단하지 않는다. 대신 `HttpClientLink` 의 disconnect / close 와 close 깨움 (`stop().accept()`) 이 올리기를 직접 IDLE 로 한다. `settleStop` + `settleUpload` 짝은 `settleUpload` 하나로 합쳤다.
- `sendUploadBytes` / `takeEarlyReplies` (`kiotty_http_client_exchange.cpp`): 올리기의 조각, chunk 머리, 줄 끝을 이것으로 보낸다. 보내기 전에 수신 Stage 에 이미 응답이 있으면 (begin 의 머리를 보내는 동안 온 것은 다음 write 의 시작 확인이 "새로 옮긴 것" 으로 알리지 않는다) 먼저 본다. 응답 머리 하나는 `receiveOneReply` (`kiotty_http_client_reply.*`, 새 함수, 1xx 를 넘기지 않고 하나만 읽음) 로 읽는다.
- `writeOnce` (`kiotty_http_client_send.*`): 한 번의 write 와 그 결과 (길이, code). `writeAll` 은 이것을 되풀이한다.
- **begin 의 남은 경주 (A1 progress)**: 재사용 연결이면 머리 전에 길이 0 의 write 로 도착을 한 번 본다 (보내는 것 없이 시작 확인만 일어남). 요청 전에 온 바이트 / EOF / RST 면 끊고 다시 연결한다. `isIdle` 의 1 ms peek 가 바이트를 못 본 경우를 이것이 닫는다.
- `refusedReply` 를 `stoppedReply` (REFUSED 는 응답을 읽고, ANSWERED 는 이미 읽음) 와 `closingReplyOf` 로 나눴다.

## 테스트

- integration `kiotty_test_http_client_upload.cpp`:
  - U2 의 두 테스트를 모든 플랫폼에서 엄격하게 (413 / 401). 401 테스트의 write 는 이제 연결 잃음이 아니라 `DATA_ARRIVED_WHILE_WRITING`.
  - 새: 올리는 중의 100 Continue 는 넘기고 body 를 이어 보내 201, 최종 응답 (507) 이 오면 write 가 `{ 0, 도착 }` 이고 그 뒤 write 는 `INVALID_ARGUMENT`, finish 가 507 과 body, 다음 begin 은 새 연결, 이른 응답 뒤 disconnect 하면 올리기가 끝남 (finish 는 `INVALID_ARGUMENT`).
- MSVC 에서 올리기 / work 테스트를 10 번 되풀이해 모두 통과 (Windows 의 RST 경우 포함).

| 빌드 | 전 (A1) | 후 (A2 + A3) |
|---|---|---|
| MSVC (`build/msvc`, TLS OFF) | 866 | 871 |
| WSL gcc / gcc-notls / clang / clang-notls / asan / tsan | 867 | 872 |
| WSL clang-release | 866 | 871 |

수칙 검사 기준선 그대로 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 남은 것

U3 (`sendHttpFile`, C++ begin / write / finish, 문서, Python 외부 확인). C++ wrapper 와 user guide 에 `DATA_ARRIVED_WHILE_WRITING` 의 뜻 ("최종 응답이 와 있다, finish 를 부른다") 을 적는다.

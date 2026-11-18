# 2026-11-18 진행 정리 : write 중 도착 사이클 A1 (base)

기준 문서: `docs/design/design_write_arrival_v0.html` (결정 1 ~ 8, 정할 것 넷 모두 닫음, 9절의 A1 행). 배경은 `docs/learning/early-reply-and-rst.html`.

## 공개 API

- `KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING` 을 `KiottyNetCode` 맨 끝 (`PEER_NOT_READING` 뒤) 에 더했다.
- `KiottyNet_writeStreamClient`, `KiottyNet_writeConnectionStream`: 시작할 때 (기다리지 않음) 와 송신이 막힐 때 상대 바이트가 오면 수신 Stage 로 옮기고 `{ 보낸 만큼, DATA_ARRIVED_WHILE_WRITING }` 으로 멈춘다. 상대가 닫았으면 EOF 는 `PEER_CLOSED`, RST 는 `SOCKET_ERROR` 이고 그 전에 옮긴 바이트는 read 로 받는다.
- `KiottyNet_writeConnectionStreamFile`: 시작할 때만 본다 (정할 것 2).
- read 는 수신 Stage 에 바이트가 있으면 flush 없이 먼저 준다.
- `requirement_v0.md` 의 code 목록, stream 함수 표, `writeStreamClient` 설명을 고쳤다.

## 안에서

- **소켓 backend**: `waitWritableOrReadable(socket, timeout)` (`kiotty_socket_transfer.h`). POSIX 는 `waitFor` 에 `POLLIN | POLLOUT` 을 주고 `readinessOf` 가 POLLIN / POLLHUP / POLLERR 이면 `SocketCode::READABLE` (새 값, 내부). Windows 는 `WaitDirection::EITHER` 로 select 의 읽기 / 쓰기 집합 모두에 넣고, 읽기나 오류 집합에 있으면 READABLE. stop 은 지금처럼 먼저 본다.
- **`ConnectionStream`** (공개 메서드 7 그대로):
  - `write` 는 `_watch` (private `ArrivalWatch`) 를 켜고 `sendWatched` (시작 확인 `takeArrival` → `send`) 를 부른다. `writeFile` 은 시작 확인만.
  - `sendAll` 의 기다림 `awaitSendable`: `_watch` 가 켜져 있고 수신 Stage 에 자리가 있으면 읽기 또는 쓰기를 기다린다. READABLE 이면 그때까지 보낸 길이와 함께 돌아온다.
  - 옮기기는 부른 쪽이 지금 보내던 구간을 commit 한 뒤에 한다 (`takeArrival` → `absorb`). 평문은 기다리지 않는 recv 한 번으로 수신 Stage 에. TLS 는 남은 암호문을 먼저 풀고, recv 한 번, 다시 풀어 평문이 나올 때만 도착이다 (`decodeArrival`). Stage 가 차 있으면 (`BUFFER_FULL`) 이번 write 에서는 더 보지 않는다 (헛돌기 방지).
  - flush 는 cipher Stage → BIO 순서를 지키도록 비어질 때까지 둘을 되풀이한다 (도착을 풀면 OpenSSL 이 BIO 의 앞을 cipher Stage 로 옮길 수 있다).
  - **TLS 레코드**: BIO pair 를 4 KiB 에서 레코드 하나가 통째로 들어가는 16960 바이트로 키웠다 (사용자 결정). 레코드마다 먼저 flush 로 BIO 를 비우고 (이때 도착을 봄) 레코드를 넣는다. 그래서 `SSL_write` 가 레코드 중간에서 다시 부르기를 기다리는 일이 없고, 멈춤은 언제나 레코드 사이다. 레코드를 넣은 뒤 남은 암호문을 보내다 도착하면 bytes 는 그 레코드까지 센다 (마지막이면 length).
  - 도착 처리는 `kiotty_connection_stream_arrival.cpp` (새 파일) 로 나눴다 (본 파일 400 줄).
- **A1 의 다리**: http 서버 work 의 `sendSpan` 과 HttpClient 의 `writeAll` 은 code 만 보고 길이를 버리므로, 도착이면 보낸 만큼 넘기고 이어 보내게 했다. 동작은 전과 같고 (도착한 바이트는 Stage 에 남아 다음 읽기가 먼저 받는다), A2 / A3 가 각자의 규칙으로 바꾼다.
- 서버 work 뒤의 Stage: `ReceiveFlow::drive` 가 다음 읽기에서 수신 Stage 를 먼저 보므로 worker 가 옮긴 다음 요청은 work 가 끝난 뒤 처리된다. A2 에서 테스트로 확인한다.

## 확인하면서 고친 것

- **기다림의 0 = 무한 경주 (A1 전부터 있던 것).** `ConnectionStream` 의 기다림은 `deadline.expired()` 를 본 뒤 `deadline.remaining()` 을 소켓 기다림에 넘겼다. 둘 사이에 기한이 지나면 `remaining()` 이 0 이고, 소켓 backend 는 0 을 "끝없이" 로 본다. 첫 매트릭스의 asan 빌드에서 U2 테스트 하나 (`FinishAfterAWriteThePeerCutOffShowsTheReplyItSentFirst`) 가 poll 에서 45 분 멈췄다 (CPU 0, 혼자 300 번 돌리면 재현되지 않음). A1 은 이 모양의 기다림을 늘리므로 함께 고쳤다: 기한이 있으면 1 ms 이상을 주는 `deadline.bound(0)` 으로 바꿨다 (`ConnectionStream::waitReadable`, `awaitSendable` 둘, `readLine`, POSIX 파일 송신의 `awaitWritable`).
- MSVC 에서 Windows 루프백이 16 MiB write 를 막히지 않고 받아들여 "막힌 write" 테스트 둘의 전제가 깨졌다. 1 MiB 조각을 막힐 때까지 되풀이하도록 고쳤다.
- 매트릭스의 ctest 에 테스트당 300 초 한도 (`--timeout 300`) 를 두고 돌렸다 (멈추면 실패로 남게).
- U2 의 `isIdle` (begin 이 재사용 연결을 1 ms peek 로 봄) 은 느린 환경에서 peek 의 기한이 소켓을 읽기 전에 지나 EOF / 와 있는 바이트를 못 볼 수 있다. A1 전에는 그 뒤 머리 송신이 우연히 성공해 숨었고, A1 에서는 write 의 시작 확인이 EOF 를 보아 begin 이 `PEER_CLOSED` 로 끝났다 (asan 에서 `BeginOnAnIdleConnectionTheServerClosedOpensANewOne`). `isIdle` 맨 앞에 기다리지 않는 `connected()` 를 두어 EOF / RST 는 시간과 상관없이 잡는다. 요청 전에 온 바이트를 1 ms 안에 못 보는 경우 (`BeginDoesNotReuseAConnectionWithUnaskedBytes`, asan 에서 한 번) 는 남는다. A3 에서 begin 의 머리 송신이 도착 code 를 받으면 (아무것도 보내지 않았으면) 다시 연결하는 것으로 닫는다.

## 메모리

TLS 연결 하나마다 BIO pair 두 버퍼가 4 KiB 에서 16960 바이트로 (OpenSSL 의 malloc, DV2). unit `TlsMemory` 의 쌍 (서버 + 클라이언트) 당 peak 가 약 148 KiB 가 되어 상한을 128 KiB 에서 160 KiB 로 올렸다 (`deviations.md` DV2, `design_base_v5.html` 고침).

## 테스트

- integration `kiotty_test_connection_stream.cpp` +8: 시작할 때 와 있는 바이트에 `{ 0, 도착 }` 과 이어 보내기, 막힌 16 MiB write 가 상대의 답에 멈추고 이어 보내 상대가 받은 바이트가 그대로, FIN 뒤 write 는 `PEER_CLOSED` 이고 앞의 바이트는 read, RST 앞에 옮긴 바이트를 read 로 받음 (Windows 포함), Stage 가 차면 계속 보내고 timeout 까지 CPU 를 쓰지 않음 (POSIX 는 `clock()` 로 확인), TLS 는 key update 에 멈추지 않고 평문에 멈춤, 막힌 TLS write 가 멈추고 이어 보내도 레코드를 잃지 않음, writeFile 은 시작할 때만.
- integration `kiotty_test_base_api.cpp` +1: 공개 C API 의 `writeStreamClient` 가 도착 code 를 주고 `readStreamClient` 가 그 바이트를 줌.
- unit `kiotty_test_tls.cpp`: `TlsMemory` 상한 160 KiB.

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC (`build/msvc`, TLS OFF) | 857 | 866 |
| WSL gcc / gcc-notls / clang / clang-notls / asan / tsan | 858 | 867 |
| WSL clang-release | 857 | 866 |

모두 통과, 경고 0. 수칙 검사 기준선 그대로 (FILE 1, LINE 4, MACRO 18, PUBLIC 3). 단일 헤더를 임시 위치에 만들어 번역 단위 하나로 g++ 컴파일 확인.

## 남은 것

A2 (HTTP 서버 work), A3 (HttpClient 올리기, U2 의 두 테스트의 Windows 예외 없앰), U3.

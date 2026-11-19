# 2026-11-19 진행 정리 : StreamClient 취소 사이클 D2

기준 문서: `docs/design/design_peer_settings_and_cancel_v0.html` 3절, 5절, 7절의 D2 행. 앞 사이클은 `261119_progress_peer_settings_d1.md`.

## 정한 것 (2026-11-19, 사용자)

- 취소는 StreamClient 에만. 서버 work stream 에는 두지 않는다.
- 취소한 뒤에도 연결은 살려 둔다 (DICOM 에서 C-MOVE 를 기다리다 취소하면 같은 association 으로 C-CANCEL 을 보낸다). connect 도 깨운다.
- 남은 cancel 은 disconnect 가 지운다.

## 공개 API

- `KIOTTY_NET_CODE_CANCELLED` (`KiottyNetCode` 의 맨 끝, `DATA_ARRIVED_WHILE_WRITING` 뒤).
- `KiottyNetCode KiottyNet_cancelStreamClient(KiottyNetStreamClientHandle client)`: 아무 thread 에서. `NULL` 이면 `INVALID_ARGUMENT`, 그 밖에는 `SUCCESS`.
- C++ `KiottyNet_StreamClient::cancel()`.

## 안에서

- 깨움은 이미 있던 `StopSignal` (StreamClient 마다 하나) 이 한다. 소켓 backend 의 기다림은 소켓과 stop handle 을 함께 기다리고, connect 의 시도 사이 쉼도 stop 을 본다. 새 운영체제 자원과 새 메모리는 없다.
- base usecases (`kiotty_client_usecases.cpp`) 에 `connectStreamClient`, `disconnectStreamClient`, `cancelStreamClient` 를 두고 C 함수가 이것을 부른다.
  - connect / read / write 는 시작할 때 `stop().accept()` 가 참이면 기다리지 않고 `{ 0, CANCELLED }`.
  - 결과 code 가 `CLOSED` (stop 으로 깨어난 기다림) 이고 `stop().accept()` 가 참이면 `CANCELLED` 로. `toNetCode` 는 바꾸지 않았다 (HttpClient 와 서버가 `CLOSED` 에 기댐).
  - read / write 의 `CANCELLED` 는 연결을 닫지 않는다. connect 는 지금의 실패 경로대로 닫힌다.
  - disconnect 는 끊은 뒤 `stop().accept()` 로 남은 cancel 을 지운다.
- TLS write 의 길이는 바꿀 것이 없었다. 조각을 넣기 전에 늘 flush 하므로 조각 하나는 BIO 에 다 들어가고, 멈춤은 flush 에서만 일어난다. 그래서 돌려주는 길이는 이미 "받아들인 길이" 다.

## 설계와 다른 것 (확인 부탁)

1. 설계 5절은 남은 cancel 을 `StreamClient::disconnect` 가 지운다고 적었다. 구현은 공개 disconnect 의 usecase 가 지운다. `StreamClient::disconnect` 는 connect 의 실패 경로와 HttpClient 도 부르는데, 거기서 지우면 HttpClient 의 close 가 걸어 둔 stop 을 먹을 수 있다. 설계 문서에 메모를 붙였다.

## 테스트

새 integration `kiotty_test_stream_client_cancel.cpp` (9 개):

- `NULL` 은 `INVALID_ARGUMENT`.
- 막힌 read (timeout 0) 를 다른 thread 가 깨움 (300 ms 뒤 cancel, 1.5 초 안), 그 뒤 같은 연결로 read / write.
- 막힌 write: 짧은 timeout 의 write 가 `TIMEOUT` 이 될 때까지 채운 뒤 (Windows loopback 은 읽지 않는 상대에게도 64 MiB 를 받아 둔다), 막힌 1 MiB write 를 깨움. 상대가 읽기 시작하면 `data + bytes` 부터 이어 보내고 상대가 받은 바이트가 순서대로 맞음.
- 막힌 호출 없이 cancel 두 번: 다음 read 만 `{ 0, CANCELLED }`, 수신 Stage 의 바이트는 그 다음 read 가 받음. 다음 write 도 같음, 그 다음은 보통.
- disconnect 뒤 connect 는 남은 cancel 을 받지 않음.
- 시도 사이 쉼 (재시도 10 회, 2 초 간격) 의 connect 를 깨움, 그 뒤 다른 상대로 connect.
- C++ `cancel()`.
- TLS: 답하지 않는 상대와의 handshake 를 깨움 (연결 없음). 읽지 않는 TLS 서버로의 write 를 깨움, 그 뒤 read 는 `TIMEOUT` (연결 그대로, cancel 은 받아짐). TLS 를 끈 빌드에서는 건너뜀.

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC (`build/msvc`, TLS OFF) | 881 | 890 (TLS 둘은 건너뜀) |
| WSL gcc / clang × TLS ON / OFF, asan-tls, tsan-tls | 882 | 891 |
| WSL release-tls | 881 | 890 |

수칙 검사 기준선 그대로 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 문서

- `requirement_v0.md`: code 표, StreamClient 의 C / C++ 목록, 취소 규칙 (표와 세 줄).

## 남은 것

D1 ~ D2 끝. 단일 헤더는 commit 뒤 다시 만든다.

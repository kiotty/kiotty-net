# 2026-11-16 진행 정리 : HTTP 클라이언트 사이클 C4 (다시 연결, close, TLS)

기준 문서: `docs/design/design_http_client_v0.html` 3절 (연결과 수명), 결정 12, 4절 code 표, 9절, 11절의 C4 행. C2 는 `261116_progress_http_client_c2.md`.

## 공개 API

`KiottyNet_closeHttpClient(client)` 를 `kiotty_net_http.h` 의 `disconnectHttpClient` 옆에 더했다. 아무 thread 에서나 부르고 BUSY 를 보지 않는다.

## keep-alive 가 닫혀 있을 때 다시 보내기

| 것 | 구현 |
|---|---|
| 재사용 연결 | 이 요청이 열지 않은, 요청을 시작할 때 이미 열려 있던 연결. 앞 요청이 쓴 연결과 `connectHttpClient` 로 열어 두고 쉬던 연결이 모두 든다 (사용자 결정 2026-11-16: 장비는 시작할 때 connect 하고 몇 분 뒤에 첫 요청을 보내는 일이 많다). 처음 구현은 "최종 응답 머리를 받은 연결" 이었다 |
| 응답 바이트 없음 | 이 요청에서 줄 (상태줄, 1xx, 헤더) 을 하나도 읽지 않았고 실패한 때 수신 Stage 가 비어 있다 |
| 다시 보내는 실패 | `PEER_CLOSED`, `SOCKET_ERROR` (EOF, reset, broken pipe 의 write) |
| 메서드 | GET, HEAD, PUT, DELETE, OPTIONS 는 새 연결로 한 번만 다시 만들어 보낸다. POST / PATCH 는 `PEER_CLOSED` (밑의 오류가 `SOCKET_ERROR` 여도) |
| 다시 보내지 않음 | `TIMEOUT`, `CLOSED`, `PROTOCOL_ERROR`, TLS 오류, 새 연결의 실패 |

## 호출의 timeout 이 다시 연결까지

- `Deadline::bound(t)`: `t` 와 남은 시간 중 작은 것. limit 가 있으면 0 을 돌려주지 않는다 (wait 의 0 은 "끝없이").
- 요청 안의 다시 연결은 connect 시도마다, TLS handshake, 재시도 사이의 쉼을 남은 시간으로 자른다. 시간이 다 되면 재시도를 멈추고 마지막 시도의 code 를 돌려준다 (bound 된 connect 가 시간을 넘기면 `TIMEOUT`, 포트가 거절하면 `SOCKET_ERROR`, handshake 는 `TLS_HANDSHAKE_TIMEOUT` 일 수 있다).
- 요청 timeout 0 은 "제한 없음" (라이브러리 전체와 같다) 이라 설정의 connect 값을 그대로 쓴다.
- `connectHttpClient` 는 timeout 인자가 없으므로 설정 그대로. EventClient 는 예전 재시도 함수를 그대로 쓴다.

## closeHttpClient

**socket 을 다른 thread 에서 건드리지 않는다.** 실험해 보니 Linux 는 `shutdown(SHUT_RDWR)` 가 걸린 `poll` 을 깨우지만 Windows 의 `shutdown(SD_BOTH)` 는 걸린 read / write / connect 를 깨우지 못하고 timeout 까지 간다. 다른 thread 에서 socket 을 닫으면 `_handle` 경쟁과 재사용된 descriptor 문제도 있다.

**`StopSignal`** (`data/infra/socket/kiotty_stop_signal.*`, StreamClient 마다 하나):

| 것 | 내용 |
|---|---|
| close 수 | atomic counter. closeHttpClient 가 올린다 |
| 본 수 | owner thread 만 만지는 "마지막으로 본 값" |
| 깨우는 handle | POSIX: non-blocking pipe. Windows: 127.0.0.1 의 자기 자신에 connect 한 UDP socket (`WSA_FLAG_OVERLAPPED` 로 만들어야 한다. 없으면 closer 의 send 가 owner 의 걸린 `select` 뒤에 줄을 서서 timeout 에야 깨웠다) |
| 수명 | pool slot 마다 한 번 열고 context 를 놓을 때 닫는다 |

- client socket 의 모든 wait (POSIX `poll`, Windows `select`) 가 깨우는 handle 도 같이 본다. wait 전과 깨어난 뒤 counter 를 보고 새 close 가 있으면 `KIOTTY_NET_CODE_CLOSED`. close 가 아닌 깨움은 비우고 남은 시간으로 다시 기다린다. connect, TLS handshake, send, receive 가 모두 이 길이다. connect 재시도 사이의 쉼은 10 ms 마다 counter 를 본다.
- **호출 사이의 close 가 다음 호출을 막지 않는다.** connect, 요청, readBody, disconnect 는 시작할 때 counter 를 따라잡는다. 그 사이 close 가 있었으면 연결을 닫고 그대로 진행한다. 그 뒤에 온 close 만 "걸린 호출" 로 보고 `CLOSED`.
- **BUSY 플래그는 셋 (idle, call, close).** 걸린 호출이 없으면 closeHttpClient 가 close 상태를 잡고 자기 thread 에서 바로 연결을 닫고 idle 로 돌린다. 걸린 호출이 있으면 signal 을 올리고 닫는 일은 owner thread 가 한다. close 가 플래그를 잡은 짧은 동안 시작한 호출은 BUSY 대신 잠깐 기다린다.
- 공통 헤더와 대상 (address, tls) 은 남는다. 다음 요청이 다시 연다. 응답 헤더는 disconnect 뒤처럼 유효하다.
- `HttpClient::disconnect` 는 `HttpClientLink` (`disconnect()`, `close()`) 로 옮겼다 (공개 메서드 7).
- 깨울 수 없는 것: connect 안의 이름 풀기 (`getaddrinfo`).
- destroy 와 경쟁하는 close 는 다른 handle 처럼 범위 밖이다. 깨우는 handle 이 client 보다 오래 살아서 늦은 close 는 자기 pipe 에 쓸 뿐 재사용된 descriptor 를 건드리지 않는다. 재사용된 slot 의 오래된 handle 이면 새 client 의 연결을 닫을 수는 있다.
- **비용**: StreamClient 마다 깨우는 handle (POSIX fd 2 개, Windows socket 1 개) 을 가진다. 평문 StreamClient 도. fd 가 모자라면 create 가 실패할 수 있다.

## 테스트

17 개 추가.

- 재시도 5 (`kiotty_test_http_client_retry.cpp`): 서버가 idle 연결을 닫은 뒤 GET, PUT, DELETE, HEAD, OPTIONS 가 한 번씩 다시 감 (연결 6, 메서드마다 한 번), POST / PATCH 는 `PEER_CLOSED` 이고 서버 count 0, 다음 POST 는 됨, connect 로 열어 두고 쉬던 연결을 서버가 끊으면 GET 은 다시 가고 POST 는 `PEER_CLOSED` (다시 연결하지 않음), 요청이 연 연결의 실패는 다시 보내지 않음, 300 ms 호출 timeout 이 retry 간격 4 s 의 다시 연결을 자름
- close 7 (`kiotty_test_http_client_close.cpp`): 조용한 Work 스트림에 걸린 read 를 깨움 (그 뒤 요청이 다시 연결하고 공통 헤더가 그대로), 머리를 기다리는 요청을 깨움, retry 쉼 중의 다시 연결을 깨움, 10.255.255.1 로 가는 connect 를 깨움 (그 주소가 바로 응답하는 망이면 skip. Windows, WSL 둘 다 skip 되지 않음), 걸린 것 없이 두 번 close 뒤 요청이 됨, peer 가 idle socket 의 닫힘을 바로 봄, 200 요청 동안 1 ms 마다 close 해도 SUCCESS 나 CLOSED 뿐 (BUSY 없음)
- TLS 5 (`kiotty_test_http_client_tls.cpp`): 한 연결로 GET, POST, 기본 아닌 포트의 Host, chunked Work 스트림, TLS 위 재시도, TLS read 를 close 로 깨움. MSVC (TLS 없음) 는 기존 `tlsSupported()` 로 skip
- 테스트 도우미 `tests/integration/support/kiotty_test_http_site.*`

반복: `HttpClient*` (그때 35) 를 MSVC 30 번, gcc 30 번, ASAN 5 번, TSAN 10 번. 모두 통과, TSAN 경고 없음. 재사용 연결 규칙을 넓힌 뒤 gcc 에서 `HttpClient*` (36) 20 번 통과.

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC | 792 | 809 |
| wsl-gcc, gcc-notls, clang, clang-notls, clang-asan, clang-tsan | 793 | 810 |
| wsl-clang-release | 792 | 809 |

`tools/check_rules.py`: FILE 1, LINE 4, MACRO 18, PUBLIC 3 (그대로).

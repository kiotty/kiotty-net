# 2026-10-29 진행 정리 : base 사이클 3a (error + socket)

기준 문서: `docs/design/design_base_v2.md` 6절, `docs/test/test_base_v0.html` 6절, `docs/plan/plan_base_v0.md` 5절 3a.

## 만든 것

- `base/error/`
  - `kiotty_socket_error.h/.cpp`: `SocketCode` 14 개, `toSocketCode` (표 탐색), `toNetCode`.
  - `backend/kiotty_posix_error.cpp`, `backend/kiotty_windows_error.cpp`: OS 코드 표 (`socketErrorTable()`) 와 `lastSocketCode()`. Windows 표에는 IOCP 가 낼 `ERROR_NETNAME_DELETED`, `ERROR_CONNECTION_REFUSED`, `ERROR_CONNECTION_ABORTED` 를 미리 넣었다.
- `base/socket/`
  - `kiotty_socket.h`: ActiveSocket, PassiveSocket (설계 6절 + `port()`), connectWithRetry.
  - `kiotty_socket.cpp`: connectWithRetry. 플랫폼과 무관하다.
  - `backend/kiotty_posix_socket.cpp` (Linux, macOS, Android, iOS), `backend/kiotty_windows_socket.cpp`. 둘 다 non-blocking 이고, TCP 는 AF_INET, PIPE 는 AF_UNIX 다 (Windows 도 afunix.h). Windows 소켓은 IOCP 를 위해 `WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT` 로 만든다.
- 빈 파일이던 `backend/kiotty_linux_socket_error.h`, `kiotty_windows_socket_error.h` 는 지웠다 (설계의 `.cpp` 둘로 대체).
- 설계 v2 6절 끝에 이 사이클의 세부 여섯 줄을 적었다.

## 테스트

- unit: SocketError 2.
- integration (라벨 시작): ActiveSocket 10, PassiveSocket 6, ConnectWithRetry 5. 테스트 v0 의 `MaxTrialZeroIsRejectedByConfigNotHere` 는 "1 로 본다" 를 확인하는 `MaxTrialZeroIsTreatedAsOneTrial` 로 바꿨다 (config 가 막는 쪽은 사이클 7).
- support: `kiotty_test_os` (raw 소켓 도우미), `kiotty_test_peer` (LoopbackPeer, unusedPort, uniqueSocketPath, copyHost), `kiotty_test_wait.h` (waitUntil, elapsedMs), `kiotty_test_platform.cpp` (gtest Environment 가 Windows 는 WSAStartup, POSIX 는 SIGPIPE 무시). 계획의 `.h` 대신 `.cpp` 하나로 했다.
- Windows 에서 닫힌 포트로 connect 하면 SYN 재시도 때문에 약 2 초 뒤에 REFUSED 가 온다. 그래서 실패를 반복하는 retry 테스트는 없는 Unix socket 경로로 바로 실패하게 만든다.
- `ConnectTimeoutExpiresOnUnroutableAddress` 는 10.255.255.1 이 TIMEOUT 이 아니면 skip 한다.
- 로컬: MSVC Debug 97/97, WSL gcc 98/98, clang 98/98, clang Release 97/97, ASan+UBSan 98/98, TSan 98/98 (integration 5 회 반복 포함).
- CI: test 와 sanitizers job 이 `-L "unit|integration"` 을 돈다. macOS 도 POSIX 소켓 층을 돈다.

## 고친 것

- retry interval 을 ms 로 잘라 계산하면 구간마다 1 ms 까지 짧아져 "총 시간 ≥ 100 ms" 가 가끔 깨졌다. `steady_clock` 그대로 잰다.
- WSL 의 `/mnt/d` 에서는 Unix socket 을 bind 할 수 없다. 테스트 경로는 POSIX 에서 `/tmp/kt_<pid>_<n>.sock`, Windows 에서 작업 디렉터리의 상대 경로다. build 스킬에 적었다.

## 미결

- `ActiveSocket` 를 OS 핸들에서 만드는 길은 PassiveSocket::accept 하나뿐이다 (friend). IOCP Acceptor 의 AcceptEx 는 `open()` 으로 미리 만든 소켓을 쓰므로 3b 에서 필요하면 그때 연다.
- getaddrinfo 실패는 UNREACHABLE 로 묶었다. 이름 해석 실패를 따로 구분할지는 Client 사이클에서 정한다.

## 다음 할 일

CI 초록 확인. 그 뒤 사이클 3b: IoAction, IoEvent, EventListener, IOCP backend (테스트 v0 7절).

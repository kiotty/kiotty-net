# 2026-11-09 진행 정리 : kqueue backend (macOS, iOS)

기준 문서: `docs/requirement/requirement_v0.md` 3 (OS 별 backend), `docs/design/design_base_v5.html` 7.3, `docs/test/test_base_v0.html` 7절. 새 테스트는 없다 (7절 "새 backend 에 새 테스트는 없다"). 기존 integration 테스트가 macOS 에서 처음으로 실제로 돈다.

## 커밋

| 커밋 | 내용 |
|---|---|
| ce6e48f | kqueue backend. epoll 과 kqueue 가 readiness listener 를 공유. CI 에 libkqueue job, macOS job 은 이제 integration 테스트를 실제로 돈다 |
| b6f2b3a | CI 의 libkqueue 를 소스로 빌드 (ubuntu-latest 26.04 에 패키지가 없다). 테스트 helper `setSendBuffer(0)` 은 Apple 에서 1 (Darwin 은 `SO_SNDBUF` 0 을 `EINVAL`) |

## 코드

- `backend/kiotty_event_backend.h` 가 backend 를 한 곳에서 고른다: `_WIN32` 는 IOCP, `__APPLE__` (또는 Linux 의 `KIOTTY_NET_WITH_LIBKQUEUE`) 는 kqueue, `__linux__` 는 epoll. 매크로는 `KIOTTY_NET_EVENT_IOCP / KQUEUE / EPOLL / READINESS` 와 `KIOTTY_NET_HAS_EVENT_BACKEND` (DV10, `check_rules.py` MACRO 10 → 16).
- epoll 과 kqueue 는 같은 readiness 모델 (걸어 두고 ready 가 오면 send / recv 를 진행) 이라 공용 파일 넷에 OS 파일 둘이다.
  - 공용: `kiotty_readiness_event.h` (`IoEvent`, `ReadinessState`: 완료 배열, generation, ready 목록), `_listener.cpp` (open / close / attach / detach. attach 는 `F_DUPFD_CLOEXEC` 복제), `_submit.cpp` (submit, `sendRest`, `checkRequest`), `_wait.cpp` (wait, `settle`, `noteWake`, recv 진행, 남은 ms), `_ready.cpp` (ready 목록, `completionOf`, `IoEvent` 소멸자).
  - OS: `*_event_listener.cpp` 가 `wake / isOpen / createHandles / releaseHandles / watch / unwatch`, `*_event_wait.cpp` 가 `poll`. epoll 쪽은 전의 세 파일에서 이 둘로 줄었다 (`kiotty_epoll_event_state.h`, `_submit.cpp` 삭제).
- kqueue 의 선택
  - 관심은 `EV_ADD | EV_ENABLE | EV_ONESHOT`. 한 번 오면 knote 가 사라지므로 `poll` 이 `registered` 를 내린다 (epoll 의 ONESHOT 은 등록이 남아 `MOD` 로 다시 건다).
  - wake 는 `EVFILT_USER` (ident 1, `EV_CLEAR`) 에 `NOTE_TRIGGER`. 여러 번 깨워도 하나로 합쳐질 수 있다 (eventfd 와 같다. 7절 `TwoWakesBeforeWait...` 가 "최대 둘" 인 이유).
  - `EV_EOF` 와 함께 온 `fflags` (socket error) 를 들고 있다가 recv 가 0 (CLOSED) 을 주면 그 오류로 바꾼다. libkqueue 는 `SO_ERROR` 를 읽어 `fflags` 에 넣으면서 소켓의 오류를 지우므로 recv 가 RST 를 0 으로 돌려준다. Darwin 도 `fflags` 에 같은 값을 준다. epoll 은 `SUCCESS` 를 넘겨 전과 같다.
  - kqueue fd 에 `FD_CLOEXEC`. 상대 소켓의 `SIGPIPE` 는 socket 층이 `SO_NOSIGPIPE` 로 이미 막는다 (`MSG_NOSIGNAL` 이 없으면 0).
- `KIOTTY_NET_WITH_LIBKQUEUE` (CMake option, 기본 OFF): Linux 에서 libkqueue 로 kqueue backend 를 빌드해 돌린다. 헤더는 SYSTEM include (gcc 의 `-Wshadow` 가 `kevent` 함수와 `struct kevent` 를 가지고 경고한다). macOS runner 없이 kqueue 코드를 고칠 때 쓴다. WSL 에서 `~/.local` 에 설치해 썼다.

## 검증

| 환경 | 결과 |
|---|---|
| WSL gcc epoll (TLS ON) | 422 / 422 |
| WSL gcc libkqueue (TLS ON) | 422 / 422. integration 104 개를 5 회 반복해도 통과 |
| WSL clang ASan + UBSan libkqueue, `ctest -j 8` | 420 / 422. 둘 다 포트 충돌 (아래 정할 것 1). 따로 돌리면 3 / 3 통과 |
| MSVC TLS ON | 422 / 422 (IOCP 는 헤더 include 만 바뀌었다) |
| backend 없는 OS | 선택 매크로를 전부 0 으로 둔 헤더로 clang `-fsyntax-only -Werror` 통과 |
| CI 1차 (ce6e48f) | macOS apple-clang 421 / 422: `setSendBuffer(0)` 이 Darwin 에서 `EINVAL`. libkqueue job 은 `apt` 에 패키지가 없어 실패. 나머지 11 job 초록 |
| CI 2차 (b6f2b3a, run 42559587053) | 13 job 전부 초록. macOS apple-clang 422 / 422 (integration 포함), libkqueue 422 / 422, MSVC, gcc, clang, ASan + UBSan, TSan, Android, iOS, benchmark |

## 정할 것

1. (고쳤다, 2026-11-09) 병렬 `ctest -j 8` 의 포트 충돌. `unusedPort()` 가 port 0 으로 bind 해 번호를 얻고 닫았는데, 두 테스트 process 가 같은 번호를 받으면 한 쪽의 listen 이 `SOCKET_ERROR` 로 끝나거나 (`DestroyDoesNotBlockALoopCallbackThatCallsContext` 의 `await`) 다른 테스트의 connect 가 이 쪽 서버로 들어왔다 (`OnDisconnectedIsCalledExactlyOnce...` 의 1000 이 1001). 커널이 막 닫힌 번호를 다음 bind(0) 에 다시 줄 수 있고, 다른 process 의 connect 가 같은 ephemeral 번호를 집을 수도 있어서다. kqueue 와 무관하고 전부터 있던 것이며 sanitizer 로 느려지면 더 자주 났다. 고친 방법은 process 사이 잠금이다: `tests/integration/support/kiotty_test_port_lease.*` 의 `PortLease` 가 temp 디렉토리의 파일 하나 (`kiotty_test_ports.lock`) 를 `flock` / `LockFileEx` 로 잠그고 그 안의 counter 를 읽어 하나 올린다. `unusedPort()` 는 counter 로 20000 ~ 32767 (Linux 의 ephemeral 32768, macOS / Windows 의 49152 아래) 의 번호를 차례로 받고, bind 가 되는지 확인한 뒤 돌려준다. 같은 번호는 12768 번 뒤에나 다시 나오고 ephemeral 범위 밖이라 connect 와도 겹치지 않는다. 잠금 파일을 열 수 없으면 전처럼 port 0 으로 돌아간다.
2. UBSan 의 gtest 소음. 테스트 target 이 `-fno-rtti` 라 UBSan 의 vptr 검사가 gtest 안에서 "does not point to an object of type" 을 찍는다 (실패는 아니다). sanitizer 빌드에 `-fno-sanitize=vptr` 을 더하면 사라진다.
3. iOS 는 build only 그대로다. kqueue 는 같은 코드이고 시뮬레이터에서 ctest 를 돌리는 job 은 두지 않았다.

## 다음 할 일

io_uring (Linux. 완료 모델이라 readiness 공용 파일을 쓰지 않고 IOCP 쪽에 가깝다), HTTP 층.

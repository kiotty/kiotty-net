# kiotty_net_base 구현 계획 v0

2026-10-29. 기준은 `docs/design/design_base_v2.md` (무엇을 만드나) 와 `docs/test/test_base_v0.html` (무엇으로 확인하나) 이다. 이 문서는 그 둘을 **사이클** 로 자른다.

## 0. 한 사이클

```text
구현 ─▶ 로컬 테스트 ─▶ commit ─▶ push ─▶ GitHub Actions (타겟 환경 매트릭스에서 유닛 테스트)
 │          │                                  │
 │          └ 사이클의 테스트 전부 초록          └ 전부 초록이면 사이클 끝. 빨강이면 같은 사이클 안에서 고쳐 다시 push
 └ 설계 v2 의 한 절, 테스트 v0 의 한 절
```

규칙

- 사이클 하나는 설계의 한 덩어리와 그 덩어리의 테스트 전부다. 테스트 없는 구현도, 구현 없는 테스트도 사이클이 아니다.
- 로컬 테스트는 둘이다. Windows (VS 18 Insiders 의 MSVC, CMake 4.3.1 과 Ninja 는 `Common7/IDE/CommonExtensions/Microsoft/CMake/` 아래, IOCP) 와 WSL Ubuntu 24.04 (cmake 3.28, gcc 13, clang 18, ninja, epoll). 저장소는 WSL 에서 `/mnt/d/repo/kiotty-net` 이다. 둘 다 초록이어야 push 한다. CI 는 그 둘이 못 보는 것 (macOS, sanitizer, 다른 gcc) 을 본다.
- commit 은 사이클당 하나를 목표로 한다. CI 빨강을 고치는 commit 은 `fix(ci):` 로 따로 쌓고 squash 하지 않는다 (무엇이 플랫폼에서 깨졌는지가 기록이다).
- commit 메시지는 `<단계> <무엇>` 한 줄 + 본문에 통과한 테스트 수와 CI job. 끝에 `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- 사이클이 끝나면 `docs/progress/` 에 날짜 파일로 결정 / 미결 / 다음 할 일을 적는다.
- 완료 조건은 늘 같다: 그 사이클의 테스트가 로컬과 CI 매트릭스 전부에서 초록, 경고 0 (`/W4 /WX`, `-Wall -Wextra -Wpedantic -Werror`).

## 1. 타겟 환경과 CI 매트릭스

requirement 2 ~ 3 번의 타겟을 "지금 테스트를 돌릴 수 있는 곳" 과 "빌드만 되는 곳" 으로 나눈다.

| 타겟 | backend | CI runner | 사이클 0 부터 하는 것 | 언제부터 테스트를 돌리나 |
|---|---|---|---|---|
| Windows x64 | IOCP | `windows-latest`, MSVC | unit + integration | 사이클 3 (IOCP 가 첫 backend) |
| Linux x64 | epoll | `ubuntu-latest`, gcc 와 clang 둘 | unit + integration, ASan + TSan 한 번 더 | 사이클 3 (epoll 은 IOCP 다음) |
| macOS arm64 | kqueue | `macos-latest`, Apple clang | unit 만. kqueue backend 는 base 완성 뒤 | 사이클 2 (core unit), integration 은 kqueue 사이클 |
| Android arm64 | epoll | `ubuntu-latest` + NDK r26, cross build | 빌드만 (unit 바이너리 생성). 실행은 에뮬레이터 job 을 나중에 | 빌드 검사는 사이클 2 부터 |
| iOS arm64 | kqueue | `macos-latest`, Xcode toolchain, cross build | 빌드만 | 빌드 검사는 사이클 2 부터 |
| Linux io_uring | io_uring | — | base 완성 뒤. epoll 과 같은 integration 테스트를 그대로 | 나중 |

표준 플래그: 라이브러리는 gcc / clang `-std=c++11`, MSVC 는 C++11 모드가 없어 `/std:c++14 /permissive-`. 테스트 타깃은 어디서나 C++14 (GoogleTest 1.15). C 헤더는 `-std=c99` / `/std:c11 /TC` 로 따로 컴파일 검사.

## 2. 사이클 0 : 빌드 기반과 CI 골격

설계 ① 앞에 하나 더 둔다. 테스트가 돌 자리가 먼저 있어야 "구현 → 테스트" 가 사이클이 된다.

| 구현 | 내용 |
|---|---|
| `CMakeLists.txt` (root) | 타깃 `kiotty_net` (static). 옵션 `KIOTTY_NET_USE_TLS` (기본 ON, 사이클 4 전까지는 stub 만 있으므로 OFF 로 두고 CI 도 OFF), `KIOTTY_NET_BUILD_TESTS` (기본 OFF), `KIOTTY_NET_BUILD_BENCHMARKS` (기본 OFF), `KIOTTY_NET_TESTING` (테스트 훅 매크로). 소스는 나열, glob 없음. backend 소스는 `if(WIN32) / elseif(LINUX) / elseif(APPLE)` 로 하나만 |
| `tests/CMakeLists.txt`, `tests/unit/CMakeLists.txt` | 참고 프로젝트 조각 그대로: FetchContent googletest v1.15.2, `gtest_force_shared_crt ON`, `INSTALL_GTEST OFF`, `cxx_std_14`, `gtest_discover_tests`. 라벨 `unit` |
| `tests/integration/CMakeLists.txt` | 같은 모양, 라벨 `integration`. 사이클 3 까지는 smoke 하나만 (add_executable 은 소스가 있어야 한다) |
| `tests/benchmark/CMakeLists.txt` | FetchContent google/benchmark v1.9.1, ctest 에 넣지 않음. 사이클 2 까지는 smoke 하나 |
| `tests/unit/kiotty_test_smoke.cpp` | `TEST(Build, GoogleTestLinksAndRuns)` 하나. 매트릭스 전부에서 초록인지 보는 용도 |
| `.github/workflows/ci.yml` | 아래 |
| `.gitignore` | `build/`, `out/`, `.vs/` |

```yaml
# .github/workflows/ci.yml (골격. 사이클마다 job 이 늘어난다)
on: [push, pull_request]
jobs:
  test:
    strategy:
      fail-fast: false
      matrix:
        include:
          - { os: windows-latest, name: msvc,       cc: cl,      cxx: cl }
          - { os: ubuntu-latest,  name: gcc,        cc: gcc,     cxx: g++ }
          - { os: ubuntu-latest,  name: clang,      cc: clang,   cxx: clang++ }
          - { os: macos-latest,   name: apple-clang, cc: clang,  cxx: clang++ }
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DKIOTTY_NET_BUILD_TESTS=ON -DKIOTTY_NET_USE_TLS=OFF
      - run: cmake --build build --config Debug --parallel
      - run: ctest --test-dir build -C Debug --output-on-failure -L unit
  sanitizers:                       # Linux clang. -fsanitize=address,undefined 한 번, thread 한 번
    runs-on: ubuntu-latest
    strategy: { matrix: { san: [address, thread] } }
    steps: [checkout, cmake -DKIOTTY_NET_SANITIZER=${{ matrix.san }}, build, ctest -L unit]
  cross-build:                      # 빌드만. Android NDK (ubuntu), iOS (macos)
    ...
```

integration 라벨은 사이클 3 에서 Windows / Linux job 에 `-L "unit|integration"` 으로 켠다. macOS 는 kqueue 가 생길 때.

| 로컬 확인 | Windows: `vcvars64.bat` 뒤 `cmake -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKIOTTY_NET_BUILD_TESTS=ON -DKIOTTY_NET_USE_TLS=OFF`, `cmake --build build/msvc`, `ctest --test-dir build/msvc -L unit --output-on-failure`. WSL: `cmake -B build/wsl-gcc -G Ninja ...` 와 `-DCMAKE_CXX_COMPILER=clang++` 로 `build/wsl-clang`. `build/` 는 .gitignore |
|---|---|
| 테스트 | smoke 1 |
| 완료 | 네 runner 전부 초록, sanitizers 둘 초록, cross-build 둘 빌드 성공 |
| commit | `build: add CMake targets, GoogleTest harness and CI matrix` |

## 3. 사이클 1 : base.h 와 statemachine.h (설계 ①)

| 구현 | 설계 v2 3절. `KiottyNetCode` 에 TLS 코드 8 개, `no_delay` + `configure*NoDelay`, `KiottyNetActionType` / `KiottyNetConnectionAction` / 여섯 action 함수, `KiottyNetConnectionDecision` + accept / ignore, 콜백이 action 을 return 하는 `KiottyNetConnectionHandler` (onConnected 에 handle 없음), `notifyConnection`, `finishWork(work, action)`, `isWorkConnected`, 넷 `*Result` + create 넷. 지우는 것: `setConnectionTimeout`, handle 받는 action 함수, `finishWorkWith*` 넷, `KiottyNetConnectionHost`. statemachine.h: `KiottyNetEntryAction` → `KiottyNetConnectionAction`, `make*EntryAction` 여섯 삭제. `kiotty_net_statemachine.cpp` 가 그대로 컴파일되게 맞춘다. http.h / websocket.h 가 base.h 의 바뀐 타입을 쓰면 같이 고친다 |
|---|---|
| 테스트 | 테스트 v0 15절 ①: 헤더 컴파일 검사만. `tests/unit/kiotty_test_headers_cpp.cpp` (base, statemachine, http, websocket 전부 include, C++) 와 `tests/unit/kiotty_test_headers_c.c` (C) 를 unit 타깃에 넣어 CI 매트릭스에서 네 컴파일러가 본다. 로컬은 memory 의 레시피 (clang 17 `-std=c++11 -fsyntax-only`, `-std=c99`; MSVC `/Zs /W4 /WX /std:c++14 /permissive-`, `/TC /std:c11`) |
| 완료 | 네 runner 컴파일 초록. `docs/requirement/requirement_v0.md` 의 5 ~ 7절이 새 시그니처와 맞게 고쳐져 있다 (문서가 코드보다 늦으면 안 된다) |
| commit | `api: return-action handler, finishWork(action), no_delay and TLS codes in kiotty_net_base.h` |

## 4. 사이클 2 : core (설계 ②)

`kiotty_stage.h` 는 지금 빈 파일이다. Stage 부터 짠다.

| 구현 | `core/kiotty_stage.h` (requirement 4절의 Provider / Consumer / compact), `kiotty_stage_set.h`, `kiotty_span.h`, `kiotty_pool_handle.h` (toRaw / fromRaw / ownerOf / slotOf, 13.3 비트 배치), `kiotty_pool.h` (reference 의 BlockPool 을 가져와 이름만 맞춘다. 16 정렬, 등급 고갈 시 heap fallback + 카운터), `kiotty_clock.h`. 전부 헤더 + 작은 .cpp. OS 호출은 clock 하나 |
|---|---|
| 테스트 | 테스트 v0 5절 전부 (Stage 10, StageSet 2, Span 2, PoolHandle 4, Pool 7, Clock 1, Holder / Result 기존 계약). 벤치마크 `PoolAllocateRelease`, `StageProvideConsume` |
| 확인받을 계약 | 테스트 v0 14절 3: `Stage::commit` 초과와 `Pool::release` 남의 포인터 → debug `assert` + release 에서 clamp / 무시. 이 사이클에서 확정하고 테스트 이름을 맞춘다 |
| 완료 | 네 runner + sanitizers 초록. cross-build 둘 빌드 성공 (여기부터 소스가 있다). `fallbackCount` 가 전 테스트에서 0 |
| commit | `core: Stage, StageSet, Span, PoolHandle, Pool, Clock with unit tests` |

## 5. 사이클 3 : error, socket, event (설계 ③)

가장 긴 사이클이라 셋으로 자른다. 3a 와 3b 는 Windows 로컬에서 끝나고, 3c 는 CI 가 주 실행 환경이다.

### 3a. error + socket

| 구현 | `error/kiotty_socket_error.h` + `backend/kiotty_windows_error.cpp`, `kiotty_posix_error.cpp`. `socket/kiotty_socket.h` (ActiveSocket, PassiveSocket, connectWithRetry) + `backend/kiotty_windows_socket.cpp`, `kiotty_posix_socket.cpp`. WSAStartup 은 Context 것이지만 테스트가 필요하니 `tests/integration/support/kiotty_test_platform.h` 가 main 에서 한 번 부른다 |
|---|---|
| 테스트 | 테스트 v0 6절 전부 (SocketError 2, ActiveSocket 10, PassiveSocket 6, ConnectWithRetry 5). support: `LoopbackPeer`, `kiotty_test_port.h`, `waitUntil`. integration 라벨 시작. `ConnectTimeoutExpiresOnUnroutableAddress` 는 네트워크 없는 runner 에서 `GTEST_SKIP` |
| CI | Windows / Linux job 에 `-L "unit|integration"`. macOS 는 posix backend 가 그대로 컴파일되므로 소켓 테스트도 돌려 본다 (kqueue 와 무관한 층). sanitizers 도 integration 포함 |
| commit | `socket: ActiveSocket, PassiveSocket, connectWithRetry on Windows and POSIX with loopback tests` |

### 3b. event, IOCP

| 구현 | `event/kiotty_io_action.h`, `kiotty_event.h` (IoEvent, pending, action), `kiotty_event_listener.h`, `backend/kiotty_iocp_event_listener.cpp`. IoEvent 의 실제 모양 (OVERLAPPED + action + pending) 은 .cpp 안, `Holder<IoEvent>` 로 든다. detach 는 closesocket 에 맡기고 CANCELLED 는 OS 완료 (ERROR_OPERATION_ABORTED) 를 변환한다 |
|---|---|
| 테스트 | 테스트 v0 7절 중 IoAction 4, IoEvent 1, EventListener 24, `EventListener_Iocp` 1. Linux 는 아직 backend 가 없으니 `KIOTTY_NET_HAS_EVENT_BACKEND` 가 꺼진 플랫폼에서는 EventListener 테스트 파일을 타깃에서 뺀다 (CMake 조건) |
| CI | Windows 초록이면 push. Linux / macOS 는 unit + socket integration 만 |
| commit | `event: IoAction, IoEvent, EventListener with IOCP backend` |

### 3c. event, epoll

| 구현 | `backend/kiotty_epoll_event_listener.cpp`. EPOLLONESHOT, readable 뒤 recv → payload, send 는 바로 하고 남으면 EPOLLOUT, wake 는 eventfd, detach 가 걸린 event 마다 CANCELLED 완료를 자기 배열에 넣는다 |
|---|---|
| 테스트 | 3b 와 **같은 파일**. 추가는 `EventListener_Epoll` 1 뿐. 두 backend 가 같은 테스트를 통과하는 것이 proactor 흉내의 완성 조건 |
| CI | WSL 의 gcc 13 / clang 18 에서 먼저 초록을 만들고 push. CI 는 ASan / TSan 과 macOS 의 posix 소켓 층을 더 본다 |
| 완료 | 매트릭스 전부 초록. 플랫폼 분기가 있는 테스트는 7절의 둘뿐 |
| commit | `event: epoll backend passes the same EventListener tests as IOCP` |

## 6. 사이클 4 : tls (설계 ④)

| 구현 | `tls/kiotty_tls_profile.h`, `kiotty_tls_engine.h`, `backend/kiotty_stub_tls.cpp` 먼저 (open 이 NOT_SUPPORTED, 나머지 INVALID_ARGUMENT), 그 다음 `backend/kiotty_openssl_tls.cpp` (SSL_CTX, SSL + memory BIO 둘, handshake / decode / encode / hasControlOutput, lastCode / lastMessage 의 8.1 분류). `KIOTTY_NET_TESTING` 아래 `debugKeyUpdate()` / `debugShutdown()`. CMake: `KIOTTY_NET_USE_TLS` ON 이면 `find_package(OpenSSL 3)` 와 openssl .cpp, OFF 면 stub .cpp. 테스트 인증서 `tests/certs/generate.sh` 와 생성된 PEM 다섯 벌 커밋 |
|---|---|
| 테스트 | 테스트 v0 8절 (TlsProfile 6, TlsEngine 23) 을 unit 으로, `TlsPump` 로 소켓 없이. stub 빌드에서는 `StubBuildReturnsNotSupportedForBothOpens` 하나만 돌고 나머지 `GTEST_SKIP`. 벤치마크 없음 (TLS 벤치마크는 사이클 8) |
| CI | 매트릭스에 `tls: [ON, OFF]` 축을 더해 8 개. OpenSSL: ubuntu `libssl-dev`, macOS `brew openssl@3` + `OPENSSL_ROOT_DIR`, windows runner 의 기본 OpenSSL (`C:\Program Files\OpenSSL`) 을 `find_package` 로 찾고 없으면 `choco install openssl`. 이 부분은 첫 push 에서 확인하고 고친다 |
| 확인받을 것 | 테스트 v0 14절 6: `debug*` 훅 둘 허용 |
| 완료 | 8 job + sanitizers 초록. TLS ON 에서 `fallbackCount` 0 (OpenSSL 할당이 전부 Pool 로) |
| commit | `tls: TlsProfile and TlsEngine over OpenSSL memory BIO, stub backend, test certificates` |

## 7. 사이클 5 : connection (설계 ⑤)

케이스가 가장 많은 사이클이지만 소켓이 없어 로컬에서 전부 끝난다. 둘로 자른다.

### 5a. WorkContext, NotificationContext, IoSource, ConnectionHost 골격, Connection 평문

| 구현 | `connection/kiotty_io_source.h`, `kiotty_work_context.h`, `kiotty_notification_context.h`, `kiotty_connection.h/.cpp` (9.2 상태 전부, 9.4 의 dispatch / apply / onOpened / onReceived / onSent / onTimeout / extend / onWorkFinished / onPeerGone / adopt / pump / stageWrite / resolve, 9.5 finishWork, 9.6 notify / onNotified / consumeNotification), `kiotty_connection_host.h/.cpp` (acquire / release / full / empty / find / broadcast, Phase::IDLE) |
|---|---|
| 테스트 | 테스트 v0 9절 중 WorkContext 5, Connection 의 평문 케이스 48, ConnectionHost 8. support: `RecordingHandler`, `ScriptedHandler`, `completion::*`. Connection 은 capacity 1 의 ConnectionHost 의 `at(0)` 으로 만든다 |
| 확인받을 계약 | 테스트 v0 14절 1 ~ 2: READ_UNTIL 이 delimiter 를 포함, READ 0 / WRITE 0 / delimiter 0 또는 5 는 onError(INVALID_ARGUMENT) + CLOSE. 이 사이클 시작 전에 확정 |
| commit | `connection: Connection dispatch/resolve, WorkContext, NotificationContext, ConnectionHost with plaintext unit tests` |

### 5b. Connection TLS 경로, Acceptor

| 구현 | `_tls.active()` 분기 네 자리 (onOpened, onReceived 의 decode / CONTROL, stageWrite 의 encode, onNotified 의 encode, onWorkFinished 의 encodeSendStage), `connection/kiotty_acceptor.h` + IOCP / epoll 쪽 resolve (AcceptEx 버퍼 / 빈 payload) |
|---|---|
| 테스트 | Connection 의 TLS 케이스 9 (TlsPump 로 DONE 까지 간 engine 을 심는다), Acceptor 3 (integration) |
| 완료 | unit 전부 매트릭스 초록 (TLS ON job 에서 TLS 케이스 포함). Acceptor integration 은 Windows / Linux |
| commit | `connection: TLS path in Connection, Acceptor as IoSource` |

## 8. 사이클 6 : io_loop (설계 ⑥)

| 구현 | `io_loop/kiotty_io_loop.h/.cpp`: run / runOnce / collect / broadcast / step / access / admit / submit / settle / shutdown 을 설계 v2 10절 그대로. `nearestDeadline`, `_completions` 배열 (listener capacity + 2 × host capacity) |
|---|---|
| 테스트 | 테스트 v0 10절 IoLoop 25 (integration). IoLoop 를 Server 없이 직접 조립 (ConnectionHost + EventListener + Acceptor + IoLoop) 하고 run 은 테스트 thread. `AcceptFailureReportsOnError...` 는 POSIX 만. 벤치마크 `EchoRoundtrip1KiB`, `IdleCpu`, `NotifyLatencyFromOtherThread`, `NotifyFromHandlerLatency` 를 IoLoop 조립으로 먼저 잰다 |
| CI | Windows / Linux integration. TSan job 이 여기서 처음 의미를 가진다 (notify / finishWork 의 cross-thread). 플래키 방지: 모든 대기는 `waitUntil`, 포트는 0 으로 bind 해 고른다, timeout 검증은 하한만 엄격하고 상한은 넉넉히 (100 ms 요청 → 100 ~ 500 ms) |
| 완료 | integration 매트릭스 초록, TSan 경고 0. 벤치마크 넷의 첫 수치를 테스트 v0 12절의 기준치로 기록 |
| commit | `io_loop: IoLoop cycle collect/broadcast/step/settle with loopback tests` |

## 9. 사이클 7 : server, client, context, config (설계 ⑦)

| 구현 | `context/kiotty_owner.h`, `kiotty_context.h/.cpp` (Holder<Owner>[16], generation, Pool, g_contexts[16], CRYPTO_set_mem_functions, WSAStartup), `config/kiotty_config.h/.cpp` (initialize* / configure* 23 개, isInitialized 4 오버로드, mark), `config/kiotty_action.h` (조립 8 개), `server/kiotty_server.h/.cpp`, `client/kiotty_event_client.h/.cpp`, `client/kiotty_stream_client.h/.cpp` |
|---|---|
| 테스트 | 테스트 v0 11절 중 Config 7, Action 2 (unit). Context 10, Server 23, EventClient 8, StreamClient 8 (integration) 을 **내부 클래스로** 먼저 돌린다 (C API 는 사이클 8). TLS 가 필요한 Server / Client 케이스는 테스트 support 에 OpenSSL blocking peer helper 를 더한다. `EXPECT_DEATH` 둘은 Debug 빌드만 |
| 완료 | 매트릭스 초록 (TLS ON / OFF). `OnDisconnectedIsCalledExactlyOncePerAcceptedConnectionOverAThousandConnections` 가 ASan 에서 누수 0 |
| commit | `owner: Server, EventClient, StreamClient, Context and config functions` |

## 10. 사이클 8 : base.cpp 번역과 C API 테스트 (설계 ⑧)

| 구현 | `src/kiotty_net_base.cpp`: 설계 v2 15절 표 그대로. `conn(h)` 의 네 단계와 C ↔ 내부 변환만. 로직 0 줄 (리뷰 기준: 분기는 nullptr 검사와 `isInitialized` 뿐) |
|---|---|
| 테스트 | 테스트 v0 11절을 `include/kiotty_net_base.h` 만 포함하는 `tests/integration/kiotty_test_base_api.cpp` 로 다시 쓴다 (사이클 7 의 내부 클래스 테스트는 그대로 둔다). handle 군 TEST_P { 0, 유효, 옛 generation, 옛 owner generation, 범위 밖, 랜덤 }. 벤치마크 12절 나머지 전부 (EchoRoundtrip32Clients, ReadHeavy*, WriteHeavy*, BroadcastFanOut, ConnectionChurn, TlsEchoOverhead, TlsHandshakeRate, WorkHandoff) |
| CI | `benchmark` job 추가. `workflow_dispatch` 와 main push 에서만, 결과 JSON 을 artifact 로. 회귀 판정은 다음 사이클 (base 밖) 에서 `benchmark/compare.py` 로 |
| 완료 | 매트릭스 초록. 벤치마크 수치를 테스트 v0 12절에 기준치로 적는다. `docs/progress/` 에 "base 완료" 와 kqueue / io_uring / HTTP 로 가는 다음 할 일 |
| commit | `base: kiotty_net_base.cpp translation layer, C API tests and benchmarks` |

## 11. 사이클 요약

| 사이클 | 설계 | 테스트 v0 | 층 | 로컬에서 끝나나 | 매트릭스 변화 |
|---|---|---|---|---|---|
| 0 | — | smoke | — | 예 | 4 runner + sanitizers 2 + cross-build 2 |
| 1 | ① base.h | 15절 ① 헤더 컴파일 | — | 예 | — |
| 2 | ② core | 5절, 벤치 2 | unit | 예 | — |
| 3a | ③ error, socket | 6절 | integration | 예 | integration 라벨 켬 |
| 3b | ③ event IOCP | 7절 | integration | 예 (Windows) | — |
| 3c | ③ event epoll | 7절 같은 파일 | integration | 예 (WSL) | — |
| 4 | ④ tls | 8절 | unit | 예 | tls ON / OFF 축, OpenSSL 설치 |
| 5a | ⑤ connection 평문 | 9절 | unit | 예 | — |
| 5b | ⑤ connection TLS, Acceptor | 9절, 10절 Acceptor | unit, integration | 예 | — |
| 6 | ⑥ io_loop | 10절, 벤치 4 | integration | 예 | — |
| 7 | ⑦ owner, config | 11절 (내부 클래스로) | unit, integration | 예 | — |
| 8 | ⑧ base.cpp | 11절 (C API 로), 벤치 나머지 | integration, benchmark | 예 | benchmark job |

## 12. 리스크와 대응

| 리스크 | 대응 |
|---|---|
| MSVC 에 C++11 모드가 없다 | 라이브러리는 gcc / clang C++11 로 CI 가 지킨다. MSVC 는 `/std:c++14 /permissive-`. C++14 전용 문법이 들어오면 Linux job 이 빨강 |
| WSL 의 커널이 5.10 이라 io_uring 이 제한된다 | base 범위 밖. io_uring 사이클은 CI 의 ubuntu-latest (6.x) 가 실행 환경이고 로컬은 epoll 로 본다 |
| CI 의 OpenSSL 위치가 runner 마다 다르다 (로컬 Windows 는 `C:\Program Files\OpenSSL` 3.x, WSL 은 `libssl-dev` 설치 필요) | 사이클 4 의 첫 push 는 "OpenSSL 찾기" 가 목적. 안 되면 `FetchContent` 로 OpenSSL 소스 빌드 (SecuritySocket 이 그렇게 한다) 로 바꾼다. 느리지만 확실 |
| 타이밍 테스트의 플래키 | 고정 sleep 금지, `waitUntil`, 상한 넉넉히, 포트 0. 두 번 연속 플래키면 그 테스트를 고치는 commit 이 먼저 |
| TSan 이 Windows 에 없다 | cross-thread 경로 (notify, finishWork, stop) 의 검증은 Linux TSan job 에 맡긴다. 그래서 TSan 은 사이클 0 부터 매트릭스에 있다 |
| Android / iOS 는 빌드만 한다 | base 범위에서는 받아들인다. 실행은 에뮬레이터 job 을 kqueue / io_uring 사이클과 함께 |
| 테스트가 설계에 없는 계약을 전제한다 | 테스트 v0 14절의 일곱 가지를 해당 사이클 시작 전에 확정하고 design v2 에 한 줄씩 적는다 (2 → 사이클 2, 1 과 2 → 사이클 5a, 6 → 사이클 4, 4 와 5 → 사이클 3b / 7) |
| 사이클이 너무 커진다 | 3 과 5 는 이미 잘랐다. 더 자를 때도 "구현 + 그 테스트" 를 함께 자른다. 테스트만 뒤로 미루지 않는다 |

## 13. 지금 할 일

사이클 0. `CMakeLists.txt`, `tests/` 의 CMake 셋, smoke 테스트, `.github/workflows/ci.yml`. 로컬에서 `ctest -L unit` 초록 → commit → push → 네 runner 와 sanitizers 초록 확인 → 사이클 1.

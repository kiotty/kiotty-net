# 2026-11-11 진행 정리 : base 사이클 B1 (한 WRITE 에 send 다음 notify, 송신 timeout)

기준 문서: `docs/design/design_http_v0.html` 2절 (2.1, 2.2) 과 11절의 B1 행. HTTP (H1 ~ H6) 와 WebSocket (W1 ~ W5) 이 이 위에 선다.

## 바꾼 것

### 송신 순서 (2.1)

| 자리 | 전 | 후 |
|---|---|---|
| `IoPayload` | `{ buffer, length }` 하나 | `{ IoPiece first; IoPiece second; }`. `single`, `pair` (first 가 비면 second 를 앞으로), `empty`, `total()`. READ 와 ACCEPT 는 first 만 |
| `IoAction` 의 write | `length` | `length` (send Stage) 와 `notify_length` (notify Stage). `IoAction::gather(length, notify_length)` 는 send 가 있으면 USER, 없으면 NOTIFY |
| `SendFlow::select` | notify 가 있으면 notify 전부, 없으면 send 전부 | send 전부 **다음에** notify 전부를 한 WRITE 로. cipher 가 남아 있으면 그것 먼저, 핸드셰이크 중이면 평문은 고르지 않음은 그대로 |
| `SendFlow::payload` | 고른 Stage 하나 | 평문은 first = send, second = notify. TLS 는 cipher send Stage 하나 |
| TLS encode | 고른 Stage 하나 | pick 때 send 다음 notify 를 encode |
| cipher send Stage 크기 | `largerOf(send, notify)` 의 암호문 상한 | `gatheredPlainBytes(send, notify)` = send + notify + 레코드 하나의 overhead 의 암호문 상한. 기본 (4 KiB, 4 KiB) 에서 연결당 약 4 KiB 늘어난다. `largerOf` 는 지웠다 |
| `SendFlow::release` | 한 맥락을 그 길이만큼 | 두 길이를 각자. notify 는 lock 안에서. send 길이가 0 보다 크면 `onWritten(그 길이)` |
| IOCP | `WSASend` 버퍼 하나 | `WSABUF` 둘 (second 가 비면 하나) |
| epoll / kqueue | `send` 를 `done` 부터 | `sendmsg` 에 남은 iovec (`backend::restMessage` 가 `done` 으로 두 조각을 나눈다) |
| io_uring | `IORING_OP_SEND` | `IORING_OP_SENDMSG`. msghdr 와 iovec 둘은 `IoEvent` 의 멤버라 완료까지 산다 |
| `Sender::stage` | span 하나 | span 여러 개를 한 번에 (총 길이를 먼저 보고, 모자라면 하나도 넣지 않음). span 하나 판은 그 위의 한 줄 |
| `NotificationDesk` | `notify(id, span)`, `broadcast(span)` | 같은 이름에 `Span<const KiottyNetSpan>` 판을 더했다. 한 lock 안에서 이어 복사, 다 들어가지 않으면 `BUFFER_FULL`. 공개 API 는 그대로 (WebSocket 의 frame 머리 + payload 가 쓸 자리) |

### 송신 timeout (2.2)

| 자리 | 내용 |
|---|---|
| 공개 헤더 | `KiottyNetServerConfiguration` / `KiottyNetClientConfiguration` 끝에 `uint32_t send_timeout_milliseconds` (기본 30000, 0 은 무한). `KiottyNet_configureServerSendTimeout`, `KiottyNet_configureClientSendTimeout`, builder 의 `sendTimeout(ms)`. `KIOTTY_NET_CODE_PEER_NOT_READING` 을 enum 끝에 |
| `ConnectionLimits` | `send_timeout_milliseconds` (`size_t`, 크기 = `4 * sizeof(size_t)`) |
| `SendFlow` | 송신 deadline (`std::atomic<uint64_t>`) 과 timeout 을 가진다. pick 이 WRITE 를 고르면 적고, release 와 `clearDeadline` 이 지운다. `limit(timeout)` 은 Connection 의 initialize / reset 이 부른다 |
| `ConnectionStatus` | `send_deadline` 과 `isSendExpired(now)`. 크기 16 → 24 |
| `IoKind` / `IoCompletion` | `SEND_TIMEOUT`, `IoCompletion::sendTimeout(slot)` |
| Connection | `SEND_TIMEOUT` 이면 deadline 을 지우고 `close(PEER_NOT_READING)`. CLOSING 에 들어갈 때도 지운다 (지우지 않으면 `stepUntilQuiet` 이 같은 slot 을 계속 깨운다) |
| IoLoop | `stepSlot` 이 송신 deadline 을 read deadline 보다 먼저 본다. 대기 시간은 둘 중 가까운 쪽 |

걸린 WRITE 의 취소는 새로 만들지 않았다. CLOSE 가 부르는 detach 가 IOCP 는 `CancelIoEx`, epoll / kqueue 는 unwatch 와 CANCELLED 완료, io_uring 은 `ASYNC_CANCEL` 로 이미 한다.

### 문서

| 문서 | 내용 |
|---|---|
| `requirement_v0.md` 1절 | `PEER_NOT_READING` |
| 같은 문서 4절 (설정) | 두 설정의 `send_timeout_milliseconds`, configure 둘, builder 둘 |
| 같은 문서 4절 (송신) | "송신 순서" 를 이 규칙으로 (전에는 "write lane 먼저" 로 적혀 코드의 "notify 먼저" 와 달랐다). Work 중에는 아무것도 보내지 않는다 (전에는 "push lane 만 보낸다"). "송신 timeout" 한 항목 |
| `implementation_guide_v0.md` 1.6 | B1 에서 바뀐 것 여섯 줄 |
| `test_base_v0.html` 17절 | 아래 테스트 |

## 테스트

새로 13 개 (MSVC 기준 488 → 501), 고친 것 11 개.

- Connection: 한 WRITE 에 send → notify, notify 가 계속 차도 send 가 굶지 않음, notify 만의 WRITE, 두 길이의 release, 송신 deadline 의 시작 / 지움 / 0, `SEND_TIMEOUT` → `PEER_NOT_READING`.
- ConnectionTls: cipher 먼저 그 다음 send → notify, TLS 의 encode 순서 (`replynotify`).
- ConnectionHost: 여러 조각의 notify / broadcast, 다 들어가지 않으면 하나도 넣지 않음, null 조각.
- EventListener: gather WRITE 의 순서와 총 길이, 소켓 버퍼보다 큰 gather WRITE 가 조각 경계를 넘어도 순서가 맞음.
- Configuration: 기본 30000, configure 둘, builder 둘, initialize 전 configure.
- BaseApi (공개 C API 만): 읽지 않는 상대에게 계속 write 하는 서버가 300 ms 뒤 `onDisconnected(PEER_NOT_READING)`.
- 고친 것: notify 먼저 / WRITE 하나에 Stage 하나를 가정하던 Connection 6 개와 ConnectionTls 5 개. 그 밖에 `IoPayload` 의 필드 이름 (`first`, `total()`), 메모리 레이아웃 테스트의 cipher 크기, `ConnectionLimits` 초기화의 넷째 값.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF (IOCP) | 501 / 501 |
| WSL gcc Debug, TLS OFF / ON (epoll) | 502 / 502, 502 / 502 |
| WSL clang Debug, TLS OFF / ON | 502 / 502, 502 / 502 |
| WSL gcc Release TLS ON | 501 / 501 |
| WSL asan,undefined TLS ON | 502 / 502 |
| WSL thread TLS ON | 502 / 502 |

WSL 의 커널은 io_uring 을 허락하지 않아 (`EventListener_Ring` 이 SKIP) 로컬에서는 epoll 로 돈다. io_uring 의 `IORING_OP_SENDMSG` 길과 kqueue 의 `sendmsg` 길은 CI (ubuntu-latest, macos-latest, libkqueue) 가 본다.

asan,undefined 빌드의 출력에 googletest 내부 (`gtest-param-util.h`, `gtest.cc`) 의 UBSan vptr `runtime error` 가 다섯 줄 있다. 테스트 등록 단계 (`--gtest_list_tests` 만으로도 나온다) 이고 실패로 세지 않는다. B1 이 바꾼 코드와 무관한 자리다.

## benchmark

WSL Release, TLS OFF (`build/wsl-bench`, epoll), `--benchmark_min_time=1s --benchmark_repetitions=3` 의 평균 (real time). 전 = HEAD (`e04b82e`), 후 = B1. 후는 두 번 돌렸다.

| benchmark | 전 | 후 1 | 후 2 |
|---|---|---|---|
| EchoRoundtrip clients 1 / max 16 | 43.8 us | 27.3 us | 56.3 us |
| EchoRoundtrip clients 128 / max 1024 | 271 us | 270 us | 508 us |
| BroadcastFanOut 16 | 66.8 us | 50.9 us | 53.3 us |
| BroadcastFanOut 128 | 213 us | 195 us | 212 us |
| WriteHeavy (Work, B1 과 무관) | 140 us | 109 us | 202 us |
| EventClientWriteHeavy (Work, B1 과 무관) | 136 us | 109 us | 105 us |

같은 코드의 두 번 (후 1, 후 2) 이 두 배까지 다르고, B1 과 무관한 WriteHeavy 도 같은 폭으로 움직인다. 이 기계에서는 전후 차이를 읽을 수 없다. 느려졌다는 신호는 없다 (BroadcastFanOut 은 세 번 모두 전 이하).

Windows MSVC Release (`build/msvc-bench`) 에서는 Server 를 쓰는 benchmark (EchoRoundtrip, WriteHeavy, BroadcastFanOut) 가 90 초 안에 끝나지 않는다. HEAD 에서도 똑같이 멈추므로 B1 이 만든 것이 아니다. EventClientWriteHeavy 는 둘 다 돈다 (575 us / 571 us). 원인은 따로 본다.

2026-11-12 원인: benchmark 의 문제였고 라이브러리는 무관하다. `BenchServer` 는 멤버 순서상 `port(unusedPort())` 가 `_context(KiottyNet_initializeContext())` 보다 먼저 돈다. benchmark 는 gtest 의 `PlatformEnvironment` 가 없어서 그때 Winsock 이 시작되지 않았고, `isBindable` 의 `socket()` 이 매번 실패해 `unusedPort` 의 `while` 이 끝나지 않았다. Client benchmark 는 Context 를 먼저 만들어서 돌았고, Linux 는 WSAStartup 이 없어서 돌았다. 고친 것: `tests/benchmark/kiotty_bench_platform.cpp` (프로세스 시작 때 WSAStartup, POSIX 는 SIGPIPE 무시), `unusedPort` 의 시도를 `LEASED_PORT_COUNT` 번으로 묶고 넘으면 `kernelChosenPort`. 그 뒤 MSVC Release 의 benchmark 26 개가 모두 끝까지 돈다 (TLS 둘은 TLS OFF 빌드라 SKIP).

## 하지 않은 것

- HTTP 설계 11절이 적은 `NotifyLatency` benchmark 는 없다. notify 쪽은 `BroadcastFanOut` 으로 대신 비교했다.
- Windows Release 의 Server benchmark 가 멈추는 것 (위). B1 전부터 있던 일이라 이 사이클에서 고치지 않았다. 2026-11-12 에 고쳤다 (위).
- StreamClient 는 `send_timeout_milliseconds` 를 쓰지 않는다 (동기 write 가 호출마다 timeout 을 받는다). requirement 의 주석에 적었다.

## CI

`306e972` 을 push 했고 CI 14 job 이 모두 초록이다 (run 42965825792). macOS (kqueue), libkqueue, ubuntu 의 io_uring (`EventListener_Ring` 이 SKIP 이 아니라 Passed) 에서 gather 테스트가 돌았다.

## 다음

H1 (http.h 공개 API 판 v2). `keep_alive` 를 `KiottyNetBool` 로, `KiottyNetHttpLimits` 에 WebSocket 타이머 셋.

세션 끝 (2026-11-11) 의 상태: B1 완료. 남은 일은 두 갈래이고 사용자가 고른다.

1. Windows MSVC Release 의 Server benchmark 가 멈추는 원인 찾기 (B1 전부터. `build/msvc-bench`, EchoRoundtrip / WriteHeavy / BroadcastFanOut 이 90 초 안에 끝나지 않음, EventClientWriteHeavy 는 돈다. Debug 의 integration 테스트와 WSL Release 는 정상).
2. H1 (`design_http_v0.html` 11절).

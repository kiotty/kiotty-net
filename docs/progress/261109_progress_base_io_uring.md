# 2026-11-09 진행 정리 : io_uring backend (Linux)

기준 문서: `docs/requirement/requirement_v0.md` 3 (Linux 는 io_uring, 지원 안 하면 epoll), `docs/design/design_base_v5.html` 7.3, `docs/test/test_base_v0.html` 7절. 브랜치 `io-uring` 은 `kqueue-backend` 위에 있다.

## 코드

- `KIOTTY_NET_WITH_IO_URING` (CMake option, 기본 ON). Linux 이고 `linux/io_uring.h` 에 `IORING_ENTER_EXT_ARG` (5.11) 가 있을 때만 켜진다. Android 는 `CMAKE_SYSTEM_NAME` 이 Linux 가 아니라 빌드하지 않는다 (앱의 seccomp 가 막는다). 선택 매크로 `KIOTTY_NET_EVENT_URING` 하나가 늘었다 (DV10, MACRO 18).
- epoll 빌드 안의 실행 시간 선택이다. `open` 이 먼저 ring 을 열고, 안 되면 (`ENOSYS`, `EPERM`, 기능 부족) 같은 listener 가 epoll 로 연다. ring 의 조건: `FEAT_SINGLE_MMAP / NODROP / EXT_ARG`, probe 의 `RECV / SEND / POLL_ADD / ASYNC_CANCEL / READ`.
- liburing 없이 `syscall` 셋과 mmap (DV17, zero-dependency). 파일 셋: `kiotty_ring_event_open.cpp` (setup, mmap, probe, close), `_submit.cpp` (SQE, enter, cancel, forget), `_wait.cpp` (CQE 를 완료로). 상태는 `RingState` (epoll 의 `EventListenerState` 안).
- readiness 공용 파일은 거의 그대로다. epoll 파일의 `createHandles / releaseHandles / watch / unwatch / forget / poll` 이 ring 이 열려 있으면 ring 함수로 넘긴다 (`if constexpr`).
  - READ 는 RECV, WRITE 는 그 자리 `send` 뒤 남은 것만 SEND, ACCEPT 는 POLL_ADD (Acceptor 가 `accept`, epoll 과 같은 계약).
  - `-EAGAIN` 이면 POLL_ADD 로 기다렸다 다시 건다. 부분 SEND 는 남은 길이로 다시 건다.
  - detach 는 ASYNC_CANCEL 을 걸고 event 를 pending 으로 둔다 (ring 이 file 참조를 쥐어 fd 를 닫아도 취소되지 않는다). 그 완료가 CANCELLED 다.
  - ring 에 걸린 채 `IoEvent` 가 소멸하면 cancel 을 걸고 CQ 에서 그 완료의 `user_data` 를 무시 표시로 덮는다. `close` 는 `ASYNC_CANCEL_ANY` 뒤 완료를 거둔다.
  - wake 는 같은 eventfd. ring 은 그 eventfd 에 READ 를 건다. 막히기 전마다 READ 가 걸려 있는지 본다.
- 함께 고친 것 (전 backend): `EventListener::open / close / wake` 를 공통 파일 `kiotty_event_listener.cpp` 가 감싼다. `wake` 는 진행 중 카운터와 열린 표시 (atomic), `close` 는 카운터가 0 이 된 뒤 fd 를 닫는다.

## 고친 버그 둘

1. 같은 Server 를 다른 thread 로 다시 run 하면 멈췄다 (`ServerAwaitTimesOutBeforeRunThenReportsRunningAndClosed`). 첫 thread 가 끝날 때 커널이 그 thread 가 건 wake READ 를 취소하고, 두 번째 run 이 그 취소 완료를 받아 "안 걸림" 으로만 표시한 채 무한 대기에 들어갔다. 이제 막히기 전마다 다시 건다.
2. TSan 의 data race (`DestroyDoesNotBlockAWorkerThatCallsContextBeforeCloseStream`). worker 의 `closeStream` 이 stream 을 놓아 destroy 가 진행하는 사이 그 `closeStream` 의 `wake` 가 닫히는 eventfd 에 썼다. epoll 에도 있던 경쟁이고, fd 번호가 재사용되면 남의 fd 에 8 바이트를 쓸 수 있었다. 위의 공통 `wake / close` 로 막았다.

## 테스트

- 새 테스트 하나: `EventListener_Ring.OpenTakesAnIoUringInstanceWhenTheKernelAllowsIt`. `/proc/self/fd` 에서 `anon_inode:[io_uring]` 를 세어 fallback 이 조용히 epoll 로 가서 나머지 테스트가 ring 을 안 보는 일을 막는다. 커널이 막으면 skip. 전체 423 개. skip 의 조건은 backend 의 `open` 과 같은 `IORING_FEAT_SINGLE_MMAP | NODROP | EXT_ARG` 다 (2026-11-09 수정. setup 만 되는 커널 5.10 의 WSL2 에서 backend 는 epoll 로 가는데 테스트는 ring 을 기대해 실패했다).
- CI 에 `gcc epoll only (io_uring OFF)` job 을 더했다. 다른 Linux job 은 이제 io_uring 을 돈다.

| 환경 (WSL2, 커널 6.6) | 결과 |
|---|---|
| gcc io_uring | 423 / 423. integration 10 회 반복 통과 |
| clang ASan + UBSan io_uring | 423 / 423. 보고 5 건은 모두 gtest 안의 vptr (`-fno-rtti`), 우리 코드 0 |
| clang TSan io_uring | 423 / 423 을 3 회. 경고 0 (고치기 전 1 건, 위 2) |
| gcc epoll only | 422 / 422 |
| gcc libkqueue | 422 / 422 |
| clang TLS OFF | 통과 |
| MSVC TLS ON | 422 / 422 |

## 벤치마크 (WSL2, Release)

WSL2 는 맨 TCP loopback 도 약 200 µs 라 두 backend 의 차이가 잡음 안이다. CI ubuntu 수치로 다시 본다.

| 벤치 | epoll | io_uring |
|---|---|---|
| EchoRoundtrip clients 1, max 1024 | 210 µs | 211 µs |
| ReadHeavy 64 KiB, max 1024 | 1.21 GB/s | 1.17 GB/s |
| WriteHeavy | 0.91 GB/s | 1.17 GB/s |
| BroadcastFanOut 128 | 66.8 k/s | 71.0 k/s |
| ConnectionChurn | 1.78 k/s | 1.75 k/s |

### CI ubuntu, 같은 runner (run 42564248374)

benchmark job 이 이제 기본 (io_uring) 빌드와 epoll 전용 빌드를 같은 runner 에서 차례로 돌려 `benchmark.json`, `benchmark-epoll.json` 을 남긴다. runner 마다 CPU 가 달라 (3.69 GHz, 3.26 GHz) 다른 run 끼리는 비교할 수 없었다.

| 벤치 | epoll | io_uring | 차이 |
|---|---|---|---|
| EchoRoundtrip clients 1, max 1024 | 27.4 µs | 26.1 µs | +5% |
| EchoRoundtrip clients 128, max 1024 | 924 µs | 1096 µs | −19% |
| ReadHeavy 64 KiB, max 1024 | 4.72 GB/s | 4.56 GB/s | −3% |
| WriteHeavy | 4.85 GB/s | 4.78 GB/s | −1% |
| BroadcastFanOut 128 | 179 k/s | 181 k/s | +1% |
| ConnectionChurn | 15.5 k/s | 15.9 k/s | +2% |
| WorkHandoff | 19.9 k/s | 14.2 k/s | −29% |
| EventClientReadHeavy 16 KiB | 3.20 GB/s | 4.34 GB/s | +36% |

한 번의 측정이라 잡음이 크다. listener 를 쓰지 않는 StreamClient 벤치도 같은 run 에서 −5 ~ +27% 로 흔들렸다. 그 폭을 넘는 것은 WorkHandoff 의 −29% 와 128 client echo 의 −19% 다. 둘 다 io_uring 쪽이 느리다. WorkHandoff 는 worker 의 wake 가 eventfd 를 거쳐 ring 의 READ 완료로 오는 길이라 epoll 보다 한 단계 길다.

## 정할 것

1. 기본을 io_uring 으로 둘지. 지금 기본 ON 이고 커널이 막으면 epoll 이다. 같은 runner 측정에서 이득은 없고 WorkHandoff 와 다수 client echo 가 느리다. 고치는 후보는 wake 를 `IORING_OP_MSG_RING` 이나 multishot poll 로 바꾸기, `IORING_SETUP_DEFER_TASKRUN | SINGLE_ISSUER` (loop thread 가 open 하도록 바꿔야 한다), SQE 를 바퀴마다 한 번에 제출하기.
2. 버퍼 등록 (`IORING_REGISTER_BUFFERS`, fixed buffer), multishot recv, `SEND_ZC` 는 넣지 않았다. 이득은 큰 메시지와 연결 수가 많을 때인데 지금 목표 (기본 1024 연결) 에서는 측정이 먼저다.
3. `ringForget` 은 CQ 가 가득 차서 커널의 overflow 목록에 그 완료가 있으면 최대 5 초 기다린 뒤 포기한다. 포기하면 그 event 주소가 나중에 읽힌다. 실제로는 IoLoop 의 shutdown 이 event 를 소멸 전에 전부 settle 하므로 테스트의 지역 event 에서만 이 길을 탄다.

# 2026-11-02 진행 정리 : base R1, R2, 3c (plan_base_v1)

기준 문서: `docs/plan/plan_base_v1.md`, `docs/design/design_base_v4.md`, `docs/test/test_base_v0.html`.

## R1. base.h 와 statemachine.h 를 v4 3절로 (commit `09b7ab0`)

- action 함수 여섯을 `KiottyNet_thenHold / thenRead / thenReadUntil / thenWrite / thenWork / thenClose` 로 바꿨다.
- `KiottyNetWorkHandle`, `KiottyNet_finishWork`, `KiottyNet_isWorkConnected` 를 지우고, `KiottyNetConnectionStream` 과 Stream 동기 함수 다섯을 넣었다 (`read / write / writeFile / isOpen / close`).
- `onWorkStarted(ctx, stream)`, `onError(app, info, code, message)`, `KiottyNet_initializeContextWithTlsPool(bytes)`.
- C++ 의 `KiottyNet_Work` 는 `KiottyNet_ConnectionStream` 이 됐다. `close(then)` 가 `BUFFER_FULL` / `INVALID_ARGUMENT` 면 Stream 을 들고 있고, 그 외에는 비운다. 소멸자는 `close(thenClose(KIOTTY_SUCCESS))`.
- statemachine.h 의 WORK_STARTED event 는 `KiottyNetConnectionStream stream` 을 싣는다.
- 문서:
  - requirement 1 ~ 8절. 5절의 Work 단락은 Stream 모델로 다시 썼다 (Work 중 notify 는 쌓였다가 뒤에 나간다, 소유자 close 는 FIN 만 보내고 run 은 worker 의 close 를 기다린다, max_clients 128 / 1024).
  - user_guide_http 1.5.
  - implementation_guide 1.5 ~ 1.6 은 v4 를 가리키는 문단으로 바꿨다.
- http.h 의 HttpWork 는 base 의 타입을 쓰지 않는 별도 API 라 그대로 두었다.

## R2. event 의 세 줄 (commit `fa34629`)

- `IoAction::as.write.length`, `IoAction::write(purpose, timeout, length)`. `from()` 이 공개 WRITE 의 span 길이를 채운다.
- `IoKind::WORK_READY`, `IoCompletion::workReady(slot)`, `IoCompletion::workDone(slot, outcome)`.
- 테스트:
  - `ReadAndWriteOnSeparateEventsCompleteIndependently` (옛 TwoSends).
  - `WriteLargerThanSocketBufferCompletesOnceWithFullLength` (SO_SNDBUF 4 KiB, 1 MiB, 느린 peer).
  - Detach 테스트의 둘째 event 를 write event 로 고쳤다.
  - IoAction 에 write 필드와 local completion 테스트 둘을 더했다.
- 테스트 계획 7절의 세 이름을 고치고, 설계 v2 머리에 "v4 로 대체됨" 을 적었다.

## 3c. epoll backend

- `backend/kiotty_epoll_event_listener.cpp`. 세부는 설계 v4 7절 끝에 적었다:
  - 복제 fd + ONESHOT.
  - `data.ptr` 은 submit 마다 다시 넣는다.
  - WRITE 는 끝까지 보낸 뒤에만 완료.
  - ready 목록 (intrusive list) 에 즉시 완료 / 즉시 실패 / CANCELLED.
  - eventfd wake.
  - detach 는 DEL 뒤 close.
- CMake: Linux 와 Android 에서 `KIOTTY_NET_HAS_EVENT_BACKEND` 를 켠다. macOS / iOS 는 kqueue 전까지 꺼져 있다.
- 공통 테스트 하나를 바꿨다. `ImmediateFailureIsDeliveredAsAnErrorCompletionNotAReturnCode` 는 "socket close 뒤 submit" 에서 "shutdown(양방향) 뒤 submit" 으로 바뀌었다. 복제 fd 에서는 원래 socket 을 닫아도 연결이 살아 있기 때문이다. 정상 경로는 detach 가 close 보다 먼저라 이 차이가 드러나지 않는다. support 에 `shutdownBoth` 를 더했다.
- 로컬: WSL gcc / clang / ASan+UBSan / TSan 128/128, clang Release 127/127. EventListener 는 ASan, TSan, gcc 에서 각 10 회 반복해도 초록이다. MSVC Debug 127/127 (EventListener 5 회 반복).
- 플랫폼 분기가 있는 테스트는 `EventListener_Iocp` 하나와 `EventListener_Epoll` 하나뿐이다.

## 미결

- R3 (테스트 계획 9 ~ 10절을 v4 로) 는 따로 진행 중이다.
- macOS 의 event backend (kqueue) 는 base 완성 뒤에 만든다. 그때까지 macOS CI 는 unit 과 socket integration 만 돈다.

## 다음 할 일

R3 마무리 (commit `4f621a5`) → 사이클 4 (tls, stub 먼저).

## 추가 (같은 날)

- 테스트 계획 11.1 "push 의 주기와 지연" 을 더했다. 파라미터 군 `PushCadence` 는 Server notifyConnection, Server broadcast, EventClient notify 셋 × TCP (no_delay) / PIPE 이고, 케이스는 넷이다.
  - 10 ms 간격으로 보내면 10 ms 간격으로 도착한다.
  - 지연 p99 는 TCP 1 ms 이하, PIPE 200 µs 이하.
  - 사용자 WRITE 중에도 주기를 지킨다.
  - broadcast 32 클라이언트.
- 12절 벤치마크 `PushCadence10ms` 를 더했다.
- OpenSSL 은 시스템 설치 대신 `openssl-cmake` v1.1.0 의 prebuilt static 라이브러리를 FetchContent 로 받는다. option 은 `KIOTTY_NET_USE_TLS` (OpenSSL 사용 여부, OFF 면 stub) 와 `KIOTTY_NET_OPENSSL_PREBUILT` (OFF 면 소스 빌드). WSL 의 `libssl-dev` 는 필요 없다.
- 로컬 TLS ON 빌드: MSVC Debug (windows-x64-msvc-debug) 127/127, WSL gcc / clang (linux-x64-release) 128/128. 아직 OpenSSL 을 부르는 코드가 없어서 링크 검증은 사이클 4 에서 끝난다.

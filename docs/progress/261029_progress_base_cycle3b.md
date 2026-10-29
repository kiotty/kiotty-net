# 2026-10-29 진행 정리 : base 사이클 3b (event, IOCP)

기준 문서: `docs/design/design_base_v2.md` 5, 7절, `docs/test/test_base_v0.html` 7절, `docs/plan/plan_base_v0.md` 5절 3b.

## 결정 (테스트 v0 14절 5 확정)

- EventListener 의 오용은 전부 코드로 거부한다. pending 인 event → `SocketCode::BUSY` (새 값, `toNetCode` 는 KIOTTY_BUSY). kind 가 READ / READ_UNTIL / WRITE / ACCEPT 가 아님, payload 가 비었음 → INVALID_ARGUMENT.
- attach 없는 submit 은 인터페이스로 없앴다. `attach(socket, slot)` 가 `Result<SocketCode, Holder<IoEvent>>` 를 돌려주고, event 가 socket / slot / listener 를 기억한다. `submit(event, action, payload)` 에 socket 인자가 없다. `detach(event)`.
- Connection 은 attach 를 두 번 (stream, notify), Acceptor 는 한 번 부른다. IOCP 는 socket 을 port 에 한 번만 붙일 수 있어서 두 번째 attach 의 ERROR_INVALID_PARAMETER 를 "이미 붙음" 으로 받는다 (실험으로 확인: 같은 port 든 다른 port 든 87, 잘못된 handle 은 6). epoll (3c) 은 event 마다 dup fd 로 따로 등록한다.
- IoSource 에 `attach(EventListener&)` / `detach(EventListener&)` 를 더했다. IoLoop 의 admit / submit(CLOSE) 는 이것을 부른다. Acceptor 의 `_next` + `_addresses` 는 `AcceptBuffer _buffer` 하나로.
- 설계 v2 7절, 9.1, 9.8, 10, 11절과 테스트 v0 7절 / 14절에 반영했다.

## 만든 것

- `base/event/kiotty_io_action.h/.cpp`: IoActionKind, IoPurpose, IoAction (factory 아홉, `readUntil` 을 더했다). delimiter 가 1 ~ 4 바이트가 아니면 length 0 으로 둔다 (거부는 사이클 5a 의 Connection).
- `base/event/kiotty_event.h`: IoPayload, IoKind, IoCompletion (`wake` 를 더했다), IoEvent base (pending, action).
- `base/event/kiotty_event_listener.h`: AcceptBuffer, EventListener (`alignas(16) unsigned char[256]` storage. MSVC 는 `aligned_storage<.., 16>` 를 막는다).
- `base/event/backend/kiotty_iocp_event_listener.cpp`: GetQueuedCompletionStatusEx, 즉시 실패와 wake 는 PostQueuedCompletionStatus, detach 는 CancelIoEx, 완료 상태는 NTSTATUS → RtlNtStatusToDosError → socketErrorTable. CMake 에 ntdll 링크, `KIOTTY_NET_HAS_EVENT_BACKEND` (지금은 WIN32 만 ON).
- Windows 오류표에 WSA_INVALID_HANDLE → INVALID_ARGUMENT.

## 테스트

- unit: IoAction 4 (`static_assert(sizeof(IoAction) < 64)` 포함). SocketError 의 toNetCode 테스트에 BUSY.
- integration (`KIOTTY_NET_HAS_EVENT_BACKEND` 일 때만): IoEvent 1, EventListener 22, EventListener_Iocp 1. EventListener_Epoll 1 은 `__linux__` 에서만 (3c).
  - 테스트 v0 대비: `FreshEventIsNotPending` → `EventFromAttachIsNotPending`, `SubmitOnUnattachedSocketIsRejected` → `SubmitOnEmptyOrDetachedEventIsRejectedWithInvalidArgument`, `AttachOnInvalidSocketIsRejected` 추가.
  - support: `LoopbackPeer::reset()` (SO_LINGER 0), `setSendBuffer()`.
- Windows 는 보내지 않는 peer 에게도 64 MiB overlapped send 를 바로 완료한다. "write 가 걸린 채 detach" 테스트는 SO_SNDBUF 0 으로 걸어 둔다.
- 로컬: MSVC Debug 125/125 (EventListener 5 회 반복 포함), WSL gcc 102/102, clang 102/102 (event backend 없음).

## 미결

- IoLoop::admit 의 attach 실패 처리 (onError + CLOSE) 는 사이클 6.
- listener close 뒤에 pending 인 IOCP event 를 없애면 커널이 OVERLAPPED 에 늦게 쓸 수 있다. 정상 경로 (IoLoop::shutdown 이 settle 로 비운 뒤 close) 에서는 생기지 않는다. 테스트는 socket 을 닫은 뒤 event 를 없앤다.

## 다음 할 일

CI 초록 확인. 그 뒤 사이클 3c: epoll backend 가 같은 EventListener 테스트를 통과하게 (dup fd, EPOLLONESHOT, eventfd wake, detach 의 CANCELLED).

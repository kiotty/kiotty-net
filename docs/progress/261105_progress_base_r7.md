# 2026-11-05 진행 정리 : R7 (IoEvent 구체 타입)

기준 문서: `docs/plan/plan_base_v2.md` R7, `docs/design/design_base_v4.md` 7절, 9.1.

## 코드

- `IoEvent` 는 backend 의 구체 struct 다. `kiotty_event_listener.h` 가 OS 매크로로 하나를 고른다.
  - `base/event/backend/kiotty_iocp_event.h`: `IocpRequest { OVERLAPPED, owner }`, listener 포인터와 generation, SOCKET, slot, failure, AcceptEx 포인터. winsock2 / mswsock 을 이 헤더가 넣는다 (`WIN32_LEAN_AND_MEAN`, `NOMINMAX`).
  - `base/event/backend/kiotty_epoll_event.h`: 복제 fd, registered / queued, result, payload, 보낸 길이, ready 목록의 next. 소멸자는 epoll backend 의 .cpp 에 있다 (attach 된 채 지워지면 ready 목록에서 빼고 DEL, close).
  - `base/event/backend/kiotty_no_event.h`: 그 밖의 OS. pending 과 action 만.
  - 멤버는 전부 public 이다 (MISRA C++:2023 Rule 14.1.1). 기본 생성자, 복사 / 이동 없음.
  - `kiotty_event.h` 의 추상 `IoEvent` (HOLDER_SIZE, protected 멤버) 는 지웠다. `pending()` / `action()` 은 필드 `pending` / `action` 이 됐다.
- `EventListener`:
  - `SocketCode attach(IoEvent& event, SocketHandle socket, int32_t slot)` 이 제자리에서 채운다. CLOSED / INVALID_ARGUMENT (무효 socket) / BUSY (pending, CANCELLED 완료가 아직 큐에 있는 것 포함) / INVALID_ARGUMENT (이미 attach 됨).
  - `submit(IoEvent&, ...)`, `detach(IoEvent&)`. detach 뒤 같은 event 를 다시 attach 할 수 있다. 같은 listener 객체의 지난 generation (close 뒤) 의 event 도 detach 가 비운다 (epoll 은 복제 fd 를 닫는다).
  - listener 상태 struct 는 `EventListenerState` 로 net namespace 에 앞으로 선언되고 각 backend .cpp 가 정의한다. EventListener 의 `_storage` 는 그대로다.
- `core/kiotty_holder.h`, `tests/unit/kiotty_test_holder.cpp` 를 지웠다. `AttachResult` 도 없다.

## 테스트

- `kiotty_test_event_listener.cpp`: `IoEvent event;` 를 fixture 멤버와 지역으로. 새 케이스 넷:
  - `IoEvent.DefaultConstructedEventIsNotPendingAndHasNoAction`
  - `EventListener.AttachOnAnEventThatIsAlreadyAttachedIsRejected`
  - `EventListener.AttachWhileACancelledCompletionIsStillQueuedIsBusy`
  - `EventListener.DetachClearsTheEventSoItCanBeAttachedAgain`
- Holder 테스트 12 개가 빠지고 넷이 더해졌다. 테스트 계획 7절을 맞췄다.

## 로컬

- MSVC Debug: TLS ON 196/196, OFF 194/194. EventListener / IoEvent 28 개 5 회 반복 초록.
- WSL TLS ON 197/197 (gcc, clang, clang ASan+UBSan, clang TSan), TLS OFF 195/195 (gcc, clang), clang Release TLS 196/196. epoll 28 개를 gcc / ASan / TSan 에서 각 10 회 반복 초록.

## 남은 것

- epoll 의 IoEvent 소멸자는 listener 를 본다. 설계 19.3 의 Server 는 `_host` (Connection 들) 가 `_listener` 보다 먼저 선언되어 listener 가 먼저 지워진다. Owner 의 reset 이 모든 event 를 detach 한 뒤 지워지는 순서를 사이클 5 / 7 에서 지킨다 (설계 7절에 적었다).

## 다음 할 일

R8. TlsMemory (GlobalPool 전역 arena + Owner 의 MemberPool 서브풀, Scope, 등록표). R8 끝에 push.

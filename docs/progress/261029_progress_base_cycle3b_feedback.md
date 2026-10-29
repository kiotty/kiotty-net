# 2026-10-29 사이클 3b 피드백

사이클 3b (event, IOCP, 커밋 `e195342`) 를 끝낸 뒤의 질문과 답, 결정을 적는다. 이 문서의 결정은 다음 작업에서 `docs/design/design_base_v2.md` 에 반영한다.

## 질문 1. slot 은 왜 필요하고 어디에 저장되나

> slot은 왜 필요한거야? slot은 어디에 저장되니? 만약, slot이 Overlapped 객체라던가 epoll 객체의 index를 나타내는 거라면 EventListener에서 비어있는 index를 내주면 되지 않을까?

답

- slot 은 OVERLAPPED 나 epoll 의 index 가 아니다. **ConnectionHost 의 Connection 배열에서 몇 번째 칸인지**를 가리키는 번호다.
- 저장: attach 때 받은 slot 을 IoEvent 가 들고 있다가 (`kiotty_iocp_event_listener.cpp` 의 `IocpEvent::slot`) 완료가 나올 때 `IoCompletion.slot` 에 복사한다.
- 쓰임
  - IoLoop::step 이 `_host.at(completion.slot)` 로 완료를 처리할 Connection 을 찾는다 (설계 v2 10절).
  - collect 가 직접 만드는 TIMEOUT / WORK_DONE 완료에도 같은 번호가 들어간다.
  - 사용자에게 주는 handle `{ generation, index }` 의 index 도 이 번호에서 나온다 (`CONNECTION_SLOT_BITS` 16).
- EventListener 가 빈 index 를 나눠 주면 "listener index → Connection" 대응표가 하나 더 필요하다. 번호의 주인은 handle 을 발급하는 ConnectionHost 이고, listener 는 그 번호를 받아 완료에 실어 주기만 한다. 지금 구조를 유지한다.

결정: 유지.

## 질문 2. 같은 socket 의 두 번째 attach 와 동시 Write

> 같은 Socket의 두번째 attach는 이미 붙어있음으로 처리한다면 NotificationEvent와 StreamEvent의 Write를 동시에 할 수 없다는 것 같은데,,? 이건 어떻게 해야함? 애초에 IoEvent를 2개 둔다는 아이디어를 잘 못 생각한건가?

답

- "이미 붙어 있음" 은 socket 과 completion port 의 연결이 이미 있다는 뜻일 뿐이다. 요청은 event 마다 자기 OVERLAPPED 로 따로 걸리므로 같은 socket 에 동시에 여러 개가 걸릴 수 있다.
  - `TwoSendsSubmittedInOrderArriveAtPeerInThatOrder`: stream WRITE 와 notify WRITE 를 동시에 걸고 둘 다 완료된다.
  - `DetachWithPendingReadAndWriteDeliversTwoCancelled`: read 와 write 를 동시에 걸어 둔 상태에서 통과했다.
- 그런데 이 질문을 보다가 **설계 v2 의 진짜 문제**를 찾았다. 메시지 경계가 섞일 수 있다.
  - USER 송신이 일부만 나가면 Connection 이 나머지를 다시 거는데 (9.4 onSent 의 "남은 조각"), 그 사이에 NOTIFY 송신이 끼어들 수 있다. 그러면 peer 쪽에서는 메시지 앞부분, notify, 메시지 뒷부분 순서로 받게 된다.
  - TLS 에서는 레코드가 깨지고, WebSocket 에서는 frame 이 깨진다.
  - IOCP 는 오류가 없으면 송신을 거의 통째로 완료해서 잘 드러나지 않지만, epoll 은 일부만 보내는 일이 흔해서 3c 에서 바로 터진다.
  - 9.6 의 "같은 socket 의 send 둘은 건 순서대로 나간다" 는 바이트 순서만 보장할 뿐 메시지 단위 순서는 보장하지 않는다.
- 그래서 event 를 둘 두는 생각 자체는 맞지만, **나누는 기준을 용도 (stream / notify) 에서 방향 (read / write) 으로 바꿔야 한다.**

| | 설계 v2 | 바꿀 모양 |
|---|---|---|
| event 둘 | stream (read 또는 write) + notify (write) | `_read_event` + `_write_event` |
| 같은 socket 에 동시에 걸리는 write | 최대 2 (섞일 수 있음) | 항상 1 |
| write 완료 | 일부만 완료될 수 있음 (Connection 이 남은 조각을 다시 건다) | payload 를 다 보내야 완료. epoll 은 listener 가 EPOLLOUT 을 받아 끝까지 보낸다 |
| USER / HANDSHAKE / CONTROL / NOTIFY 의 순서 | 각자 따로 | write lane 하나에서 메시지 단위로 번갈아 보낸다 |

- 좋아지는 점
  - 송신이 섞이는 일이 구조상 없어진다.
  - Connection 의 "남은 조각" 경로가 사라진다.
  - kqueue 의 `EVFILT_READ` / `EVFILT_WRITE`, epoll 의 EPOLLIN / EPOLLOUT 과 그대로 대응한다.
  - read 를 걸어 둔 채 push 하는 것은 그대로 된다.
- 바뀌는 범위
  - 설계 9절: Connection 이 `_read_event` / `_write_event` 를 들고, onSent 와 onNotified 가 write lane 하나를 나눠 쓴다.
  - 설계 11절: TIMEOUT 뒤의 BUSY 규칙을 "read 가 걸려 있을 때의 WRITE" 기준으로 다시 정한다.
  - 3b 코드: listener 는 거의 그대로다. 계약 한 줄 ("WRITE 완료의 length 는 payload 전체") 을 더하고, `WriteLargerThanSocketBufferCompletesPossiblyPartiallyAndTheRestIsResubmittable` 을 그 계약에 맞게 바꾼다. `TwoSendsSubmittedInOrder...` 와 `DetachWithPendingReadAndWrite...` 는 read event + write event 모양으로 다시 쓴다.

결정 (사용자): **read_event 와 write_event 로 간다. `design_base_v2.md` 를 수정한다.**

### 이어진 질문. Notification Stage 와 Tx Stage 는 따로 둬도 되나

> 근데 Notification Stage와 Tx Stage는 따로 둬도 되는거지?

답: 된다. 평문 Stage 는 따로 두는 것이 맞다.

- 둘은 쓰는 쪽이 다르다. notify Stage 는 아무 thread 에서 `NotificationContext::mutex` 를 잡고 쌓는다. send (Tx) Stage 는 IO Thread 만 쓰고, USER / HANDSHAKE / CONTROL 의 순서가 있는 한 줄이다. 합치면 IO Thread 의 send 경로까지 mutex 를 잡아야 하고 순서 규칙도 섞인다.
- 바뀌는 것은 "누가 socket 에 내보내나" 뿐이다. write lane 하나가 submit 할 때마다 두 Stage 중 하나를 골라 그 시점의 readable 전체를 한 번에 보낸다. 고르는 단위가 곧 메시지 경계다.
- **주의할 점 (TLS)**: TLS 레코드의 순서 번호는 암호화하는 순간에 정해진다. 그래서 암호화한 순서와 보내는 순서가 같아야 한다.
  - 설계 v2 처럼 USER 는 stageWrite 에서, NOTIFY 는 onNotified 에서 각자 미리 암호화해 cipher send / cipher notify 에 쌓아 두면, write lane 이 그 순서를 지켜 보낸다는 보장이 없다.
  - 그래서 암호화는 **write lane 이 다음에 보낼 평문 Stage 를 고른 그 순간에** 한다. 평문은 둘, 암호문은 write lane 하나 (cipher tx 하나) 로 둔다. 암호화 순서가 곧 송신 순서가 된다.
  - 이것은 설계 9.6 의 "암호화는 IO Thread 만, 레코드 순서 = submit 순서" 를 구조로 지키는 방법이기도 하다.
- 고르는 순서 (send Stage 를 먼저 볼지, notify 를 먼저 볼지, 번갈아 볼지) 는 설계 수정 때 정한다. 설계 v2 의 "broadcast 우선" 원칙과 USER WRITE 의 timeout 이 notify 뒤에서 기다리는 시간을 함께 본다.

결정: 평문 send Stage 와 notify Stage 는 따로 둔다. 암호문 쪽과 고르는 순서는 설계 수정에서 확정한다.

## 질문 3. 최대 32 connection 이 너무 작다

> 최대 32개 connection 지원이 너무 작다는 의견이 있어. browser로 같은 서버에 대한 거 몇 개만 띄워도 금방 찬다고 하더라고.. 현재 design_base_v2.md를 바탕으로 필요한 메모리는 객체가 생성될 때 모두 할당한다는 대전제를 만족하는 하에 개선방법이 있을까?

답

- 32 는 고정 한도가 아니라 `max_clients` 의 기본값이다. handle 의 slot 은 16 bit 라 65535 까지 들어간다. 문제는 기본값을 올렸을 때의 메모리와 한 바퀴의 비용이다.
- "생성할 때 전부 할당" 을 지키면서 할 수 있는 방법 (효과 순)
  1. **기본값 상향 + requirement 수정** (바로 가능)
     - 설계 v2 16절 수치로 128 개면 평문 약 1.5 MiB, TLS 약 5.4 MiB + SSL.
     - requirement 29번 줄의 "최대 32 Client 까지 지원하는 것으로 가정" 을 바꿔야 한다.
     - Context 의 TLS Pool (고정 2 MiB) 을 `max_clients` 에 비례하게 계산해야 한다. 아니면 OpenSSL 할당이 heap fallback 으로 넘어간다.
  2. **브라우저가 slot 을 붙잡는 문제를 정책으로 막기** (메모리 0)
     - 브라우저는 host 당 6 개쯤 keep-alive 연결을 열어 두고 놀린다. 설계 v2 는 slot 이 차면 accept 를 멈추므로 backlog 의 새 연결이 그냥 기다린다.
     - (a) 쉬는 연결의 HOLD timeout 기본값, (b) 가득 찼을 때 가장 오래 쉰 연결을 닫고 새 연결 받기. slot 수를 늘리는 것보다 효과가 크다.
  3. **한 바퀴의 비용을 걸린 연결 수만큼으로** (생성 시점에 고정 배열)
     - collect / broadcast / settle 은 매 바퀴 slot 전부를 돈다. notify 된 slot, work 가 끝난 slot 을 담는 크기 고정 ring 과 deadline 용 min-heap 배열을 생성 시점에 잡아 둔다.
     - 수천 개 단위에서 의미가 있다.
  4. **slot 과 buffer 분리** (가장 크게 줄이지만 큰 재설계)
     - Connection 객체 (1 KiB 안팎) 는 `max_clients` 개, Stage buffer 묶음은 그보다 적은 `max_active` 개를 생성 시점에 잡는다. 데이터가 올 때만 buffer 를 빌린다.
     - IOCP 는 0 바이트 WSARecv 로 "읽을 수 있음" 만 기다린 뒤 빌려서 읽는다. epoll 은 원래 그 방식이다.
     - 쉬는 keep-alive 연결이 buffer 를 쥐지 않는다. 대신 proactor 흉내의 read 경로와 TLS 의 미완성 레코드 처리가 바뀌므로 base v0 범위 밖으로 둔다.
- 추천: 이번 base 에서 1 과 2 (쉬는 연결의 timeout 기본값까지). 3 과 4 는 base 를 마친 뒤 벤치마크 수치를 보고 정한다.

결정: **미정.** 정해야 할 것은 아래 둘이다.

- `max_clients` 기본값 (예: 128) 과 requirement 29번 줄의 수정.
- 가득 찼을 때 가장 오래 쉰 연결을 닫는 정책을 넣을지, HOLD timeout 기본값을 둘지.

## 다음 할 일

1. 설계 v2 수정: 7절 (WRITE 완료는 payload 전체), 9절 (`_read_event` / `_write_event`, write lane, 암호화 시점), 9.6, 10절, 11절의 BUSY 규칙, 16절 메모리.
2. 3b 코드와 테스트를 새 계약에 맞춘다 (listener 의 WRITE 계약, event listener 테스트 세 개).
3. 질문 3 의 결정 뒤 requirement 와 config 기본값을 고친다.
4. 그 다음 사이클 3c (epoll).

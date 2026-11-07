# 2026-11-07 진행 정리 : 5b (Connection TLS 경로, Acceptor)

기준 문서: `docs/plan/plan_base_v2.md` 5b, `docs/design/design_base_v5.html` 8, 9절 (9.9 를 이 사이클의 코드로 고쳤다), `docs/test/test_base_v0.html` 9절 TLS / Acceptor.

## 코드

- `data/repository/connection/kiotty_acceptor.h/.cpp`: `IoSource` 를 구현한다. public 은 `open(endpoint, backlog, NoDelay) / close / accept / listening / isPending / port` 여섯과 IoSource 일곱 (DV9). slot 은 -1, event 는 하나다. `resolve(ACCEPT)` 는 `AcceptBuffer` 전체이고, 준비한 socket 이 없으면 그 자리에서 `PassiveSocket::prepare` 한다. `shutdown` 은 `close` (listening socket 을 닫는다).
- `data/infra/socket/kiotty_passive_socket.h`: `prepare()` 와 `accept(ActiveSocket&& prepared)` 를 더했다 (public 7, overload 는 하나로 센다. DV11). Windows: `prepare` 는 같은 transport 의 socket 을 열고, `accept(prepared)` 는 `SO_UPDATE_ACCEPT_CONTEXT` 뒤 `getpeername` 으로 peer 를 채운다. POSIX: `prepare` 는 빈 ActiveSocket, `accept(prepared)` 는 그것을 닫고 `accept()`. AcceptEx 의 OS 차이가 infra 안에서 끝나므로 Acceptor (repository) 에는 `#if` 가 없다.
- Connection 의 TLS 분기 (onOpened 의 deadline + onHandshake, `ReceiveFlow::refill` 의 decode, `SendFlow::select` 의 cipherSend 우선과 `encoded`, `SendFlow::release` 의 cipherSend commit, `ReceiveFlow::stageFor`) 는 5a 와 리팩터링 6단계에서 이미 썼다. 이 사이클의 테스트가 처음 지났고 고칠 곳은 없었다.
- "handshake DONE 뒤의 decode 한 번" 은 따로 두지 않았다. DONE 은 onReady 로 가고 그 action 이 READ 면 `ReceiveFlow::drive` 의 refill 이 decode 한다 (`ApplicationDataArrivingWithFinishedIsDeliveredByRefillAtOnReady`). onReady 가 HOLD 면 SSL 안에 남은 것은 다음 READ 의 첫 refill 이 꺼낸다.

## 테스트

| 파일 | 수 |
|---|---|
| `tests/unit/kiotty_test_connection_tls.cpp` | TEST_P 23 × {TLS 1.2, 1.3} = 46 (1.3 전용 넷은 1.2 에서 skip) |
| `tests/integration/kiotty_test_acceptor.cpp` | 4 (`UnopenedAcceptorIsNotListening` 은 backend 없는 OS 에서도) |

`TlsLink` (테스트 파일 안): `ConnectionBench<1>` 의 Connection 과 따로 연 상대 `TlsEngine`. `fromPeer(read)` 는 상대의 cipherSend 를 `resolve(read)` 자리에 옮겨 RECEIVE 를 dispatch, `toPeer(write)` 는 `resolve(write)` 를 상대의 cipherReceive 에 넣고 SEND 를 dispatch, `handshake` 는 둘을 번갈아 DONE 까지. listener 는 쓰지 않으므로 write event 는 늘 비어 있고, 테스트는 WRITE 마다 완료를 먼저 돌려준 뒤 다음 pick 을 본다.

테스트 계획과 다른 것:

- `Tls13TicketGoesOutAsOneMoreHandshakeSendBeforeOnReady` → `Tls13HandshakeIsDoneRightAfterClientFinishedWithoutATicket`. R8 의 `SSL_CTX_set_num_tickets(0)` 로 ticket 이 없다.
- `PickWriteDuringHandshakingOffersOnlyCipherSend` 에 5a 에서 넘긴 `NotifyDuringHandshakingIsAccepted` 를 합쳤다 (`PickDuringHandshakingOffersOnlyCipherSendAndNotifyIsAccepted`).
- `PickWritePutsCipherSendBeforeNotifyAndSend` 의 READY 중 cipherSend 잔여는 상대의 KeyUpdate 로 만든다. 그래서 1.3 전용이다.
- `ReleaseThenOpenReusesTheEngineWithoutPoolFallback` → `...AndHandshakesAgain`. Pool fallback 은 R8 에서 없어졌다. 같은 slot, 다른 generation, 두 번째 onReady 를 본다.
- `debugKeyUpdate / debugShutdown` 은 `TlsPipe::requestKeyUpdate / sendCloseNotify` (`KIOTTY_NET_TESTING`).
- 1.3 전용 넷: no ticket, cipherSend 우선 (KeyUpdate), Finished 와 같이 온 응용 데이터 (1.2 에서는 클라이언트가 마지막 flight 를 보내지 않는다), KeyUpdate.

## 로컬

- MSVC Debug TLS ON / OFF: 357 / 357.
- WSL: gcc / clang × TLS ON / OFF, clang ASan+UBSan, TSan 358 / 358. clang Release TLS 357 / 357 (Debug 전용 death test 하나 빠짐). sanitizer 보고 0.
- `tools/check_rules.py src/implementation`: MACRO (DV10) 말고 결과 없음.

## 다음 할 일

6 (io_loop). `domain/usecases/io_loop/` 에 `LoopParts` 와 `StopPolicy` (설계 v5 10절). settle 이 쓸 "반납해도 되나" 의 자리와 host 의 `count` 노출을 그때 정한다. Acceptor 의 submit 이 `INVALID_ARGUMENT` 를 돌려줄 때 (Windows 에서 prepare 실패) IoLoop 이 어떻게 하는지도 정한다.

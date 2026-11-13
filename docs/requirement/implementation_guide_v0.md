# Kiotty-Net 구현 가이드 v0

[requirement_v0.md](requirement_v0.md) 는 외부 인터페이스와 규칙만 담는다. 이 문서는 그 인터페이스를 어떻게 구현하는지의 예시와 설계 근거다.
두 문서가 다르면 requirement_v0.md 가 우선한다. 여기의 코드는 의사코드가 아니라 컴파일되는 형태로 쓰되, 소켓과 IO loop 같은 OS 의존 부분은 호출 이름만 적는다.

목차

1. Stage 와 수신 / 송신 흐름 (ReadRequest, Connection::read / readUntil, IOThread::pump, Work, write lane / push lane / drain, broadcast)
2. StateMachine (Handler, Connection 이 기억하는 것, 표 찾기와 검사, dispatch, admit 과 CLOSED, HTTP / WebSocket 이 얹히는 방법)
3. 예제 : Echo (프로토콜, EchoServer, EchoClient 두 가지, C 판)
4. HttpServer : Connection 별 Request / Response 공급 (HttpSession, 줄 단위 헤더 읽기, 응답 버퍼 배치, handle, Work, 조각 송신과 파일, run, Work 스트림, content type)
5. WebSocket : HttpSession 위의 frame 행 (Session 에 더한 것, handle, Upgrade, frame 읽기, 타이머, 송신의 두 길, close 의 갈래, 클라이언트, 다른 thread)
6. 미결

---

## 1. Stage 와 수신 / 송신 흐름

requirement_v0.md 4절의 `Stage` 와 5절의 `Connection::read / readUntil` 이 IO Thread 에서 어떻게 맞물리는지다. 송신 (write lane, push lane, drain, broadcast) 은 1.6 이다.
핵심은 세 가지다.

- IO Thread 는 read 요청이 있든 없든 계속 recv 해서 수신 Stage 에 쌓는다. `Connection::read*` 는 "이 조건이 되면 onRead 한 번" 이라는 **조건 등록**일 뿐이다.
- 한 번의 recv 로 요청 여러 개가 연달아 충족될 수 있다. 콜백이 return 하면서 등록한 다음 요청을 같은 데이터로 바로 다시 검사한다.
- IOThread 가 아는 것은 `ConnectionHandler` 뿐이다. 그 위에 StateMachine 이 있는지는 모른다.

### 1.1 ReadRequest

```cpp
// Connection 이 들고 있는 "마지막 read 요청". Connection::read / Connection::readUntil 이 등록한다.
// 한 Connection 은 read 요청을 동시에 하나만 가진다. 요청 하나가 충족되면 onRead 한 번이고, 그 즉시 NONE 으로 돌아간다.
struct ReadRequest {
    enum Kind { NONE, EXACT, UNTIL };
    Kind     kind;          // NONE  : read 요청 없음 (write 중, Work 중, close 중). 들어온 데이터는 Stage 에 쌓아만 둔다
    size_t   length;        // EXACT : 정확히 받을 길이.  UNTIL : 최대 길이. 이 안에 delimiter 가 없으면 CLOSED(PROTOCOL_ERROR)
    char     delim[4];      // UNTIL : delimiter 바이트. 고정 4 바이트로 Connection 안에 복사 (호출자 버퍼에 대한 dangling 없음)
    uint8_t  delim_length;  // UNTIL : 1..4
    size_t   scanned;       // UNTIL : 이미 delimiter 를 찾아본 consumer offset. recv 마다 처음부터 다시 scan 하지 않기 위함
};
```

### 1.2 Connection::read / readUntil

```cpp
// 등록. 요구 길이(exact / max)가 수신 Stage capacity 보다 크면 등록 시점에 close(REQUEST_TOO_LARGE).
// 등록만 하고 끝나지 않는다. 이미 Stage 에 쌓여 있는 데이터로 충족될 수 있으므로 콜백 전달이 끝나면 IOThread 가 pump() 를 한 번 돈다 (1.3).
void Connection::read(size_t exact)
{
    if (exact > _recv_stage.capacity()) { close(REQUEST_TOO_LARGE); return; }
    _read_request.kind   = ReadRequest::EXACT;
    _read_request.length = exact;
}

void Connection::readUntil(Span delim, size_t max)
{
    if (max > _recv_stage.capacity() || delim.length == 0 || delim.length > 4) { close(REQUEST_TOO_LARGE); return; }
    _read_request.kind         = ReadRequest::UNTIL;
    _read_request.length       = max;
    memcpy(_read_request.delim, delim.data, delim.length);
    _read_request.delim_length = (uint8_t)delim.length;
    _read_request.scanned      = 0;
}
```

### 1.3 IOThread::onRecvComplete / pump

```cpp
// Read 완료 루틴.
//   IOCP / io_uring : 완료 통지에 read_length 가 실려 온다.
//   epoll / kqueue  : readable 통지를 받으면 recv() 를 호출하고, 그 반환값을 같은 함수에 넘긴다.
void IOThread::onRecvComplete(Connection* connection, int32_t read_length)
{
    connection->_recv_pending = false;
    if (read_length <= 0) {                                    // 0: peer 가 FIN, <0: 소켓 에러
        close(connection, read_length == 0 ? PEER_CLOSED : SOCKET_ERROR);   // WORKING 이면 미뤄 둔다 (1.5). 아니면 onDisconnected 한 번
        return;
    }
    connection->_recv_stage.provider().commit(read_length);
    pump(connection);
}

// 쌓인 데이터로 충족되는 read 요청을 전부 처리하고, 필요하면 recv 를 다시 건다.
// recv 완료 뒤에만 부르는 것이 아니다. onConnected / onWritten / onTimeout / finish* 적용 어떤 경우든 전달이 끝나면
// IOThread 는 pump() 를 한 번 돈다. 콜백 안에서 등록한 요청이 이미 Stage 에 있는 데이터로 충족될 수 있기 때문이다.
void IOThread::pump(Connection* connection)
{
    Stage&             stage    = connection->_recv_stage;
    Stage::Provider&   provider = stage.provider();
    Stage::Consumer&   consumer = stage.consumer();
    const ConnectionHandler& handler = *_handler;              // run 에 넘겨받은 함수 표

    // 한 번의 recv 로 요청 여러 개가 연달아 충족될 수 있다 (예: WS 의 header, ext length, mask, payload 가 한 패킷에 들어옴).
    for (;;) {
        ReadRequest& req    = connection->_read_request;
        const char*  buffer = 0;
        size_t       length = 0;

        if (connection->_work_state != RUNNING) break;         // Work 중. 콜백을 내보내지 않는다
        if (req.kind == ReadRequest::NONE) break;              // write / close 중. 데이터는 Stage 에 남겨 둔다

        if (req.kind == ReadRequest::EXACT) {
            buffer = (const char*)consumer.prepare(req.length); // 아직 req.length 만큼 안 모였으면 nullptr
            if (!buffer) break;                                 // 더 받아야 함
            length = req.length;
        }
        else {                                                  // UNTIL
            size_t readable = consumer.readable();
            if (readable == 0) break;
            const char* view = (const char*)consumer.prepare(readable);   // 쌓인 전부를 들여다본다 (peek). 아직 commit 하지 않음

            // req.scanned 부터 검사. 이전 recv 의 끝과 이번 recv 의 처음에 걸쳐 있는 delimiter 를 놓치지 않도록
            // scanned 는 항상 (delim_length - 1) 바이트 겹쳐서 기록된다.
            size_t pos = findDelimiter(view, readable, req.scanned, req.delim, req.delim_length);   // 없으면 (size_t)-1
            if (pos == (size_t)-1) {
                if (readable >= req.length) { close(connection, PROTOCOL_ERROR); return; }         // max 안에 delimiter 없음
                req.scanned = (readable < req.delim_length) ? 0 : readable - (req.delim_length - 1);
                break;                                                                             // 더 받아야 함
            }
            length = pos + req.delim_length;                   // delimiter 포함한 길이
            if (length > req.length) { close(connection, PROTOCOL_ERROR); return; }
            buffer = view;
        }

        // 요청 하나 -> onRead 하나. 먼저 NONE 으로 내리고 콜백을 부른다. 콜백 안에서 Connection::read / readUntil 이 다시 불리면
        // 그 요청이 등록되고, 루프 다음 회차에서 남은 데이터로 바로 검사된다.
        req.kind = ReadRequest::NONE;
        Span data = { buffer, length };
        handler.onRead(connection->_connection_context, connection, data);
        consumer.commit(length);                               // 콜백이 return 한 뒤에 commit. 이 줄부터 data 는 무효
        if (connection->_closing) return;                      // 콜백 안에서 close() 가 불림. 이후 recv 를 걸지 않는다
    }

    stage.compact();                                           // 남은 데이터를 앞으로 당겨 provider 공간을 확보

    // recv 재등록. 요청이 NONE 이거나 Work 중이어도 상대가 보내는 데이터는 계속 받아 Stage 에 쌓는다. Stage 가 가득 차면
    // writable == 0 이 되어 recv 를 걸지 않게 되고, 그것이 TCP backpressure 가 된다. 요청이 걸려 있는데 writable == 0 이면
    // 등록 시 capacity 검사에 걸렸어야 하므로 논리 오류다.
    if (connection->_recv_pending) return;                     // IOCP / io_uring: outstanding recv 는 한 번에 하나만
    size_t writable = provider.writable();
    if (writable == 0) { assert(connection->_read_request.kind == ReadRequest::NONE); return; }
    connection->_recv_pending = true;
    socket.recv(provider.prepare(writable), writable);         // 완료는 다시 onRecvComplete 로. epoll: EPOLLIN 관심 유지가 이 줄에 해당
}
```

### 1.4 findDelimiter

delimiter 가 4 바이트 이하로 고정이므로 `memchr` 로 첫 바이트를 찾고 나머지를 비교하는 것으로 충분하다. `from` 은 `req.scanned` 다.

```cpp
static size_t findDelimiter(const char* view, size_t length, size_t from, const char* delim, size_t delim_length)
{
    const char* p = view + from;
    const char* end = view + length;
    while (p < end && (size_t)(end - p) >= delim_length) {
        p = (const char*)memchr(p, delim[0], (size_t)(end - p) - (delim_length - 1));
        if (!p) return (size_t)-1;
        if (memcmp(p, delim, delim_length) == 0) return (size_t)(p - view);
        ++p;
    }
    return (size_t)-1;
}
```

### 1.5 Work / timeout

설계 v4 (지금은 `docs/design/design_base_v5.html`, 절 번호가 같다) 9.5 (dispatch → apply, onTimeout), 9.7 (WorkContext 네 상태, ConnectionStream, closeStream), 11절 (TIMEOUT 과 CLOSE) 이 이 절을 대신한다. 옛 DRAINING / startWork(handle) 설명은 지웠다.

### 1.6 송신 : write lane, notify, broadcast

설계 v4 9.3 (Sender), 9.6 (drain 쪽 : pickWrite → resolve), 9.8 (NotificationContext, notify, broadcast) 와 10절 (한 바퀴의 drain) 이 바탕이다. base 사이클 B1 (`design_http_v0.html` 2절) 에서 아래가 바뀌었다.

- **한 WRITE 에 둘.** `SendFlow::pick` 은 send Stage 의 readable 전부와 notify Stage 의 readable 전부를 한 WRITE 로 고른다 (`IoAction::gather(length, notify_length)`). send 쪽 길이가 0 보다 크면 purpose 는 USER, 아니면 NOTIFY 다. TLS 의 cipher 가 남아 있으면 그것 먼저 (HANDSHAKE / CONTROL), 핸드셰이크 중이면 평문은 고르지 않는 것은 그대로다.
- **IoPayload 조각 둘.** `IoPayload { IoPiece first; IoPiece second; }`. 평문 WRITE 는 first 가 send Stage, second 가 notify Stage 다 (send 가 비면 notify 가 first). READ 와 ACCEPT 는 first 만 쓴다. backend 는 IOCP `WSASend` 의 `WSABUF` 둘, epoll / kqueue `sendmsg` 의 iovec 둘, io_uring `IORING_OP_SENDMSG` (msghdr 와 iovec 은 `IoEvent` 의 멤버) 로 보낸다. 부분 송신은 `IoEvent::done` 하나로 두 조각을 이어서 센다 (`backend::restMessage`).
- **TLS.** pick 때 send 다음 notify 를 cipher send Stage 에 encode 한다. 암호문은 버퍼 하나라 gather 가 없다. cipher send Stage 는 두 평문을 함께 담도록 `gatheredPlainBytes(send, notify)` (= send + notify + 레코드 하나의 overhead) 로 잡는다.
- **release.** pick 때 정한 두 길이를 각자 치운다. notify 쪽은 notification lock 안에서 치운다. send 쪽 길이가 0 보다 크면 `onWritten(그 길이)` 가 한 번 온다.
- **notify 의 여러 조각.** `NotificationDesk::notify(id, Span<const KiottyNetSpan>)` 와 `broadcast(Span<const KiottyNetSpan>)` 는 한 lock 안에서 조각들을 이어 복사하고, 다 들어가지 않으면 하나도 넣지 않는다 (`BUFFER_FULL`). WebSocket 의 frame 머리 + payload 가 이것을 쓴다. 공개 API 는 그대로다.
- **송신 timeout.** `SendFlow` 가 송신 deadline 을 가진다. pick 이 WRITE 를 고르면 `now + send_timeout` 을 적고 (`limit` 으로 받은 값. 0 이면 적지 않는다), release 와 CLOSING 진입이 지운다. `ConnectionStatus::send_deadline` 으로 보이며, IoLoop 의 `stepSlot` 이 지나간 것을 `IoKind::SEND_TIMEOUT` 으로 Connection 에 주고 Connection 은 `close(PEER_NOT_READING)` 을 돌려준다. CLOSE 는 detach 로 걸린 WRITE 를 취소한다. IoLoop 의 대기 시간은 read deadline 과 송신 deadline 중 가까운 쪽이다.

**보내고 닫기 (B3, 2026-11-13).** `thenCloseWith(data, reason)` 을 받으면 `adoptAction` 이 data 를 send Stage 에 복사하고 `IoAction::closeAfterSend(reason)` (CLOSE 에 `is_after_send`) 을 만든다. `Connection::apply` 는 그것을 보고 바로 닫지 않고 `Phase::FLUSHING` 에 들어간다 (deadline 을 지우고 reason 을 `_flush_reason` 에). FLUSHING 은 `isAlive` 가 아니라서 notify / broadcast 는 `PEER_CLOSED` 로 거절되고, `isOpen` 이라서 `SendFlow::pick` 은 계속 돈다. 다만 `select` 는 notify Stage 를 고르지 않는다 (push lane 은 버린다). 그 사이의 완료는 `Connection::onFlushing` 이 받는다: SEND 는 release 뒤 `SendFlow::isDrained` (send Stage 와 TLS cipher 가 비었는가) 면 `close(_flush_reason)`, RECEIVE 는 실패면 닫고 아니면 버린다, SEND_TIMEOUT 은 `PEER_NOT_READING`. 핸들러에는 onWritten 도 onRead 도 가지 않고 마지막에 CLOSED 하나만 간다. 소유자의 close 는 FLUSHING 에서도 바로 닫는다.

### 1.7 주소와 TLS 는 run / connect 때 (B2)

`design_http_v0.html` 2.3 과 1절 결정 18 이 바탕이다. create 는 자원만, run / connect 는 주소와 TLS 를 받는다.

- **설정과 주소.** `KiottyNetServerConfiguration` / `KiottyNetClientConfiguration` 에 `address` 가 없다. `initializeServerConfiguration` / `initializeClientConfiguration` 은 인자 없이 기본값을 채우고, 주소는 `initializeTcpAddress` / `initializePipeAddress` 가 지금까지의 검사 (빈 이름, 길이, UTF-8, TCP 의 port 0) 그대로 만든다. run / connect 는 받은 주소를 `isAddressValid` 로 다시 본다 (transport, NUL 로 끝나는 비지 않은 host, TCP 의 port). 번역 층은 주소를 run / connect 의 stack 에 복사해 넘긴다.
- **Owner 의 initialize.** `Server` / `EventClient` / `StreamClient` 의 `configuration_type` 은 공개 설정 struct 그대로다 (`ServerConfiguration`, `ClientConfiguration` 묶음은 지웠다). initialize 는 TLS 프로파일을 열지 않는다. TLS Stage 는 `MemoryLimits::TLS` 이면 언제나 잡는다 (Server / EventClient 는 `cipherOf(limits)`, StreamClient 는 `reserveTlsStages`). 공식은 그대로다.
- **run.** 순서는 handler 검사 → 주소 검사 → TLS 설정의 mark → `RunGate::enter` → `openProfile(tls)` → 서버는 `listen(address)`, EventClient 는 `connectWithRetry(configuration, address, closing)`. `openProfile` 은 먼저 `TlsProfile::close` 로 지난 SSL_CTX 를 버리고, tls 가 있으면 `openServer` / `openClient` 로 새로 만든다. 그래서 평문 run 은 프로파일이 비어 있고, Connection 은 지금처럼 `profile.valid()` 로 TLS 여부를 가른다. 프로파일 실패는 `leave(code)` 로 run 의 반환값과 await 가 같은 값을 본다.
- **connect.** `StreamClient::connect(address, tls)` 는 `disconnect` (TLS 엔진, 소켓, Stage 정리) → 주소 / mark 검사 → `openProfile` → `connectWithRetry` → `secure(address)`. SNI 와 hostname 검증의 이름은 `hostnameOf(address)` (Pipe 면 빈 이름) 이다.
- **memory size.** `serverMemorySize` / `clientMemorySize` 는 TLS 빌드에서 연결마다 cipher Stage (receive + `alignStage(cipherSendBytes(gatheredPlainBytes(send, push)))`) 를 더한다.

## 2. StateMachine

requirement_v0.md 8절의 구현이다. 공개 API 는 `KiottyNet_runStatefulServer` / `KiottyNet_runStatefulEventClient` 둘뿐이고 나머지는 모두 라이브러리 안이다. Runner 객체도 사용자 pool 도 없다. 사용자가 보는 것은 `KiottyNetStatefulAppContext` 하나다. 코드의 자리와 이유는 `docs/design/design_statemachine_v0.md` 3 ~ 6절이고, 이 절은 그 요약이다.

### 2.1 Handler : 콜백을 부르는 자리 하나

Connection, IoLoop, Server, EventClient 는 사용자 함수를 직접 부르지 않는다. run 을 부른 thread 의 stack 에 있는 `Handler` 하나를 거친다.

```cpp
class Handler                                       // domain/entity/kiotty_handler.h
{
public:
    virtual KiottyNetCode               validate() const = 0;          // run 의 첫 줄
    virtual int32_t                     initialState() const = 0;      // 연결마다 machine state 의 처음 값
    virtual KiottyNetConnectionDecision admit(const KiottyNetConnectionInfo& info) const = 0;
    virtual KiottyNetConnectionAction   dispatch(const Caller& caller, const KiottyNetConnectionEvent& event) const = 0;
    virtual void                        report(const KiottyNetConnectionInfo& info, KiottyNetCode code, const char* message) const = 0;
    KiottyNetEndpoint& owner;
};
```

| | `CallbackHandler` (`KiottyNet_runServer / runEventClient`) | `StatefulHandler` (`KiottyNet_runStatefulServer / runStatefulEventClient`) |
|---|---|---|
| validate | SUCCESS | 표 검사 (2.3) |
| initialState | 0 | `machine->initial` |
| admit | `onConnected(app_context, info)` | `createConnectionContext(app_context, info)` (2.5) |
| dispatch | event type 에 맞는 콜백 (`onReady`, `onRead`, `onWritten`, `onTimeout`, `onWorkStarted`, `onDisconnected`) | 표를 찾아 transition action (2.4) |
| report | `onError` | `onError` (NULL 이면 부르지 않는다) |

```cpp
KiottyNetCode KiottyNet_runStatefulServer(KiottyNetServerHandle server, const KiottyNetAddress* address, const KiottyNetTlsServerConfiguration* tls, const KiottyNetStatefulAppContext* app_context)
{
    if (server == nullptr || address == nullptr || app_context == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
    const KiottyNetAddress copied = *address;
    const StatefulHandler handler(*app_context, *server);
    return server->run(handler, copied, tls);       // run 의 첫 줄이 handler.validate(). 실패하면 listen 도 하지 않고 그 code
}
```

- 사용자의 `KiottyNetStatefulAppContext` 는 참조로만 들린다. 라이브러리가 따로 잡는 per-run 객체는 없다.
- 사용자는 이 struct 와 표를 run 을 부른 함수의 stack 에 두어도 된다. run 이 모든 CLOSED 뒤에 return 하기 때문이다.

### 2.2 Connection 이 기억하는 것

```cpp
class Connection {
    // ...
    KiottyNetConnectionContext _context;         // admit 이 돌려준 값. 이후 action 의 첫 인자. 라이브러리는 읽지 않는다
    int32_t                    _machine_state;   // onOpened 가 handler.initialState() 로 놓는다. IO thread 만 읽고 쓴다
};
```

- dispatch 마다 Connection 은 `Caller { handler, info, send, machine_state&, context, id }` 를 만들어 넘긴다. StatefulHandler 는 `caller.machine_state` 를 읽고 옮긴다. CallbackHandler 는 보지 않는다.
- machine 은 Connection 이 모른다. run 의 stack 에 있는 StatefulHandler 가 들고 있다. 풀 안의 객체 (Server, Connection) 는 run 바깥을 가리키지 않는다.

### 2.3 표 찾기와 검사

```cpp
RuleResult findRule(const KiottyNetStateMachine& machine, int32_t state, KiottyNetConnectionEventType event)
{
    const KiottyNetRule* fallback = nullptr;
    for (const KiottyNetRule& rule : Span<const KiottyNetRule>{ machine.rules, machine.count })
    {
        if (rule.event != event) { continue; }
        if (rule.state == state) { return RuleResult::success(rule); }       // 정확 일치가 우선
        if (rule.state == KIOTTY_NET_ANY_STATE) { fallback = &rule; }
    }
    if (fallback == nullptr) { return RuleResult::failure(KIOTTY_NET_CODE_UNHANDLED_EVENT); }
    return RuleResult::success(*fallback);
}
```

- 찾기는 선형 탐색이다. 표는 정렬되어 있을 필요가 없다 (C 표는 아무 순서).
- `checkRules` 가 run 의 첫 줄이다. machine / rules NULL, count 0, transition action NULL, 범위 밖 event, `next.count > KIOTTY_NET_MAX_NEXT`, `KIOTTY_NET_ANY_STATE` 보다 작은 state 나 next 는 `KIOTTY_NET_CODE_INVALID_ARGUMENT`. 같은 (state, event) 가 두 번이면 `KIOTTY_NET_CODE_DUPLICATE_RULE` (O(n²) 비교). C++ 의 `KiottyNet_rules` 는 컴파일 타임에 이미 거른 표라 그냥 통과한다.

### 2.4 dispatch

```cpp
KiottyNetConnectionAction StatefulHandler::dispatch(const Caller& caller, const KiottyNetConnectionEvent& event) const
{
    const RuleResult found = findRule(*_stateful.machine, caller.machine_state, event.type);
    if (event.type == KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED) { return finish(caller, event, found); }  // 행이 있으면 action, Transition 은 버리고 release
    if (!found) { return refuse(caller, KIOTTY_NET_CODE_UNHANDLED_EVENT, UNHANDLED_EVENT_MESSAGE); }    // onError 뒤 thenClose
    const KiottyNetRule& rule = found.value();
    const KiottyNetTransition transition = rule.transition_action(caller.context, caller.handle(), event);
    if (!isAllowed(rule, transition.next)) { return refuse(caller, KIOTTY_NET_CODE_ILLEGAL_TRANSITION, ILLEGAL_TRANSITION_MESSAGE); }
    caller.machine_state = transition.next;
    return transition.entry_action;
}
```

- `ILLEGAL_TRANSITION` 검사는 release 빌드에서도 한다 (`next.count` 가 0 이면 어디로 가도 된다).
- 돌려준 entry action 은 base 의 콜백 return 과 같은 길로 Connection 에 적용된다 (requirement 5절의 action 표).
- `WORK_STARTED` 에서 돌려준 entry action 은 Connection 이 아니라 Stream 에 적용된다. `KiottyNet_thenWork()` 면 stream 은 worker 의 것이고 worker 가 `KiottyNet_closeConnectionStream` 으로 끝낸다. 그 밖 (HOLD / READ / READ_UNTIL / WRITE / CLOSE) 이면 Connection 이 그 자리에서 `closeConnectionStream(stream, entry_action)` 과 같은 길로 닫는다. 그 action 이 잘못되었으면 (`INVALID_ARGUMENT`, `BUFFER_FULL`) `onError` 로 알리고 stream 은 남는다. 행이 없으면 `UNHANDLED_EVENT` 의 `thenClose` 로 닫힌다.

### 2.5 admit 과 CLOSED

```cpp
KiottyNetConnectionDecision StatefulHandler::admit(const KiottyNetConnectionInfo& info) const
{
    if (_stateful.createConnectionContext == nullptr) { return acceptedDecision(nullptr); }
    const KiottyNetConnectionContext session = _stateful.createConnectionContext(_stateful.app_context, info);
    if (session == nullptr)
    {
        report(info, KIOTTY_NET_CODE_SESSION_EXHAUSTED, SESSION_EXHAUSTED_MESSAGE);
        return ignoredDecision();                   // 받은 socket 을 바로 닫는다
    }
    return acceptedDecision(session);
}
```

- create 가 NULL 함수면 session 은 NULL 이고 그대로 진행한다. action 은 `connection_context == NULL` 을 받고 release 는 불리지 않는다.
- create 가 NULL 을 돌려주면 `onError(KIOTTY_NET_CODE_SESSION_EXHAUSTED)` 로 알리고 연결을 받지 않는다. Session 이 없으므로 CONNECTED 도 CLOSED 행도 release 도 없다.
- CLOSED 행이 있으면 그 action 을 부르고 Transition 은 버린다. 행이 없어도 release 는 한다 (정리할 것이 release 뿐인 Session). release 는 CLOSED 행 뒤다. action 이 살아 있는 session 을 마지막으로 본다.

### 2.6 HTTP / WebSocket 이 얹히는 방법

`KiottyNet_runHttpServer(http_server)` 는 자기 stack 에 `KiottyNetStatefulAppContext` 를 만든다. machine 은 라이브러리의 HTTP 표, app_context 는 HttpServer 자신 (라우트 표, HttpSession pool), create / release 는 그 pool 의 acquire / release 다. 그 다음 `KiottyNet_runStatefulServer` 를 부른다. 라우트는 run 전에 이미 표에 들어가 있다.
HttpSession pool 은 `createHttpServer` 가 `max_clients` 만큼 잡는다. 사용자 프로토콜이 Session 저장소를 들고 있는 것과 같은 방식이고, 그 저장소가 라이브러리 안에 있을 뿐이다. HttpSession 이 사용자에게 주는 `KiottyNetHttpRequest` 와 `KiottyNetHttpResponseHandle` 을 어떻게 공급하는지는 4절이다.
WebSocket 은 HTTP 와 한 표, 한 Session 이다. 라이브러리의 표 하나가 Upgrade 전후를 다 밟는다 (`design_statemachine_v0.md` 11절, 14절 7, 8).

---

## 3. 예제 : Echo

2절의 StateMachine 을 사용자 쪽에서 쓰는 모습이다. 서버와 클라이언트가 같은 프로토콜을 쓰므로 표 두 장이 서로의 거울이다.
코드는 `kiotty_net_statemachine.h` 만 포함하면 컴파일되는 형태다. 긴 작업과 notify 는 이 예제에 없다. 긴 작업이 있는 판은 progress 문서의 echo 예제에 있다.
오류로 닫을 때는 `ErrorOccurred` 로, 정상으로 닫힐 때는 `Closed` 로 옮긴다. Transition 은 언제나 다음 상태를 정한다.

### 3.1 프로토콜

```
frame = length (4 byte, big endian) + body (length byte, 1..1024)
```

- 클라이언트가 frame 하나를 보내면 서버가 같은 frame 을 그대로 돌려준다.
- 서버는 header 를 30 초 안에 받지 못하면 끊는다. body 길이가 0 이거나 1024 를 넘으면 `KIOTTY_NET_CODE_PROTOCOL_ERROR` 로 끊는다.
- 클라이언트는 응답이 보낸 것과 같은지 확인하고 스스로 끊는다.

```cpp
static const size_t   HEADER_LENGTH = 4;
static const size_t   MAX_BODY      = 1024;
static const uint32_t IDLE_TIMEOUT_MS = 30000;

static uint32_t decodeLength(KiottyNetSpan header)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(header.data);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static void encodeLength(char* header, uint32_t length)
{
    header[0] = char(length >> 24);
    header[1] = char(length >> 16);
    header[2] = char(length >> 8);
    header[3] = char(length);
}
```

### 3.2 EchoServer

연결마다 들고 있어야 하는 것은 돌려줄 frame 하나다. 상태는 라이브러리가 들고 있으므로 Session 에 없다.

```cpp
enum class ServerState
{
    ReadingHeader,
    ReadingBody,
    Writing,
    ErrorOccurred,
    Closed
};

struct ServerSession
{
    char   frame[HEADER_LENGTH + MAX_BODY];     // header 를 받은 자리에 그대로 두고 body 를 뒤에 붙여서 통째로 돌려준다
    size_t frame_length;
};

typedef KiottyNet_Transition<ServerState> ServerNext;

static ServerSession& serverSession(KiottyNetConnectionContext connection_context)
{
    return *static_cast<ServerSession*>(connection_context);
}

static KiottyNetTransition serverReadHeader()
{
    return ServerNext::moveToNextState(ServerState::ReadingHeader, KiottyNet_thenRead(HEADER_LENGTH, IDLE_TIMEOUT_MS));
}

static KiottyNetTransition serverOnConnected(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return serverReadHeader();
}

static KiottyNetTransition serverOnHeader(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    ServerSession& s      = serverSession(connection_context);
    uint32_t       length = decodeLength(e.data);
    if (length == 0 || length > MAX_BODY)
    {
        return ServerNext::moveToNextState(ServerState::ErrorOccurred, KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR));
    }
    std::memcpy(s.frame, e.data.data, HEADER_LENGTH);                 // data 는 return 하면 무효라 복사
    s.frame_length = HEADER_LENGTH + length;
    return ServerNext::moveToNextState(ServerState::ReadingBody, KiottyNet_thenRead(length, 0));
}

static KiottyNetTransition serverOnBody(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    ServerSession& s = serverSession(connection_context);
    std::memcpy(s.frame + HEADER_LENGTH, e.data.data, e.data.length);
    KiottyNetSpan frame = { s.frame, s.frame_length };
    return ServerNext::moveToNextState(ServerState::Writing, KiottyNet_thenWrite(frame, 0));   // write lane 으로 복사되므로 s.frame 은 바로 다시 써도 된다
}

static KiottyNetTransition serverOnWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return serverReadHeader();
}

static KiottyNetTransition serverOnTimeout(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ServerNext::moveToNextState(ServerState::ErrorOccurred, KiottyNet_thenClose(KIOTTY_NET_CODE_TIMEOUT));
}

static KiottyNetTransition serverOnClosed(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ServerNext::moveToNextState(ServerState::Closed, KiottyNet_thenHold(0));   // CLOSED 의 Transition 은 버려진다. 인자 둘이 언제나 필요하다
}

static constexpr auto SERVER_RULES = KiottyNet_rules(
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED)                                  .then(serverOnConnected).to(ServerState::ReadingHeader),
    KiottyNet_from(ServerState::ReadingHeader).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ)      .then(serverOnHeader)   .to(ServerState::ReadingBody, ServerState::ErrorOccurred),
    KiottyNet_from(ServerState::ReadingBody)  .when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ)      .then(serverOnBody)     .to(ServerState::Writing),
    KiottyNet_from(ServerState::Writing)      .when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN)   .then(serverOnWritten)  .to(ServerState::ReadingHeader),
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT)                                    .then(serverOnTimeout),
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED)                                     .then(serverOnClosed));
static constexpr KiottyNetStateMachine SERVER_MACHINE = KiottyNet_makeStateMachine(SERVER_RULES, ServerState::ReadingHeader);
```

Session 저장소는 사용자 것이다. 연결 수만큼 고정 배열을 두고 create / release 로 빌려 준다.

```cpp
template <typename Session, size_t N>
struct SessionPool
{
    Session session[N];
    bool    in_use[N];

    static KiottyNetConnectionContext acquire(KiottyNetAppContext app_context, KiottyNetConnectionInfo)
    {
        SessionPool& self = *static_cast<SessionPool*>(app_context);
        for (size_t i = 0; i < N; ++i)
        {
            if (!self.in_use[i])
            {
                self.in_use[i]  = true;
                self.session[i] = Session();
                return &self.session[i];
            }
        }
        return nullptr;                                                          // 라이브러리가 onError(KIOTTY_NET_CODE_SESSION_EXHAUSTED) 로 알리고 연결을 받지 않는다
    }

    static void release(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context)
    {
        SessionPool& self = *static_cast<SessionPool*>(app_context);
        self.in_use[static_cast<Session*>(connection_context) - self.session] = false;
    }
};

int runEchoServer(uint16_t port)
{
    static SessionPool<ServerSession, 32> sessions = {};
    KiottyNetStatefulAppContext           stateful = { &sessions, &SERVER_MACHINE, &SessionPool<ServerSession, 32>::acquire, &SessionPool<ServerSession, 32>::release, nullptr };   // onError 는 NULL 이어도 된다

    KiottyNetServerConfiguration configuration;
    KiottyNetAddress             address;
    if (KiottyNet_ServerConfigurationBuilder(configuration).maxClients(32).build() != KIOTTY_NET_CODE_SUCCESS
        || KiottyNet_initializeTcpAddress(&address, "0.0.0.0", port) != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Context context;
    KiottyNet_Server  server(context.handle(), configuration);
    if (!context || !server)
    {
        return 1;
    }

    std::thread io([&]() { KiottyNet_runStatefulServer(server.handle(), &address, nullptr, &stateful); });   // stateful 과 sessions 는 io 가 끝날 때까지 살아 있다. TLS 면 nullptr 자리에 TLS 설정
    if (server.await(3000) != KIOTTY_NET_CODE_SUCCESS)
    {
        io.join();
        return 1;
    }
    waitForShutdownSignal();
    server.close();
    io.join();
    return 0;
}
```

### 3.3 EchoClient : EventClient 판

서버와 같은 StateMachine 을 쓴다. 연결이 하나라 Session 저장소도 하나다. 연결되면 frame 을 보내고, 돌아온 frame 이 보낸 것과 같은지 확인한 뒤 스스로 끊는다.

```cpp
enum class ClientState
{
    Sending,
    ReadingHeader,
    ReadingBody,
    ErrorOccurred,
    Closed
};

struct ClientSession
{
    char   frame[HEADER_LENGTH + MAX_BODY];
    size_t frame_length;
    size_t expected_body;
    bool   matched;
};

typedef KiottyNet_Transition<ClientState> ClientNext;

static ClientSession& clientSession(KiottyNetConnectionContext connection_context)
{
    return *static_cast<ClientSession*>(connection_context);
}

static KiottyNetTransition clientOnConnected(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    ClientSession& s     = clientSession(connection_context);
    KiottyNetSpan  frame = { s.frame, s.frame_length };
    return ClientNext::moveToNextState(ClientState::Sending, KiottyNet_thenWrite(frame, 5000));
}

static KiottyNetTransition clientOnWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ClientNext::moveToNextState(ClientState::ReadingHeader, KiottyNet_thenRead(HEADER_LENGTH, 5000));
}

static KiottyNetTransition clientOnHeader(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    ClientSession& s      = clientSession(connection_context);
    uint32_t       length = decodeLength(e.data);
    if (length != s.frame_length - HEADER_LENGTH)
    {
        return ClientNext::moveToNextState(ClientState::ErrorOccurred, KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR));
    }
    s.expected_body = length;
    return ClientNext::moveToNextState(ClientState::ReadingBody, KiottyNet_thenRead(length, 5000));
}

static KiottyNetTransition clientOnBody(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    ClientSession& s = clientSession(connection_context);
    s.matched = std::memcmp(s.frame + HEADER_LENGTH, e.data.data, s.expected_body) == 0;
    return ClientNext::moveToNextState(ClientState::Closed, KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS));   // 할 일이 끝났다. CLOSED(KIOTTY_NET_CODE_SUCCESS) 가 마지막으로 온다
}

static KiottyNetTransition clientOnTimeout(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ClientNext::moveToNextState(ClientState::ErrorOccurred, KiottyNet_thenClose(KIOTTY_NET_CODE_TIMEOUT));
}

static KiottyNetTransition clientOnClosed(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ClientNext::moveToNextState(ClientState::Closed, KiottyNet_thenHold(0));
}

static constexpr auto CLIENT_RULES = KiottyNet_rules(
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED)                                  .then(clientOnConnected).to(ClientState::Sending),
    KiottyNet_from(ClientState::Sending)      .when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN)   .then(clientOnWritten)  .to(ClientState::ReadingHeader),
    KiottyNet_from(ClientState::ReadingHeader).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ)      .then(clientOnHeader)   .to(ClientState::ReadingBody, ClientState::ErrorOccurred),
    KiottyNet_from(ClientState::ReadingBody)  .when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ)      .then(clientOnBody)     .to(ClientState::Closed),
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT)                                    .then(clientOnTimeout),
    KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED)                                     .then(clientOnClosed));
static constexpr KiottyNetStateMachine CLIENT_MACHINE = KiottyNet_makeStateMachine(CLIENT_RULES, ClientState::Sending);

static KiottyNetConnectionContext singleSession(KiottyNetAppContext app_context, KiottyNetConnectionInfo)
{
    return app_context;                                                          // 연결이 하나라 app_context 가 곧 Session 이다
}

int runEchoClient(const char* host, uint16_t port, KiottyNetSpan message)
{
    ClientSession session = {};
    encodeLength(session.frame, uint32_t(message.length));
    std::memcpy(session.frame + HEADER_LENGTH, message.data, message.length);
    session.frame_length = HEADER_LENGTH + message.length;

    KiottyNetStatefulAppContext stateful = { &session, &CLIENT_MACHINE, &singleSession, nullptr, nullptr };   // release 는 할 일이 없어서 NULL, onError 도 NULL

    KiottyNetClientConfiguration configuration;
    KiottyNetAddress             address;
    if (KiottyNet_ClientConfigurationBuilder(configuration).connectTimeout(3000).retry(3, 1000).build() != KIOTTY_NET_CODE_SUCCESS
        || KiottyNet_initializeTcpAddress(&address, host, port) != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Context     context;
    KiottyNet_EventClient client(context.handle(), configuration);
    if (!context || !client)
    {
        return 1;
    }

    KiottyNet_runStatefulEventClient(client.handle(), &address, nullptr, &stateful);   // 연결이 닫히면 return 한다. 이 thread 가 IO Thread 다
    return session.matched ? 0 : 1;
}
```

- 연결이 하나이므로 create 는 app_context 를 그대로 Session 으로 돌려주고 release 는 NULL 이다.
- `session` 과 `stateful` 은 run 을 부른 함수의 stack 에 있다. run 이 CLOSED 뒤에 return 하므로 안전하다.
- 결과는 Session 에 남긴 `matched` 로 읽는다. run 의 반환값은 끊어진 이유이고 echo 가 맞았는지는 알려 주지 않는다.

### 3.4 EchoClient : StreamClient 판

이벤트도 표도 없이 같은 일을 하는 동기 판이다. IO Thread 가 없고 호출한 thread 가 기다린다. 한 번 묻고 한 번 받는 도구에는 이쪽이 짧다.

```cpp
static bool readExact(KiottyNet_StreamClient& client, char* buffer, size_t length, uint32_t timeout_milliseconds)
{
    size_t received = 0;
    while (received < length)
    {
        KiottyNetResult result = client.read(buffer + received, length - received, timeout_milliseconds);   // 최대 length 바이트. 모자라면 다시 부른다
        if (result.code != KIOTTY_NET_CODE_SUCCESS)
        {
            return false;
        }
        received += size_t(result.bytes);
    }
    return true;
}

int runEchoClientSync(const char* host, uint16_t port, KiottyNetSpan message)
{
    char frame[HEADER_LENGTH + MAX_BODY];
    encodeLength(frame, uint32_t(message.length));
    std::memcpy(frame + HEADER_LENGTH, message.data, message.length);
    KiottyNetSpan request = { frame, HEADER_LENGTH + message.length };

    KiottyNetClientConfiguration configuration;
    KiottyNetAddress             address;
    if (KiottyNet_ClientConfigurationBuilder(configuration).connectTimeout(3000).retry(3, 1000).build() != KIOTTY_NET_CODE_SUCCESS
        || KiottyNet_initializeTcpAddress(&address, host, port) != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Context      context;
    KiottyNet_StreamClient client(context.handle(), configuration);
    if (!context || !client || client.connect(address) != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }

    char header[HEADER_LENGTH];
    char body[MAX_BODY];
    bool ok = client.write(request, 5000).code == KIOTTY_NET_CODE_SUCCESS
           && readExact(client, header, HEADER_LENGTH, 5000)
           && decodeLength(KiottyNetSpan{ header, HEADER_LENGTH }) == message.length
           && readExact(client, body, message.length, 5000)
           && std::memcmp(body, message.data, message.length) == 0;
    client.disconnect();
    return ok ? 0 : 1;
}
```

### 3.5 C 판 EchoServer

같은 표를 C 로 쓰면 이렇다. 행 순서는 아무렇게나 써도 되고, 중복은 run 이 시작할 때 `KIOTTY_NET_CODE_DUPLICATE_RULE` 로 걸러진다. 클라이언트 두 가지도 같은 방법으로 옮겨진다.

```c
enum ServerState
{
    SERVER_READING_HEADER,
    SERVER_READING_BODY,
    SERVER_WRITING,
    SERVER_ERROR_OCCURRED,
    SERVER_CLOSED
};

typedef struct ServerSession
{
    char   frame[HEADER_LENGTH + MAX_BODY];
    size_t frame_length;
} ServerSession;

static KiottyNetTransition server_read_header(void)
{
    return KiottyNet_moveToNextState(SERVER_READING_HEADER, KiottyNet_thenRead(HEADER_LENGTH, IDLE_TIMEOUT_MS));
}

static KiottyNetTransition server_on_connected(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return server_read_header();
}

static KiottyNetTransition server_on_header(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    ServerSession* s      = (ServerSession*)session;
    uint32_t       length = decode_length(event.data);
    (void)connection;
    if (length == 0 || length > MAX_BODY)
    {
        return KiottyNet_moveToNextState(SERVER_ERROR_OCCURRED, KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR));
    }
    memcpy(s->frame, event.data.data, HEADER_LENGTH);
    s->frame_length = HEADER_LENGTH + length;
    return KiottyNet_moveToNextState(SERVER_READING_BODY, KiottyNet_thenRead(length, 0));
}

static KiottyNetTransition server_on_body(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    ServerSession* s = (ServerSession*)session;
    KiottyNetSpan  frame;
    (void)connection;
    memcpy(s->frame + HEADER_LENGTH, event.data.data, event.data.length);
    frame.data   = s->frame;
    frame.length = s->frame_length;
    return KiottyNet_moveToNextState(SERVER_WRITING, KiottyNet_thenWrite(frame, 0));
}

static KiottyNetTransition server_on_written(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return server_read_header();
}

static KiottyNetTransition server_on_timeout(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return KiottyNet_moveToNextState(SERVER_ERROR_OCCURRED, KiottyNet_thenClose(KIOTTY_NET_CODE_TIMEOUT));
}

static KiottyNetTransition server_on_closed(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return KiottyNet_moveToNextState(SERVER_CLOSED, KiottyNet_thenHold(0));
}

static const KiottyNetRule SERVER_RULES[] = {
    KIOTTY_NET_RULE(KIOTTY_NET_ANY_STATE,       KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED, server_on_connected, SERVER_READING_HEADER),
    KIOTTY_NET_RULE(SERVER_READING_HEADER,  KIOTTY_NET_CONNECTION_EVENT_TYPE_READ,      server_on_header,    SERVER_READING_BODY, SERVER_ERROR_OCCURRED),
    KIOTTY_NET_RULE(SERVER_READING_BODY,    KIOTTY_NET_CONNECTION_EVENT_TYPE_READ,      server_on_body,      SERVER_WRITING),
    KIOTTY_NET_RULE(SERVER_WRITING,         KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN,   server_on_written,   SERVER_READING_HEADER),
    KIOTTY_NET_RULE_UNCHECKED(KIOTTY_NET_ANY_STATE, KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT, server_on_timeout),
    KIOTTY_NET_RULE_UNCHECKED(KIOTTY_NET_ANY_STATE, KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED,  server_on_closed),
};
static const KiottyNetStateMachine SERVER_MACHINE = { SERVER_RULES, KIOTTY_NET_RULE_COUNT(SERVER_RULES), SERVER_READING_HEADER };

static ServerSession sessions[32];
static bool          in_use[32];

static KiottyNetConnectionContext acquire_session(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
{
    size_t i;
    (void)app_context;
    (void)info;
    for (i = 0; i < 32; ++i)
    {
        if (!in_use[i])
        {
            in_use[i] = true;
            memset(&sessions[i], 0, sizeof(sessions[i]));
            return &sessions[i];
        }
    }
    return NULL;
}

static void release_session(KiottyNetAppContext app_context, KiottyNetConnectionContext session)
{
    (void)app_context;
    in_use[(ServerSession*)session - sessions] = false;
}

static const KiottyNetStatefulAppContext SERVER_APP = { NULL, &SERVER_MACHINE, acquire_session, release_session, NULL };

int run_echo_server(uint16_t port)
{
    KiottyNetServerConfiguration configuration;
    KiottyNetAddress             address;
    KiottyNetContextResult       context;
    KiottyNetServerResult        server;
    KiottyNetCode                code;

    if (KiottyNet_initializeServerConfiguration(&configuration) != KIOTTY_NET_CODE_SUCCESS
        || KiottyNet_initializeTcpAddress(&address, "0.0.0.0", port) != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }
    KiottyNet_configureServerMaxClients(&configuration, 32);
    context = KiottyNet_initializeContext();
    if (context.code != KIOTTY_NET_CODE_SUCCESS)
    {
        return 1;
    }
    server = KiottyNet_createServer(context.handle, &configuration);
    if (server.code != KIOTTY_NET_CODE_SUCCESS)
    {
        KiottyNet_releaseContext(context.handle);
        return 1;
    }
    code = KiottyNet_runStatefulServer(server.handle, &address, NULL, &SERVER_APP);   /* 이 thread 가 IO Thread 다. 다른 thread 의 KiottyNet_closeServer 로 끝난다 */
    KiottyNet_destroyServer(context.handle, server.handle);
    KiottyNet_releaseContext(context.handle);
    return code == KIOTTY_NET_CODE_CLOSED ? 0 : 1;
}
```

---

## 4. HttpServer : Connection 별 Request / Response 공급

requirement_v0.md 9절과 `include/kiotty_net_http.h` (공개 API 판 v2) 의 구현이다. cycle H3 (완성 응답), H4 (Work 와 Work 스트림), H5 (정적 서빙) 에서 만들었고 progress 는 `docs/progress/261112_progress_http_h3 / h4 / h5.md`, `261112_01_100-continue.md` 다. 설계는 `docs/design/design_http_v0.html` 3절, 8절이다. 이 절의 코드는 소스에서 줄인 것이다 (이름과 흐름은 소스 그대로).

사용자는 `KiottyNetHttpRequest*` 를 받고 `KiottyNetHttpResponseHandle` 을 채우는데, 둘 다 Connection 마다 하나씩 있는 **HttpSession** 안에 있다. HttpSession 은 2.6 의 "라이브러리 안에 있는 Session 저장소" 다. 사용자 프로토콜이 `createConnectionContext` 로 Session 을 공급하는 것과 같은 자리에서 HttpServer 가 공급한다.

핵심은 네 가지다.

- 요청은 수신 Stage 에서 **HttpSession 의 버퍼로 복사**된다. Stage 의 데이터는 onRead 가 return 하면 사라지지만, 요청은 body 를 다 읽을 때까지, 그리고 Work 가 끝날 때까지 살아 있어야 하기 때문이다. 복사하면 `receive_capacity` (기본 4 KiB) 와 `max_header_bytes` / `max_body_bytes` (기본 8 / 64 KiB) 가 서로 묶이지 않는다.
- 완성 응답은 HttpSession 의 응답 버퍼 하나에 쌓이고, 송신할 때 `send_capacity` 크기의 조각으로 write lane 에 옮긴다. 응답 버퍼가 write lane 보다 커도 된다. 파일 body 는 같은 응답 버퍼로 `send_capacity` 만큼씩 읽어 보낸다 (sendfile 없음).
- Work 스트림은 worker 가 base Stream 에 동기 write 한다 (`writeConnectionStream`). chunk 의 머리 / 꼬리는 응답 버퍼에서 조립한다. 새 버퍼는 없다.
- 메모리는 `KiottyNet_createHttpServer` 가 `max_clients` 분량을 한 번 잡는다. run 중 할당은 없다.

| 층 | 파일 | 하는 일 |
|---|---|---|
| context | `kiotty_http_server` | `HttpServer : Server`. close / await / lifetime / 연결 관리는 base 그대로. `run` 이 HTTP 표로 `StatefulHandler` 를 만든다 (4.8) |
| | `kiotty_http_work` | `HttpWork` : setter 가 쓸 Session 찾기 (`fill`), `start`, `write`, `finishWithResponse`, `finishWithClose`, `isConnected` (4.5, 4.6, 4.9) |
| | `kiotty_http_configuration` | `KiottyNetHttpLimits` 의 initialize / configure |
| data/repository/http | `kiotty_http_site` | 서버 하나의 공유 부분 (4.1) |
| | `kiotty_http_session`, `kiotty_http_session_pool` | Session 과 그 풀 (4.2) |
| | `kiotty_http_request_store` | 요청 줄 / 헤더 줄을 헤더 버퍼로 복사하고 자른다. body 를 이어 붙인다 (4.3) |
| | `kiotty_http_response_store` | 응답 버퍼의 배치, setter, `seal`, 송신 쪽 읽기 `ResponseFrame` (4.4) |
| | `kiotty_http_registry`, `kiotty_http_notices` | 라우트 / 정적 경로 표, 끊김 / 오류 action |
| | `kiotty_http_file_body` | 파일 body 의 `attachFile`, 송신 조각 `pieceAt`, `detachFile` (4.7) |
| domain/usecases/http | `kiotty_http_machine` | HTTP 표와 transition action (4.3) |
| | `kiotty_http_exchange` | 요청 하나의 흐름: `beginExchange`, `dispatch`, `startWork`, `sendNext`, `reject` |
| | `kiotty_http_static` | 정적 경로의 응답 (`answerStatic`) |
| | `kiotty_http_content_type`, `kiotty_http_response_text` 외 파서 | content type 표 (4.10), 상태줄 / Date / Allow / chunk 머리, H2 의 파서 |
| C API | `src/kiotty_net_http.cpp` | 번역 층. 문자열 인자를 `parseText` 로 보고 HttpServer 에 넘긴다 |

### 4.1 HttpServer 가 잡는 것

```cpp
class HttpServer : public Server                      // context/kiotty_http_server
{
    HttpSite             _site;
    std::atomic<int32_t> _runs;                       // run 중이면 registry() / notices() 가 KIOTTY_NET_CODE_BUSY
};

struct HttpSite                                       // data/repository/http/kiotty_http_site.h. Session 이 이것을 참조한다
{
    HttpRegistry              registry;               // 라우트와 정적 경로의 표, not found 라우트
    HttpNotices               notices;                // 끊김 / 오류 action 과 각자의 app_context
    HttpSessionPool           sessions;               // max_clients 개 (4.2)
    KiottyNetHttpLimits       limits;
    size_t                    receive_capacity;
    size_t                    send_capacity;
    KiottyNetHttpServerHandle owner;                  // handle 의 server
    std::atomic<bool>         is_closing;             // close() 가 켜고 run 이 시작할 때 끈다 (4.9 의 CLOSED)
};

class HttpRegistry                                    // data/repository/http/kiotty_http_registry
{
    struct RouteEntry                                 // 라우트와 정적 경로가 한 표에 들어간다. 등록 순서 = 매칭 순서
    {
        FixedString<KIOTTY_NET_MAX_URL_LENGTH>  path;      // 등록 때 복사. PathKind 가 정확 / "/*" wildcard / "/" 로 끝나는 디렉토리 접두
        FixedString<KIOTTY_NET_MAX_PATH_LENGTH> location;  // DIRECTORY 는 "/" 나 "\" 로 끝나게 (없으면 "/" 를 붙인다), FILE 은 파일 경로
        KiottyNetHttpRoute                      route;     // 라우트 (source == NONE)
        KiottyNetSpan                           memory;    // MEMORY. 복사하지 않는다. 서버 생명 동안 사용자가 유효하게 둔다
        KiottyNetHttpContentType                type;      // MEMORY 는 등록한 값, FILE 은 등록 때 확장자로 고른 값, DIRECTORY 는 BY_EXTENSION
        uint32_t                                methods;   // KIOTTY_NET_HTTP_METHOD_BIT. 라우트는 비트 하나, 정적 경로는 GET | HEAD
        PathKind                                kind;
        StaticKind                              source;    // NONE, DIRECTORY, FILE, MEMORY
    };
    std::vector<RouteEntry> _entries;                 // initialize 가 max_routes 만큼 reserve 한다
    KiottyNetHttpRoute      _not_found;               // onRequest == NULL 이면 미등록
    size_t                  _max_routes;
};
```

- `KiottyNet_createHttpServer` 는 Context 의 `LazyReusablePool<KiottyNetHttpServer>` 에서 객체를 꺼내 `HttpServer::initialize(id, { configuration, limits })` 를 부른다. `Server::initialize` (Connection 슬롯, Stage) 뒤 `prepare` 가 limits 를 보고 (initialization mark, `max_routes > 0`, `response_bytes ≥ RESPONSE_PREFIX_BYTES + 2`) 표와 Session 풀을 잡는다. 결과는 `{ code, handle }` 이다.
- 등록 함수는 `registry()` / `notices()` 를 거친다. `_runs > 0` 이면 `KIOTTY_NET_CODE_BUSY` 다. 표는 run 중에 IO Thread 가 읽기만 하고 등록은 run 전에만 하므로 lock 이 없다.
- 등록의 검사: 같은 path 의 같은 method 비트가 있으면 `DUPLICATE_RULE`, `max_routes` 에 차면 `TABLE_FULL`. `serveHttpDirectory` 의 prefix 는 `/` 로 끝나야 하고, `serveHttpMemory` 는 목록 안의 type (BY_EXTENSION, EVENT_STREAM, CUSTOM 이 아닌 것) 이어야 하며 텍스트 계열이면 여기서 UTF-8 을 한 번 본다.
- `KiottyNet_destroyHttpServer` 는 Server 와 같은 길이다. 객체는 풀에서 은퇴만 하므로 옛 handle 의 `server` 포인터는 살아 있고 generation 이 거른다 (설계 결정 1). 다시 create 하면 같은 객체의 블록을 (크면) 다시 쓰고 Session 의 generation 은 이어 간다.
- 메모리를 잡지 못하면 process 가 끝난다 (base 와 같다. `OUT_OF_MEMORY` 는 없다).

### 4.2 HttpSession

```cpp
enum class SessionPhase : uint8_t { READING, FILLING, WORKING, STREAMING, SENDING };

struct HttpSession                                    // data/repository/http/kiotty_http_session.h
{
    HttpRequestStore          request;                // 헤더 버퍼, 헤더 span 배열, body 버퍼. KiottyNetHttpRequest 를 채운다 (4.3)
    HttpResponseStore         response;               // 응답 버퍼 (4.4)
    HttpSite&                 site;
    InputFile&                file;                   // 풀이 Session 마다 하나 잡는다. 파일 body (4.7)
    KiottyNetConnectionInfo   info;                   // acquire 때. 끊김 action 에 준다
    KiottyNetHttpRoute        route;                  // 이번 요청에서 onRequest 를 부른 라우트 (not found 라우트 포함). onWorkStarted 를 여기서 찾는다
    KiottyNetConnectionStream stream;                 // WORK_STARTED 가 준 base Stream. Work 의 write 와 finish 가 쓴다
    uint64_t                  body_remaining;         // Content-Length 의 남은 바이트. chunked 는 지금 chunk 의 남은 바이트
    uint64_t                  header_deadline;        // 요청 줄이 온 시각 + header_timeout. 0 이면 무한
    size_t                    sent;                   // 이번 응답에서 write lane 으로 넘긴 바이트 (헤더 블록 + body)
    PoolHandle                id;                     // { generation, index }. response / work handle 의 id (4.5)
    KiottyNetCode             close_reason;           // 응답 뒤 닫을 때 끊김 reason. 거절이면 PROTOCOL_ERROR
    KiottyNetCode             stream_failure;         // Work 스트림의 첫 실패. 다음 write 와 finish 가 본다 (4.9)
    SessionPhase              phase;
    Persistence               persistence;            // KEEP_ALIVE, CLOSE
    bool                      is_work_requested;      // startHttpWork 가 켠다. onRequest 가 return 하면 afterRoute 가 본다
    bool                      is_worker_owned;        // Work 를 열 때 켠다. worker 의 finish 가 handBack 으로 끈다
    bool                      is_orphaned;            // worker 가 들고 있는 동안 IO Thread 가 release 하려 했다
};
```

| phase | 언제부터 | 언제까지 | setter (`HttpWork::fill`) |
|---|---|---|---|
| READING | `beginExchange` (연결, keep-alive 의 다음 요청) | 요청 끝 | BUSY. 이 generation 의 handle 은 아직 아무도 갖지 않았다 |
| FILLING | `dispatch` (요청 끝) | `onRequest` 의 return | 된다 (IO Thread) |
| WORKING | `awaitWork` (Work 를 열 때) | 스트림의 첫 write, 또는 finish | 된다 (worker) |
| STREAMING | 스트림의 첫 write (헤더가 나감) | finish | BUSY |
| SENDING | `startSending`, finish | 다음 `beginExchange` 또는 연결의 끝 | BUSY |

Session 의 버퍼 넷 (헤더, 헤더 span, body, 응답) 은 풀이 create 때 잡은 블록 안을 가리킨다. 블록은 `max_clients × (max_header_bytes + max_body_bytes + response_bytes)` 의 `char` 하나, 헤더 span 은 `max_clients × max_headers` 의 `KiottyNetHttpHeader` 하나다. 곱셈과 덧셈은 `checkedAdd` / `checkedMultiply` 로 먼저 본다. `InputFile` 도 Session 마다 하나를 create 때 잡는다.

acquire / release 는 2.6 의 `createConnectionContext` / `releaseConnectionContext` 다 (`kiotty_http_machine.cpp` 의 `createSession`, `releaseSession` 이 풀로 넘긴다). 풀에는 mutex 하나가 있다. IO Thread 의 acquire / release 와 worker 의 handBack 이 이 안에서 만난다.

```cpp
SessionResult HttpSessionPool::acquire(const KiottyNetConnectionInfo& info)
{
    const std::lock_guard<std::mutex> lock(_mutex);
    if (_free.empty()) { return SessionResult::failure(KIOTTY_NET_CODE_SESSION_EXHAUSTED); }   // createSession 이 NULL 을 돌려주고 Stateful 계층이 닫는다 (2.5)
    const int32_t index = _free.back();
    _free.pop_back();
    HttpSession& session = _sessions[static_cast<size_t>(index)];
    session.info = info;
    return SessionResult::success(session);           // beginExchange 는 CONNECTED 행이 한다 (4.3)
}

void HttpSessionPool::release(HttpSession& session)
{
    const std::lock_guard<std::mutex> lock(_mutex);
    session.is_orphaned = session.is_worker_owned;
    if (session.is_orphaned) { return; }              // worker 가 들고 있다. 그 finish 가 돌려준다 (4.6)
    session.file.close();
    session.id.generation = nextSessionGeneration(session.id.generation);   // 들고 있던 response / work handle 이 모두 무효
    _free.push_back(session.id.index);
}

bool HttpSessionPool::handBack(HttpSession& session)  // worker 의 finish 가 closeStream 전에 부른다
{
    const std::lock_guard<std::mutex> lock(_mutex);
    session.is_worker_owned = false;
    return session.is_orphaned;                       // true 면 finish 가 closeStream 뒤 release 한다
}
```

- HTTP 표의 CLOSED 행 (`onClosed`) 이 끊김 action 을 부르고, StatefulHandler 가 그 뒤 release 를 부른다 (2.5).
- base 의 `max_clients` 와 Session 수가 같으므로 acquire 가 실패하는 일은 정상 경로에서는 없다. 그래도 검사한다.
- generation 은 `INT32_MAX` 다음이 1 이다. handle 을 내주기 전에 `beginExchange` 가 한 번 올리므로 generation 0 인 handle 은 나가지 않는다.

### 4.3 요청 공급 : Stage → HttpSession

HTTP 표는 사용자 프로토콜의 표와 같은 `KiottyNetStateMachine` 이고 `domain/usecases/http/kiotty_http_machine.cpp` 안에 `constexpr` 로 있다 (8절의 C++ `KiottyNet_rules`). `.to(...)` 의 다음 상태가 여섯까지 있어 `KIOTTY_NET_MAX_NEXT` 는 8 이다 (`261112_01_100-continue.md`).

```cpp
constexpr auto HTTP_RULES = KiottyNet_rules(
    KiottyNet_when(CONNECTED).then(onConnected).to(REQUEST_LINE),
    KiottyNet_from(REQUEST_LINE).when(READ).then(onRequestLine).to(HEADER_LINE, SENDING),
    KiottyNet_from(HEADER_LINE).when(READ).then(onHeaderLine).to(HEADER_LINE, BODY, CHUNK_SIZE, CONTINUING, SENDING, WORKING),
    KiottyNet_from(BODY).when(READ).then(onBody).to(BODY, SENDING, WORKING),
    KiottyNet_from(CHUNK_SIZE).when(READ).then(onChunkSize).to(CHUNK_DATA, TRAILER, SENDING),
    KiottyNet_from(CHUNK_DATA).when(READ).then(onChunkData).to(CHUNK_DATA, CHUNK_END, SENDING),
    KiottyNet_from(CHUNK_END).when(READ).then(onChunkEnd).to(CHUNK_SIZE, SENDING),
    KiottyNet_from(TRAILER).when(READ).then(onTrailer).to(TRAILER, SENDING, WORKING),
    KiottyNet_from(CONTINUING).when(WRITTEN).then(onContinued).to(BODY, CHUNK_SIZE),
    KiottyNet_from(WORKING).when(WORK_STARTED).then(onWorkStarted).to(WORKING),
    KiottyNet_from(WORKING).when(WRITTEN).then(onSent).to(SENDING, REQUEST_LINE, CLOSING),
    KiottyNet_from(WORKING).when(READ).then(onRequestLine).to(HEADER_LINE, SENDING),
    KiottyNet_from(SENDING).when(WRITTEN).then(onSent).to(SENDING, REQUEST_LINE, CLOSING),
    KiottyNet_when(TIMEOUT).then(onTimeout).to(CLOSING),
    KiottyNet_when(CLOSED).then(onClosed));
```

| 상태 | entry action | 이벤트가 오면 |
|---|---|---|
| `REQUEST_LINE` | `readUntil(CRLF, lineLimit)` + `header_timeout` (keep-alive 의 다음 요청이면 `keep_alive_timeout`) | READ: `startHeaderClock`, 줄을 헤더 버퍼로 복사하고 method / path / query / version 을 자른다 (`takeRequestLine`). 400 / 414 / 431 / 501 은 `reject` |
| `HEADER_LINE` | `readUntil(CRLF, lineLimit)` + `headerTimeLeft` | READ: 빈 줄이면 `afterHeaders`. 아니면 `takeHeader` (복사하고 `{ name, value }`). 공간이나 개수가 모자라면 431, 형식은 400 |
| `BODY` | `read(min(body_remaining, receive_capacity))` + `header_timeout` | READ: body 버퍼에 이어 붙인다. 남은 것이 0 이면 `dispatch` |
| `CHUNK_SIZE` | `readUntil(CRLF, lineLimit)` | READ: 크기를 읽는다 (확장은 버린다). 0 이면 `TRAILER`. 합이 `max_body_bytes` 를 넘으면 413 |
| `CHUNK_DATA` | `read` 조각 | READ: 이어 붙인다. chunk 가 끝나면 `read(2)` 로 `CHUNK_END` |
| `CHUNK_END` | `read(2)` | READ: CRLF 가 아니면 400 |
| `TRAILER` | `readUntil(CRLF, lineLimit)` | READ: 빈 줄이면 `dispatch`. trailer 는 버린다 |
| `CONTINUING` | `write("HTTP/1.1 100 Continue\r\n\r\n")` | WRITTEN: `readFirstBody` (Content-Length 면 `BODY`, chunked 면 `CHUNK_SIZE`) |
| `WORKING` | `thenWork()` | WORK_STARTED: `startWork` (4.6). WRITTEN: finish 뒤의 조각 (4.7 의 `onSent`). READ: worker 가 다음 요청을 연 경우 (4.9 의 HEAD) |
| `SENDING` | `write(조각)` | WRITTEN 마다 `sendNext` (4.7) |
| `CLOSING` | `close(reason)` | — |

TIMEOUT 은 어느 상태든 `close(TIMEOUT)` 이다 (408 은 보내지 않는다). CLOSED 는 `onClosed` 가 끊김 action 을 부른다.

**헤더는 줄 단위로 읽는다.** `readUntil(CRLFCRLF, max_header_bytes)` 한 번이 더 간단하지만, 그러면 `max_header_bytes ≤ receive_capacity` 여야 한다 (1.2 의 `REQUEST_TOO_LARGE`). 줄 단위로 읽으면 Stage 에 걸리는 조건은 "줄 하나 ≤ receive_capacity" 뿐이고 헤더 전체 크기는 헤더 버퍼가 정한다. 그래서 기본값 (recv 4 KiB, header 8 KiB) 이 그대로 성립한다. `lineLimit = min(receive_capacity, max_header_bytes + 2)` 이고, 줄이 이것을 넘으면 base 가 `KIOTTY_NET_CODE_PROTOCOL_ERROR` 로 닫는다 (응답 없음). 헤더 공간이 모자라면 HttpServer 가 431 을 보낸다.

```cpp
KiottyNetTransition beginExchange(HttpSession& session, KiottyNetConnectionHandle connection, uint32_t wait_milliseconds)
{
    session.id.generation = nextSessionGeneration(session.id.generation);   // 이전 요청의 response / work handle 무효화
    detachFile(session);                                                     // 지난 응답의 파일을 닫는다
    session.request.begin(connection);
    session.response.begin();
    session.body_remaining = 0;
    session.sent = 0;
    session.close_reason = KIOTTY_NET_CODE_SUCCESS;
    session.stream = KiottyNetConnectionStream{ nullptr, 0 };
    session.stream_failure = KIOTTY_NET_CODE_SUCCESS;
    session.is_work_requested = false;
    session.phase = SessionPhase::READING;
    session.persistence = Persistence::KEEP_ALIVE;
    return readLine(session, HttpState::REQUEST_LINE, wait_milliseconds);
}

KiottyNetTransition afterHeaders(HttpSession& session)
{
    const FramingResult framing = session.request.finishHeaders();           // keep_alive 를 정하고 body framing 을 고른다. CL + TE 는 400, chunked 아닌 TE 는 501
    if (!framing) { return reject(session, framing.code); }
    const bool is_kept = session.request.request().keep_alive != 0;
    session.persistence = is_kept ? Persistence::KEEP_ALIVE : Persistence::CLOSE;
    switch (framing.value.kind)
    {
    case BodyKind::NONE:    return dispatch(session);
    case BodyKind::LENGTH:  return afterLength(session, framing.value.length);   // max_body_bytes 를 넘으면 읽기 전에 413. 0 이면 dispatch
    case BodyKind::CHUNKED: return beginBody(session);
    }
    return dispatch(session);
}

KiottyNetTransition beginBody(HttpSession& session)
{
    if (!session.request.expectsContinue()) { return readFirstBody(session); }  // HTTP/1.1 이고 Expect: 100-continue
    return Next::moveToNextState(CONTINUING, thenWrite(CONTINUE_SPAN, 0));
}
```

- `request.path` / `query` / `headers[i].name` / `value` 는 모두 헤더 버퍼 안의 span 이다. 그래서 다음 `beginExchange` 까지, 즉 onRequest 가 return 하거나 Work 가 finish 될 때까지 유효하다.
- body 는 `receive_capacity` 조각으로 받아 body 버퍼에 이어 붙인다. 조각마다 READ 가 오고 같은 상태로 돌아간다 (`BODY → READ → BODY`). 조각의 timeout 은 매번 `header_timeout` 이다.
- 헤더의 timeout 은 요청 줄이 온 때 (`startHeaderClock`) 부터 흐른다. 줄마다 `headerTimeLeft` (남은 시간, 지났으면 1 ms) 를 건다.
- GET / HEAD 에 body 가 와도 같은 길로 읽는다. 요청 경계를 맞추려면 읽어야 한다.
- `reject(session, status)` 는 `response.begin()` 으로 사용자가 놓은 것을 지우고 status 만 놓은 뒤 `persistence = CLOSE`, `close_reason = PROTOCOL_ERROR` 로 `startSending` 에 간다. 자동 응답도 사용자 응답과 같은 버퍼, 같은 송신 경로다.

`dispatch` 가 표를 찾고 사용자 콜백을 부른다.

```cpp
KiottyNetTransition dispatch(HttpSession& session)
{
    session.phase = SessionPhase::FILLING;
    const KiottyNetHttpRequest& request = session.request.request();
    const RouteMatch match = session.site.registry.find(request.method, Span<const char>{ request.path.data, request.path.length });
    if (match.kind == MatchKind::ROUTE) { return callRoute(session, match.route); }   // not found 라우트도 ROUTE 로 온다
    answerAutomatically(session, match);                                     // STATIC (4.7), NOT_FOUND 404, NOT_ALLOWED 405 + Allow, OPTIONS 204 + Allow
    return startSending(session);
}

KiottyNetTransition callRoute(HttpSession& session, const KiottyNetHttpRoute& route)
{
    session.route = route;                                                   // onWorkStarted 를 찾기 위해
    route.onRequest(route.app_context, &session.request.request(), responseHandleOf(session));   // 사용자 코드. setter, startHttpWork
    return afterRoute(session);
}

KiottyNetTransition afterRoute(HttpSession& session)
{
    const bool is_stream = session.response.frame().isStream();
    const bool is_head = session.request.request().method == KIOTTY_NET_HTTP_METHOD_HEAD;
    const bool is_working = session.is_work_requested && !(is_head && is_stream);   // HEAD 의 스트림 라우트는 Work 를 열지 않는다 (설계 결정 16)
    if (is_working) { return awaitWork(session); }                           // phase = WORKING, is_worker_owned = true, { WORKING, thenWork() }
    if (is_stream && !session.is_work_requested) { answerWithoutWork(session); }    // 보낼 사람이 없다. 500 + Connection: close
    return startSending(session);
}
```

`HttpRegistry::find(method, path)` 의 순서다.

1. path 가 `*` (요청 줄은 OPTIONS 에만 허용한다) 면 OPTIONS, Allow 는 표 전체의 method.
2. 등록 순서대로 path 가 맞는 항목 중 method 비트가 맞는 첫 항목. HEAD 는 HEAD 와 GET 비트를 같이 찾는다 (HEAD 를 GET 라우트로). 라우트면 ROUTE, 정적 경로면 STATIC. 지나간 항목의 method 는 `allowed` 에 OR 한다. 그래서 `/` 정적 디렉토리를 라우트 뒤에 등록하면 HEAD 도 GET 과 같이 라우트로 간다.
3. path 가 맞는 항목이 없으면 not found 라우트 (있으면 ROUTE), 없으면 NOT_FOUND.
4. path 는 맞고 method 가 없으면 OPTIONS 는 OPTIONS (204), 나머지는 NOT_ALLOWED (405).

`allowListOf(allowed)` 는 GET 이 있으면 HEAD 를 더하고 OPTIONS 를 늘 더해 `"GET, HEAD, POST, OPTIONS"` 꼴로 만든다 (method enum 순서).

- 사용자 콜백은 IO Thread 에서, Stateful 계층의 transition action 안에서 불린다. 그래서 콜백 안에서 부른 setter 는 2절의 규칙과 충돌하지 않는다. 콜백 안에서 Connection 함수를 직접 부를 길은 없다 (handle 을 주지 않는다).

### 4.4 응답 공급 : setter → 응답 버퍼

응답 버퍼 (`response_bytes`) 의 배치다. `HttpResponseStore` (`data/repository/http/kiotty_http_response_store`) 가 들고, 필드는 `ResponseLayout` 하나로 묶인다.

```
buffer
│← RESPONSE_PREFIX_BYTES (256) →│← 사용자 헤더 (앞에서 뒤로) →│     (빈 공간)     │← 복사된 body (뒤에서 앞으로) →│
│  seal 이 상태줄 + 라이브러리   │  "X-Foo: bar\r\n" ...          │                   │                                 │
│  헤더를 오른쪽 정렬로 쓴다    │                                 │                   │                                 │
^                ^              ^                                 ^                   ^                                 ^
0          header_start   RESPONSE_PREFIX_BYTES               header_end          body_start                       response_bytes
```

```cpp
enum class BodyStorage : uint8_t { COPY, REFERENCE, STREAM, FILE };

struct ResponseLayout
{
    Span<char>               buffer;                  // Session 의 응답 버퍼
    Span<const char>         body;                    // COPY : 버퍼 뒤쪽. REFERENCE : 사용자 포인터. STREAM : seal 뒤 끝 chunk "0\r\n\r\n". FILE : 비어 있다
    uint64_t                 file_length;             // FILE 의 크기 (attachFile 이 연 파일)
    size_t                   header_start;            // seal 전에는 RESPONSE_PREFIX_BYTES
    size_t                   header_end;              // 사용자 헤더의 끝. seal 이 빈 줄 "\r\n" 을 붙인다
    size_t                   body_start;              // COPY 면 body 의 시작, 아니면 response_bytes
    size_t                   sendable_body;           // seal 이 정한다. HEAD 와 1xx / 204 / 304 는 0
    KiottyNetHttpContentType type;
    BodyStorage              storage;
    bool                     has_body;                // body setter 를 불렀는가. 아니면 Content-Type 이 없다
};
```

- `RESPONSE_PREFIX_BYTES` (256) 는 상태줄과 라이브러리 헤더 (`Date`, `Content-Length` 또는 `Transfer-Encoding: chunked`, `Content-Type`, `Connection: close`) 의 자리다. limits 의 `response_bytes` 는 이것 + 64 이상이어야 한다 (configure 의 범위).
- 사용자 헤더는 `header_end` 에서 뒤로 쌓인다. 복사된 body 는 **버퍼 끝에서 앞으로** 놓인다. 그래서 헤더와 body 를 어느 순서로 불러도 서로 밀어내지 않는다. 둘이 만나면 (빈 줄 `\r\n` 자리를 남기고) `KIOTTY_NET_CODE_BUFFER_FULL` 이다.
- 송신 직전 `seal` 이 상태줄과 라이브러리 헤더를 stack 의 `PrefixText` 에 만들고, 그 길이만큼 `RESPONSE_PREFIX_BYTES` 에서 **오른쪽 정렬**로 복사한다. 그러면 `[header_start, header_end)` 가 빈 줄까지 포함한 연속된 헤더 블록이 된다. 송신은 이 블록 뒤에 body 를 이어 보낸다.

```cpp
KiottyNetCode HttpResponseStore::addHeader(Span<const char> name, Span<const char> value)
{
    if (!isToken(name) || isReservedName(name)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }   // Content-Length, Transfer-Encoding, Connection, Date, Content-Type (대소문자 무시)
    if (!isFieldValue(value)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }                      // 탭 밖의 제어 문자, DEL. CR / LF 가 여기서 걸린다
    if (!isUtf8(value)) { return KIOTTY_NET_CODE_INVALID_ENCODING; }
    const size_t needed = name.length + value.length + 4;                                       // "name: value\r\n"
    if (needed > headerRoom()) { return KIOTTY_NET_CODE_BUFFER_FULL; }                          // body_start - header_end - 2
    writeField(Span<char>{ _layout.buffer.data + _layout.header_end, needed }, name, value);
    _layout.header_end += needed;
    _is_touched = true;                                                                         // setter 가 하나라도 불리면 기본 200
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetCode HttpResponseStore::setBody(KiottyNetHttpContentType type, KiottyNetSpan data, BodyStorage storage)
{
    const bool is_inline = storage == BodyStorage::COPY || storage == BodyStorage::REFERENCE;
    if (is_inline && data.data == nullptr && data.length > 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
    const KiottyNetCode checked = checkBodyType(type, storage);                                 // 4.10
    if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
    const Span<const char> body{ data.data, is_inline ? data.length : 0 };
    if (textKindOf(type) != TextKind::BINARY && !isUtf8(body)) { return KIOTTY_NET_CODE_INVALID_ENCODING; }
    const bool is_copy = storage == BodyStorage::COPY;
    if (is_copy && body.length > _layout.buffer.length - _layout.header_end - 2) { return KIOTTY_NET_CODE_BUFFER_FULL; }
    _layout.body_start = is_copy ? _layout.buffer.length - body.length : _layout.buffer.length;
    // COPY 면 body_start 에 복사. 이전 body 는 대체된다 (넷 중 하나만)
    _layout.file_length = storage == BodyStorage::FILE ? data.length : 0;                      // FILE 은 data.length 로 크기를 받는다
    _layout.type = type;
    _layout.storage = storage;
    _layout.has_body = true;
    _is_touched = true;
    return KIOTTY_NET_CODE_SUCCESS;
}

void HttpResponseStore::seal(const ResponseSeal& seal)                                         // { method, persistence }
{
    const int32_t status = finalStatus();                                                      // 놓은 status, 아니면 setter 가 있었으면 200, 없었으면 404
    const bool is_bodiless = isBodiless(status);                                               // 1xx, 204, 304
    _layout.body = sealedBodyOf(_layout, is_bodiless);                                         // STREAM 이면 끝 chunk "0\r\n\r\n" (bodiless 면 비움)
    PrefixText prefix;
    appendStatusLine(prefix, status);                                                          // "HTTP/1.1 200 OK"
    appendDateField(prefix, currentUtcTime());                                                 // IMF-fixdate
    if (!is_bodiless) { appendFraming(prefix, _layout); }                                      // STREAM 이면 Transfer-Encoding: chunked, 아니면 Content-Length (FILE 은 파일 크기)
    if (_layout.has_body && !is_bodiless) { appendField(prefix, "Content-Type", contentTypeText()); }
    if (seal.persistence == Persistence::CLOSE) { appendField(prefix, "Connection", "close"); }
    _layout.header_start = RESPONSE_PREFIX_BYTES - prefix.length();                           // 오른쪽 정렬
    // prefix 를 header_start 에 복사하고 header_end 에 빈 줄 "\r\n"
    const bool is_head = seal.method == KIOTTY_NET_HTTP_METHOD_HEAD;
    _layout.sendable_body = is_head || is_bodiless ? 0 : bodyLength(_layout);                  // HEAD 는 Content-Length 만 적고 body 를 보내지 않는다
}
```

- 공개 setter 는 handle 을 Session 으로 바꾸고 (4.5) `HttpResponseStore` 를 부른다. 번역 층이 이름 / 값을 `parseText` (상한 `response_bytes`) 로 먼저 본다. `setStatus` 는 100..599 만 받는다.
- 자동 응답 (404, 405 + Allow, 204 + Allow, 403 / 404 의 정적 경로) 과 거절 (400 / 413 / 414 / 431 / 501) 도 같은 store 를 쓴다. 다른 길이 없다.
- HEAD : 라우트는 GET 으로 실행되고 setter 도 다 동작한다. `seal` 이 `Content-Length` 를 GET 과 같게 쓰되 `sendable_body` 가 0 이라 송신 (4.7) 이 body 조각을 내보내지 않는다.
- 송신 쪽은 `frame()` 이 주는 `ResponseFrame` 으로 읽는다. `chunkAt(offset, max)` (헤더 블록 → body 순서의 조각), `filePieceAt` (4.7), `isStream`, `hasSendableBody`, `checkChunk`, `frameChunk` (4.9).

### 4.5 handle : `{ server, id }`

`KiottyNetHttpResponseHandle` 과 `KiottyNetHttpWorkHandle` 은 둘 다 `{ KiottyNetHttpServerHandle server; int64_t id; }` 다 (설계 결정 1, `design_statemachine_v0.md` 12.6). `server` 는 HttpServer 객체의 주소이고 `id` 는 Session 의 `PoolHandle { generation, index }` 를 `value()` 로 묶은 값이다. 소유자는 `server` 가 바로 준다. Work handle 을 따로 둔 이유는 사용자가 base 의 `KiottyNet_closeConnectionStream` 으로 HTTP 프레이밍을 건너뛰지 못하게 하려는 것이다.

```cpp
KiottyNetHttpResponseHandle responseHandleOf(const HttpSession& session)          // callRoute, startWork 가 사용자에게 준다
{
    return KiottyNetHttpResponseHandle{ session.site.owner, session.id.value() };
}

SessionResult HttpSessionPool::find(int64_t id)                                   // 모든 response / work 함수의 첫 줄
{
    const PoolHandle handle = PoolHandle::fromValue(id);
    const bool is_inside = handle.index >= 0 && static_cast<size_t>(handle.index) < _sessions.size();
    if (!is_inside) { return SessionResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
    HttpSession& session = _sessions[static_cast<size_t>(handle.index)];
    if (session.id.generation != handle.generation) { return SessionResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }   // 이전 요청, 닫힌 연결
    return SessionResult::success(session);
}

SessionResult HttpWork::fill(int64_t id)                                          // setter 일곱의 길 (C API 의 fillingOf)
{
    const SessionResult found = _site.sessions.find(id);
    if (!found) { return found; }
    const SessionPhase phase = found.value().phase;
    if (phase != SessionPhase::FILLING && phase != SessionPhase::WORKING) { return SessionResult::failure(KIOTTY_NET_CODE_BUSY); }   // 헤더가 나갔다
    return found;
}
```

- C API 는 `response.server == NULL` 이면 `PEER_CLOSED` 를 돌려주고, 아니면 `response.server->work()` (`HttpWork`) 로 넘긴다. Work 함수는 `HttpWork::working(id)` 를 거친다. phase 가 WORKING / STREAMING 이고 stream 이 있어야 하며, 아니면 `BUSY` 다 (finish 뒤, Work 가 아닌 요청).
- generation 은 요청마다 (`beginExchange`), 연결이 닫힐 때 (풀의 `release`) 오른다. 그래서 keep-alive 로 같은 Session 이 다음 요청을 받으면 이전 handle 은 자동으로 무효다. worker 가 뒤늦게 setter 를 부르면 `KIOTTY_NET_CODE_PEER_CLOSED` 만 받는다.
- 소유자 (HttpServer) 는 handle 의 `server` 다. 전역 소유자 표가 필요 없다. HttpServer 를 destroy 해도 객체는 Context 의 풀에서 은퇴만 하므로 포인터는 살아 있고 generation 이 거른다.
- generation 을 쓰는 것은 IO Thread (`beginExchange`, 풀의 `release`) 와, worker 의 finish 두 경우다. orphan 이면 finish 가 `release` 를 부르고 (4.6), 헤더만 나간 스트림이면 finish 가 `beginExchange` 로 다음 요청을 연다 (4.9). worker 는 `find` 에서 읽기만 한다. Work 중에는 IO Thread 가 이 Session 의 generation 을 바꾸지 않는다. Work 를 열 때 `is_worker_owned` 를 켜고 IO Thread 의 release 는 그것을 보고 미루기 때문이다 (4.2). Work 가 아닐 때 worker 가 handle 을 들고 있다면 그것은 사용자 규칙 위반이다.

### 4.6 Work

Work 는 `context/kiotty_http_work` 의 `HttpWork` 가 한다 (`HttpServer::work()` 가 `HttpSite` 를 들고 만든다). Work stream 의 write 와 close 는 `stream.owner` (Endpoint, 곧 이 HttpServer) 로 부른다.

```cpp
KiottyNetCode HttpWork::start(int64_t id)                                         // KiottyNet_startHttpWork
{
    const SessionResult found = _site.sessions.find(id);
    if (!found) { return found.code; }
    HttpSession& session = found.value();
    const bool is_first = session.phase == SessionPhase::FILLING && !session.is_work_requested;
    if (!is_first) { return KIOTTY_NET_CODE_BUSY; }                               // onRequest 안에서만, 한 번만
    if (session.route.onWorkStarted == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
    session.is_work_requested = true;                                             // 실제 Work 는 onRequest 가 return 한 뒤 afterRoute 가 연다 (4.3)
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetTransition startWork(HttpSession& session, KiottyNetConnectionStream stream)   // (WORKING, WORK_STARTED) 행
{
    session.stream = stream;
    const KiottyNetHttpRoute route = session.route;
    const KiottyNetHttpWorkHandle work{ session.site.owner, session.id.value() };
    route.onWorkStarted(route.app_context, &session.request.request(), responseHandleOf(session), work);   // 사용자가 handle 둘을 worker 로 넘긴다
    return Next::moveToNextState(HttpState::WORKING, thenWork());                // stream 은 worker 의 것 (2.4). 이 뒤로 IO Thread 는 Session 을 건드리지 않는다
}

KiottyNetCode HttpWork::finishWithResponse(int64_t id)                            // KiottyNet_finishHttpWorkWithResponse
{
    const SessionResult found = working(id);                                      // WORKING / STREAMING 이 아니면 BUSY (두 번째 finish)
    if (!found) { return found.code; }
    HttpSession& session = found.value();
    const SessionPhase phase = session.phase;
    const KiottyNetConnectionStream stream = session.stream;
    session.phase = SessionPhase::SENDING;
    const KiottyNetConnectionAction then = finishingActionOf(session, phase);     // 아래
    return handOver(session, stream, then);
}

KiottyNetCode HttpWork::finishWithClose(int64_t id, KiottyNetCode reason)
{
    const SessionResult found = working(id);
    if (!found) { return found.code; }
    HttpSession& session = found.value();
    const KiottyNetConnectionStream stream = session.stream;
    session.phase = SessionPhase::SENDING;
    return handOver(session, stream, thenClose(reason));                          // 끝 chunk 없이 닫는다. reason 이 끊김 action 의 reason
}

KiottyNetCode handOver(HttpSession& session, KiottyNetConnectionStream stream, const KiottyNetConnectionAction& then)
{
    HttpSessionPool& sessions = session.site.sessions;
    const bool is_orphaned = sessions.handBack(session);                          // 소유를 내려놓고, IO Thread 가 그사이 release 하려 했는지 받는다
    const KiottyNetCode closed = stream.owner->closeStream(stream.id, then);      // closeConnectionStream(stream, then)
    if (is_orphaned) { sessions.release(session); }                               // 미뤄 둔 반납
    return closed;
}

bool HttpWork::isConnected(int64_t id)
{
    const SessionResult found = working(id);
    if (!found) { return false; }
    if (_site.is_closing.load()) { return false; }                                // 서버 close 중
    const KiottyNetConnectionStream stream = found.value().stream;
    return stream.owner->isStreamOpen(stream.id);
}
```

`finishingActionOf(session, phase)` 가 finish 때 stream 에 걸 action 을 고른다.

| finish 전 phase | 상태 | action |
|---|---|---|
| WORKING | 아무것도 보내지 않았다 (완성 응답, 또는 chunk 를 쓰지 않은 스트림) | `seal` 하고 첫 조각 `thenWrite(chunkAt(0, send_capacity))`. 스트림이면 헤더 + 끝 chunk 가 나간다 |
| STREAMING | 스트림이 열려 있다 (`stream_failure == SUCCESS`) | 끝 chunk 부터 `thenWrite(chunkAt(헤더 길이, send_capacity))` |
| STREAMING | `CLOSED` 로 끝났고 보낼 body 가 없다 (HEAD, 1xx / 204 / 304) | `nextExchangeOf`: keep-alive 면 `beginExchange` 의 `thenReadUntil`, 아니면 `thenClose(SUCCESS)` (4.9) |
| STREAMING | 그 밖의 실패 | `thenClose(stream_failure)` |

- 나머지 조각은 `(WORKING, WRITTEN)` 행의 `onSent` 가 완성 응답과 같은 길 (`sendNext`, 4.7) 로 잇는다. 첫 조각은 `send_capacity` 이하이고 write lane 은 비어 있으므로 `closeConnectionStream(thenWrite)` 가 `BUFFER_FULL` 을 돌려주는 경우는 없다.
- Work 중 worker 가 setter 를 부르는 것이 안전한 이유는 base 의 Work 규칙 그대로다. WORKING 동안 IO Thread 는 이 Connection 에 콜백을 내지 않고 write lane 도 건드리지 않는다. Session 의 응답 필드는 write lane 과 같은 소유 규칙을 따른다. `onRequest` 동안은 IO Thread, Work 중이면 worker, 동시에 둘인 순간은 없다.
- Work 중 peer 가 끊거나 서버가 close 해도 Session 은 worker 의 finish 까지 풀로 돌아가지 않고 generation 도 그대로다 (아래 소유 표시). 그래서 finish 전까지 worker 의 setter 와 write 는 안전하고, 끊김은 write 의 코드로 안다.

**Work 중 서버 close (H4 에서 고친 것).** base 는 서버 close 때 Work 중인 연결에도 CLOSED 를 곧바로 dispatch 하고 (`Connection::enterClosing`), stream 은 finish 까지 살려 둔다. 그래서 HTTP 의 `onClosed` (끊김 action `CLOSED`) 와 그 뒤의 release 가 worker 의 finish 보다 먼저 온다. release 가 그 자리에서 generation 을 올리면 worker 의 finish 가 `PEER_CLOSED` 로 끝나 `closeConnectionStream` 을 부르지 못하고, base 는 Work 를 영원히 기다린다 (run 이 돌아오지 않는다). 그래서 Session 의 소유를 표시한다.

1. Work 를 열 때 (`awaitWork`) `is_worker_owned = true`.
2. IO Thread 의 release 는 `is_worker_owned` 면 `is_orphaned` 만 켜고 돌려주지 않는다. generation 도 그대로라 worker 의 handle 이 산다.
3. worker 의 finish 는 `handBack` (소유를 내려놓고 orphan 여부를 받음) → `closeConnectionStream` → orphan 이면 `release`.

셋 다 풀의 mutex 안이다 (acquire 도). 그래서 run 은 모든 Work 가 finish 된 뒤 `CLOSED` 로 돌아온다. 서버 close 중 worker 의 write 는 `CLOSED` 를 받는다 (4.9).

### 4.7 송신 : 응답 버퍼 → write lane 조각

```cpp
KiottyNetTransition startSending(HttpSession& session)        // IO Thread 경로 (Work 없이 onRequest 가 return 한 경우, 자동 응답, 거절)
{
    session.phase = SessionPhase::SENDING;
    session.response.seal(ResponseSeal{ session.request.request().method, session.persistence });
    session.sent = 0;
    const Span<const char> chunk = session.response.frame().chunkAt(0, session.site.send_capacity);   // 첫 조각은 헤더 블록
    return Next::moveToNextState(HttpState::SENDING, thenWrite(KiottyNetSpan{ chunk.data, chunk.length }, 0));
}

KiottyNetTransition sendNext(HttpSession& session, KiottyNetConnectionHandle connection)   // onSent: session.sent += event.length 뒤
{
    const PieceResult piece = pieceAt(session, session.sent);                     // 다음 조각. 파일이면 여기서 읽는다
    if (!piece) { return closeWith(piece.code); }                                 // 파일을 읽지 못함 : NOT_FOUND 로 닫는다
    const Span<const char> chunk = piece.value;
    if (!chunk.empty()) { return Next::moveToNextState(HttpState::SENDING, thenWrite(KiottyNetSpan{ chunk.data, chunk.length }, 0)); }
    if (session.persistence != Persistence::KEEP_ALIVE) { return closeWith(session.close_reason); }   // SUCCESS, 거절이면 PROTOCOL_ERROR
    return beginExchange(session, connection, session.site.limits.keep_alive_timeout_milliseconds);   // 다음 요청. 파일은 여기서 닫힌다
}

PieceResult pieceAt(HttpSession& session, size_t offset)      // data/repository/http/kiotty_http_file_body
{
    const size_t capacity = session.site.send_capacity;
    const ResponseFrame frame = session.response.frame();
    const FilePiece piece = frame.filePieceAt(offset, capacity);
    if (!piece.is_file) { return PieceResult::success(frame.chunkAt(offset, capacity)); }   // 헤더 블록, 복사 / 참조 body, 끝 chunk
    const FileReadResult read = session.file.read(piece.file_offset, piece.target);       // 응답 버퍼 앞부터 읽는다. 헤더 블록은 이미 나갔다
    if (!read || read.value != piece.target.length) { return PieceResult::failure(KIOTTY_NET_CODE_NOT_FOUND); }
    return PieceResult::success(Span<const char>{ piece.target.data, piece.target.length });
}
```

- `chunkAt(offset, max)` 는 `offset` 이 헤더 블록 안이면 그 나머지, 넘으면 `body.skip(offset - 헤더 길이)` 의 최대 `max` 바이트다. body 는 `sendable_body` 까지라 HEAD 와 1xx / 204 / 304 는 헤더 블록에서 끝난다. FILE 이면 body 쪽은 비어 있고 `filePieceAt` 이 대신한다.
- `filePieceAt(offset, max)` 는 FILE 이고 `offset` 이 헤더 블록을 지났을 때 `{ 응답 버퍼 앞 min(남은 파일, max, response_bytes) 바이트, 파일 offset }` 을 준다. 읽기 자리는 응답 버퍼의 앞부분 (prefix + 헤더 영역) 을 다시 쓰므로 추가 메모리가 없다.
- 조각이 하나뿐이면 `write → WRITTEN → 다음 요청` 으로 끝난다. 큰 응답은 `SENDING → WRITTEN → SENDING` 을 돈다. 1.6 의 write lane 규칙 (한 번에 메시지 하나, onWritten 뒤 다음 write) 을 그대로 지킨다.
- `write` 의 data 는 `send_capacity` 이하이고 write lane 은 비어 있으므로 (이전 WRITTEN 뒤) `KIOTTY_NET_CODE_BUFFER_FULL` 로 닫히는 일은 없다.
- `sendfile` / `TransmitFile` 은 쓰지 않는다 (설계 결정 6). Stage 를 거치지 않는 OS 경로라 base 의 drain, TLS 와 맞지 않는다.

정적 경로와 `setHttpFile`.

- `attachFile(session, type, path)` 가 Session 의 `InputFile` 을 열고 (실패하면 `NOT_FOUND`), 크기를 `size_t` 로 담을 수 있는지 보고, BY_EXTENSION 이면 경로의 확장자로 type 을 고른 뒤 `setBody(type, { nullptr, size }, FILE)` 한다. setBody 가 실패하면 (EVENT_STREAM 등) 파일을 닫는다.
- `answerStatic` (`domain/usecases/http/kiotty_http_static`): MEMORY 는 참조 body (`REFERENCE`). FILE 은 `attachFile`, 실패하면 `response.begin()` 뒤 404. DIRECTORY 는 prefix 뒤의 나머지를 `isSafeRelativePath` 로 보고 (`..` 조각, `\`, `:`, NUL 이면 403), 디렉토리 경로에 붙인 `FixedString<KIOTTY_NET_MAX_PATH_LENGTH>` 를 연다. 붙이지 못하면 (길이) 404.
- `setHttpFile` 은 번역 층이 `parseText` 와 `hasParentSegment` (`/`, `\` 로 나눈 `..` 조각) 를 본 뒤 `attachFile`. Work 중에도 같은 길이다.
- 파일은 다음 요청 (`beginExchange` 의 `detachFile`) 이나 반납 (풀의 `release`) 때 닫는다.

### 4.8 run

```cpp
KiottyNetCode HttpServer::run(const KiottyNetAddress& address, const KiottyNetTlsServerConfiguration* tls)
{
    _runs.fetch_add(1);                                                           // 등록 함수가 BUSY 를 돌려주기 시작한다
    _site.is_closing.store(false);
    const KiottyNetStatefulAppContext stateful = httpStatefulOf(_site);           // { &site, &HTTP_MACHINE, createSession, releaseSession, reportError }
    const StatefulHandler handler(stateful, *this);                               // run 의 stack 에만 산다 (2.1)
    const KiottyNetCode code = Server::run(handler, address, tls);
    _runs.fetch_sub(1);
    return code;
}

void HttpServer::close()
{
    _site.is_closing.store(true);                                                 // Work 의 write 가 PEER_CLOSED 대신 CLOSED 를 돌려주게 (4.9)
    Server::close();
}
```

- `KiottyNet_runHttpServer` 는 server / address NULL 을 거르고 주소를 stack 에 복사해 넘긴다. 주소 / TLS 검사, `RunGate`, SSL_CTX 는 base 의 run 그대로다 (1.7).
- `reportError` 는 base 의 onError (TLS handshake 실패, accept 실패, `SESSION_EXHAUSTED`) 를 오류 action 으로 넘긴다. 끊김 action 은 CLOSED 행의 `onClosed` 가 Session 의 `info` 와 event 의 reason 으로 부른다.
- `await` 는 base 그대로다.

HttpServer 가 base 에 더 요구하는 것은 없다. 쓰는 것은 `Server` 의 initialize / run / close / await, action 함수 (`then*`), Endpoint 의 `writeStream`, `closeStream`, `isStreamOpen` (`KiottyNet_writeConnectionStream`, `closeConnectionStream`, `isConnectionStreamOpen` 의 안쪽) 뿐이다.

### 4.9 Work 스트림

`setHttpBodyStream(response, type)` 은 `setBody(type, { nullptr, 0 }, STREAM)` 이다. 헤더는 아직 나가지 않는다. 스트림을 보내는 것은 worker 의 `HttpWork::write` 다.

```cpp
KiottyNetResult HttpWork::write(int64_t id, KiottyNetSpan data, uint32_t timeout_milliseconds)   // KiottyNet_writeHttpWorkChunk
{
    const SessionResult found = working(id);                                      // WORKING / STREAMING 이 아니면 BUSY
    if (!found) { return resultOf(0, found.code); }
    HttpSession& session = found.value();
    const Span<const char> piece{ data.data, data.length };
    if (piece.length > MAX_CHUNK_BYTES) { return resultOf(0, KIOTTY_NET_CODE_INVALID_ARGUMENT); }   // bytes 가 int32_t 라 0x7FFFFFFF 까지
    const KiottyNetCode checked = session.response.frame().checkChunk(piece);    // 스트림이 아님, 빈 data : INVALID_ARGUMENT. 텍스트 계열의 UTF-8 아님 : INVALID_ENCODING
    if (checked != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, checked); }
    if (session.stream_failure != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, session.stream_failure); }   // 한 번 실패하면 계속 그 코드
    const Deadline deadline(timeout_milliseconds);                               // 호출 전체에 하나. 0 은 무한
    const size_t kept = openStream(session);                                     // 첫 호출이면 seal, phase = STREAMING, 헤더 블록 길이. 아니면 0
    const bool has_body = session.response.frame().hasSendableBody();
    const KiottyNetCode sent = has_body ? sendChunk(session, piece, kept, deadline)
                                        : sendHeaderOnly(session, kept, deadline);   // HEAD, 1xx / 204 / 304 : 헤더만 보내고 CLOSED
    session.stream_failure = failureOf(session, sent);
    return resultOf(sent == KIOTTY_NET_CODE_SUCCESS ? piece.length : 0, session.stream_failure);
}

KiottyNetCode sendChunk(HttpSession& session, Span<const char> data, size_t kept, const Deadline& deadline)
{
    const Span<const char> framed = session.response.frame().frameChunk(data, kept, session.site.send_capacity);
    if (framed.empty()) { return sendPieces(session, data, kept, deadline); }     // 다 들어가지 않으면 조각 넷을 차례로
    return sendSpan(session, framed, deadline);                                   // 헤더 + 머리 + data + 꼬리를 write 한 번
}

KiottyNetCode sendPieces(HttpSession& session, Span<const char> data, size_t kept, const Deadline& deadline)
{
    const ChunkHead head = chunkHeadOf(data.length);                              // 길이 hex + CRLF
    const Span<const char> pieces[] = { session.response.frame().chunkAt(0, kept), head.span(), data, Span<const char>{ "\r\n", 2 } };
    for (const Span<const char>& piece : pieces)                                  // 빈 조각 (kept == 0 의 헤더) 은 건너뛴다
    {
        const KiottyNetCode sent = sendSpan(session, piece, deadline);
        if (sent != KIOTTY_NET_CODE_SUCCESS) { return sent; }
    }
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetCode sendSpan(const HttpSession& session, Span<const char> data, const Deadline& deadline)
{
    if (data.empty()) { return KIOTTY_NET_CODE_SUCCESS; }
    if (deadline.expired()) { return KIOTTY_NET_CODE_TIMEOUT; }
    const uint32_t left = deadline.remaining();
    const uint32_t wait = deadline.limited() && left < 1 ? 1 : left;            // 남은 시간이 0 ms 로 잘리면 1 ms (0 은 무한이라)
    return session.stream.owner->writeStream(session.stream.id, KiottyNetSpan{ data.data, data.length }, wait).code;
}

KiottyNetCode failureOf(const HttpSession& session, KiottyNetCode code)           // base 의 코드를 HTTP 의 뜻으로
{
    const bool is_lost = code == KIOTTY_NET_CODE_PEER_CLOSED || code == KIOTTY_NET_CODE_SOCKET_ERROR;
    if (!is_lost) { return code; }
    if (session.site.is_closing.load()) { return KIOTTY_NET_CODE_CLOSED; }      // 서버 close 중 : FIN 뒤의 EPIPE 를 base 는 PEER_CLOSED 로 준다
    const bool is_open = session.stream.owner->isStreamOpen(session.stream.id);
    return is_open ? code : KIOTTY_NET_CODE_PEER_CLOSED;                          // 상대가 사라짐 : RST 를 base 는 SOCKET_ERROR 로 준다
}
```

- **조립.** `frameChunk(data, kept, limit)` 는 응답 버퍼의 `header_start` 부터 `room = min(response_bytes - header_start, send_capacity)` 안에 `[헤더 블록 kept 바이트][머리][data][CRLF]` 가 들어가면 머리 / data / 꼬리를 헤더 블록 뒤에 복사하고 그 span 을 준다. 첫 write 의 헤더 블록은 이미 그 자리에 있으므로 복사하지 않는다. 들어가지 않으면 빈 span 이고 `sendPieces` 가 헤더 / 머리 / data / 꼬리를 넷의 write 로 보낸다. data 는 사용자 버퍼에서 바로 나간다.
- **timeout.** `Deadline` 하나가 그 호출의 write 전부에 걸친다. 그래서 `TIMEOUT` 이면 일부가 나갔을 수 있다. 그 뒤는 `finishHttpWorkWithClose` 뿐이다.
- **끼어듦.** 조각 넷 사이에 다른 바이트가 끼지 않는다. Work 중에는 base 의 drain 이 이 연결의 send / notify Stage 를 고르지 않는다 (`SendFlow::isBlocked`, 설계 결정 7).
- **실패의 기억.** 첫 실패 코드를 `stream_failure` 에 남긴다. 다음 write 는 보내지 않고 같은 코드를 돌려주며, finish 는 그 코드로 닫는다 (4.6 의 표).
- **HEAD.** onRequest 가 스트림을 정하고 `startHttpWork` 한 HEAD 는 `afterRoute` 가 Work 를 열지 않고 `startSending` 으로 헤더만 보낸다 (4.3). worker 가 스트림을 정한 HEAD 는 `sealFor` 가 `sendable_body = 0` 으로 두므로 첫 write 가 `sendHeaderOnly` 로 헤더만 보내고 `CLOSED` 를 돌려준다. 그 뒤 finish 는 `nextExchangeOf` 로 worker thread 에서 `beginExchange` 를 하고 `closeConnectionStream(thenReadUntil)` 로 다음 요청을 연다. 그래서 `(WORKING, READ)` 행이 있다.
- **끝.** `finishHttpWorkWithResponse` 의 끝 chunk 는 `seal` 이 body 자리에 둔 `"0\r\n\r\n"` 이다. chunk 를 한 번도 쓰지 않은 스트림은 WORKING 에서 finish 하므로 `seal` 뒤 헤더 + 끝 chunk 가 완성 응답처럼 나간다.
- **Work 없는 스트림.** `afterRoute` 가 `answerWithoutWork` 로 응답을 비우고 500 + `persistence = CLOSE` 로 보낸다. `close_reason` 은 SUCCESS 그대로다.

### 4.10 content type

content type 의 표는 `domain/usecases/http/kiotty_http_content_type.cpp` 의 `constexpr` 배열 둘이다. enum 순서와 같은 `CONTENT_TYPES[] = { type, 문자열, TextKind }` (`static_assert` 로 개수와 순서를 본다), 확장자 → enum 의 `EXTENSIONS[]`. 할당은 없다.

| TextKind | 값 | 하는 일 |
|---|---|---|
| `UTF8_WITH_CHARSET` | TEXT_*, JAVASCRIPT, EVENT_STREAM | 문자열에 `; charset=utf-8` 이 들어 있다. UTF-8 을 본다 |
| `UTF8` | JSON 계열, XML, YAML, FORM_URLENCODED, DICOM_JSON / XML, FHIR_JSON / XML, SVG | UTF-8 만 본다. 문법은 보지 않는다 |
| `BINARY` | 나머지 | 보지 않는다 |

```cpp
bool isNamedType(KiottyNetHttpContentType type, BodyStorage storage)               // response store 의 checkBodyType 이 먼저 부른다
{
    const bool is_listed = type >= KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM && type < KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION;
    const bool is_event_stream = type == KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM;
    return is_listed && (storage == BodyStorage::STREAM || !is_event_stream);      // EVENT_STREAM 은 스트림에만
}
// checkBodyType : isNamedType 이면 SUCCESS, CUSTOM 이고 이름이 정해졌으면 SUCCESS, 아니면 INVALID_ARGUMENT
```

- `BY_EXTENSION` 은 `isNamedType` 을 지나지 못한다. 파일은 `attachFile` 이 `setBody` 전에 `contentTypeOfPath` 로 바꿔 넘기므로 store 에는 늘 목록 안의 값이 들어간다. `contentTypeOfPath` 는 마지막 `.` 뒤를 대소문자 무시로 `EXTENSIONS` 에서 찾고, 없으면 OCTET_STREAM 이다.
- CUSTOM 의 이름은 response store 의 `FixedString<KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH>` 에 둔다. `parseCustomContentType` 이 `parseText` (UTF-8, 길이) 뒤 `token/token` 과 `; 이름=값` (값은 token 또는 quoted-string) 의 반복을 보고, CR / LF 를 포함한 다른 글자가 있으면 `INVALID_ARGUMENT` 다. `response.begin()` (다음 요청, 거절) 이 지운다.
- Content-Type 은 `seal` 이 `has_body` 이고 bodiless status 가 아닐 때 한 줄 쓴다. CUSTOM 이면 그 이름, 아니면 표의 문자열이다. `setHttpHeader` 의 예약 이름에 `Content-Type` 이 있어 두 번 쓰일 일이 없다.
- 텍스트 계열의 UTF-8 검사 자리는 셋이다. body setter (`setBody`, 복사 / 참조), Work 스트림의 chunk (`checkChunk`), `serveHttpMemory` 의 등록 (`checkMemory`). 파일과 CUSTOM 은 보지 않는다.

---

## 5. WebSocket : HttpSession 위의 frame 행

[requirement_v0.md](requirement_v0.md) 10, 11절과 [design_websocket_v0.html](../design/design_websocket_v0.html) 6, 7절을 코드로 옮긴 것이다 (cycle W2 ~ W4). 새 Session 종류도 새 버퍼 묶음도 없다. 4절의 HttpSession 이 Upgrade 뒤에 frame 상태를 들고, HTTP 표에 frame 행을 더한다. 서버와 클라이언트가 같은 frame 행을 쓰고 차이는 `HttpSite::role` 하나다.

### 5.1 어디에 무엇이 있나

| 층 | 파일 | 하는 일 |
|---|---|---|
| core | `kiotty_sha1`, `kiotty_base64` | `Sec-WebSocket-Accept` 와 키. 할당 없음 |
| domain/usecases/websocket | `kiotty_websocket_frame` | frame 머리 파싱 / 만들기 (최대 14 바이트), `applyMask(span, key, offset)` |
| | `kiotty_websocket_close` | close payload 파싱 / 만들기, 보낼 수 있는 status |
| | `kiotty_websocket_handshake` | `isWebSocketKey`, `acceptOf` |
| | `kiotty_websocket_upgrade` | 서버의 Upgrade 검사와 101 (`answerUpgrade`), 클라이언트의 101 검사 (`isUpgradeResponse`), subprotocol 선택 |
| | `kiotty_websocket_rows` | 서버 / 클라이언트가 같이 쓰는 frame 행 (`onFrameStart` ~ `onSocketTimeout`), `openSocket`, `endSocket` |
| | `kiotty_websocket_timing` | 다음 read 의 timeout, `onClose` 의 `{ code, status, reason }` |
| | `kiotty_websocket_lane` | push lane 으로 frame 하나 (`pushFrame`), 닫힘 플래그 + close frame (`pushClose`) |
| | `kiotty_websocket_access` | 공개 함수의 몸 (info, header, select, send, close, broadcast) |
| | `kiotty_websocket_client_machine` | 클라이언트의 표 (Upgrade 요청 → 상태줄 → 헤더 줄 → frame 행) |
| repository/websocket | `kiotty_websocket_socket` | `SocketState` (IO thread 만), `SocketLink` (다른 thread 도), `WebSocketTarget`, `ClientUpgrade`, `PushLane` |
| context | `KiottyNetWebSocketEndpoint` | handle 의 owner. 소켓 함수 다섯의 가상 함수. HttpServer 와 WebSocketClient 가 상속 |
| context | `WebSocketClient` | `EventClient` + `PushLane` + `KiottyNetWebSocketEndpoint` |

Session 에 더한 것은 셋이다. 메모리는 모두 create 때 Session 과 같이 잡힌다.

```cpp
struct HttpSession {
    // 4.2 의 필드들
    SocketLink&  link;      // pool 의 links 배열의 한 칸. 다른 thread 가 atomic 으로 읽는다
    SocketState  socket;    // IO thread 만 읽고 쓴다
    SessionPhase phase;     // UPGRADING (onUpgrade 안), SOCKET (onOpen 뒤) 을 더했다
};

struct SocketLink {                      // 다른 thread 의 send / close / broadcast 가 보는 것
    std::atomic<int64_t>  socket;        // 열린 소켓의 handle id. 0 이면 열려 있지 않다
    std::atomic<int64_t>  connection;    // base 의 connection id (push lane 의 주소)
    std::atomic<int32_t>  path;          // 등록 경로 번호 (broadcast). 클라이언트는 NO_SOCKET_PATH
    std::atomic<uint16_t> closing;       // 닫힘 플래그. 0 이면 열림, 아니면 우리가 보낸 status
};
```

`SocketState` 는 frame 상태 (`remaining`, `offset`, `key`, opcode, FIN, MASK), 조각 중인 메시지의 opcode, 마지막 read (길이와 상태. TIMEOUT 뒤 같은 read 를 다시 걸 때), ping / close deadline, control payload 버퍼 (125), write lane 으로 보낼 close frame 버퍼 (`CLOSE_FRAME_BYTES` = 머리 2 + mask key 4 + payload 125), 고른 subprotocol, 상대 close 의 status, `is_open` 이다. 메시지 자체는 4.3 의 body Stage 에 모인다.

### 5.2 handle

handle 은 `{ site.socket_owner, session.id.value() }` 다. id 는 4.5 와 같이 generation + index 이고, generation 은 두 번 오른다.

- 서버: Upgrade 를 받아들여 101 을 만들 때 (`acceptUpgrade`). 그래서 `onUpgrade` 의 handle 과 `onOpen` 의 handle 이 다르다. 그 전의 HTTP 요청에 쓴 handle 로도 소켓을 만질 수 없다.
- 클라이언트: 연결마다 (`onConnected`). 서버의 요청마다 오르는 것과 같은 자리다. 처음 연결의 generation 이 0 이면 id 가 0 (무효) 이 되기 때문이기도 하다.

다른 thread 의 함수는 Session 을 보지 않고 `SocketLink` 만 본다.

```cpp
WebSocketAccess::LinkResult WebSocketAccess::openLink(int64_t id)
{
    const PoolHandle handle = PoolHandle::fromValue(id);
    const Span<SocketLink> links = _site.sessions.links();
    const size_t index = static_cast<size_t>(handle.index);
    const bool is_inside = handle.index >= 0 && index < links.length;
    if (!is_inside) { return LinkResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
    SocketLink& link = links[index];
    if (!isOpenSocket(link, id)) { return LinkResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
    return LinkResult::success(link);
}
```

`openSocket` (IO thread) 이 `connection`, `path`, `closing = 0` 을 쓰고 마지막에 `socket` 을 쓴다. `endSocket` 은 `onClose` 전에 `socket` 을 0 으로 둔다. 그래서 `onClose` 안의 `send` 는 `PEER_CLOSED` 다. `onUpgrade` 안에서만 되는 둘 (`findWebSocketUpgradeHeader`, `selectWebSocketProtocol`) 은 `sessions.find(id)` 로 Session 을 찾고 `phase == UPGRADING` 이 아니면 `BUSY` 다 (IO thread 의 콜백 안에서만 부르는 함수라 Session 을 직접 본다).

### 5.3 Upgrade (서버)

라우트 표 (4.1) 의 WebSocket 항목은 path 의 모든 메서드를 차지하고 (`DUPLICATE_RULE` 검사) 매칭에는 GET 비트 하나로 든다. 그래서 OPTIONS 와 다른 메서드는 4절의 자동 응답 (204 / 405) 이고, GET 과 HEAD 만 `MatchKind::SOCKET` 으로 `answerUpgrade` 에 온다.

```
answerUpgrade(session, target)
    GET 아님 (HEAD)                         → 405 + Allow: GET, Connection: close
    has_body                                → 400
    Upgrade / Connection / Version 13 / Key 중 없음 → 426 + Upgrade: websocket + Sec-WebSocket-Version: 13
    Key 가 16 바이트의 base64 아님          → 400
    socket.reset(), socket.target = target, phase = UPGRADING
    onUpgrade 가 0                          → 403
    101 + Upgrade + Sec-WebSocket-Accept (+ 고른 Protocol), persistence = UPGRADE (Connection: Upgrade)
    generation 을 올린다
    → { SOCKET_OPENING, thenWrite(101) }
```

응답은 4.4 의 응답 버퍼에서 4.7 의 조각으로 나간다. `SOCKET_OPENING` 의 WRITTEN 행 (`onSocketWritten`) 이 남은 조각을 보내고, 다 보냈으면 `openSocket` 이다.

```cpp
KiottyNetTransition openSocket(HttpSession& session)
{
    const KiottyNetConnectionHandle connection = session.request.request().connection;
    session.request.begin(connection);                 // body Stage 를 메시지 버퍼로 비운다
    session.phase = SessionPhase::SOCKET;
    session.socket.is_open = true;
    const WebSocketTarget& target = session.socket.target;
    session.link.connection.store(connection.id);
    session.link.path.store(target.path);
    session.link.closing.store(0);
    session.link.socket.store(session.id.value());     // 이때부터 다른 thread 가 보낼 수 있다
    const KiottyNetWebSocketHandle handle = socketHandleOf(session);
    session.socket.context = target.handler.onOpen(target.app_context, handle);
    Parker* opened = session.site.upgrade.opened;      // 클라이언트의 await (5.8). 서버는 NULL
    const auto wake = []() {};
    if (opened != nullptr) { opened->unpark(wake); }
    return nextFrame(session);
}
```

### 5.4 frame 읽기

상태는 `FRAME_START` (2 바이트), `FRAME_LENGTH` (2 또는 8), `FRAME_MASK` (4), `FRAME_PAYLOAD` 다. 닫는 중에 상대 close 를 기다리는 따로 된 상태는 없다. 닫힘 플래그가 서 있으면 같은 `FRAME_*` 상태에서 read 의 timeout 만 `close_timeout` 이 된다 (5.5).

| 행 | 하는 일 |
|---|---|
| `onFrameStart` | `parseFrameStart` (FIN, RSV 0, 아는 opcode, 제어 frame 은 FIN 이고 125 이하). MASK 비트가 역할과 맞는지 (서버는 있어야, 클라이언트는 없어야), 조각 순서 (`isInOrder`: 메시지 중이면 continuation 만, 아니면 continuation 이 아닌 것만. 제어 frame 은 언제나). 아니면 1002 |
| `onFrameLength` | 확장 길이. 최소 표현이 아니거나 최상위 비트가 서면 1002 |
| `afterLength` | 데이터 frame 이면 지금까지 모인 body 에 더해 `max_body_bytes` 를 넘는지 먼저 본다 (넘으면 payload 를 읽기 전에 1009) |
| `onFrameMask` | mask key |
| `onFramePayload` | `min(남은 길이, receive_capacity)` 씩 읽는다. 데이터 frame 은 `appendBody(piece, key, offset)` 가 body Stage 로 복사하며 unmask 하고, 제어 frame 은 `socket.control` 로. offset 이 조각마다 이어진다 |
| `finishControl` | ping → pong (닫는 중이면 보내지 않는다), pong → `ping_deadline = 0`, close → `answerClose` (5.7) |
| `finishData` | FIN 이 아니면 다음 frame. FIN 이면 TEXT 의 UTF-8 검사 (아니면 1007), 닫는 중이 아니면 `onMessage(context, handle, { opcode, body })`, body Stage 를 비운다 |

모든 read 는 `readNext` 하나로 건다. 마지막 read 의 길이와 상태를 `socket.read` 에 적어 두어 TIMEOUT 행이 같은 read 를 다시 걸 수 있게 한다.

```cpp
KiottyNetTransition readNext(HttpSession& session, HttpState state, size_t length)
{
    session.socket.read = SocketRead{ length, static_cast<int32_t>(state) };
    const uint32_t timeout = readTimeoutOf(session);
    return Next::moveToNextState(state, thenRead(length, timeout));
}
```

### 5.5 타이머

ping, pong, close 세 타이머는 따로 된 timer 가 아니라 다음 read 의 `timeout_milliseconds` 하나다 (8절의 entry action). 0 은 무한이다.

```cpp
uint32_t readTimeoutOf(HttpSession& session)
{
    if (isClosing(session)) { return closeTimeOf(session); }          // 처음 부를 때 close_deadline 을 잡고 남은 시간
    const uint64_t ping_deadline = session.socket.ping_deadline;
    if (ping_deadline != 0) { return timeLeft(ping_deadline); }       // ping 을 보냈다. pong 까지 남은 시간
    return session.site.limits.websocket_ping_interval_milliseconds;  // 받은 frame 이 없으면 이 뒤에 ping
}
```

`onSocketTimeout` 이 TIMEOUT 을 가른다.

| 그때 | 하는 일 |
|---|---|
| 닫힘 플래그가 서 있다 | `closeWith(TIMEOUT)` |
| `ping_deadline != 0` (pong 늦음) | `pushClose(1001)`. 플래그가 서므로 같은 read 를 다시 걸 때 timeout 이 `close_timeout` 이 된다. 상대가 close 로 답하면 `SUCCESS`, 아니면 다음 TIMEOUT 에 `TIMEOUT` |
| 그 밖 (ping 차례) | push lane 에 ping, `ping_deadline = now + pong_timeout`, 같은 read 를 다시 |

pong 늦음의 close frame 이 write lane 이 아니라 push lane 인 까닭: TIMEOUT 행 안에서는 read 가 걸려 있고 그 read 를 다시 걸어야 상대의 close 를 받는다. 그 자리에서 write 를 걸면 base 가 `BUSY` 로 거절한다 (read 와 write 는 한 번에 하나). pong timeout 이 0 이면 `ping_deadline` 이 0 으로 남아 pong 을 검사하지 않고 ping 간격마다 ping 만 보낸다.

frame 이 오면 timeout 이 매번 다시 계산되므로 ping 은 "마지막 frame 뒤 ping 간격" 에 나간다. 다른 thread 의 close 는 걸려 있던 read 의 timeout 을 바꾸지 못한다. 그래서 상대가 아무것도 보내지 않으면 끊기기까지 `close_timeout` 이 아니라 그 read 의 남은 시간 (최대 ping 간격) 이 걸린다 (설계 결정 9).

### 5.6 송신의 두 길

**push lane.** `PushLane::push(connection, opcode, payload)` 하나다. 소유자가 구현한다.

```cpp
KiottyNetCode HttpServer::push(int64_t connection, FrameOpcode opcode, Span<const char> payload)
{
    const FrameHead head = buildFrameHead(opcode, payload.length);
    const KiottyNetSpan pieces[] = {
        KiottyNetSpan{ head.bytes, head.length },
        KiottyNetSpan{ payload.data, payload.length },
    };
    return Server::notify(connection, Span<const KiottyNetSpan>{ pieces, 2 });   // 조각 둘을 한 lock 안에서 이어 복사 (1.6, B1)
}

KiottyNetCode WebSocketClient::push(int64_t connection, FrameOpcode opcode, Span<const char> payload)
{
    const std::lock_guard<std::mutex> lock(_push_mutex);
    const MaskKey key = nextMaskKey();
    const FrameHead head = buildMaskedFrameHead(opcode, payload.length, key);
    const size_t total = head.length + payload.length;
    if (total > _scratch.size()) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
    const Span<char> frame{ _scratch.data(), total };
    (void)frame.copyFrom(head.span());
    const Span<char> body = frame.skip(head.length);
    (void)body.copyFrom(payload);
    applyMask(body, key, 0);
    return EventClient::notify(connection, KiottyNetSpan{ frame.data, frame.length });
}
```

base 의 NotificationDesk 는 조각을 그대로 복사하므로 mask 는 그 앞에서 씌운다. 클라이언트는 `push_capacity` 크기의 scratch 버퍼를 initialize 때 한 번 잡고 mutex 하나로 지킨다 (send 는 아무 thread 에서나 오므로). push lane 보다 큰 frame 은 들어갈 수 없으니 scratch 도 그 크기면 된다. base 는 바뀌지 않았다.

**write lane.** frame 행 안에서 우리가 연결을 닫을 때 (1002 / 1007 / 1009, 상대 close 에 대한 답) 는 close frame 을 `socket.outgoing` 에 만들어 `thenCloseWith(close_frame, code)` 하나로 `CLOSING` 에 간다 (B3, 1.6). base 가 close frame 만 보내고 push lane 의 남은 것은 버린 뒤 닫으므로 close frame 뒤에 데이터 frame 이 나가지 않는다 (RFC 6455 5.5.1). B3 전에는 `thenWrite` 와 `SOCKET_CLOSING` 상태의 WRITTEN 행이었고, 같은 WRITE 에 push lane 이 붙어 close frame 뒤에 데이터 frame 이 나갈 수 있었다.

### 5.7 close 의 갈래

닫힘 플래그 (`SocketLink::closing`) 는 CAS 로 한 번만 선다. 먼저 세운 쪽이 close frame 을 보내고, 늦은 쪽은 보내지 않는다.

| 누가 | 어디서 | 플래그를 세웠으면 | 이미 서 있었으면 |
|---|---|---|---|
| `closeWebSocket` | 아무 thread | push lane 에 close frame | `BUSY` |
| pong 늦음 | TIMEOUT 행 | push lane 에 1001 | (닫는 중이면 TIMEOUT 행이 먼저 끝낸다) |
| `failSocket` (1002 / 1007 / 1009) | READ 행 | `thenCloseWith(그 status 의 close frame, 그 코드)` | 바로 그 코드로 close |
| `answerClose` (상대 close) | READ 행 | 상대 status 를 그대로 (reason 없이, 1005 면 빈 payload) 담아 `thenCloseWith(..., SUCCESS)` | 우리가 시작한 close 의 답이다. 바로 `SUCCESS` |

`onClose` 는 CLOSED 행의 `endSocket` 이 한 번 부른다. `{ code, status, reason }` 은 `closeOf` 가 만든다.

```cpp
KiottyNetWebSocketClose closeOf(const HttpSession& session, KiottyNetCode reason)
{
    KiottyNetWebSocketClose close{ reason, ABNORMAL, KiottyNetSpan{ nullptr, 0 } };   // 1006
    if (session.socket.has_peer_close)                                                 // 상대 close 를 받았으면 그 status / reason
    {
        close.status = session.socket.peer_status;
        close.reason = peerReasonOf(session);
        return close;
    }
    const bool is_dropped = reason == KIOTTY_NET_CODE_PEER_CLOSED
                            || reason == KIOTTY_NET_CODE_SOCKET_ERROR;
    if (is_dropped) { return close; }
    const uint16_t sent = session.link.closing.load();
    const bool is_owner_close = reason == KIOTTY_NET_CODE_CLOSED || sent == 0;
    close.status = is_owner_close ? GOING_AWAY : sent;                                 // 소유자 close 는 1001, 아니면 우리가 보낸 값
    return close;
}
```

### 5.8 클라이언트

`WebSocketClient` 는 `EventClient` 에 HttpSite 하나 (Session 하나, 라우트 표 1 칸, `role = CLIENT`) 를 든 것이다. create 때 잡는 것: Session 의 버퍼 (4.2 와 같은 limits), Upgrade 요청 버퍼 (768 바이트), Accept 값, push scratch, mask 상태.

| 상태 | 사건 | 행 |
|---|---|---|
| (어디서나) | CONNECTED | generation 을 올리고 Session 을 비운다. `header_deadline` 을 잡고 Upgrade 요청을 write lane 으로 (timeout `header_timeout`) |
| SENDING | WRITTEN | 남았으면 이어서, 다 보냈으면 상태줄 read (남은 header 시간) |
| REQUEST_LINE (상태줄) | READ | `HTTP/1.1 101` 다음이 끝이거나 공백이 아니면 `PROTOCOL_ERROR` |
| HEADER_LINE | READ | 4.3 의 `takeHeader` 로 요청 저장소에 (`max_header_bytes`, `max_headers`). 빈 줄이면 `isUpgradeResponse` 를 보고 `openSocket` |
| FRAME_* | | 5.4 ~ 5.7 그대로 |
| (어디서나) | TIMEOUT | `TIMEOUT` (frame 행의 TIMEOUT 은 위의 `onSocketTimeout` 이 먼저 잡는다) |
| (어디서나) | CLOSED | 열리기 전이면 끊긴 이유를 `upgrade.failure` 에 적는다. `endSocket` |

- **요청.** `prepareUpgrade` 가 run 마다 만든다. 키는 `std::random_device` 16 바이트의 base64 이고 기대하는 Accept 를 같이 계산해 둔다. Host 는 address 의 host (IPv6 literal 이면 `[...]`), 포트가 80 (ws) / 443 (wss) 이 아니면 `:port`.
- **run 의 반환값.** `EventClient::run` 의 반환값을 그대로 쓰되, 열리기 전에 우리가 `PROTOCOL_ERROR` / `TIMEOUT` 으로 닫았으면 base 는 `PEER_CLOSED` 를 돌려주므로 `upgrade.failure` 로 바꾼다.
- **await.** `Parker` 하나다. `openSocket` 이 깨우고 (그때 `SocketLink::socket` 이 0 이 아니다), run 이 끝날 때 결과를 적고 깨운다. await 는 "소켓이 열렸으면 `SUCCESS`, run 이 끝났으면 그 결과" 가 될 때까지 기다린다. run 의 시작에서 결과를 지우므로 그 전의 짧은 동안은 앞 run 의 결과가 보인다.
- **mask key.** `nextMaskKey` 는 splitmix64 다. 상태는 `std::random_device` 로 seed 하고 `fetch_add` 로 frame 마다 나아간다 (여러 thread 의 send 가 같은 key 를 받지 않는다). write lane 의 close frame (5.6) 은 IO thread 에서 단조 시계로 만든 key 다. RFC 6455 10.3 의 요구는 "상대가 다음 key 를 예측할 수 없을 것" 이고 암호 수준의 난수는 요구하지 않는다.

### 5.9 다른 thread 에서 오는 것

`WebSocketAccess` 가 공개 함수의 몸이다. 소유자 (HttpServer, WebSocketClient) 는 `KiottyNetWebSocketEndpoint` 의 가상 함수에서 `WebSocketAccess(_site)` 를 만들어 넘긴다.

- `send`: 인자 검사 (빈 data, 모르는 opcode) → `openLink` → 플래그가 서 있으면 `CLOSED` → `pushFrame`.
- `close`: `openLink` → `pushClose` (status 와 reason 검사, CAS, push lane).
- `broadcast`: path 를 라우트 표에서 경로 번호로 찾고 (없으면 `INVALID_ARGUMENT`, NULL 이면 전부) `links()` 를 돌며 열려 있고 닫는 중이 아니고 경로가 같은 칸에 `pushFrame`. 하나라도 `BUFFER_FULL` 이면 그것을 돌려준다.

IO thread 는 깨우지 않는다. push lane 의 drain 은 base 의 notify 가 IO thread 에 알린다 (1.6). 닫힘 플래그는 그 연결의 다음 READ / TIMEOUT 행이 본다.

---

## 6. 미결

- done ring 의 MPSC 구현은 1.5 대로 lock-free 로 간다. 32 client 라 mutex 로도 충분하지만 IO Thread 가 worker 에 막히는 일이 없어야 해서다.
- (닫힘, 2026-11-09) WebSocket upgrade 뒤의 표: HTTP 와 WebSocket 은 한 표, 한 Session 이다 (`design_statemachine_v0.md` 11절, 14절 7, 8).
- (닫힘, 2026-11-09) HttpResponse / HttpWork handle 의 소유자: handle 이 `{ server, id }` 라 `ownerOf` 가 필요 없다 (4.5, 공개 API 판 v1).
- (닫힘, 2026-11-10) 정적 파일의 `sendfile` / `TransmitFile`: 쓰지 않고 4.7 의 응답 버퍼 조각으로 보낸다 (`design_http_v0.html` 1절 결정 6, H5 에서 구현).
- (닫힘, 2026-11-13) WebSocket 의 다른 thread close 를 drain 뒤 훅으로 할지: 두지 않는다. 닫힘 플래그를 READ / TIMEOUT 행이 본다 (5.5, 5.7, 설계 결정 3).
- (닫힘, 2026-11-13) 클라이언트의 mask 를 어디서 씌울지: base 를 바꾸지 않고 클라이언트의 push 가 scratch 버퍼에서 씌운다 (5.6).

# Kiotty-Net 구현 가이드 v0

[requirement_v0.md](requirement_v0.md) 는 외부 인터페이스와 규칙만 담는다. 이 문서는 그 인터페이스를 어떻게 구현하는지의 예시와 설계 근거다.
두 문서가 다르면 requirement_v0.md 가 우선한다. 여기의 코드는 의사코드가 아니라 컴파일되는 형태로 쓰되, 소켓과 IO loop 같은 OS 의존 부분은 호출 이름만 적는다.

목차

1. Stage 와 수신 / 송신 흐름 (ReadRequest, Connection::read / readUntil, IOThread::pump, Work, write lane / push lane / drain, broadcast)
2. StateMachine (Stateful run, Connection 이 기억하는 것, dispatch, entry action 적용, HTTP / WebSocket 이 얹히는 방법)
3. 예제 : Echo (프로토콜, EchoServer, EchoClient 두 가지, C 판)
4. HttpServer : Connection 별 Request / Response 공급 (HttpSession, 줄 단위 헤더 읽기, 응답 버퍼 배치, handle, Work, 조각 송신)
5. 미결

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

**pool handle.** 하위 32비트 slot index, 상위 32비트 generation. Connection 이 닫힐 때 generation 을 1 올린다. 그래서 같은 slot 이 새 peer 에게 재사용되어도 옛 handle 은 맞지 않는다. `KiottyNetWorkHandle` 은 그 연결의 handle 과 같은 값이다. 타입만 다르다.

**Connection 의 Work 상태.** atomic 하나로 표현한다. IO Thread 와 worker 가 모두 CAS 로만 바꾸므로 lock 이 없다.

```
RUNNING ──startWork──> DRAINING ──(write lane 비움, 다음 loop)──> WORKING ──finish*──> DONE ──(IO Thread 가 요청 적용)──> RUNNING
```

| 전이 | 누가 | 방법 |
|---|---|---|
| RUNNING → DRAINING | startWork 를 부른 thread (IO Thread 콜백 안이거나 앱 thread) | CAS. 실패하면 `KIOTTY_BUSY`. payload 와 generation 을 슬롯에 기록하고 IO Thread 를 깨운다 |
| DRAINING → WORKING | IO Thread | write lane 이 비고 (onWritten 까지 나간 뒤) 다음 loop 에서 저장(store). push lane 은 비어 있을 필요가 없다. 걸려 있던 read 요청과 남은 deadline 을 `_saved_request`, `_saved_deadline` 에 옮긴다. 그 다음 onWorkStarted |
| WORKING → DONE | finish* 를 부른 thread | 요청(op, n, delimiter, timeout)을 `_finish_request` 에 쓰고 CAS. 실패하면 `KIOTTY_BUSY` (이미 finish 됨). generation 이 다르면 `KIOTTY_PEER_CLOSED`. 성공하면 done ring 에 index 를 넣고 깨운다 |
| DONE → RUNNING | IO Thread | done ring 에서 꺼내 `_finish_request` 를 적용한다. `finishWithWrite` 는 `_saved_request` 와 `_saved_deadline` 을 복원하고 drain 한 번 (1.6, write lane 의 메시지가 나간다). 그 뒤 pump 한 번 (쌓인 데이터로 바로 onRead 가 나올 수 있다) |

**finishWithWrite 의 복사.** WORKING 상태에서 write lane 을 만지는 thread 는 worker 하나뿐이다 (IO Thread 는 DRAINING 에서 다 비웠고 WORKING 에서는 write lane 을 읽지도 쓰지도 않는다. push lane 만 drain 한다). 그래서 worker 가 write lane 에 직접 복사해도 된다. 복사는 CAS 전에 끝나므로 IO Thread 가 DONE 을 본 시점에는 데이터가 다 들어 있고, DONE → RUNNING 에서 drain 이 보낸다. data 가 `send_capacity` 보다 크면 상태를 바꾸지 않고 `KIOTTY_BUFFER_FULL`.

**WORKING 중 IO Thread 가 하는 일.** recv 는 계속 걸어 수신 Stage 에 쌓는다. 가득 차면 recv 를 걸지 않는다. push lane 은 평소처럼 drain 한다 (broadcast 와 WebSocket control frame 이 Work 중에도 나간다). FIN / 에러는 `_peer_closed` (atomic) 에 기록만 하고 close 는 미룬다. deadline scan 은 WORKING 인 연결을 건너뛴다. `KiottyNet_isWorkConnected` 는 generation 일치와 `_peer_closed == false` 를 읽는다. DONE → RUNNING 전이에서 `_peer_closed` 면 요청을 적용하는 대신 `close(KIOTTY_PEER_CLOSED)` 한다.

**깨우기.** Windows 는 `PostQueuedCompletionStatus` 로 index 를 실어 보낸다. Linux 는 eventfd (io_uring 은 msg ring), mac 은 pipe 로 깨운 뒤 done ring 을 비운다.

**done ring.** 연결 하나는 동시에 하나의 DONE 만 가질 수 있으므로 `max_clients` 칸이면 넘치지 않는다. worker 가 여럿이라 producer 는 여럿, consumer 는 IO Thread 하나다. `fetch_add` 로 칸을 받아 index 를 쓰고 그 칸의 ready 플래그를 release 로 세운다. IO Thread 는 head 부터 ready 가 세워진 칸을 acquire 로 읽어 비운다. startWork 를 앱 thread 가 부르는 경우도 같은 ring 과 깨우기를 쓴다 (전이만 다르다).

**소유자 close().** WORKING 인 연결은 `_peer_closed` 와 무관하게 `onDisconnected(KIOTTY_CLOSED)` 를 내고 generation 을 올린다. 뒤늦은 finish* 는 generation 불일치로 `KIOTTY_PEER_CLOSED` 다. destroy 뒤의 finish* 도 같은 길로 no-op 이 되려면 handle 에서 소유자를 찾는 전역 표에도 generation 이 있어야 한다 (요구사항 미결).

**timeout.** `Connection::setTimeout(ms)` 는 Connection 의 deadline 하나를 갱신한다. IO loop 는 대기 시간을 가장 가까운 deadline 으로 잡고 (IOCP: `GetQueuedCompletionStatusEx` 의 timeout,
epoll: timerfd 또는 timeout 인자, kqueue: `EVFILT_TIMER`), 깨어나면 배열 scan 으로 만료된 Connection 에 `onTimeout` 을 전달한다. DRAINING 인 연결은 만료되면 onTimeout 대신 `close(KIOTTY_TIMEOUT)`, WORKING 인 연결은 건너뛴다. 32 client 면 scan 으로 충분하다.

### 1.6 송신 : write lane, push lane, drain, broadcast

requirement 4절의 두 lane 이다. 둘 다 Connection 안의 고정 버퍼이고, socket 에 쓰는 코드는 `drain` 하나다. 설계의 요점은 셋이다.

- write lane 은 메시지 하나다. 사용자의 `write → onWritten → write` 교대가 곧 흐름 제어이므로 경계 큐가 없다.
- push lane 은 IO Thread 혼자 쓰고 읽는다. 다른 thread (broadcast 호출자, worker) 는 push lane 을 직접 만지지 않는다. 그래서 Work 상태와 무관하고 lock 이 없다.
- notify 와 broadcast 호출자가 돌아오는 시점은 "push lane 에 복사됐다" 이고 socket 상태와 무관하다. 느린 peer 는 자기 push lane 만 채운다. 가득 차면 그 notify 만 들어가지 않고 연결은 그대로다. 호출자를 붙잡지 않는다.

```cpp
struct WriteLane {                       // 메시지 하나. ring 이 아니다
    char*             bytes;             // send_capacity
    size_t            length;
    size_t            sent;              // kernel 로 넘어간 길이
    std::atomic<bool> pending;           // 메시지가 들어 있다. RUNNING 에서는 IO Thread 가, WORKING 에서는 worker 가 세운다 (release). IO Thread 가 acquire 로 읽는다
};

struct PushLane {                        // [uint32_t 길이][바이트] 가 여러 개. 수신 Stage 와 같은 클래스
    Stage  ring;                         // push_capacity
    size_t head_sent;                    // head 메시지 중 kernel 로 넘어간 길이
};

enum Lane { NONE, WRITE, PUSH };         // Connection::_sending : 지금 보내는 중인 메시지가 어느 lane 인지. 메시지 경계에서만 바뀐다
```

**producer.** 셋 모두 복사만 하고 socket 은 만지지 않는다.

```cpp
void Connection::write(Span data)                      // IO Thread (콜백 안). RUNNING
{
    if (_write.pending)              { close(KIOTTY_BUSY); return; }          // 이전 write 의 onWritten 전
    if (data.length > send_capacity) { close(KIOTTY_BUFFER_FULL); return; }
    memcpy(_write.bytes, data.data, data.length);
    _write.length = data.length;
    _write.sent   = 0;
    _write.pending.store(true, std::memory_order_release);
    _io.drain(this);                                                           // (W)
}
// finishWorkWithWrite 는 같은 복사를 worker 가 한 뒤 done ring 에 넣는다 (1.5). drain 은 DONE → RUNNING 에서 IO Thread 가 부른다 (D).

bool Connection::push(Span data)                       // IO Thread 만. notify / broadcast 분배, WebSocket control frame
{
    uint32_t length = (uint32_t)data.length;
    char* p = (char*)_push.ring.provider().prepare(sizeof length + data.length);
    if (!p) return false;                                                      // 호출자가 KIOTTY_BUFFER_FULL 을 돌려준다. 연결은 그대로
    memcpy(p, &length, sizeof length);
    memcpy(p + sizeof length, data.data, data.length);
    _push.ring.provider().commit(sizeof length + data.length);
    _io.drain(this);                                                           // (B)
    return true;
}
```

**consumer.** `pickSegment` 가 보낼 구간을 고른다. 보내던 메시지는 끝까지 보내고, 메시지 경계에서만 lane 을 바꾼다. 둘 다 있으면 write lane 먼저. WORKING 이면 write lane 은 worker 것이므로 push lane 만 본다.

```cpp
Segment IOThread::pickSegment(Connection* c)
{
    if (c->_sending == NONE) {
        if (c->_state == RUNNING && c->_write.pending.load(std::memory_order_acquire)) c->_sending = WRITE;
        else if (c->_push.ring.consumer().readable() > 0)                                 c->_sending = PUSH;
        else return Segment();                                                             // 보낼 것 없음
    }
    if (c->_sending == WRITE) return Segment(c->_write.bytes + c->_write.sent, c->_write.length - c->_write.sent);

    Stage::Consumer& consumer = c->_push.ring.consumer();
    uint32_t length; memcpy(&length, consumer.prepare(sizeof length), sizeof length);
    const char* bytes = (const char*)consumer.prepare(sizeof length + length) + sizeof length;
    return Segment(bytes + c->_push.head_sent, length - c->_push.head_sent);
}

void IOThread::advance(Connection* c, size_t n)      // n 바이트가 kernel 로 넘어갔다
{
    if (c->_sending == WRITE) {
        c->_write.sent += n;
        if (c->_write.sent < c->_write.length) return;                        // 메시지가 아직 남았다
        c->_write.pending = false;
        c->_written       = c->_write.length;                                 // onWritten 은 loop 끝의 completion 에서 (아래)
    } else {
        c->_push.head_sent += n;
        Stage::Consumer& consumer = c->_push.ring.consumer();
        uint32_t length; memcpy(&length, consumer.prepare(sizeof length), sizeof length);
        if (c->_push.head_sent < length) return;
        consumer.commit(sizeof length + length);                              // 콜백 없음
        c->_push.head_sent = 0;
    }
    c->_sending = NONE;                                                        // 다음 pickSegment 가 lane 을 다시 고른다
}
```

**drain.** 플랫폼이 다른 곳은 여기뿐이다. IOCP 는 "이 구간을 보내 달라고 걸어 두고 완료를 받는다", epoll 은 "지금 kernel 에 넣고 안 받아 주면 EPOLLOUT 을 기다린다".

```cpp
// IOCP (proactor). 연결마다 WSASend 는 항상 하나만 걸려 있다.
void IOThread::drain(Connection* c)
{
    if (c->_send_pending) return;                                              // 완료가 오면 onSendComplete 가 다시 부른다
    Segment s = pickSegment(c);
    if (s.empty()) return;
    c->_send_pending = true;
    WSASend(c->_socket, s.ptr, s.length, &c->_send_ov);                        // 유일한 WSASend. 완료까지 s 가 가리키는 구간은 그대로 둔다
}

void IOThread::onSendComplete(Connection* c, DWORD bytes)                      // (S)
{
    c->_send_pending = false;
    if (bytes == 0) { close(c, KIOTTY_SOCKET_ERROR); return; }
    advance(c, bytes);
    drain(c);                                                                  // 남았으면 다음 WSASend
}

// epoll (reactor). 받아 주는 만큼 반복해서 넣고, EAGAIN 이면 EPOLLOUT 을 켠다. Stage 가 비면 끈다 (level-triggered 에서 매 loop 깨어나지 않게).
void IOThread::drain(Connection* c)
{
    for (;;) {
        Segment s = pickSegment(c);
        if (s.empty()) {
            if (c->_epollout) { epoll_ctl(_ep, EPOLL_CTL_MOD, c->_socket, EPOLLIN); c->_epollout = false; }
            return;
        }
        ssize_t n = send(c->_socket, s.ptr, s.length, MSG_NOSIGNAL);           // 유일한 send
        if (n > 0) { advance(c, (size_t)n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!c->_epollout) { epoll_ctl(_ep, EPOLL_CTL_MOD, c->_socket, EPOLLIN | EPOLLOUT); c->_epollout = true; }
            return;
        }
        close(c, KIOTTY_SOCKET_ERROR); return;
    }
}
// EPOLLOUT 이 오면 drain(c) 를 부른다 (S).
```

drain 을 부르는 곳은 네 군데이고 모두 IO Thread 다. worker 와 notify / broadcast 호출자는 WSASend / send 를 부르지 않는다. 복사하고 깨우기만 한다.

| | 누가 | 언제 |
|---|---|---|
| (W) | `Connection::write` | 콜백 안에서 write 한 직후 |
| (D) | DONE → RUNNING | worker 의 `finishWithWrite` 가 깨운 뒤 요청을 적용한 직후 |
| (B) | `Connection::push` | notify / broadcast 분배 / control frame 을 push lane 에 복사한 직후 |
| (S) | 완료 패킷 / EPOLLOUT | 이전에 보낸 것이 끝났을 때 |

**completion.** `advance` 는 `onWritten` 을 바로 내지 않고 `_written` 에 길이만 기록한다. loop 는 그 바퀴의 콜백이 모두 돌아온 뒤 `_written != 0` 인 RUNNING 연결에 `onWritten(_written)` 을 내고 0 으로 돌린 다음 pump 한 번 (1.3) 을 돈다. 두 가지를 막기 위해서다. epoll 에서 `send()` 가 `write()` 안에서 바로 성공하면 `onWritten` 이 `write()` 안에서 재진입으로 나오게 되는 것, 그리고 콜백 안에서 콜백이 나오는 것이다. IOCP 는 완료가 늘 GQCS 로 오므로 재진입은 없지만 같은 자리에서 내서 두 플랫폼의 순서를 같게 한다.

**notify / broadcast.** 라이브러리 쪽 버퍼는 없다. 호출자가 돌아올 때까지 block 되므로 호출자의 `data` 가 유효하고, IO Thread 가 거기서 push lane 으로 바로 복사한다. 동시 호출은 slot 으로 받고 slot 이 모두 차면 `KIOTTY_BUSY` 다. notify 와 broadcast 는 같은 slot 을 쓰고 대상만 다르다.

```cpp
struct PushSlot {                                   // Server 에 4 개, EventClient 에 1 개. 고정
    std::atomic<int>          state;                // IDLE → FILLED (호출자) → DONE (IO Thread) → IDLE (호출자)
    KiottyNetConnectionHandle target;               // 0 이면 broadcast. 아니면 그 연결 하나
    Span                      data;                 // 호출자 버퍼
    KiottyNetCode             code;
    std::mutex                mutex;
    std::condition_variable   cv;
};

KiottyNetCode Owner::post(KiottyNetConnectionHandle target, Span data)   // 아무 thread. notifyConnection, notifyEventClient, broadcastServer 가 모두 여기로
{
    if (data.length == 0 || data.length > _push_capacity) return KIOTTY_INVALID_ARGUMENT;
    if (_io.isCurrentThread()) return deliver(target, data);                   // 콜백 안. 기다릴 것이 없다
    PushSlot* slot = acquireSlot();                                            // IDLE → FILLED CAS. 전부 실패면 nullptr
    if (!slot) return KIOTTY_BUSY;
    slot->target = target;
    slot->data   = data;
    _io.wake();                                                                // done ring 과 같은 PostQueuedCompletionStatus / eventfd
    {
        std::unique_lock<std::mutex> lock(slot->mutex);
        slot->cv.wait(lock, [slot] { return slot->state.load() == DONE; });
    }
    KiottyNetCode code = slot->code;
    slot->state = IDLE;
    return code;
}

void IOThread::processPushSlots()                   // 깨어난 직후, socket 이벤트와 콜백 전에. index 순서라 모든 연결이 같은 순서로 받는다
{
    for (PushSlot& slot : _slots) {
        if (slot.state.load() != FILLED) continue;
        slot.code = _owner.deliver(slot.target, slot.data);
        { std::lock_guard<std::mutex> lock(slot.mutex); slot.state = DONE; }
        slot.cv.notify_one();
    }
}

KiottyNetCode Owner::deliver(KiottyNetConnectionHandle target, Span data)   // IO Thread
{
    if (_closing) return KIOTTY_CLOSED;
    if (target != 0) {
        Connection* c = find(target);                                          // generation 이 다르면 nullptr
        if (!c || !c->connected() || c->_closing) return KIOTTY_PEER_CLOSED;
        return c->push(data) ? KIOTTY_SUCCESS : KIOTTY_BUFFER_FULL;
    }
    KiottyNetCode code = KIOTTY_SUCCESS;
    for (Connection& c : _pool) {
        if (!c.connected() || c._closing) continue;                            // onConnected 전 (accept, TLS 핸드셰이크 중) 이거나 닫히는 중
        if (!c.push(data)) code = KIOTTY_BUFFER_FULL;                          // 그 연결만 건너뛴다. 끊지 않는다
    }
    return code;
}
```

`notifyEventClient` 는 target 이 EventClient 의 연결 하나인 같은 코드다. 연결 전이면 `KIOTTY_PEER_CLOSED` 다. WebSocket 의 pong / close frame 은 IO Thread 가 `Connection::push` 를 직접 부른다. slot 을 거치지 않는다.

IO Thread loop 한 바퀴의 순서는 다음과 같다. (b) 가 (c) 앞에 있어서 분배는 콜백 사이에서만 일어난다.

```
a. 이벤트 대기 (GetQueuedCompletionStatusEx / epoll_wait, timeout 은 가장 가까운 deadline)
b. 깨우기 신호면 : done ring 처리 (D), push slot 처리 (B)
c. socket 이벤트 : recv 완료 → pump → onRead 등 (W 가 이 안에서), send 완료 / EPOLLOUT → drain (S)
d. deadline scan → onTimeout
e. completion : _written 인 RUNNING 연결에 onWritten, 그 뒤 pump
```

---

## 2. StateMachine

requirement_v0.md 8절의 구현이다. 공개 API 는 `KiottyNet_runStatefulServer` / `KiottyNet_runStatefulEventClient` 둘뿐이고 나머지는 모두 라이브러리 안이다. Runner 객체도 사용자 pool 도 없다. 사용자가 보는 것은 `KiottyNetStatefulAppContext` 하나다.

### 2.1 Stateful run

```cpp
static const KiottyNetConnectionHandler STATEFUL_HANDLER = { &statefulOnConnected, &statefulOnDisconnected, &statefulOnRead, &statefulOnWritten, &statefulOnWorkStarted, &statefulOnTimeout };

KiottyNetCode KiottyNet_runStatefulServer(KiottyNetServerHandle server, const KiottyNetStatefulAppContext* app_context)
{
    KiottyNetCode code = validate(app_context);        // NULL, machine NULL, rules NULL → KIOTTY_INVALID_ARGUMENT. 같은 (state, event) 가 두 번 → KIOTTY_DUPLICATE_RULE
    if (code != KIOTTY_SUCCESS) return code;
    return KiottyNet_runServer(server, &STATEFUL_HANDLER, const_cast<KiottyNetStatefulAppContext*>(app_context));   // 7절 run 그대로. app_context 자리에 사용자의 struct 가 들어간다
}
```

- 사용자의 `KiottyNetStatefulAppContext` 가 그대로 base 의 app_context 다. 라이브러리가 따로 잡는 per-run 객체는 없다. Stateful handler 는 이것을 읽기만 하므로 `const` 를 벗기는 것은 안전하다.
- 사용자는 이 struct 를 run 을 부른 함수의 stack 에 두어도 된다. run 이 모든 onDisconnected 뒤에 return 하기 때문이다 (7절).
- 검사는 표를 한 번 훑는다. 행 수가 작아서 O(n²) 비교로 충분하다. C++ 의 `KiottyNet_rules` 는 컴파일 타임에 이미 거른 표라 그냥 통과한다.
- 표는 정렬되어 있을 필요가 없다. 찾기는 선형 탐색이다 (2.3).

### 2.2 Connection 이 기억하는 것

내부 `Connection` 에 필드 둘이 있다. 둘 다 공개 API 로 읽거나 쓰는 함수가 없다.

```cpp
struct Connection {
    // ...
    KiottyNetConnectionContext _connection_context;   // onConnected 가 return 한 값. 이후 콜백의 첫 인자. 라이브러리는 읽지 않는다
    int32_t                    _state;                // Stateful 계층만 쓴다. onConnected 에서 machine->initial 로 놓는다
    Owner*                     _owner;                // Server 또는 EventClient. 지금 run 의 app_context 를 안다
};
```

- `_connection_context` 는 base 가 `handler.onConnected()` 의 반환값을 저장하는 자리다. 지워진 `KiottyNet_setConnectionContext` 의 대신이다. base 는 저장하고 넘겨주기만 한다.
- `_state` 는 base 가 건드리지 않는다. Stateful handler 만 읽고 쓴다. Connection 마다 int32 하나가 Stateful 계층의 per-connection 비용 전부다.
- machine 은 `connection->_owner->statefulAppContext()` 로 찾는다. 소유자가 들고 있는 지금 run 의 app_context 가 사용자의 `KiottyNetStatefulAppContext*` 이고 그 안에 machine 이 있다. onRead 같은 콜백은 app_context 를 받지 않지만 Stateful 모듈은 라이브러리 안이라 이 길로 간다.

### 2.3 dispatch

```cpp
static const KiottyNetRule* find(const KiottyNetStateMachine& m, int32_t state, KiottyNetConnectionEventType event)
{
    const KiottyNetRule* any = nullptr;
    for (size_t i = 0; i < m.count; ++i) {
        const KiottyNetRule& r = m.rules[i];
        if (r.event != event) continue;
        if (r.state == state) return &r;                                        // 정확 일치가 우선
        if (r.state == KIOTTY_ANY_STATE) any = &r;
    }
    return any;
}

static void deliver(Connection* c, const KiottyNetConnectionEvent& event)
{
    const KiottyNetStatefulAppContext& app = *c->_owner->statefulAppContext();
    const KiottyNetRule* rule = find(*app.machine, c->_state, event.type);
    if (!rule) { fail(c, event, KIOTTY_UNHANDLED_EVENT); return; }

    KiottyNetTransition t = rule->transition_action(c->_connection_context, c->handle(), event);
    if (event.type == KIOTTY_CONNECTION_EVENT_CLOSED) return;                  // 마지막 이벤트. Transition 은 무시
#ifndef NDEBUG
    if (!allowed(*rule, t)) { fail(c, event, KIOTTY_ILLEGAL_TRANSITION); return; }
#endif
    c->_state = t.next;
    if (event.type == KIOTTY_CONNECTION_EVENT_WORK_STARTED) { applyToWork(event.as.work_started.work, t.entry_action); return; }
    apply(c, t.entry_action);
}

static bool allowed(const KiottyNetRule& rule, const KiottyNetTransition& t)
{
    if (rule.next_count == 0) return true;
    for (uint32_t i = 0; i < rule.next_count && i < KIOTTY_MAX_NEXT; ++i) if (rule.next[i] == t.next) return true;
    return false;
}

static void fail(Connection* c, const KiottyNetConnectionEvent& event, KiottyNetCode code)
{
    if (event.type == KIOTTY_CONNECTION_EVENT_CLOSED) return;
    if (event.type == KIOTTY_CONNECTION_EVENT_WORK_STARTED) { KiottyNet_finishWorkWithClose(event.as.work_started.work, code); return; }
    KiottyNet_closeConnection(c->handle(), code);
}
```

onRead / onWritten / onWorkStarted / onTimeout 은 event 를 채워 `deliver` 를 부르는 한 줄짜리다.

### 2.4 entry action 적용

| entry action | Connection 에 (`apply`) | Work 에 (`applyToWork`, WORK_STARTED 에서만) |
|---|---|---|
| NONE | 아무것도 안 함. deadline 도 그대로 | Work 를 그대로 둔다. action 이 worker 에게 넘겼다 |
| READ(exact) | `readConnection(exact)` 뒤 `setConnectionTimeout(timeout_ms)` | `finishWorkWithRead(exact, timeout_ms)` |
| READ_UNTIL(delimiter, max) | `readConnectionUntil` 뒤 `setConnectionTimeout` | `finishWorkWithReadUntil` |
| WRITE(data) | `writeConnection(data)` 뒤 `setConnectionTimeout` | `finishWorkWithWrite(data)` |
| START_WORK | `startWork()`. 실패 코드면 `close(code)`. 성공이면 `setConnectionTimeout` | `KIOTTY_INVALID_ARGUMENT` 로 `finishWorkWithClose` |
| CLOSE(reason) | `closeConnection(reason)` | `finishWorkWithClose(reason)` |

- `setConnectionTimeout(0)` 은 deadline 해제다. 그래서 timeout 없는 entry action 은 이전 deadline 을 지운다. NONE 만 예외다.
- `finishWorkWith*` 가 실패하면 그 코드로 `finishWorkWithClose` 한다.

### 2.5 onConnected / onDisconnected

```cpp
static KiottyNetConnectionContext statefulOnConnected(KiottyNetAppContext raw, KiottyNetConnectionHandle handle)
{
    const KiottyNetStatefulAppContext& app = *static_cast<const KiottyNetStatefulAppContext*>(raw);
    Connection* c = Connection::of(handle);
    KiottyNetConnectionContext session = app.createConnectionContext ? app.createConnectionContext(app.app_context, handle) : nullptr;
    if (app.createConnectionContext && !session) { KiottyNet_closeConnection(handle, KIOTTY_SESSION_EXHAUSTED); return nullptr; }
    c->_state              = app.machine->initial;
    c->_connection_context = session;                                           // CONNECTED 행이 보기 전에 붙인다
    deliver(c, makeEvent(KIOTTY_CONNECTION_EVENT_CONNECTED));
    return session;
}

static void statefulOnDisconnected(KiottyNetAppContext raw, KiottyNetConnectionContext session, KiottyNetConnectionHandle handle, KiottyNetCode reason)
{
    const KiottyNetStatefulAppContext& app = *static_cast<const KiottyNetStatefulAppContext*>(raw);
    KiottyNetConnectionEvent event = makeEvent(KIOTTY_CONNECTION_EVENT_CLOSED);
    event.as.closed.reason = reason;
    deliver(Connection::of(handle), event);                                     // CLOSED 행. Transition 무시
    if (app.releaseConnectionContext && session) app.releaseConnectionContext(app.app_context, session, handle);
}
```

- create 가 NULL 함수면 session 은 NULL 이고 그대로 진행한다. create 가 있는데 NULL 을 돌려주면 `KIOTTY_SESSION_EXHAUSTED` 로 닫는다. 이때 CONNECTED 행은 돌지 않고 onDisconnected 가 바로 온다. 그 CLOSED 행은 돈다 (session 은 NULL).
- release 는 CLOSED 행 뒤다. action 이 살아 있는 session 을 마지막으로 본다.

### 2.6 HTTP / WebSocket 이 얹히는 방법

`KiottyNet_runHttpServer(http_server)` 는 자기 stack 에 `KiottyNetStatefulAppContext` 를 만든다. machine 은 라이브러리의 HTTP 표, app_context 는 HttpServer 자신 (라우트 표, HttpSession pool), create / release 는 그 pool 의 acquire / release 다. 그 다음 `KiottyNet_runStatefulServer` 를 부른다. 라우트는 run 전에 이미 표에 들어가 있다.
HttpSession pool 은 `createHttpServer` 가 `max_clients` 만큼 잡는다. 사용자 프로토콜이 Session 저장소를 들고 있는 것과 같은 방식이고, 그 저장소가 라이브러리 안에 있을 뿐이다. HttpSession 이 사용자에게 주는 `KiottyNetHttpRequest` 와 `KiottyNetHttpResponseHandle` 을 어떻게 공급하는지는 4절이다.
WebSocket 은 HTTP 의 101 뒤에 같은 Connection 에서 machine 과 session 을 바꿔 타야 한다. 방법은 미결이다 (5절).

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
- 서버는 header 를 30 초 안에 받지 못하면 끊는다. body 길이가 0 이거나 1024 를 넘으면 `KIOTTY_PROTOCOL_ERROR` 로 끊는다.
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
    return ServerNext::moveToNextState(ServerState::ReadingHeader, KiottyNet_EntryAction::read(HEADER_LENGTH).timeout(IDLE_TIMEOUT_MS));
}

static KiottyNetTransition serverOnConnected(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return serverReadHeader();
}

static KiottyNetTransition serverOnHeader(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    ServerSession& s      = serverSession(connection_context);
    uint32_t       length = decodeLength(e.as.read.data);
    if (length == 0 || length > MAX_BODY)
    {
        return ServerNext::moveToNextState(ServerState::ErrorOccurred, KiottyNet_EntryAction::close(KIOTTY_PROTOCOL_ERROR));
    }
    std::memcpy(s.frame, e.as.read.data.data, HEADER_LENGTH);                 // data 는 return 하면 무효라 복사
    s.frame_length = HEADER_LENGTH + length;
    return ServerNext::moveToNextState(ServerState::ReadingBody, KiottyNet_EntryAction::read(length));
}

static KiottyNetTransition serverOnBody(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    ServerSession& s = serverSession(connection_context);
    std::memcpy(s.frame + HEADER_LENGTH, e.as.read.data.data, e.as.read.data.length);
    KiottyNetSpan frame = { s.frame, s.frame_length };
    return ServerNext::moveToNextState(ServerState::Writing, KiottyNet_EntryAction::write(frame));   // write lane 으로 복사되므로 s.frame 은 바로 다시 써도 된다
}

static KiottyNetTransition serverOnWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return serverReadHeader();
}

static KiottyNetTransition serverOnTimeout(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ServerNext::moveToNextState(ServerState::ErrorOccurred, KiottyNet_EntryAction::close(KIOTTY_TIMEOUT));
}

static KiottyNetTransition serverOnClosed(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ServerNext::moveToNextState(ServerState::Closed);
}

static constexpr auto SERVER_RULES = KiottyNet_rules(
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_CONNECTED)                                  .then(serverOnConnected).to(ServerState::ReadingHeader),
    KiottyNet_from(ServerState::ReadingHeader).when(KIOTTY_CONNECTION_EVENT_READ)      .then(serverOnHeader)   .to(ServerState::ReadingBody, ServerState::ErrorOccurred),
    KiottyNet_from(ServerState::ReadingBody)  .when(KIOTTY_CONNECTION_EVENT_READ)      .then(serverOnBody)     .to(ServerState::Writing),
    KiottyNet_from(ServerState::Writing)      .when(KIOTTY_CONNECTION_EVENT_WRITTEN)   .then(serverOnWritten)  .to(ServerState::ReadingHeader),
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_TIMEOUT)                                    .then(serverOnTimeout),
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_CLOSED)                                     .then(serverOnClosed));
static constexpr KiottyNetStateMachine SERVER_MACHINE = KiottyNet_makeStateMachine(SERVER_RULES, ServerState::ReadingHeader);
```

Session 저장소는 사용자 것이다. 연결 수만큼 고정 배열을 두고 create / release 로 빌려 준다.

```cpp
template <typename Session, size_t N>
struct SessionPool
{
    Session session[N];
    bool    in_use[N];

    static KiottyNetConnectionContext acquire(KiottyNetAppContext app_context, KiottyNetConnectionHandle)
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
        return nullptr;                                                          // 라이브러리가 KIOTTY_SESSION_EXHAUSTED 로 닫는다
    }

    static void release(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle)
    {
        SessionPool& self = *static_cast<SessionPool*>(app_context);
        self.in_use[static_cast<Session*>(connection_context) - self.session] = false;
    }
};

int runEchoServer(uint16_t port)
{
    static SessionPool<ServerSession, 32> sessions = {};
    KiottyNetStatefulAppContext           stateful = { &sessions, &SERVER_MACHINE, &SessionPool<ServerSession, 32>::acquire, &SessionPool<ServerSession, 32>::release };

    KiottyNetServerConfig config;
    if (KiottyNet_ServerConfigBuilder::tcp(config, "0.0.0.0", port).maxClients(32).build() != KIOTTY_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Context context;
    KiottyNet_Server  server(context.handle(), config);
    if (!context.valid() || !server.valid())
    {
        return 1;
    }

    std::thread io([&]() { KiottyNet_runStatefulServer(server.handle(), &stateful); });   // stateful 과 sessions 는 io 가 끝날 때까지 살아 있다
    if (server.await(3000) != KIOTTY_SUCCESS)
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
    return ClientNext::moveToNextState(ClientState::Sending, KiottyNet_EntryAction::write(frame).timeout(5000));
}

static KiottyNetTransition clientOnWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ClientNext::moveToNextState(ClientState::ReadingHeader, KiottyNet_EntryAction::read(HEADER_LENGTH).timeout(5000));
}

static KiottyNetTransition clientOnHeader(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    ClientSession& s      = clientSession(connection_context);
    uint32_t       length = decodeLength(e.as.read.data);
    if (length != s.frame_length - HEADER_LENGTH)
    {
        return ClientNext::moveToNextState(ClientState::ErrorOccurred, KiottyNet_EntryAction::close(KIOTTY_PROTOCOL_ERROR));
    }
    s.expected_body = length;
    return ClientNext::moveToNextState(ClientState::ReadingBody, KiottyNet_EntryAction::read(length).timeout(5000));
}

static KiottyNetTransition clientOnBody(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    ClientSession& s = clientSession(connection_context);
    s.matched = std::memcmp(s.frame + HEADER_LENGTH, e.as.read.data.data, s.expected_body) == 0;
    return ClientNext::moveToNextState(ClientState::Closed, KiottyNet_EntryAction::close());   // 할 일이 끝났다. CLOSED(KIOTTY_SUCCESS) 가 마지막으로 온다
}

static KiottyNetTransition clientOnTimeout(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ClientNext::moveToNextState(ClientState::ErrorOccurred, KiottyNet_EntryAction::close(KIOTTY_TIMEOUT));
}

static KiottyNetTransition clientOnClosed(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
{
    return ClientNext::moveToNextState(ClientState::Closed);
}

static constexpr auto CLIENT_RULES = KiottyNet_rules(
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_CONNECTED)                                  .then(clientOnConnected).to(ClientState::Sending),
    KiottyNet_from(ClientState::Sending)      .when(KIOTTY_CONNECTION_EVENT_WRITTEN)   .then(clientOnWritten)  .to(ClientState::ReadingHeader),
    KiottyNet_from(ClientState::ReadingHeader).when(KIOTTY_CONNECTION_EVENT_READ)      .then(clientOnHeader)   .to(ClientState::ReadingBody, ClientState::ErrorOccurred),
    KiottyNet_from(ClientState::ReadingBody)  .when(KIOTTY_CONNECTION_EVENT_READ)      .then(clientOnBody)     .to(ClientState::Closed),
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_TIMEOUT)                                    .then(clientOnTimeout),
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_CLOSED)                                     .then(clientOnClosed));
static constexpr KiottyNetStateMachine CLIENT_MACHINE = KiottyNet_makeStateMachine(CLIENT_RULES, ClientState::Sending);

static KiottyNetConnectionContext singleSession(KiottyNetAppContext app_context, KiottyNetConnectionHandle)
{
    return app_context;                                                          // 연결이 하나라 app_context 가 곧 Session 이다
}

int runEchoClient(const char* host, uint16_t port, KiottyNetSpan message)
{
    ClientSession session = {};
    encodeLength(session.frame, uint32_t(message.length));
    std::memcpy(session.frame + HEADER_LENGTH, message.data, message.length);
    session.frame_length = HEADER_LENGTH + message.length;

    KiottyNetStatefulAppContext stateful = { &session, &CLIENT_MACHINE, &singleSession, nullptr };   // release 는 할 일이 없어서 NULL

    KiottyNetClientConfig config;
    if (KiottyNet_ClientConfigBuilder::tcp(config, host, port).connectTimeout(3000).retry(3, 1000).build() != KIOTTY_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Context     context;
    KiottyNet_EventClient client(context.handle(), config);
    if (!context.valid() || !client.valid())
    {
        return 1;
    }

    KiottyNet_runStatefulEventClient(client.handle(), &stateful);               // 연결이 닫히면 return 한다. 이 thread 가 IO Thread 다
    return session.matched ? 0 : 1;
}
```

- 연결이 하나이므로 create 는 app_context 를 그대로 Session 으로 돌려주고 release 는 NULL 이다.
- `session` 과 `stateful` 은 run 을 부른 함수의 stack 에 있다. run 이 CLOSED 뒤에 return 하므로 안전하다.
- 결과는 Session 에 남긴 `matched` 로 읽는다. run 의 반환값은 끊어진 이유이고 echo 가 맞았는지는 알려 주지 않는다.

### 3.4 EchoClient : StreamClient 판

이벤트도 표도 없이 같은 일을 하는 동기 판이다. IO Thread 가 없고 호출한 thread 가 기다린다. 한 번 묻고 한 번 받는 도구에는 이쪽이 짧다.

```cpp
static bool readExact(KiottyNet_StreamClient& client, char* buffer, size_t length, uint32_t timeout_ms)
{
    size_t received = 0;
    while (received < length)
    {
        KiottyNetResult result = client.read(buffer + received, length - received, timeout_ms);   // 최대 length 바이트. 모자라면 다시 부른다
        if (result.code != KIOTTY_SUCCESS)
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

    KiottyNetClientConfig config;
    if (KiottyNet_ClientConfigBuilder::tcp(config, host, port).connectTimeout(3000).retry(3, 1000).build() != KIOTTY_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Context      context;
    KiottyNet_StreamClient client(context.handle(), config);
    if (!context.valid() || !client.valid() || client.connect() != KIOTTY_SUCCESS)
    {
        return 1;
    }

    char header[HEADER_LENGTH];
    char body[MAX_BODY];
    bool ok = client.write(request, 5000).code == KIOTTY_SUCCESS
           && readExact(client, header, HEADER_LENGTH, 5000)
           && decodeLength(KiottyNetSpan{ header, HEADER_LENGTH }) == message.length
           && readExact(client, body, message.length, 5000)
           && std::memcmp(body, message.data, message.length) == 0;
    client.disconnect();
    return ok ? 0 : 1;
}
```

### 3.5 C 판 EchoServer

같은 표를 C 로 쓰면 이렇다. 행 순서는 아무렇게나 써도 되고, 중복은 run 이 시작할 때 `KIOTTY_DUPLICATE_RULE` 로 걸러진다. 클라이언트 두 가지도 같은 방법으로 옮겨진다.

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
    return KiottyNet_moveToNextState(SERVER_READING_HEADER, KiottyNet_setEntryActionTimeout(KiottyNet_makeReadEntryAction(HEADER_LENGTH), IDLE_TIMEOUT_MS));
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
    uint32_t       length = decode_length(event.as.read.data);
    (void)connection;
    if (length == 0 || length > MAX_BODY)
    {
        return KiottyNet_moveToNextState(SERVER_ERROR_OCCURRED, KiottyNet_makeCloseEntryAction(KIOTTY_PROTOCOL_ERROR));
    }
    memcpy(s->frame, event.as.read.data.data, HEADER_LENGTH);
    s->frame_length = HEADER_LENGTH + length;
    return KiottyNet_moveToNextState(SERVER_READING_BODY, KiottyNet_makeReadEntryAction(length));
}

static KiottyNetTransition server_on_body(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    ServerSession* s = (ServerSession*)session;
    KiottyNetSpan  frame;
    (void)connection;
    memcpy(s->frame + HEADER_LENGTH, event.as.read.data.data, event.as.read.data.length);
    frame.data   = s->frame;
    frame.length = s->frame_length;
    return KiottyNet_moveToNextState(SERVER_WRITING, KiottyNet_makeWriteEntryAction(frame));
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
    return KiottyNet_moveToNextState(SERVER_ERROR_OCCURRED, KiottyNet_makeCloseEntryAction(KIOTTY_TIMEOUT));
}

static KiottyNetTransition server_on_closed(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return KiottyNet_moveToNextState(SERVER_CLOSED, KiottyNet_makeNoEntryAction());
}

static const KiottyNetRule SERVER_RULES[] = {
    KIOTTY_RULE(KIOTTY_ANY_STATE,       KIOTTY_CONNECTION_EVENT_CONNECTED, server_on_connected, SERVER_READING_HEADER),
    KIOTTY_RULE(SERVER_READING_HEADER,  KIOTTY_CONNECTION_EVENT_READ,      server_on_header,    SERVER_READING_BODY, SERVER_ERROR_OCCURRED),
    KIOTTY_RULE(SERVER_READING_BODY,    KIOTTY_CONNECTION_EVENT_READ,      server_on_body,      SERVER_WRITING),
    KIOTTY_RULE(SERVER_WRITING,         KIOTTY_CONNECTION_EVENT_WRITTEN,   server_on_written,   SERVER_READING_HEADER),
    KIOTTY_RULE_UNCHECKED(KIOTTY_ANY_STATE, KIOTTY_CONNECTION_EVENT_TIMEOUT, server_on_timeout),
    KIOTTY_RULE_UNCHECKED(KIOTTY_ANY_STATE, KIOTTY_CONNECTION_EVENT_CLOSED,  server_on_closed),
};
static const KiottyNetStateMachine SERVER_MACHINE = { SERVER_RULES, KIOTTY_RULE_COUNT(SERVER_RULES), SERVER_READING_HEADER };

static ServerSession sessions[32];
static bool          in_use[32];

static KiottyNetConnectionContext acquire_session(KiottyNetAppContext app_context, KiottyNetConnectionHandle connection)
{
    size_t i;
    (void)app_context;
    (void)connection;
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

static void release_session(KiottyNetAppContext app_context, KiottyNetConnectionContext session, KiottyNetConnectionHandle connection)
{
    (void)app_context;
    (void)connection;
    in_use[(ServerSession*)session - sessions] = false;
}

static const KiottyNetStatefulAppContext SERVER_APP = { NULL, &SERVER_MACHINE, acquire_session, release_session };

int run_echo_server(uint16_t port)
{
    KiottyNetContext      context;
    KiottyNetServerConfig config;
    KiottyNetServerHandle server;
    KiottyNetCode         code;

    if (KiottyNet_initializeTcpServerConfig(&config, "0.0.0.0", port) != KIOTTY_SUCCESS)
    {
        return 1;
    }
    KiottyNet_configureServerMaxClients(&config, 32);
    context = KiottyNet_initializeContext();
    if (context == NULL)
    {
        return 1;
    }
    server = KiottyNet_createServer(context, &config, NULL);
    if (server == NULL)
    {
        KiottyNet_releaseContext(context);
        return 1;
    }
    code = KiottyNet_runStatefulServer(server, &SERVER_APP);                   /* 이 thread 가 IO Thread 다. 다른 thread 의 KiottyNet_closeServer 로 끝난다 */
    KiottyNet_destroyServer(context, server);
    KiottyNet_releaseContext(context);
    return code == KIOTTY_CLOSED ? 0 : 1;
}
```

---

## 4. HttpServer : Connection 별 Request / Response 공급

requirement_v0.md 9절과 `include/kiotty_net_http.h` 의 구현이다. 사용자는 `KiottyNetHttpRequest*` 를 받고 `KiottyNetHttpResponseHandle` 을 채우는데, 둘 다 Connection 마다 하나씩 있는 **HttpSession** 안에 있다. HttpSession 은 2.6 의 "라이브러리 안에 있는 Session 저장소" 다. 사용자 프로토콜이 `createConnectionContext` 로 Session 을 공급하는 것과 같은 자리에서 HttpServer 가 공급한다.

핵심은 세 가지다.

- 요청은 수신 Stage 에서 **HttpSession 의 버퍼로 복사**된다. Stage 의 데이터는 onRead 가 return 하면 사라지지만, 요청은 body 를 다 읽을 때까지, 그리고 Work 가 끝날 때까지 살아 있어야 하기 때문이다. 복사하면 `recv_capacity` (기본 4 KiB) 와 `max_header_bytes` / `max_body_bytes` (기본 8 / 64 KiB) 가 서로 묶이지 않는다.
- 응답은 HttpSession 의 응답 버퍼 하나에 쌓이고, 송신할 때 `send_capacity` 크기의 조각으로 write lane 에 옮긴다. 응답 버퍼가 write lane 보다 커도 된다.
- 메모리는 `KiottyNet_createHttpServer` 가 `max_clients` 분량을 한 번 잡는다. 그 뒤 할당은 없다.

### 4.1 HttpServer 가 잡는 것

```cpp
struct RouteEntry {                                    // 라우트, 정적 경로, Upgrade 경로가 한 표에 들어간다. 등록 순서 = 매칭 순서
    enum Kind { ROUTE, DIRECTORY, FILE, MEMORY, UPGRADE } kind;
    uint32_t           methods;                        // KIOTTY_HTTP_METHOD_BIT 의 합. ROUTE 는 비트 하나, 정적 경로는 GET | HEAD
    char               path[KIOTTY_MAX_URL_LENGTH];    // 등록 때 복사. 끝이 "/*" 면 wildcard, DIRECTORY 는 "/" 로 끝나는 접두
    size_t             path_length;
    KiottyNetHttpRoute route;                          // ROUTE, UPGRADE
    char               file_path[KIOTTY_MAX_PATH_LENGTH];                // DIRECTORY (디렉토리), FILE (파일)
    KiottyNetSpan      memory;                         // MEMORY. 복사하지 않는다. 서버 생명 동안 사용자가 유효하게 둔다
    char               content_type[KIOTTY_MAX_CONTENT_TYPE_LENGTH];     // MEMORY
};

struct HttpServer {
    KiottyNetContext      _context;
    KiottyNetServerHandle _server;                     // 7절 Server. HttpServer 는 그 위의 프로토콜일 뿐이다
    KiottyNetServerConfig _config;                     // recv_capacity, send_capacity, max_clients 를 뒤에 참조한다
    KiottyNetHttpLimits   _limits;
    RouteEntry*           _routes;                     // limits.max_routes 개
    size_t                _route_count;
    KiottyNetHttpRoute    _not_found;                  // onRequest == NULL 이면 미등록
    HttpSession*          _sessions;                   // config.max_clients 개 (4.2)
    int32_t*              _free;                       // 비어 있는 session index 의 stack
    size_t                _free_count;
    bool                  _running;                    // run 뒤의 등록은 KIOTTY_BUSY
};
```

- `createHttpServer` 는 `_server = KiottyNet_createServer(context, config, tls)` 를 만들고, 표와 session 배열을 **블록 하나**로 잡아 잘라 쓴다. 블록 크기는 `max_routes × sizeof(RouteEntry) + max_clients × sessionBytes(limits)` 다. 어느 하나라도 실패하면 전부 되돌리고 NULL 을 돌려준다.
- `sessionBytes(limits) = sizeof(HttpSession) + max_header_bytes + max_headers × sizeof(KiottyNetHttpHeader) + max_body_bytes + response_bytes`. 곱셈과 덧셈은 오버플로를 먼저 검사한다.
- `KiottyNet_destroyHttpServer` 는 base Server 를 destroy 하고 블록을 돌려준다. run 이 return 한 뒤에만 불린다는 것은 사용자 규칙이다 (base 와 같다).

### 4.2 HttpSession

```cpp
struct HttpSession {
    HttpServer*               _server;
    KiottyNetConnectionHandle _connection;
    int32_t                   _index;                  // _sessions 안의 위치. handle 의 index 부분
    int32_t                   _generation;             // 요청마다 1 씩 증가. handle 의 generation 부분 (4.5)

    // 요청. 수신 Stage 에서 여기로 복사된다
    char*                     _header_buffer;          // limits.max_header_bytes. request line + 헤더 줄들
    size_t                    _header_length;
    KiottyNetHttpHeader*      _headers;                // limits.max_headers. name / value 는 _header_buffer 안을 가리킨다
    char*                     _body_buffer;            // limits.max_body_bytes
    size_t                    _body_length;
    size_t                    _body_remaining;         // Content-Length 에서 아직 안 읽은 바이트. chunked 는 현재 chunk 의 남은 바이트
    KiottyNetHttpRequest      _request;                // 사용자에게 주는 struct. span 들은 위 버퍼를 가리킨다
    const RouteEntry*         _matched;                // 이번 요청이 고른 표 항목. 없으면 NULL
    const KiottyNetHttpRoute* _matched_route;          // 실제로 onRequest 를 부른 라우트 (표 항목 또는 _not_found). onWorkStarted 를 여기서 찾는다
    bool                      _chunked;

    // 응답. setter 가 여기에 쓴다 (4.4)
    char*                     _response_buffer;        // limits.response_bytes
    size_t                    _header_end;             // 사용자 헤더가 쌓인 끝. RESPONSE_PREFIX_BYTES 부터 시작
    int32_t                   _status;                 // 0 = setter 가 하나도 불리지 않음 → 404
    enum BodyKind { BODY_NONE, BODY_COPIED, BODY_REF, BODY_FILE } _body_kind;
    KiottyNetSpan             _body;                   // COPIED : _response_buffer 뒤쪽. REF : 사용자 포인터. FILE : 길이만 (stat 결과)
    char                      _file_path[KIOTTY_MAX_PATH_LENGTH];
    char                      _content_type[KIOTTY_MAX_CONTENT_TYPE_LENGTH];
    enum Phase { FILLING, WORKING, SENDING } _phase;   // setter 는 FILLING / WORKING 에서만 받는다

    // Work (4.6)
    bool                      _work_requested;         // startHttpWork 가 켠다. onRequest 가 return 하면 읽는다
    KiottyNetWorkHandle       _base_work;              // WORK_STARTED 이벤트가 준 base Work. finishHttpWork* 가 쓴다
    bool                      _work_finished;          // finish 는 한 번. 두 번째는 KIOTTY_BUSY

    // 송신 (4.7)
    size_t                    _sent;                   // 이번 응답에서 write lane 으로 넘긴 바이트 (헤더 블록 + body)
    size_t                    _header_start;           // 상태줄을 채운 뒤의 헤더 블록 시작 (RESPONSE_PREFIX_BYTES 보다 작거나 같다)
    FileHandle                _file;                   // BODY_FILE 일 때 열어 둔 파일. OS 의존
};
```

session 하나의 포인터 넷 (`_header_buffer`, `_headers`, `_body_buffer`, `_response_buffer`) 은 4.1 의 블록 안을 가리킨다. 포인터를 저장하는 대신 `_index × sessionBytes` 로 계산할 수도 있지만 코드가 길어져서 포인터로 둔다.

acquire / release 는 2.6 의 `createConnectionContext` / `releaseConnectionContext` 다.

```cpp
static KiottyNetConnectionContext acquireSession(KiottyNetAppContext app, KiottyNetConnectionHandle connection)
{
    HttpServer* server = static_cast<HttpServer*>(app);
    if (server->_free_count == 0) return nullptr;                                // Stateful 계층이 KIOTTY_SESSION_EXHAUSTED 로 닫는다 (2.5)
    HttpSession* s = &server->_sessions[server->_free[--server->_free_count]];
    s->_connection = connection;
    beginRequest(s);                                                             // 4.3. generation 을 올리고 요청 / 응답 필드를 비운다
    return s;
}

static void releaseSession(KiottyNetAppContext app, KiottyNetConnectionContext session, KiottyNetConnectionHandle)
{
    HttpServer*  server = static_cast<HttpServer*>(app);
    HttpSession* s      = static_cast<HttpSession*>(session);
    closeFile(s);
    ++s->_generation;                                                            // 들고 있던 response / work handle 이 모두 무효가 된다
    server->_free[server->_free_count++] = s->_index;
}
```

base 의 `max_clients` 와 session 수가 같으므로 `acquireSession` 이 NULL 을 돌려주는 일은 정상 경로에서는 없다. 그래도 검사한다.

### 4.3 요청 공급 : Stage → HttpSession

HTTP 표의 상태다. 사용자 프로토콜의 표와 같은 `KiottyNetStateMachine` 이고, HttpServer 의 `.cpp` 안에 `static const` 로 있다.

| 상태 | entry action | READ 가 오면 |
|---|---|---|
| `ReadingRequestLine` | `readUntil(CRLF, min(max_header_bytes, recv_capacity))` + `header_timeout_ms` | 줄을 `_header_buffer` 에 복사하고 method / path / query 를 자른다. 모르는 method 는 501, 긴 path 는 414 |
| `ReadingHeaderLine` | `readUntil(CRLF, min(남은 header 공간, recv_capacity))` (timeout 은 그대로 흐른다) | 빈 줄이면 헤더 끝. 아니면 복사하고 `_headers[n] = { name, value }`. `max_headers` 를 넘으면 431 |
| `ReadingBody` | `read(min(_body_remaining, recv_capacity))` | `_body_buffer` 에 이어 붙이고 `_body_remaining` 을 줄인다. 0 이 되면 `handle(s)` (아래) |
| `ReadingChunkSize` / `ReadingChunkData` / `ReadingChunkTrailer` | chunked 전용. 크기 줄은 `readUntil(CRLF)`, 데이터는 `ReadingBody` 와 같이 조각으로 | 합친 길이가 `max_body_bytes` 를 넘으면 413 |
| `Working` | `startWork()` | WORK_STARTED 에서 `onWorkStarted` (4.6) |
| `Sending` | `write(nextChunk(s))` | WRITTEN 마다 다음 조각. 끝나면 keep-alive 면 `ReadingRequestLine` + `keep_alive_timeout_ms`, 아니면 `close(KIOTTY_SUCCESS)` |

**헤더는 줄 단위로 읽는다.** `readUntil(CRLFCRLF, max_header_bytes)` 한 번이 더 간단하지만, 그러면 `max_header_bytes ≤ recv_capacity` 여야 한다 (1.2 의 `REQUEST_TOO_LARGE`). 줄 단위로 읽으면 Stage 에 걸리는 조건은 "줄 하나 ≤ recv_capacity" 뿐이고 헤더 전체 크기는 `_header_buffer` 가 정한다. 그래서 기본값 (recv 4 KiB, header 8 KiB) 이 그대로 성립한다. 줄이 `recv_capacity` 를 넘으면 base 가 `KIOTTY_PROTOCOL_ERROR` 로 닫고, 헤더 공간이 모자라면 HttpServer 가 431 을 보낸다.

```cpp
static void beginRequest(HttpSession* s)
{
    ++s->_generation;                                  // 이전 요청의 response / work handle 무효화
    s->_header_length = 0;  s->_body_length = 0;  s->_body_remaining = 0;  s->_chunked = false;
    s->_matched = nullptr;
    memset(&s->_request, 0, sizeof s->_request);
    s->_request.connection = s->_connection;
    s->_request.headers    = s->_headers;
    s->_header_end = RESPONSE_PREFIX_BYTES;  s->_status = 0;  s->_body_kind = HttpSession::BODY_NONE;
    s->_phase = HttpSession::FILLING;  s->_work_requested = false;  s->_work_finished = false;  s->_sent = 0;
}

static KiottyNetTransition onHeaderLine(KiottyNetConnectionContext session, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    HttpSession*  s    = static_cast<HttpSession*>(session);
    KiottyNetSpan line = e.as.read.data;                                         // CRLF 포함. return 하면 무효라 먼저 복사한다
    if (line.length == 2) return afterHeaders(s);                                // 빈 줄 = 헤더 끝
    if (s->_header_length + line.length > s->_server->_limits.max_header_bytes) return reject(s, 431);
    if (s->_request.header_count >= s->_server->_limits.max_headers)             return reject(s, 431);
    char* at = s->_header_buffer + s->_header_length;
    memcpy(at, line.data, line.length - 2);                                      // CRLF 는 버린다
    s->_header_length += line.length - 2;
    KiottyNetHttpHeader* h = &s->_headers[s->_request.header_count++];
    if (!splitHeader(at, line.length - 2, h)) return reject(s, 400);            // "name: value". name 은 공백 없는 token, value 는 양끝 trim
    return Next::moveToNextState(HttpState::ReadingHeaderLine, KiottyNet_EntryAction::readUntil(CRLF, lineMax(s)));
}

static KiottyNetTransition afterHeaders(HttpSession* s)
{
    const KiottyNetHttpLimits& limits = s->_server->_limits;
    KiottyNetSpan te = KiottyNet_findHttpHeader(&s->_request, "Transfer-Encoding");
    KiottyNetSpan cl = KiottyNet_findHttpHeader(&s->_request, "Content-Length");
    s->_request.keep_alive = keepAlive(s);                                       // HTTP/1.1 이고 "Connection: close" 가 아니면 1
    s->_matched = match(s->_server, s->_request.method, s->_request.path);       // 4.4. 405 / 404 판정은 handle 에서
    if (equalsIgnoreCase(te, "chunked")) { s->_chunked = true; return Next::moveToNextState(HttpState::ReadingChunkSize, KiottyNet_EntryAction::readUntil(CRLF, lineMax(s))); }
    if (cl.length) {
        if (!parseLength(cl, &s->_body_remaining))          return reject(s, 400);
        if (s->_body_remaining > limits.max_body_bytes)     return reject(s, 413);   // 읽기 전에 거절. 연결은 닫는다 (body 를 버릴 수 없다)
        if (s->_body_remaining > 0) return Next::moveToNextState(HttpState::ReadingBody, KiottyNet_EntryAction::read(bodyChunk(s)));
    }
    return handle(s);
}

static size_t lineMax(const HttpSession* s)  { return min(s->_server->_config.recv_capacity, s->_server->_limits.max_header_bytes - s->_header_length + 2); }
static size_t bodyChunk(const HttpSession* s) { return min(s->_body_remaining, s->_server->_config.recv_capacity); }
```

- `_request.path` / `query` / `headers[i].name` / `value` 는 모두 `_header_buffer` 안의 span 이다. 그래서 다음 `beginRequest` 까지, 즉 onRequest 가 return 하거나 Work 가 finish 될 때까지 유효하다. 요구사항 9절의 "request 는 defer 한 뒤 resumed 까지 유효" 가 이것이다.
- body 는 `recv_capacity` 조각으로 받아 `_body_buffer` 에 이어 붙인다. 조각마다 READ 가 오고 같은 상태로 돌아간다 (`ReadingBody → READ → ReadingBody`). `_request.body = { _body_buffer, _body_length }`.
- GET / HEAD 에 body 가 와도 같은 길로 읽는다. 읽고 나서 `_request.body` 로 주되 사용자가 보통 보지 않을 뿐이다. 요청 경계를 맞추려면 읽어야 한다.
- `reject(s, status)` 는 응답 setter 로 status 만 놓고 `_request.keep_alive = 0` 으로 바꾼 뒤 `startSending(s)` 로 간다. 자동 응답도 사용자 응답과 같은 버퍼, 같은 송신 경로다.

`handle(s)` 가 사용자 콜백을 부른다.

```cpp
static KiottyNetTransition handle(HttpSession* s)
{
    const RouteEntry* m = s->_matched;
    if (!m) {
        if (s->_server->_not_found.onRequest) callRoute(s, &s->_server->_not_found);
        else                                  s->_status = 404;
        return afterHandle(s);
    }
    if (!(m->methods & KIOTTY_HTTP_METHOD_BIT(s->_request.method))) {
        if (s->_request.method == KIOTTY_HTTP_HEAD && (m->methods & KIOTTY_HTTP_METHOD_BIT(KIOTTY_HTTP_GET))) { /* GET 으로 처리 */ }
        else if (s->_request.method == KIOTTY_HTTP_OPTIONS) { s->_status = 204; setHeader(s, "Allow", allowList(s->_server, s->_request.path)); return afterHandle(s); }
        else { s->_status = 405; setHeader(s, "Allow", allowList(s->_server, s->_request.path)); return afterHandle(s); }
    }
    switch (m->kind) {
        case RouteEntry::ROUTE:     callRoute(s, &m->route); break;
        case RouteEntry::DIRECTORY: serveFile(s, joinUnderRoot(m->file_path, s->_request.path, m->path_length)); break;   // ".." 가 있으면 403
        case RouteEntry::FILE:      serveFile(s, m->file_path); break;
        case RouteEntry::MEMORY:    s->_status = 200; setBodyRef(s, m->memory, m->content_type); break;
        case RouteEntry::UPGRADE:   /* websocket 모듈. 5절 미결 */ break;
    }
    return afterHandle(s);
}

static void callRoute(HttpSession* s, const KiottyNetHttpRoute* route)
{
    s->_matched_route = route;                                                   // onWorkStarted 를 찾기 위해
    route->onRequest(route->app_context, &s->_request, responseHandle(s));       // 사용자 코드. 여기서 setter 또는 startHttpWork
}

static KiottyNetTransition afterHandle(HttpSession* s)
{
    if (s->_work_requested) { s->_phase = HttpSession::WORKING; return Next::moveToNextState(HttpState::Working, KiottyNet_EntryAction::startWork()); }
    return startSending(s);
}
```

- `match` 는 표를 등록 순서대로 훑어 **path 가 맞는 첫 항목**을 돌려준다. method 는 그 뒤에 본다. 같은 path 에 GET 과 POST 라우트가 따로 등록되어 있으면 두 항목의 path 가 같으므로, `match` 는 path 가 같은 항목들 중 method 비트가 맞는 것을 고르고, 하나도 없으면 첫 항목을 돌려준다 (405 판정용). `allowList` 는 path 가 같은 모든 항목의 methods 를 OR 해서 `"GET, HEAD, OPTIONS"` 꼴로 만든다.
- 사용자 콜백은 IO Thread 에서, Stateful 계층의 transition action 안에서 불린다. 그래서 콜백 안에서 부른 setter 는 2절의 규칙과 충돌하지 않는다. 콜백 안에서 Connection 함수를 직접 부를 길은 없다 (handle 을 주지 않는다).

### 4.4 응답 공급 : setter → 응답 버퍼

응답 버퍼 (`response_bytes`) 의 배치다.

```
_response_buffer
│← RESPONSE_PREFIX_BYTES →│← 사용자 헤더 (앞에서 뒤로) →│      (빈 공간)      │← 복사된 body (뒤에서 앞으로) →│
│  상태줄 + 라이브러리 헤더  │  "X-Foo: bar\r\n" ...          │                     │                                 │
^                          ^                                 ^                     ^                                 ^
0                 RESPONSE_PREFIX_BYTES                  _header_end         _body.data                      response_bytes
```

- `RESPONSE_PREFIX_BYTES` 는 상태줄과 라이브러리가 넣는 헤더 (`Content-Length`, `Connection`, `Date`, `Content-Type`) 의 최대 길이다. 상수 (예: 192) 로 두고 `createHttpServer` 가 `response_bytes > RESPONSE_PREFIX_BYTES + 2` 를 검사한다.
- 사용자 헤더는 `_header_end` 에서 앞으로 쌓인다. 복사된 body 는 **버퍼 끝에서 뒤로** 놓인다. 그래서 헤더와 body 를 어느 순서로 불러도 서로 밀어내지 않는다. 둘이 만나면 `KIOTTY_BUFFER_FULL` 이다.
- 송신 직전 `finalizeHeaders(s)` 가 상태줄과 라이브러리 헤더를 stack 의 작은 버퍼에 만들고, 그 길이만큼 `RESPONSE_PREFIX_BYTES` 에서 **오른쪽 정렬**로 복사한다. 그러면 `[_header_start, _header_end)` 가 빈 줄 `\r\n` 까지 포함한 연속된 헤더 블록이 된다. 송신은 이 블록 뒤에 body 를 이어 보낸다.

```cpp
static KiottyNetCode setHeader(HttpSession* s, const char* name, const char* value)
{
    if (s->_phase == HttpSession::SENDING) return KIOTTY_BUSY;
    size_t n = strnlen(name, s->_server->_limits.response_bytes), v = strnlen(value, s->_server->_limits.response_bytes);
    size_t need = n + 2 + v + 2;                                                 // "name: value\r\n"
    if (need > bodyStart(s) - s->_header_end - 2) return KIOTTY_BUFFER_FULL;     // 마지막 "\r\n" 자리를 남긴다
    char* at = s->_response_buffer + s->_header_end;
    memcpy(at, name, n);  memcpy(at + n, ": ", 2);  memcpy(at + n + 2, value, v);  memcpy(at + n + 2 + v, "\r\n", 2);
    s->_header_end += need;
    if (s->_status == 0) s->_status = 200;                                       // setter 가 하나라도 불리면 기본 200
    return KIOTTY_SUCCESS;
}

static KiottyNetCode setBody(HttpSession* s, KiottyNetSpan data, const char* content_type)
{
    if (s->_phase == HttpSession::SENDING) return KIOTTY_BUSY;
    if (data.length > s->_server->_limits.response_bytes - s->_header_end - 2) return KIOTTY_BUFFER_FULL;
    char* at = s->_response_buffer + s->_server->_limits.response_bytes - data.length;
    memcpy(at, data.data, data.length);                                          // 이전 body 가 있었으면 덮어쓴다 (셋 중 하나만)
    s->_body_kind = HttpSession::BODY_COPIED;  s->_body = { at, data.length };
    copyContentType(s, content_type);                                            // NULL 이면 "application/octet-stream". 길이 초과는 KIOTTY_INVALID_ARGUMENT
    if (s->_status == 0) s->_status = 200;
    return KIOTTY_SUCCESS;
}
```

- `bodyStart(s)` 는 `BODY_COPIED` 면 `_body.data - _response_buffer`, 아니면 `response_bytes` 다.
- `setBodyRef` 는 복사 없이 `_body = data` 를 들고 `BODY_REF`. `setFile` 은 경로를 `_file_path` 에 복사하고 `stat` 으로 길이를 얻어 `_body.length` 에 둔다 (`BODY_FILE`). 파일이 없으면 `KIOTTY_NOT_FOUND`, `..` 가 있으면 `KIOTTY_INVALID_ARGUMENT`. 파일은 송신 시작 때 연다.
- 공개 함수 `KiottyNet_setHttpStatus` 등은 handle 을 session 으로 바꾸고 (4.5) 이 static 함수를 부른다. `_status` 는 100..599 만 받는다.
- 자동 응답 (404, 405, 413, 431, 501, 204 + Allow) 도 같은 setter 를 쓴다. 다른 길이 없다.
- HEAD : 라우트는 GET 으로 실행되고 setter 도 다 동작한다. `finalizeHeaders` 가 `Content-Length` 를 `_body.length` 로 쓰되, 송신 (4.7) 이 body 조각을 내보내지 않는다.

### 4.5 handle : HttpSession 을 int64 로

`KiottyNetHttpResponseHandle` 과 `KiottyNetHttpWorkHandle` 은 같은 값이다. 둘 다 `{ generation = s->_generation, index = s->_index }` 를 `kiotty::net::PoolHandle` 로 묶은 것이고, 타입만 다르다. Work handle 을 따로 둔 이유는 사용자가 base 의 `KiottyNet_finishWorkWithWrite` 로 HTTP 프레이밍을 건너뛰지 못하게 하려는 것이다.

```cpp
static KiottyNetHttpResponseHandle responseHandle(const HttpSession* s) { return packHandle(s->_generation, s->_index); }

static HttpSession* sessionOf(int64_t handle)                // setter / finish 의 첫 줄. 실패하면 NULL 이고 공개 함수는 KIOTTY_PEER_CLOSED 를 돌려준다
{
    PoolHandle   h      = unpackHandle(handle);
    HttpServer*  server = ownerOf(h.index);                   // Connection handle 과 같은 방식. requirement 9절 미결 "pool handle 을 소유자로 되돌리기" 를 따른다
    if (!server || slotOf(h.index) >= server->_config.max_clients) return nullptr;
    HttpSession* s = &server->_sessions[slotOf(h.index)];
    if (s->_generation != h.generation) return nullptr;       // 이전 요청 / 닫힌 연결의 handle
    return s;
}
```

- `_generation` 은 요청마다 (`beginRequest`), 연결이 닫힐 때 (`releaseSession`) 오른다. 그래서 keep-alive 로 같은 session 이 다음 요청을 받으면 이전 응답 handle 은 자동으로 무효다. worker 가 뒤늦게 setter 를 부르면 `KIOTTY_PEER_CLOSED` 만 받는다.
- 소유자 (HttpServer) 를 찾는 방법은 base 의 Connection handle 과 같은 방식을 쓴다 (전역 소유자 표 또는 index 상위 비트). 그 결정은 5절 미결이고, 이 모듈은 `ownerOf` / `slotOf` 두 함수 뒤에 숨긴다.
- `_generation` 은 IO Thread 만 쓴다. worker 는 `sessionOf` 에서 읽기만 한다. Work 중에는 IO Thread 가 이 session 에 아무 일도 하지 않으므로 (5절 Work 규칙) 값이 바뀌지 않고, Work 가 아닐 때 worker 가 handle 을 들고 있다면 그것은 사용자 규칙 위반이다. 그래도 읽기는 int32 하나라 찢어진 값은 없다.

### 4.6 Work

```cpp
KiottyNetCode KiottyNet_startHttpWork(KiottyNetHttpResponseHandle handle)
{
    HttpSession* s = sessionOf(handle);
    if (!s) return KIOTTY_PEER_CLOSED;
    if (s->_phase != HttpSession::FILLING || !s->_matched_route) return KIOTTY_BUSY;   // onRequest 안에서만, 한 번만
    if (!s->_matched_route->onWorkStarted) return KIOTTY_INVALID_ARGUMENT;
    s->_work_requested = true;                                                   // 실제 startWork 는 onRequest 가 return 한 뒤 afterHandle 의 entry action 이 한다
    return KIOTTY_SUCCESS;
}

static KiottyNetTransition onWorkStarted(KiottyNetConnectionContext session, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    HttpSession* s = static_cast<HttpSession*>(session);
    s->_base_work = e.as.work_started.work;
    const KiottyNetHttpRoute* r = s->_matched_route;
    r->onWorkStarted(r->app_context, &s->_request, responseHandle(s), workHandle(s));   // 사용자가 handle 둘을 worker 로 넘긴다
    return Next::moveToNextState(HttpState::Working);                            // entry action 없음 = Work 를 그대로 둔다 (2.4). finish 는 worker 의 몫
}

KiottyNetCode KiottyNet_finishHttpWorkWithResponse(KiottyNetHttpWorkHandle handle)
{
    HttpSession* s = sessionOf(handle);
    if (!s) return KIOTTY_PEER_CLOSED;
    if (s->_phase != HttpSession::WORKING || s->_work_finished) return KIOTTY_BUSY;
    s->_work_finished = true;
    s->_phase = HttpSession::SENDING;
    finalizeHeaders(s);                                                          // 4.4. worker thread 에서 한다. IO Thread 는 이 session 을 안 본다
    KiottyNetCode code = KiottyNet_finishWorkWithWrite(s->_base_work, nextChunk(s));   // 첫 조각. 나머지는 WRITTEN 이벤트가 Sending 상태에서 이어 간다
    if (code != KIOTTY_SUCCESS) KiottyNet_finishWorkWithClose(s->_base_work, code);
    return code;
}

KiottyNetCode KiottyNet_finishHttpWorkWithClose(KiottyNetHttpWorkHandle handle, KiottyNetCode reason)
{
    HttpSession* s = sessionOf(handle);
    if (!s) return KIOTTY_PEER_CLOSED;
    if (s->_phase != HttpSession::WORKING || s->_work_finished) return KIOTTY_BUSY;
    s->_work_finished = true;
    return KiottyNet_finishWorkWithClose(s->_base_work, reason);
}

bool KiottyNet_isHttpWorkConnected(KiottyNetHttpWorkHandle handle)
{
    HttpSession* s = sessionOf(handle);
    return s && s->_phase == HttpSession::WORKING && KiottyNet_isWorkConnected(s->_base_work);
}
```

- Work 중 worker 가 setter 를 부르는 것이 안전한 이유는 base 의 Work 규칙 그대로다. WORKING 동안 IO Thread 는 이 Connection 에 콜백을 내지 않고 write lane 도 건드리지 않는다. session 의 응답 필드는 write lane 과 같은 소유 규칙을 따른다. RUNNING 이면 IO Thread, WORKING 이면 worker, 동시에 둘인 순간은 없다.
- 첫 조각이 `send_capacity` 를 넘는 일은 없다. `nextChunk` 가 `send_capacity` 로 자른다. 그래서 `finishWorkWithWrite` 가 `KIOTTY_BUFFER_FULL` 을 돌려주는 경우는 없고, 돌려주는 코드는 `KIOTTY_PEER_CLOSED` 다.
- Work 중 peer 가 끊으면 base 가 slot 을 붙잡아 두고 finish 뒤 `onDisconnected(KIOTTY_PEER_CLOSED)` 를 낸다. 그때 `releaseSession` 이 돌아 generation 이 오른다. finish 전까지 session 은 살아 있으므로 worker 의 setter 는 안전하다.

### 4.7 송신 : 응답 버퍼 → write lane 조각

```cpp
static KiottyNetTransition startSending(HttpSession* s)      // IO Thread 경로 (Work 없이 onRequest 가 return 한 경우, 자동 응답)
{
    s->_phase = HttpSession::SENDING;
    finalizeHeaders(s);
    return Next::moveToNextState(HttpState::Sending, KiottyNet_EntryAction::write(nextChunk(s)));
}

static KiottyNetSpan nextChunk(HttpSession* s)               // _sent 부터 최대 send_capacity 바이트. 헤더 블록 → body 순서
{
    size_t cap    = s->_server->_config.send_capacity;
    size_t header = s->_header_end - s->_header_start;
    if (s->_sent < header) { size_t n = min(cap, header - s->_sent); return { s->_response_buffer + s->_header_start + s->_sent, n }; }
    if (s->_request.method == KIOTTY_HTTP_HEAD) return { nullptr, 0 };
    size_t off = s->_sent - header;
    switch (s->_body_kind) {
        case HttpSession::BODY_COPIED:
        case HttpSession::BODY_REF:  return { s->_body.data + off, min(cap, s->_body.length - off) };
        case HttpSession::BODY_FILE: return readFileInto(s, s->_response_buffer + RESPONSE_PREFIX_BYTES, min(cap, s->_body.length - off));   // 헤더 블록은 이미 나갔으므로 그 자리를 파일 조각의 staging 으로 쓴다
        default:                     return { nullptr, 0 };
    }
}

static KiottyNetTransition onSent(KiottyNetConnectionContext session, KiottyNetConnectionHandle, KiottyNetConnectionEvent e)
{
    HttpSession* s = static_cast<HttpSession*>(session);
    s->_sent += e.as.written.length;
    KiottyNetSpan next = nextChunk(s);
    if (next.length) return Next::moveToNextState(HttpState::Sending, KiottyNet_EntryAction::write(next));
    closeFile(s);
    if (!s->_request.keep_alive) return Next::moveToNextState(HttpState::Closing, KiottyNet_EntryAction::close(KIOTTY_SUCCESS));
    beginRequest(s);
    return Next::moveToNextState(HttpState::ReadingRequestLine, KiottyNet_EntryAction::readUntil(CRLF, lineMax(s)).timeout(s->_server->_limits.keep_alive_timeout_ms));
}
```

- 조각이 하나뿐이면 `write → WRITTEN → 다음 요청` 으로 끝난다. 큰 응답은 `Sending → WRITTEN → Sending` 을 돈다. 1.6 의 write lane 규칙 (한 번에 메시지 하나, onWritten 뒤 다음 write) 을 그대로 지킨다.
- 파일은 `send_capacity` 만큼 읽어 보낸다. staging 은 응답 버퍼의 prefix + 헤더 영역을 재사용하므로 추가 메모리가 없다. `sendfile` / `TransmitFile` 은 Stage 를 거치지 않는 OS 경로라 base 의 drain 과 맞지 않으므로 v0 에서는 쓰지 않는다 (요구사항 9절의 "가능하면" 을 "v0 에서는 하지 않음" 으로 둔다).
- `write` 의 data 는 `send_capacity` 이하이고 write lane 은 비어 있으므로 (이전 WRITTEN 뒤) `KIOTTY_BUFFER_FULL` 로 닫히는 일은 없다.

### 4.8 run

```cpp
KiottyNetCode KiottyNet_runHttpServer(KiottyNetHttpServerHandle server)
{
    if (!server) return KIOTTY_INVALID_ARGUMENT;
    server->_running = true;
    KiottyNetStatefulAppContext stateful = { server, &HTTP_MACHINE, &acquireSession, &releaseSession };   // run 동안만 살아 있으면 된다 (2.1)
    KiottyNetCode code = KiottyNet_runStatefulServer(server->_server, &stateful);
    server->_running = false;
    return code;
}
```

`close` / `await` 는 base 로 그대로 넘긴다. `addHttpRoute` / `serveHttp*` 는 `_running` 이면 `KIOTTY_BUSY` 다. 표는 run 중에 IO Thread 가 읽기만 하고 등록 함수는 run 전에만 쓰므로 lock 이 없다.

HttpServer 가 base 에 더 요구하는 것은 없다. 쓰는 것은 `createServer / runStatefulServer / closeServer / awaitServer / destroyServer`, entry action 다섯 가지, `finishWorkWith{Write, Close}`, `isWorkConnected` 뿐이다.

---

## 5. 미결

- done ring 의 MPSC 구현은 1.5 대로 lock-free 로 간다. 32 client 라 mutex 로도 충분하지만 IO Thread 가 worker 에 막히는 일이 없어야 해서다.
- WebSocket upgrade 뒤 같은 Connection 이 HTTP 표에서 WebSocket 표로 넘어가는 방법. 두 표를 한 표로 합치는지, Connection 마다 machine 포인터를 덮어쓸 수 있게 하는지.
- HttpResponse / HttpWork handle 에서 HttpServer 를 찾는 `ownerOf(index)` (4.5). Connection handle 의 소유자 찾기와 같은 결정을 기다린다.
- 정적 파일의 `sendfile` / `TransmitFile`. v0 는 응답 버퍼를 staging 으로 쓰는 `read + write` 조각 송신만 한다 (4.7). OS 경로를 쓰려면 drain 이 "파일 조각" 을 lane 의 한 종류로 알아야 한다.

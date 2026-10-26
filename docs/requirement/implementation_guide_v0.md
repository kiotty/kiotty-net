# Kiotty-Net 구현 가이드 v0

[requirement_v0.md](requirement_v0.md) 는 외부 인터페이스와 규칙만 담는다. 이 문서는 그 인터페이스를 어떻게 구현하는지의 예시와 설계 근거다.
두 문서가 다르면 requirement_v0.md 가 우선한다. 여기의 코드는 의사코드가 아니라 컴파일되는 형태로 쓰되, 소켓과 IO loop 같은 OS 의존 부분은 호출 이름만 적는다.

목차

1. Stage 와 수신 흐름 (ReadRequest, Connection::read / readUntil, IOThread::pump, resume 큐)
2. StateMachine 내부 (설계 근거, rules() 컴파일 타임 정렬, C++ Runner, C runner)
3. StateMachine 활용 예제 (HTTP/1.1 서버, WebSocket frame 수신, C 표)
4. 검증 상태
5. 미결

---

## 1. Stage 와 수신 흐름

requirement_v0.md 4절의 `Stage` 와 5절의 `Connection::read / readUntil` 이 IO Thread 에서 어떻게 맞물리는지다.
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
    Kind     kind;          // NONE  : read 요청 없음 (write 중, suspend 중, close 중). 들어온 데이터는 Stage 에 쌓아만 둔다
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
        close(connection, read_length == 0 ? PEER_CLOSED : SOCKET_ERROR);   // suspend 중이면 미뤄 둔다 (1.5). 아니면 onDisconnected 한 번
        return;
    }
    connection->_recv_stage.provider().commit(read_length);
    pump(connection);
}

// 쌓인 데이터로 충족되는 read 요청을 전부 처리하고, 필요하면 recv 를 다시 건다.
// recv 완료 뒤에만 부르는 것이 아니다. onConnected / onWritten / onResumed / onTimeout 어떤 콜백이든 전달이 끝나면
// IOThread 는 pump() 를 한 번 돈다. 콜백 안에서 등록한 요청이 이미 Stage 에 있는 데이터로 충족될 수 있기 때문이다.
void IOThread::pump(Connection* connection)
{
    Stage&             stage    = connection->_recv_stage;
    Stage::Provider&   provider = stage.provider();
    Stage::Consumer&   consumer = stage.consumer();
    ConnectionHandler& handler  = connection->_handler;

    // 한 번의 recv 로 요청 여러 개가 연달아 충족될 수 있다 (예: WS 의 header, ext length, mask, payload 가 한 패킷에 들어옴).
    for (;;) {
        ReadRequest& req    = connection->_read_request;
        const char*  buffer = 0;
        size_t       length = 0;

        if (connection->_suspended) break;                     // suspend 중. 콜백을 내보내지 않는다
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
        handler.onRead(handler.user_context, connection, data);
        consumer.commit(length);                               // 콜백이 return 한 뒤에 commit. 이 줄부터 data 는 무효
        if (connection->_closing) return;                      // 콜백 안에서 close() 가 불림. 이후 recv 를 걸지 않는다
    }

    stage.compact();                                           // 남은 데이터를 앞으로 당겨 provider 공간을 확보

    // recv 재등록. 요청이 NONE 이거나 suspend 중이어도 상대가 보내는 데이터는 계속 받아 Stage 에 쌓는다. Stage 가 가득 차면
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
RUNNING ──startWork──> DRAINING ──(송신 비움, 다음 loop)──> WORKING ──finish*──> DONE ──(IO Thread 가 요청 적용)──> RUNNING
```

| 전이 | 누가 | 방법 |
|---|---|---|
| RUNNING → DRAINING | startWork 를 부른 thread (IO Thread 콜백 안이거나 앱 thread) | CAS. 실패하면 `KIOTTY_BUSY`. payload 와 generation 을 슬롯에 기록하고 IO Thread 를 깨운다 |
| DRAINING → WORKING | IO Thread | 송신 Stage 가 비고 걸린 send 완료가 모두 돌아온 뒤, 다음 loop 에서 저장(store). 걸려 있던 read 요청과 남은 deadline 을 `_saved_request`, `_saved_deadline` 에 옮긴다. 그 다음 onWorkStarted |
| WORKING → DONE | finish* 를 부른 thread | 요청(op, n, delimiter, timeout)을 `_finish_request` 에 쓰고 CAS. 실패하면 `KIOTTY_BUSY` (이미 finish 됨). generation 이 다르면 `KIOTTY_PEER_CLOSED`. 성공하면 done ring 에 index 를 넣고 깨운다 |
| DONE → RUNNING | IO Thread | done ring 에서 꺼내 `_finish_request` 를 적용한다. `finishWork` / `finishWithWrite` 는 `_saved_request` 와 `_saved_deadline` 을 복원. 그 뒤 pump 한 번 (쌓인 데이터로 바로 onRead 가 나올 수 있다) |

**finishWithWrite 의 복사.** WORKING 상태에서 송신 Stage 를 만지는 thread 는 worker 하나뿐이다 (IO Thread 는 DRAINING 에서 다 비웠고 WORKING 에서는 송신 Stage 를 건드리지 않는다). 그래서 worker 가 Stage 의 Provider 로 직접 복사해도 된다. 복사는 CAS 전에 끝나므로 IO Thread 가 DONE 을 본 시점에는 데이터가 다 들어 있다. 여유가 없으면 상태를 바꾸지 않고 `KIOTTY_BUFFER_FULL`.

**WORKING 중 IO Thread 가 하는 일.** recv 는 계속 걸어 수신 Stage 에 쌓는다. 가득 차면 recv 를 걸지 않는다. FIN / 에러는 `_peer_closed` (atomic) 에 기록만 하고 close 는 미룬다. deadline scan 은 WORKING 인 연결을 건너뛴다. `KiottyNet_isWorkConnected` 는 generation 일치와 `_peer_closed == false` 를 읽는다. DONE → RUNNING 전이에서 `_peer_closed` 면 요청을 적용하는 대신 `close(KIOTTY_PEER_CLOSED)` 한다.

**깨우기.** Windows 는 `PostQueuedCompletionStatus` 로 index 를 실어 보낸다. Linux 는 eventfd (io_uring 은 msg ring), mac 은 pipe 로 깨운 뒤 done ring 을 비운다.

**done ring.** 연결 하나는 동시에 하나의 DONE 만 가질 수 있으므로 `max_clients` 칸이면 넘치지 않는다. worker 가 여럿이라 producer 는 여럿, consumer 는 IO Thread 하나다. `fetch_add` 로 칸을 받아 index 를 쓰고 그 칸의 ready 플래그를 release 로 세운다. IO Thread 는 head 부터 ready 가 세워진 칸을 acquire 로 읽어 비운다. startWork 를 앱 thread 가 부르는 경우도 같은 ring 과 깨우기를 쓴다 (전이만 다르다).

**소유자 close().** WORKING 인 연결은 `_peer_closed` 와 무관하게 `onDisconnected(KIOTTY_CLOSED)` 를 내고 generation 을 올린다. 뒤늦은 finish* 는 generation 불일치로 `KIOTTY_PEER_CLOSED` 다. destroy 뒤의 finish* 도 같은 길로 no-op 이 되려면 handle 에서 소유자를 찾는 전역 표에도 generation 이 있어야 한다 (요구사항 미결).

**timeout.** `Connection::setTimeout(ms)` 는 Connection 의 deadline 하나를 갱신한다. IO loop 는 대기 시간을 가장 가까운 deadline 으로 잡고 (IOCP: `GetQueuedCompletionStatusEx` 의 timeout,
epoll: timerfd 또는 timeout 인자, kqueue: `EVFILT_TIMER`), 깨어나면 배열 scan 으로 만료된 Connection 에 `onTimeout` 을 전달한다. DRAINING 인 연결은 만료되면 onTimeout 대신 `close(KIOTTY_TIMEOUT)`, WORKING 인 연결은 건너뛴다. 32 client 면 scan 으로 충분하다.

---

## 2. StateMachine 내부

### 2.1 왜 StateMachine 인가

콜백 모델만 있으면 프로토콜 작성자는 콜백마다 `switch (state)` 를 쓰게 된다.

```cpp
onResumed(Connection* connection, void* user_context)
{
    auto& next_state = user_context->state;
    when(next_state) {
        State::V1 -> connection->read(user_context->buffer, length);
        State::V2 -> connection->write(user_context->buffer, length);
    }
}
onRead, onWritten(Connection* connection, void* user_context, const char* buffer, size_t length)
{
    auto& next_state = when(user_context->state) {
        State::V1 -> doSomething(buffer, length);
        State::V4 -> { connection->suspend(); doLongtermSomething(connection->id(), ...); }
    }
    when(next_state) { State::V1 -> connection->read(...); State::V2 -> connection->write(...); }
}
```

같은 switch 가 콜백 수만큼 복제되고, "지금 read 가 걸려 있는데 또 read 를 부른다" 같은 상태 불일치를 막을 방법이 없다.
StateMachine 은 이것을 `(Old State, Event) -> Handler -> (New State, Request)` 한 줄로 바꾼다. Handler 는 Request 를 **return** 할 뿐 IO 를 직접 부르지 않으므로
라이브러리가 요청의 개수와 순서를 보장할 수 있다.

### 2.2 설계 포인트

1. **Handler 는 순수 함수.** 입력 (Session, Event), 출력 Transition. `Connection&` 을 받지만 `info()`, `id()` 조회용이다. debug 빌드에서는 Handler 안의 `read/write/suspend/close` 호출을 assert 로 막는다.
2. **표 = 문서 = 계약.** `rules(...)` 하나가 상태 전이표다. `.to()` 가 목적지를 표에 올리고 debug Runner 가 그것을 검사한다. 매칭되지 않는 (state, event) 는 `CLOSED(UNHANDLED_EVENT)` 로 끊는다. 조용히 무시하지 않는다.
3. **메모리.** 표는 `.rodata` 의 상수, Session 은 Runner 의 Slot 배열에 고정 크기, `Event::data` 는 수신 Stage 를 직접 가리킨다. 핸들러가 return 하면 `consumer.commit()` 되므로 보관하려면 Session 으로 복사한다. 런타임 동적 할당은 0.
4. **read 요청은 두 종류뿐.** `read(exact)` 와 `readUntil(delim, max)`. 큰 body 는 `read(min(remaining, chunk))` 반복으로, chunked encoding 은 `readUntil("\r\n")` 뒤 `read(size + 2)` 로 된다. `readSome` 은 프레이밍 없는 raw 터널링에만 필요해서 뺐다.
5. **Transition 은 요청 하나.** read 또는 write 중 하나다. `andWrite` 로 둘을 같이 던질 필요가 없는 이유는 1절의 흐름 때문이다. write 하는 동안 들어온 데이터는 Stage 에 쌓였다가 다음 read 가 등록되는 즉시 READ 로 온다. "pong 보내고 다음 frame 읽기" 는 상태 하나(`PongSent`)로 표현하고, 그게 오히려 표에 흐름을 드러낸다.
6. **push 도 표를 거친다.** 다른 thread 의 `Server::resume(id, payload)` 는 `RESUMED` 이벤트로 들어온다. 수신 흐름 중간(예: WS 의 Mask 상태)에 올 수 있으므로 핸들러는 `Transition::stay(Request::write(frame))` 로 상태를 유지한 채 write 만 낸다. 그 write 의 `WRITTEN` 은 `when(WRITTEN).then(ignore)` 행이 `stay()` 로 흡수한다. 정확 일치 행이 ANY 행보다 우선하므로 `from(PongSent).when(WRITTEN)` 같은 행은 영향받지 않는다. 단, PongSent 상태에서 push 의 WRITTEN 이 pong 의 WRITTEN 보다 먼저 오면 onPongSent 가 한 번 일찍 불린다. read(2) 가 그만큼 일찍 등록될 뿐 동작은 같다.
7. **timeout 은 시간이 들어오는 유일한 입구.** 헤더 반만 보내고 멈춘 peer 가 슬롯을 영원히 잡는 것을 막고, WS keep-alive ping 을 "30초간 READ 가 없으면 ping" 으로, 클라이언트 재시도를 "TIMEOUT 이면 카운터 올리고 같은 Request 재반환" 으로 표현한다.
8. **state 는 Runner 가 소유.** Session 구조체에 `state` 멤버를 요구하지 않는다. Runner 의 Slot 이 `{ state, in_use, session }` 이다. C runner 가 불투명한 session 안에서 state 를 찾을 수 없기 때문이고, C++ 도 같은 구조를 쓰면 C core 위에 얇게 얹을 수 있다 (2.6).
9. **Runner 는 Server 를 모른다.** worker 는 `Server::resume(id, payload)` 를 직접 부르고, Runner 는 자기 `onResumed` 콜백을 `Event::RESUMED` 로 바꿀 뿐이다. 생성 순서 의존이 없다.

### 2.3 rules() : 컴파일 타임 정렬과 중복 검출

`rules(row, row, ...)` 는 행을 `(state, event)` 키로 컴파일 타임에 정렬한 `Table<Rule, N>` 을 돌려준다. `ANY = -1` 이므로 ANY 행이 맨 앞에 모인다.
작성자는 행 순서를 신경 쓰지 않고, Runner 는 이진 탐색한다. 같은 `(state, event)` 가 두 번 나오면 컴파일 에러다.

C++11 constexpr 의 제약 때문에 모양이 이렇다.

- constexpr 함수 본문은 return 하나(와 static_assert) 뿐이다. 정렬은 **삽입 정렬을 재귀로** 표현한다. `sorted(first, rest...) = insert(sorted(rest...), first)`.
- `std::index_sequence` 가 없다. 삽입은 "새 표의 k 번째 원소는 무엇인가" 를 고르는 `pick()` 을 직접 만든 `MakeSeq<N>` 의 인덱스 팩에 펼쳐 한 번에 만든다.
- 중복 검출은 `checked()` 가 선언만 된 non-constexpr 함수를 조건부로 호출한다. 중복이 있으면 그 호출이 평가되어 "식이 상수로 계산되지 않았습니다" 에러가 나고, 메시지에 `duplicate_rule_in_table` 이 보인다. 예외도 런타임 비용도 없다.
- N 은 인자 개수에서 추론된다. 깊이 N 의 재귀이므로 수십 행이면 충분하다.

```cpp
namespace fsm {

template <typename R, size_t N> struct Table { R rule[N]; };

template <size_t... I> struct Seq {};
template <size_t N, size_t... I> struct MakeSeq : MakeSeq<N - 1, N - 1, I...> {};
template <size_t... I> struct MakeSeq<0, I...> { typedef Seq<I...> type; };

template <typename R> constexpr bool less (const R& a, const R& b) { return a.state < b.state || (a.state == b.state && a.event < b.event); }
template <typename R> constexpr bool equal(const R& a, const R& b) { return a.state == b.state && a.event == b.event; }

// r 이 들어갈 위치 : 정렬된 t 에서 r 보다 큰 첫 원소의 index (같으면 뒤에. 안정 정렬)
template <typename R, size_t N>
constexpr size_t insertPos(const Table<R, N>& t, const R& r, size_t i) { return i == N ? N : (less(r, t.rule[i]) ? i : insertPos(t, r, i + 1)); }

// 새 표의 k 번째 원소
template <typename R, size_t N>
constexpr R pick(const Table<R, N>& t, const R& r, size_t pos, size_t k) { return k < pos ? t.rule[k] : (k == pos ? r : t.rule[k - 1]); }

template <typename R, size_t N, size_t... I>
constexpr Table<R, N + 1> insertImpl(const Table<R, N>& t, const R& r, size_t pos, Seq<I...>) { return Table<R, N + 1>{{ pick(t, r, pos, I)... }}; }
template <typename R, size_t N>
constexpr Table<R, N + 1> insert(const Table<R, N>& t, const R& r) { return insertImpl(t, r, insertPos(t, r, 0), typename MakeSeq<N + 1>::type()); }

// 인접한 두 행의 키가 같으면 중복
template <typename R, size_t N>
constexpr bool unique(const Table<R, N>& t, size_t i) { return i + 1 >= N ? true : (!equal(t.rule[i], t.rule[i + 1]) && unique(t, i + 1)); }
template <typename R, size_t N> Table<R, N> duplicate_rule_in_table();        // 선언만. 평가되면 상수식이 아니므로 컴파일 에러
template <typename R, size_t N>
constexpr Table<R, N> checked(const Table<R, N>& t) { return unique(t, 0) ? t : duplicate_rule_in_table<R, N>(); }

template <typename R>
constexpr Table<R, 1> sorted(R r) { return Table<R, 1>{{ r }}; }
template <typename R, typename... Rs>
constexpr Table<R, 1 + sizeof...(Rs)> sorted(R first, Rs... rest) { return insert(sorted(rest...), first); }

template <typename R, typename... Rs>
constexpr Table<R, 1 + sizeof...(Rs)> rules(R first, Rs... rest) { return checked(sorted(first, rest...)); }

} // namespace fsm
```

빌더 쪽 (`from`, `when`, `.then`, `.to`) 은 requirement_v0.md 8절의 선언 그대로다. 모두 단일 return constexpr 이고, `State` 와 `Session` 은 `.then(handler)` 에 넘긴 함수 포인터 타입에서 추론된다.

검토한 대안: `[state][event]` 2차원 index 표를 컴파일 타임에 만들면 O(1) 이지만 State 개수를 라이브러리가 알아야 한다 (enum 의 `COUNT` 또는 템플릿 인자).
행 10개 남짓의 이진 탐색은 3~4번 비교이므로 v0 는 정렬 + 이진 탐색으로 둔다.

### 2.4 Runner (C++)

`ConnectionHandler` 를 채우는 어댑터다. Connection 을 만지는 코드는 `apply()` 한 곳뿐이다. 상태는 Slot 에 있고, 사용자 Session 에는 state 멤버가 없다.

```cpp
namespace fsm {

template <typename State>
struct Transition {
    State   next;
    bool    keep;                                                     // true 면 상태 유지 (stay)
    Request request;
    Transition(State s)            : next(s), keep(false), request()  {}
    Transition(State s, Request r) : next(s), keep(false), request(r) {}
    static Transition stay(Request r = Request()) { Transition t(State(), r); t.keep = true; return t; }
};

template <typename State, typename Session>
struct Slot { State state; bool in_use; Session session; };

template <typename State, typename Session, size_t N>
struct Slots { Slot<State, Session> slot[N]; };                    // 사용자가 한 번 잡아 두는 저장소. heap 없음

template <typename State, typename Session>
class Runner {
public:
    typedef Rule<State, Session> R;
    typedef Slot<State, Session> S;

    template <size_t N>
    Runner(const Machine<State, Session>& m, Slots<State, Session, N>& storage) : _m(m), _slot(storage.slot), _count(N) {
        for (size_t i = 0; i < N; ++i) _slot[i].in_use = false;
    }

    ConnectionHandler handler() {
        ConnectionHandler h = { this, &onConnected, &onDisconnected, &onRead, &onWritten, &onResumed, &onTimeout };
        return h;
    }

private:
    static Runner& self(void* ctx)      { return *static_cast<Runner*>(ctx); }
    static S&      slot(Connection* c)  { return *static_cast<S*>(c->context()); }

    S* acquire() { for (size_t i = 0; i < _count; ++i) if (!_slot[i].in_use) { _slot[i].in_use = true; return &_slot[i]; } return 0; }

    static void onConnected(void* ctx, Connection* c) {
        S* s = self(ctx).acquire();
        if (!s) { c->close(SESSION_EXHAUSTED); return; }
        s->state = self(ctx)._m.initial;
        c->setContext(s);
        Event e = { Event::CONNECTED, Span{ 0, 0 }, 0, SUCCESS };
        self(ctx).deliver(c, e);
    }
    static void onDisconnected(void* ctx, Connection* c, NetCode reason) {
        Event e = { Event::CLOSED, Span{ 0, 0 }, 0, reason };
        self(ctx).deliver(c, e);                                                // 핸들러는 정리만 한다. Transition 은 무시된다
        slot(c).in_use = false;
        c->setContext(0);
    }
    static void onRead(void* ctx, Connection* c, Span data)         { Event e = { Event::READ,    data,              0, SUCCESS }; self(ctx).deliver(c, e); }
    static void onWritten(void* ctx, Connection* c, size_t length)  { Event e = { Event::WRITTEN, Span{ 0, length }, 0, SUCCESS }; self(ctx).deliver(c, e); }
    static void onResumed(void* ctx, Connection* c, void* payload)  { Event e = { Event::RESUMED, Span{ 0, 0 }, payload, SUCCESS }; self(ctx).deliver(c, e); }
    static void onTimeout(void* ctx, Connection* c)                 { Event e = { Event::TIMEOUT, Span{ 0, 0 }, 0, SUCCESS };       self(ctx).deliver(c, e); }

    // (state, event) 로 정렬된 표의 이진 탐색
    const R* find(int state, int event) const {
        size_t lo = 0, hi = _m.count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            const R& r = _m.rule[mid];
            if (r.state < state || (r.state == state && r.event < event)) lo = mid + 1;
            else if (r.state == state && r.event == event)                return &r;
            else                                                          hi = mid;
        }
        return 0;
    }

    void deliver(Connection* c, const Event& e) {
        S& s = slot(c);
        const R* r = find(static_cast<int>(s.state), e.kind);                   // 정확 일치 먼저
        if (!r) r = find(ANY, e.kind);                                           // 없으면 ANY 행
        if (!r) { c->close(UNHANDLED_EVENT); return; }                          // 프로토콜 정의 누락. 조용히 무시하지 않는다

        Transition<State> t = r->handler(s.session, *c, e);
        if (e.kind == Event::CLOSED) return;                                     // CLOSED 다음은 없다
#ifndef NDEBUG
        if (!t.keep && r->next.count) {                                          // .to() 계약 검사. stay 는 검사 대상이 아니다
            bool ok = false;
            for (int i = 0; i < r->next.count; ++i) ok = ok || (r->next.state[i] == static_cast<int>(t.next));
            if (!ok) { c->close(ILLEGAL_TRANSITION); return; }
        }
#endif
        if (!t.keep) s.state = t.next;
        apply(c, t.request);
    }

    static void apply(Connection* c, const Request& q) {
        switch (q.op) {
        case Request::READ:       c->read(q.n);                                  break;
        case Request::READ_UNTIL: c->readUntil(q.data, q.n);                     break;   // q.data == delimiter
        case Request::WRITE:      c->write(q.data);                              break;
        case Request::SUSPEND:    c->suspend();                                  break;
        case Request::CLOSE:      c->close(SUCCESS);                             break;
        case Request::NONE:                                                      break;
        }
        if (q.op != Request::NONE) c->setTimeout(q.timeout_ms);                  // 0 == deadline 해제. stay() 로 요청이 없으면 기존 deadline 유지
    }

    const Machine<State, Session>& _m;
    S*                             _slot;
    size_t                         _count;
};

} // namespace fsm
```

### 2.5 C runner

C 에는 constexpr 가 없으므로 정렬하지 않고 선형 탐색한다. 정확 일치를 먼저, 없으면 ANY 행을 쓰는 우선순위는 한 번의 scan 으로 된다.
행 10개 남짓이면 이진 탐색과 차이가 없다. 중복 행은 init 에서 한 번 O(N²) 로 검사한다.

Pool 은 Connection 마다 `[16 바이트 header | session_size 바이트]` 블록을 하나씩 준다. header 에 state 와 in_use 가 있다. `kiotty_state_machine_t::session_size` 가 이 때문에 필요하다.

```c
typedef struct kiotty_fsm_slot {           /* per-connection block: header + user session */
    int32_t state;
    int32_t in_use;
    /* machine->session_size bytes of user session follow, 16-byte aligned */
} kiotty_fsm_slot_t;

#define KIOTTY_SLOT_HEADER 16
#define KIOTTY_SLOT_SESSION(slot) ((void*)((char*)(slot) + KIOTTY_SLOT_HEADER))

struct kiotty_fsm_runner {
    const kiotty_state_machine_t* machine;
    char*                         pool;        /* max_clients * stride. 서버 생성 시 한 번 */
    size_t                        stride;
    size_t                        max_clients;
};

kiotty_code_t kiotty_fsm_runner_init(kiotty_fsm_runner_t* r, const kiotty_state_machine_t* m, void* pool, size_t max_clients)
{
    size_t i, j;
    for (i = 0; i < m->count; ++i)
        for (j = i + 1; j < m->count; ++j)
            if (m->rules[i].state == m->rules[j].state && m->rules[i].event == m->rules[j].event) return KIOTTY_DUPLICATE_RULE;
    r->machine = m; r->pool = (char*)pool; r->max_clients = max_clients;
    r->stride = KIOTTY_SLOT_HEADER + ((m->session_size + 15) & ~(size_t)15);
    memset(pool, 0, r->stride * max_clients);
    return KIOTTY_SUCCESS;
}

static kiotty_fsm_slot_t* kiotty_fsm_acquire(kiotty_fsm_runner_t* r)
{
    size_t i;
    for (i = 0; i < r->max_clients; ++i) {
        kiotty_fsm_slot_t* s = (kiotty_fsm_slot_t*)(r->pool + i * r->stride);
        if (!s->in_use) { s->in_use = 1; return s; }
    }
    return NULL;
}

static const kiotty_rule_t* kiotty_fsm_find(const kiotty_state_machine_t* m, int32_t state, int32_t event)
{
    const kiotty_rule_t* fallback = NULL;
    size_t i;
    for (i = 0; i < m->count; ++i) {
        const kiotty_rule_t* r = &m->rules[i];
        if (r->event != event) continue;
        if (r->state == state) return r;                     /* 정확 일치 우선 */
        if (r->state == KIOTTY_ANY_STATE && !fallback) fallback = r;
    }
    return fallback;
}

static void kiotty_fsm_apply(kiotty_connection_t* c, const kiotty_request_t* q)
{
    switch (q->op) {
    case KIOTTY_REQUEST_READ:       kiotty_connection_read(c, q->n);                  break;
    case KIOTTY_REQUEST_READ_UNTIL: kiotty_connection_read_until(c, q->data, q->n);   break;   /* data == delimiter */
    case KIOTTY_REQUEST_WRITE:      kiotty_connection_write(c, q->data);              break;
    case KIOTTY_REQUEST_SUSPEND:    kiotty_connection_suspend(c);                     break;
    case KIOTTY_REQUEST_CLOSE:      kiotty_connection_close(c, KIOTTY_SUCCESS);       break;
    case KIOTTY_REQUEST_NONE:                                                         break;
    }
    if (q->op != KIOTTY_REQUEST_NONE) kiotty_connection_set_timeout(c, q->timeout_ms);
}

static void kiotty_fsm_deliver(kiotty_fsm_runner_t* r, kiotty_connection_t* c, const kiotty_event_t* e)
{
    kiotty_fsm_slot_t*   slot = (kiotty_fsm_slot_t*)kiotty_connection_context(c);
    const kiotty_rule_t* rule = kiotty_fsm_find(r->machine, slot->state, (int32_t)e->kind);
    kiotty_transition_t  t;
    if (!rule) { kiotty_connection_close(c, KIOTTY_UNHANDLED_EVENT); return; }

    t = rule->handler(KIOTTY_SLOT_SESSION(slot), c, e);
    if (e->kind == KIOTTY_EVENT_CLOSED) return;
#ifndef NDEBUG
    if (!t.keep && rule->next_count) {
        int32_t i, ok = 0;
        for (i = 0; i < rule->next_count; ++i) ok = ok || (rule->next[i] == t.next);
        if (!ok) { kiotty_connection_close(c, KIOTTY_ILLEGAL_TRANSITION); return; }
    }
#endif
    if (!t.keep) slot->state = t.next;
    kiotty_fsm_apply(c, &t.request);
}

static void fsm_on_connected(void* ctx, kiotty_connection_t* c)
{
    kiotty_fsm_runner_t* r = (kiotty_fsm_runner_t*)ctx;
    kiotty_fsm_slot_t* slot = kiotty_fsm_acquire(r);
    kiotty_event_t e = { KIOTTY_EVENT_CONNECTED, { NULL, 0 }, NULL, KIOTTY_SUCCESS };
    if (!slot) { kiotty_connection_close(c, KIOTTY_SESSION_EXHAUSTED); return; }
    slot->state = r->machine->initial;
    kiotty_connection_set_context(c, slot);
    kiotty_fsm_deliver(r, c, &e);
}
static void fsm_on_disconnected(void* ctx, kiotty_connection_t* c, kiotty_code_t reason)
{
    kiotty_fsm_runner_t* r = (kiotty_fsm_runner_t*)ctx;
    kiotty_fsm_slot_t* slot = (kiotty_fsm_slot_t*)kiotty_connection_context(c);
    kiotty_event_t e = { KIOTTY_EVENT_CLOSED, { NULL, 0 }, NULL, reason };
    kiotty_fsm_deliver(r, c, &e);
    slot->in_use = 0;
    kiotty_connection_set_context(c, NULL);
}
static void fsm_on_read(void* ctx, kiotty_connection_t* c, kiotty_span_t data)   { kiotty_event_t e = { KIOTTY_EVENT_READ, data, NULL, KIOTTY_SUCCESS }; kiotty_fsm_deliver((kiotty_fsm_runner_t*)ctx, c, &e); }
static void fsm_on_written(void* ctx, kiotty_connection_t* c, size_t length)     { kiotty_event_t e = { KIOTTY_EVENT_WRITTEN, { NULL, length }, NULL, KIOTTY_SUCCESS }; kiotty_fsm_deliver((kiotty_fsm_runner_t*)ctx, c, &e); }
static void fsm_on_resumed(void* ctx, kiotty_connection_t* c, void* payload)     { kiotty_event_t e = { KIOTTY_EVENT_RESUMED, { NULL, 0 }, payload, KIOTTY_SUCCESS }; kiotty_fsm_deliver((kiotty_fsm_runner_t*)ctx, c, &e); }
static void fsm_on_timeout(void* ctx, kiotty_connection_t* c)                    { kiotty_event_t e = { KIOTTY_EVENT_TIMEOUT, { NULL, 0 }, NULL, KIOTTY_SUCCESS }; kiotty_fsm_deliver((kiotty_fsm_runner_t*)ctx, c, &e); }

kiotty_connection_handler_t kiotty_fsm_handler(kiotty_fsm_runner_t* r)
{
    kiotty_connection_handler_t h = { r, fsm_on_connected, fsm_on_disconnected, fsm_on_read, fsm_on_written, fsm_on_resumed, fsm_on_timeout };
    return h;
}
```

### 2.6 C++ Runner 를 C core 위에 얹기 (제안)

2.4 와 2.5 는 dispatch 로직이 두 벌이다. 한 벌로 줄이려면 C++ 핸들러 타입 `Transition<State>(*)(Session&, Connection&, const Event&)` 을 C 의 `kiotty_handler_t` 로 넘겨야 하는데,
포인터를 그대로 캐스팅해서 호출하는 것은 미정의 동작이다. 표준이 보장하는 길은 다음이다.

- `kiotty_rule_t` 에 범용 함수 포인터 슬롯 `void (*typed)(void)` 를 하나 더 둔다.
- C++ `.then(onHeader)` 는 `handler` 에 `(State, Session)` 당 하나뿐인 thunk 를, `typed` 에 `onHeader` 를 `reinterpret_cast<void(*)(void)>` 로 넣는다.
- thunk 는 `self` 행의 `typed` 를 원래 타입으로 되돌려 호출한다. 함수 포인터 간 왕복 변환은 원래 값을 보장한다. 그러려면 `kiotty_handler_t` 에 `const kiotty_rule_t* self` 인자가 하나 더 필요하다.

이러면 dispatch 는 C 한 벌만 남고 C++ `Rule` 은 `kiotty_rule_t` 의 typed 껍질이 된다. 이 경우 2.3 의 컴파일 타임 정렬은 C++ 쪽에서만 동작하고, C 표는 정렬 없이 선형 탐색한다 (runner 가 `sorted` 플래그를 보고 고른다). v0 에서는 두 벌로 두고, 구현이 안정되면 합친다.

---

## 3. StateMachine 활용 예제

### 3.1 HTTP/1.1 서버 (C++, keep-alive, 긴 작업)

```cpp
using namespace fsm;

enum class HttpState { Idle, ReadingHeader, ReadingBody, Processing, Responding };

struct HttpSession {                       // state 멤버 없음. Runner 의 Slot 이 가진다
    ConnectionId    id;                    // worker 가 Server::resume 에 쓴다
    HttpRequest     request;
    char            body[64 * 1024];
    HttpResponse    response;
};
typedef Transition<HttpState> Next;

static const Span CRLF2 = { "\r\n\r\n", 4 };

static Next readHeader() { return Next(HttpState::ReadingHeader, Request::readUntil(CRLF2, 8 * 1024).timeout(10000)); }

static Next onConnected(HttpSession& s, Connection& c, const Event&) { s.id = c.id(); return readHeader(); }

static Next onHeader(HttpSession& s, Connection& c, const Event& e)
{
    if (!parseRequestLineAndHeaders(s.request, e.data))
        return Next(HttpState::Responding, Request::write(BAD_REQUEST));
    if (s.request.content_length > 0)
        return Next(HttpState::ReadingBody, Request::read(s.request.content_length));
    return process(s, c);
}

static Next onBody(HttpSession& s, Connection& c, const Event& e)
{
    memcpy(s.body, e.data.data, e.data.length);             // e.data 는 return 하면 무효
    return process(s, c);
}

static Next process(HttpSession& s, Connection&)
{
    if (isLongJob(s.request)) {
        workerPool.post(&s);                                // worker: 작업 후 server.resume(s->id, &s->response)
        return Next(HttpState::Processing, Request::suspend());
    }
    buildResponse(s.response, s.request);
    return Next(HttpState::Responding, Request::write(s.response.span()));
}

static Next onResumed(HttpSession&, Connection&, const Event& e)
{
    HttpResponse* r = static_cast<HttpResponse*>(e.payload);
    return Next(HttpState::Responding, Request::write(r->span()));
}

static Next onResponded(HttpSession& s, Connection&, const Event&) { return s.request.keep_alive ? readHeader() : Next(HttpState::Idle, Request::close()); }
static Next onTimeout  (HttpSession&, Connection&, const Event&)   { return Next(HttpState::Idle, Request::close()); }
static Next onClosed   (HttpSession& s, Connection&, const Event&) { releaseResources(s); return HttpState::Idle; }

// 이 표가 상태 전이 다이어그램이다. 행 순서는 자유다. rules() 가 정렬한다.
static constexpr auto HTTP_RULES = rules(
    when(Event::CONNECTED)                                .then(onConnected).to(HttpState::ReadingHeader),
    from(HttpState::ReadingHeader).when(Event::READ)      .then(onHeader)   .to(HttpState::ReadingBody, HttpState::Processing, HttpState::Responding),
    from(HttpState::ReadingBody)  .when(Event::READ)      .then(onBody)     .to(HttpState::Processing, HttpState::Responding),
    from(HttpState::Processing)   .when(Event::RESUMED)   .then(onResumed)  .to(HttpState::Responding),
    from(HttpState::Responding)   .when(Event::WRITTEN)   .then(onResponded).to(HttpState::ReadingHeader, HttpState::Idle),
    when(Event::TIMEOUT)                                  .then(onTimeout)  .to(HttpState::Idle),
    when(Event::CLOSED)                                   .then(onClosed)
);
static constexpr Machine<HttpState, HttpSession> HTTP_MACHINE(HTTP_RULES, HttpState::Idle);

// 주입
Slots<HttpState, HttpSession, 32> slots;
Runner<HttpState, HttpSession>    runner(HTTP_MACHINE, slots);
Server                            server(config, runner.handler());

// worker thread
void workerMain(HttpSession* s) { buildResponse(s->response, s->request); server.resume(s->id, &s->response); }
```

### 3.2 WebSocket frame 수신 (C++, full-duplex, 가변 길이 header, push)

pong 은 상태 하나로 표현한다. pong 을 쓰는 동안 들어온 frame 은 Stage 에 쌓였다가 다음 `read(2)` 가 등록되는 즉시 READ 로 온다.
다른 thread 의 push 는 `Server::resume(id, payload)` 가 `RESUMED` 로 들어오고, 어느 상태에서든 `stay(write)` 로 처리한다. 그 WRITTEN 은 ANY 행이 `stay()` 로 흡수한다.

```cpp
enum class WsState { Header, ExtLength, Mask, Payload, PongSent };
typedef Transition<WsState> Next;

static Next onHeader(WsSession& s, Connection&, const Event& e)
{
    s.frame.parseFixedHeader(e.data);                       // 2 bytes
    if (s.frame.len7 == 126) return Next(WsState::ExtLength, Request::read(2));
    if (s.frame.len7 == 127) return Next(WsState::ExtLength, Request::read(8));
    return Next(WsState::Mask, Request::read(4));           // 서버 수신 frame 은 항상 masked
}
static Next onExtLength(WsSession& s, Connection&, const Event& e) { s.frame.parseExtLength(e.data); return Next(WsState::Mask, Request::read(4)); }
static Next onMask     (WsSession& s, Connection&, const Event& e) { s.frame.parseMask(e.data);      return Next(WsState::Payload, Request::read(s.frame.payload_length)); }

static Next onPayload(WsSession& s, Connection&, const Event& e)
{
    s.frame.unmask(e.data);
    switch (s.frame.opcode) {
    case PING:  return Next(WsState::PongSent, Request::write(s.pong(e.data)));
    case CLOSE: return Next(WsState::Header,   Request::close());
    default:    s.onMessage(e.data); return Next(WsState::Header, Request::read(2));
    }
}
static Next onPongSent(WsSession&, Connection&, const Event&)      { return Next(WsState::Header, Request::read(2)); }
static Next onPush    (WsSession& s, Connection&, const Event& e)  { return Next::stay(Request::write(s.frame(static_cast<Message*>(e.payload)))); }   // 상태 유지 + write
static Next ignore    (WsSession&, Connection&, const Event&)      { return Next::stay(); }                                                             // push 의 WRITTEN
static Next onClosed  (WsSession& s, Connection&, const Event&)    { s.release(); return WsState::Header; }

static constexpr auto WS_RULES = rules(
    from(WsState::Header)   .when(Event::READ)   .then(onHeader)   .to(WsState::ExtLength, WsState::Mask),
    from(WsState::ExtLength).when(Event::READ)   .then(onExtLength).to(WsState::Mask),
    from(WsState::Mask)     .when(Event::READ)   .then(onMask)     .to(WsState::Payload),
    from(WsState::Payload)  .when(Event::READ)   .then(onPayload)  .to(WsState::Header, WsState::PongSent),
    from(WsState::PongSent) .when(Event::WRITTEN).then(onPongSent) .to(WsState::Header),
    when(Event::RESUMED)                         .then(onPush),                        // 어느 상태에서든. stay
    when(Event::WRITTEN)                         .then(ignore),                        // push 의 완료. PongSent 는 정확 일치 행이 우선
    when(Event::CLOSED)                          .then(onClosed)
);
```

HTTP Upgrade 뒤 WS 로 넘어가는 것은 requirement_v0.md 9절 `HttpServer` 가 내부에서 한다. 101 응답의 WRITTEN 에서 Connection 의 handler 를 WS Runner 의 것으로 바꾸고 `read(2)` 를 등록한다.
Session 은 HTTP 와 WS 의 union 이며, Slot 하나에 둘 다 들어간다.

### 3.3 HTTP 표 (C)

C 의 표는 `static const` 배열이다. `KIOTTY_RULE` 매크로가 `sizeof((int32_t[]){ __VA_ARGS__ })` 로 next 개수를 컴파일 타임에 센다. C99 이상이 필요하다.

```c
enum http_state { HTTP_IDLE, HTTP_READING_HEADER, HTTP_READING_BODY, HTTP_PROCESSING, HTTP_RESPONDING };

typedef struct http_session { kiotty_connection_id_t id; size_t content_length; int keep_alive; } http_session_t;   /* state 멤버 없음 */

static const kiotty_span_t CRLF2 = { "\r\n\r\n", 4 };

static kiotty_transition_t read_header(void) { return kiotty_go(HTTP_READING_HEADER, kiotty_timeout(kiotty_read_until(CRLF2, 8 * 1024), 10000)); }

static kiotty_transition_t on_connected(void* vs, kiotty_connection_t* c, const kiotty_event_t* e) { http_session_t* s = (http_session_t*)vs; (void)e; s->id = kiotty_connection_id(c); return read_header(); }
static kiotty_transition_t on_header(void* vs, kiotty_connection_t* c, const kiotty_event_t* e)
{
    http_session_t* s = (http_session_t*)vs; (void)c; (void)e;
    if (s->content_length) return kiotty_go(HTTP_READING_BODY, kiotty_read(s->content_length));
    return kiotty_go(HTTP_RESPONDING, kiotty_write(OK));
}
/* on_body, on_resumed, on_responded, on_timeout 은 3.1 과 같은 흐름 */
static kiotty_transition_t on_closed(void* s, kiotty_connection_t* c, const kiotty_event_t* e) { (void)s; (void)c; (void)e; return kiotty_go(HTTP_IDLE, kiotty_none()); }

static const kiotty_rule_t HTTP_RULES[] = {
    /*          from                  event                    handler       -> reachable next states */
    KIOTTY_RULE(KIOTTY_ANY_STATE,     KIOTTY_EVENT_CONNECTED,  on_connected, HTTP_READING_HEADER),
    KIOTTY_RULE(HTTP_READING_HEADER,  KIOTTY_EVENT_READ,       on_header,    HTTP_READING_BODY, HTTP_PROCESSING, HTTP_RESPONDING),
    KIOTTY_RULE(HTTP_READING_BODY,    KIOTTY_EVENT_READ,       on_body,      HTTP_PROCESSING, HTTP_RESPONDING),
    KIOTTY_RULE(HTTP_PROCESSING,      KIOTTY_EVENT_RESUMED,    on_resumed,   HTTP_RESPONDING),
    KIOTTY_RULE(HTTP_RESPONDING,      KIOTTY_EVENT_WRITTEN,    on_responded, HTTP_READING_HEADER, HTTP_IDLE),
    KIOTTY_RULE(KIOTTY_ANY_STATE,     KIOTTY_EVENT_TIMEOUT,    on_timeout,   HTTP_IDLE),
    KIOTTY_RULE_UNCHECKED(KIOTTY_ANY_STATE, KIOTTY_EVENT_CLOSED, on_closed),
};
static const kiotty_state_machine_t HTTP_MACHINE = { HTTP_RULES, KIOTTY_RULE_COUNT(HTTP_RULES), HTTP_IDLE, sizeof(http_session_t) };

/* 주입 */
static char pool[KIOTTY_FSM_POOL_SIZE(sizeof(http_session_t), 32)];
kiotty_fsm_runner_t runner;
kiotty_fsm_runner_init(&runner, &HTTP_MACHINE, pool, 32);
kiotty_server_t* server = kiotty_server_create(&config, kiotty_fsm_handler(&runner));

/* worker thread */
kiotty_server_resume(server, s->id, &s->response);
```

---

## 4. 검증 상태

2절과 3절의 코드는 Connection 을 "마지막 호출을 기록하는 stub" 으로 바꾼 샘플로 MSVC 에서 빌드해 확인했다.

- C++ (`/W4 /permissive-`, 경고 없음): `rules()` 가 뒤섞어 넣은 7행을 `(state, event)` 순으로 정렬한 것, `to<...>()` 템플릿 형태, 행 개수 추론, 핸들러 포인터 보존을 static_assert 로 확인.
  같은 행을 두 번 넣은 빌드는 `duplicate_rule_in_table` 을 가리키며 컴파일 실패. 런타임은 Slot 방식 Runner 로 onConnected -> onRead -> onWritten -> onDisconnected 를 Slot 상태와 Connection 호출로 확인.
- C (`/TC /std:c11 /W4`, 경고 없음): 3.3 의 표와 2.5 의 runner 로 같은 네 단계 통과. `KIOTTY_RULE` 의 next 개수 계산 확인.
- **아직 재검증하지 않은 변경**: 콜백 시그니처의 `Span` 전환, `Transition::stay` / `keep` 플래그, `Request` 의 delimiter 를 Span 으로 받는 것, 1.5 의 resume 큐. 모두 기계적인 변경이지만 샘플을 갱신해 다시 빌드해야 한다.
- MSVC 는 C++14 미만 모드가 없다. C++ 코드는 C++11 constexpr 규칙(단일 return, mem-initializer, 직접 만든 Seq)을 지켜 작성했으나 gcc `-std=c++11` 과 `-std=c99` 확인은 아직이다.

## 5. 미결

- 2.6 의 단일 C core 통합 시점.
- `[state][event]` 2차원 index 표 (2.3 대안).
- CONNECTED 이벤트를 `Machine::initial` 을 `Transition` 으로 바꿔 없앨지. 서버는 거의 항상 첫 read 만, 클라이언트는 첫 요청만 던진다.
- 송신 Stage 에서 `Connection::write` 호출 단위로 onWritten 을 내보내기 위한 길이 큐 (여러 write 가 Stage 에 연달아 복사될 때 경계 추적).
- done ring 의 MPSC 구현은 1.5 대로 lock-free 로 간다. 32 client 라 mutex 로도 충분하지만 IO Thread 가 worker 에 막히는 일이 없어야 해서다.
- gcc `-std=c++11` / `-std=c99` 검증.

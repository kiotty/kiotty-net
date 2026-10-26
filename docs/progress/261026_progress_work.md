# 2026-10-26 진행 정리 : 인터페이스 1~7절, Work 모델, 송신 lane

다른 컴퓨터에서 이어서 작업하기 위한 메모. 결정된 것, 열린 질문, 다음 할 일 순서로 적는다. 기준 문서는 `docs/requirement/requirement_v0.md` 이고 코드는 `include/kiotty_net_base.h` 다.

## 오늘 결정된 것

### naming 과 구조 (requirement 규약 1~8)

- C 함수 `KiottyNet_` + 타동사 + 명사 (`KiottyNet_createServer`). C++ 공개 클래스 `KiottyNet_Server`. 공용 struct / enum / handle 은 `KiottyNetServerConfig` 처럼 언더스코어 없음. enum 값은 `KIOTTY_`.
- 공개 enum 은 `enum class` 금지. handle 이 아닌 struct 는 C / C++ 한 벌.
- 소유 handle (불투명 포인터, create / destroy) : StreamClient, EventClient, Server, HttpServer, WebSocketServer, HttpClient, WebSocketClient. pool handle (`int64_t`, 내부 `{ generation, index }`) : Connection, Work, WebSocket, HttpResponse.
- C++ 래퍼는 template 이 아니면 C 함수 하나를 부르는 얇은 래퍼. 헤더 inline. 공개 인터페이스에 `KiottyResult` 를 쓰지 않는다 (내부 전용). 출력 인자 금지.
- 내부 구현은 `kiotty::net` namespace.
- 주석을 넣지 않는다. 인터페이스가 확정된 뒤 요청이 있을 때만 짧은 영어 주석. 규칙은 `.claude/agents/cpp-developer.md`.

### 설정

- 설정 struct 는 사용자가 잡고 `KiottyNet_initializeXxx` 한 번 + `KiottyNet_configureXxx` 여러 번. 모든 함수가 `KiottyNetCode` 를 돌려주고 잘못된 값이면 struct 를 바꾸지 않는다. validate 없음. initialize 가 기본값을 모두 채우고 기본값이 없는 필수 값 (주소, TLS 서버 인증서) 은 인자로 받는다.
- 문자열은 `char[]` 고정 길이. Endpoint host 256, 경로 260, 암호 128.
- TLS 는 transport 가 아니다. transport 는 TCP / Pipe 둘. TLS 설정은 create 함수에 따로 넘기고 NULL 이면 평문. TLS over Pipe 도 막지 않는다.
- C++ Builder 는 외부 struct 를 참조로 받아 C 함수만 부르고 처음 실패한 코드를 `build()` 로 돌려준다. 서버 / 클라이언트는 `KiottyNet_ServerConfigBuilder::tcp(config, host, port)` / `::pipe(config, name)`. `kiotty::net::FirstFailure` 클래스는 지웠다. Builder 마다 `KiottyNetCode _code` 와 private `record()` (SUCCESS 일 때만 덮어씀) 를 둔다.
- 버퍼 크기는 셋 : `recv_capacity`, `send_capacity`, `push_capacity`. 모두 기본 4 KiB. `configureXxxBufferCapacity(recv, send, push)`.

### 수명

- loop 를 가진 객체는 `run / close / await(timeout_ms)` 셋뿐. `run` 이 열고 loop 를 돈다. `close` 는 기다리지 않는다. `await` 는 loop 가 돌기 시작할 때까지 기다리고, run 이 이미 끝났으면 run 의 반환값. `KIOTTY_CLOSED` 코드 추가.

### Work (suspend / resume 대체)

- `KiottyNet_startWork(handle)` 는 어느 thread 에서나. IO Thread 는 write lane 을 비우고 (`onWritten` 까지), 걸린 read 요청과 남은 deadline 을 기억한 뒤 **다음 loop 에서** `onWorkStarted(connection_context, handle, work)`.
- **context 는 둘이다.** `handler_context` 는 `KiottyNetConnectionHandler` 의 필드이고 `onConnected` 만 받는다. `onConnected` 는 이것으로 연결마다의 데이터를 만들어 `KiottyNet_setConnectionContext(connection, connection_context)` 로 붙이고 첫 read / write 를 건다. 그 뒤의 완료 콜백 `onRead`, `onWritten`, `onWorkStarted`, `onTimeout`, `onDisconnected` 는 첫 인자로 그 `connection_context` 를 받아 다음 read / write / startWork 로 이어 간다. Work 전용 인자는 없다 (2026-10-27).
- 그 뒤 finish 까지 콜백 없음. IO Thread 는 수신 Stage 채우기, push lane 송신, peer 끊김 기록만 한다. deadline 멈춤. 라이브러리는 Work 에 timeout 을 두지 않는다.
- finish 네 가지 : `finishWorkWithRead(exact, timeout_ms)`, `finishWorkWithReadUntil(delim, max, timeout_ms)`, `finishWorkWithWrite(data)` (그 자리에서 write lane 으로 복사, 이전 read 와 deadline 복원), `finishWorkWithClose(reason)`. 한 번만. 두 번째는 `KIOTTY_BUSY`. **`finishWork` (요청 없이 돌려주기) 는 지웠다.** 용도가 C++ 소멸자 안전망과 push 취소뿐이었고, 둘 다 close 로 대신한다.
- `KiottyNet_isWorkConnected(work)` : IO Thread 가 기록한 플래그를 읽을 뿐 socket 을 건드리지 않는다.
- Work 중 peer 가 끊으면 slot 은 유지, finish 뒤 `onDisconnected(KIOTTY_PEER_CLOSED)`. 소유자 `close()` 는 Work 중인 연결에도 바로 `onDisconnected(KIOTTY_CLOSED)`, 뒤늦은 finish 는 `KIOTTY_PEER_CLOSED`.
- C++ `KiottyNet_Work` 는 이동만, 소멸자와 move 대입이 finish 하지 않은 Work 를 `finishWithClose()` 로 닫는다.
- lock-free 구조 (atomic 상태 `RUNNING → DRAINING → WORKING → DONE`, `max_clients` 칸 MPSC done ring, eventfd / PQCS) 는 구현 가이드 1.5.

### 송신 : write lane / push lane / broadcast (requirement 4절, 7절, 구현 가이드 1.6)

- 송신 Stage 하나를 **write lane** 과 **push lane** 둘로 나눴다. 한 ring 에 사용자 write 와 broadcast 를 같이 넣으면 (1) 메시지 경계 record 큐가 필요하고 (2) broadcast 가 몰리면 프로토콜을 지킨 사용자의 write 가 BUFFER_FULL 로 끊긴다. 나누면 둘 다 사라진다.
- **write lane** (`send_capacity`) : 메시지 하나. `writeConnection` 과 `finishWorkWithWrite` 가 넣는다. 연결당 write 는 한 번에 하나 (`write → onWritten → write`). onWritten 전에 다시 write 하면 `close(KIOTTY_BUSY)`. 메시지가 크면 `close(KIOTTY_BUFFER_FULL)`. RUNNING 에서는 IO Thread, WORKING 에서는 worker 가 쓴다. 동시에 둘인 순간이 없어 CAS 가 없다.
- **push lane** (`push_capacity`) : `[u32 길이][바이트]` ring. `broadcastServer`, `writeEventClient`, WebSocket control frame 이 들어간다. IO Thread 혼자 쓰고 읽는다. 완료 콜백 없음. Work 중인 연결에도 들어간다. 여유 없으면 Server 는 `close(KIOTTY_BUFFER_FULL)` (slow consumer), EventClient 는 `KIOTTY_BUFFER_FULL` 반환하고 연결 유지.
- **drain** : socket 에 쓰는 코드는 하나. 보내던 메시지는 끝까지, 메시지 경계에서만 lane 교체, 둘 다 있으면 write lane 먼저, WORKING 이면 push lane 만. IOCP 는 WSASend 하나 걸고 완료에서 다음, epoll 은 EAGAIN 까지 send 하고 EPOLLOUT 대기. drain 을 부르는 곳은 write 직후, DONE → RUNNING, push 직후, 완료 / EPOLLOUT 네 곳이고 모두 IO Thread.
- **onWritten 은 loop 끝 (notify) 에서** 낸다. epoll 에서 `write()` 안에서 `send()` 가 바로 성공해도 재진입이 없고, 두 플랫폼의 콜백 순서가 같아진다.
- **broadcast** : Server 버퍼 없음. 호출자가 block 되므로 IO Thread 가 호출자의 span 을 각 연결의 push lane 으로 직접 복사. 복사가 끝나면 호출자를 깨운다 (socket 상태와 무관). slot 4 개, 넘치면 `KIOTTY_BUSY`. IO Thread 콜백 안에서 부르면 그 자리에서 분배. `writeEventClient` 는 연결 하나인 같은 구조.
- `KiottyNet_sendEventClient` → `KiottyNet_writeEventClient`, C++ `send` → `write`.
- 기각 : Work 중 worker 가 socket I/O 를 하는 `readWork` / `writeWork`. 두 번 thread 를 건너야 해서 느리고, Work 모델은 "socket 을 건드리지 않는 긴 계산" 에 맞춰 둔다.

## 열린 질문

1. **연결 확인.** FIN 은 IO Thread 의 recv 가 잡는다. FIN 없이 죽은 peer 는 select 로도 못 잡고 TCP keepalive 나 앱 ping 뿐이다. `configureServerKeepAlive` 같은 설정을 둘지.
2. `KiottyNet_Work` 소멸자가 `finishWithClose` 에 넘기는 reason. 지금은 기본값 `KIOTTY_SUCCESS`. 버그 경로임을 구분하는 코드를 둘지.
3. broadcast 의 "kernel 까지 넘어갔다" 완료가 필요해지면 push lane 헤더에 slot id 를 더하고 slot 에 카운터를 두면 된다. 느린 peer 가 호출자를 붙잡게 되므로 기본으로는 두지 않는다.

## 다음 할 일

1. requirement 8절 StateMachine : `Request::suspend()` + `RESUMED` → `Request::startWork()` + `WORK_STARTED(work)`.
2. 9절 HttpServer : `defer` + `resumed` 콜백 → HTTP 전용 Work (`finishWithBody` 등).
3. 10절 WebSocket : push → `startWork` 또는 broadcast. control frame 은 IO Thread 가 push lane 으로.
4. 11절 HttpClient / WebSocketClient 의 열린 질문 4개.
5. 헤더 `kiotty_net_statemachine.h`, `kiotty_net_http.h`, `kiotty_net_websocket.h` 작성. `src/` 는 아직 비어 있다. Stage 는 내부 구현이라 아직 안 짰다.
6. `implementation_guide_v0.md` 의 1.3 (`_suspended`), 2, 3절은 옛 이름 (`NetCode`, `fsm::`, `resume`) 그대로다.

## 검증 상태

- `include/kiotty_net_base.h` 는 MSVC 에서 C++14 `/W4 /WX /EHsc` 와 C11 `/TC /W4 /WX` 로 경고 없이 컴파일된다. MSVC 에 C++11 모드가 없어 C++11 은 미확인. gcc / clang 미확인.
- 검사용 소스는 `include` 만 포함하는 `use.cpp` / `use.c` 였고 저장소에는 넣지 않았다. 구현이 없으므로 링크는 하지 않았다.

## StateMachine 사용 예제 (2026-10-27)

`include/kiotty_net_statemachine.h` 를 어떻게 쓰는지 하나의 프로토콜로 처음부터 끝까지 보인다. 아래 코드는 MSVC C++14 `/W4 /WX /permissive-` 와 C11 `/TC /W4 /WX` 에서 경고 없이 컴파일되는 것을 확인했다 (구현이 없어 링크는 안 함).

### 머릿속 모델

StateMachine 없이 `KiottyNetConnectionHandler` 를 직접 쓰면 콜백 6개 안에서 각자 `switch (state)` 를 하고 `KiottyNet_readConnection` 같은 IO 함수를 직접 부른다. StateMachine 은 이것을 **표 한 장** 으로 바꾼다.

```
(지금 상태, 일어난 이벤트) ──표에서 찾음──> transition action(session, event) ──돌려줌──> (다음 상태, entry action)
                                                                                              │
                                                     Runner 가 entry action 을 IO 호출로 바꿈 ──┘
```

- **Event** 는 라이브러리가 알려주는 "무슨 일이 일어났나" 다. 타입마다 쓸 수 있는 필드가 `event.as.<타입>` 아래에 있다.
- **Transition action** 은 표의 한 행이 실행하는 사용자 함수다. Session 과 Event 를 받아 Transition 을 돌려준다. IO 함수를 직접 부르지 않는다.
- **Transition** 은 "다음 상태 + entry action" 이다. 상태를 옮기려면 `moveToNextState`, 지금 상태에 머물려면 `stayInCurrentState` 로 만든다.
- **Entry action** 은 상태에 들어가면서 Runner 에게 시키는 IO 하나다. 실제 `read` / `write` / `startWork` / `close` 호출은 Runner 가 한 곳에서 한다.
- **state 는 Runner 가 들고 있다.** Session 에 state 멤버를 두지 않는다.

| Event | 언제 | 쓸 수 있는 필드 |
|---|---|---|
| `KIOTTY_CONNECTION_EVENT_CONNECTED` | 연결됨. Session 이 막 새로 만들어졌다 | 없음 |
| `KIOTTY_CONNECTION_EVENT_READ` | 걸어 둔 read / readUntil 조건이 찼다 | `as.read.data` (transition action 이 return 하면 무효) |
| `KIOTTY_CONNECTION_EVENT_WRITTEN` | write 하나가 끝났다 | `as.written.length` |
| `KIOTTY_CONNECTION_EVENT_WORK_STARTED` | startWork 가 받아들여져 연결이 IO Thread 에서 떨어졌다 | `as.work_started.work` |
| `KIOTTY_CONNECTION_EVENT_TIMEOUT` | entry action 에 건 timeout 이 지났다 | 없음 |
| `KIOTTY_CONNECTION_EVENT_CLOSED` | 마지막 이벤트. 돌려준 Transition 은 무시된다 | `as.closed.reason` |

| Entry action (C++ / C) | Runner 가 하는 일 |
|---|---|
| `KiottyNet_EntryAction::read(exact)` / `KiottyNet_makeReadEntryAction` | exact 바이트가 모이면 READ |
| `KiottyNet_EntryAction::readUntil(delimiter, max)` / `KiottyNet_makeReadUntilEntryAction` | delimiter 까지 모이면 READ. max 안에 없으면 끊는다 |
| `KiottyNet_EntryAction::write(data)` / `KiottyNet_makeWriteEntryAction` | 보내고, 끝나면 WRITTEN |
| `KiottyNet_EntryAction::startWork()` / `KiottyNet_makeStartWorkEntryAction` | 연결을 떼어내고 WORK_STARTED |
| `KiottyNet_EntryAction::close()` / `KiottyNet_makeCloseEntryAction` | 끊는다. 마지막으로 CLOSED |
| 생략 / `KiottyNet_makeNoEntryAction` | 아무것도 하지 않는다 |
| `.timeout(ms)` / `KiottyNet_setEntryActionTimeout` | 위 entry action 에 deadline 을 붙인다. 지나면 TIMEOUT |

| Transition (C++ / C) | 뜻 |
|---|---|
| `Next::moveToNextState(state, entry_action)` / `KiottyNet_moveToNextState(state, entry_action)` | state 로 옮기고 entry action 을 실행 |
| `Next::stayInCurrentState(entry_action)` / `KiottyNet_stayInCurrentState(entry_action)` | 상태는 그대로 두고 entry action 만 실행 (`next = KIOTTY_SAME_STATE`) |

C++ 은 entry action 인자를 생략하면 "없음" 이다.

**WORK_STARTED 만 규칙이 다르다.** 이 이벤트에서 돌려준 entry action 은 Connection 이 아니라 Work 에 적용된다. read / readUntil / write / close 를 돌려주면 Runner 가 그 자리에서 `finishWorkWith*` 로 Work 를 끝낸다. entry action 없이 돌려주면 Work 를 그대로 두는데, transition action 이 `work` 를 worker 에게 넘겼다는 뜻이다. 그러면 finish 는 worker 의 책임이고, worker 가 finish 한 결과는 보통의 READ / WRITTEN 이벤트로 다시 표에 들어온다.

### 예제 프로토콜 : 길이 접두 echo

1. 클라이언트가 4 바이트 big-endian 길이를 보낸다.
2. 그 길이만큼 본문을 보낸다 (최대 1024).
3. 서버는 본문을 worker thread 에서 처리해 (여기서는 그대로 복사) 돌려준다.
4. 다시 1 로 간다. 30 초 동안 다음 헤더가 오지 않으면 끊는다.

```
ReadingHeader ──READ(4)──> ReadingBody ──READ(n)──> Processing ──WORK_STARTED──> (worker 가 계산)
      ^                                                 │
      └──────────────────────WRITTEN────────────────────┘   (worker 의 finishWithWrite 가 끝나면)
```

### C++

```cpp
// 1. State : 연결 하나가 지금 무엇을 기다리는가
enum class EchoState
{
    ReadingHeader,
    ReadingBody,
    Processing
};

// 2. Session : 연결 하나가 들고 있는 데이터. state 는 Runner 가 따로 가지므로 넣지 않는다
struct EchoSession
{
    uint32_t body_length;
    char     body[1024];
    char     response[1024];
    size_t   response_length;
};

typedef KiottyNet_Transition<EchoState> Next;

static const uint32_t IDLE_TIMEOUT_MS = 30000;

static uint32_t decodeLength(KiottyNetSpan data)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// worker thread 에서 도는 긴 작업. 끝나면 Work 를 write 로 돌려준다
static void computeResponse(EchoSession* s, KiottyNetWorkHandle work)
{
    std::memcpy(s->response, s->body, s->body_length);
    s->response_length = s->body_length;

    KiottyNet_Work finisher(work);
    KiottyNetSpan  response = { s->response, s->response_length };
    finisher.finishWithWrite(response);
}

// 3. Transition action : (Session, Event) 를 받아 "다음 상태 + entry action" 을 돌려준다. IO 함수를 직접 부르지 않는다
static Next readHeader()
{
    return Next::moveToNextState(EchoState::ReadingHeader, KiottyNet_EntryAction::read(4).timeout(IDLE_TIMEOUT_MS));
}

static Next onConnected(EchoSession&, KiottyNet_Connection&, const KiottyNetConnectionEvent&)
{
    return readHeader();
}

static Next onHeader(EchoSession& s, KiottyNet_Connection&, const KiottyNetConnectionEvent& e)
{
    s.body_length = decodeLength(e.as.read.data);
    if (s.body_length == 0 || s.body_length > sizeof(s.body))
    {
        return Next::stayInCurrentState(KiottyNet_EntryAction::close());
    }
    return Next::moveToNextState(EchoState::ReadingBody, KiottyNet_EntryAction::read(s.body_length));
}

static Next onBody(EchoSession& s, KiottyNet_Connection&, const KiottyNetConnectionEvent& e)
{
    std::memcpy(s.body, e.as.read.data.data, e.as.read.data.length);
    return Next::moveToNextState(EchoState::Processing, KiottyNet_EntryAction::startWork());
}

static Next onWorkStarted(EchoSession& s, KiottyNet_Connection&, const KiottyNetConnectionEvent& e)
{
    std::thread(computeResponse, &s, e.as.work_started.work).detach();
    return Next::stayInCurrentState();
}

static Next onResponded(EchoSession&, KiottyNet_Connection&, const KiottyNetConnectionEvent&)
{
    return readHeader();
}

static Next onTimeout(EchoSession&, KiottyNet_Connection&, const KiottyNetConnectionEvent&)
{
    return Next::stayInCurrentState(KiottyNet_EntryAction::close());
}

static Next onClosed(EchoSession&, KiottyNet_Connection&, const KiottyNetConnectionEvent&)
{
    return Next::stayInCurrentState();
}

// 4. 표 : "이 상태에서 이 이벤트가 오면 이 transition action 을 돌리고, 갈 수 있는 다음 상태는 이것뿐" 을 한 줄씩
static constexpr auto ECHO_RULES = KiottyNet_rules(
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_CONNECTED)                               .then(onConnected)  .to(EchoState::ReadingHeader),
    KiottyNet_from(EchoState::ReadingHeader).when(KIOTTY_CONNECTION_EVENT_READ)     .then(onHeader)     .to(EchoState::ReadingBody),
    KiottyNet_from(EchoState::ReadingBody)  .when(KIOTTY_CONNECTION_EVENT_READ)     .then(onBody)       .to(EchoState::Processing),
    KiottyNet_from(EchoState::Processing)   .when(KIOTTY_CONNECTION_EVENT_WORK_STARTED).then(onWorkStarted),
    KiottyNet_from(EchoState::Processing)   .when(KIOTTY_CONNECTION_EVENT_WRITTEN)  .then(onResponded)  .to(EchoState::ReadingHeader),
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_TIMEOUT)                                 .then(onTimeout),
    KiottyNet_when(KIOTTY_CONNECTION_EVENT_CLOSED)                                  .then(onClosed));


// 5. 조립 : Slots (연결 32개 분 Session 저장소) + Runner + Server
static KiottyNet_Slots<EchoState, EchoSession, 32> slots;

int main()
{
    KiottyNet_Runner<EchoState, EchoSession> runner(ECHO_RULES, EchoState::ReadingHeader, slots);

    KiottyNetServerConfig config;
    if (KiottyNet_ServerConfigBuilder::tcp(config, "0.0.0.0", 9000).maxClients(32).build() != KIOTTY_SUCCESS)
    {
        return 1;
    }

    KiottyNet_Server server(config, runner.handler());
    std::thread      io([&server]() { server.run(); });
    if (server.await(3000) != KIOTTY_SUCCESS)
    {
        io.join();
        return 1;
    }
    server.close();
    io.join();
    return 0;
}
```

### 한 요청이 흘러가는 순서

| # | 일어난 일 | 그때 상태 | 표에서 찾은 행 | transition action 이 돌려준 것 | Runner 가 한 일 |
|---|---|---|---|---|---|
| 1 | accept | (Slot 받음) ReadingHeader | `when(CONNECTED)` | `moveToNextState(ReadingHeader, read(4) + 30 초)` | `read(4)`, `setTimeout(30000)` |
| 2 | 4 바이트 도착 | ReadingHeader | `from(ReadingHeader).when(READ)` | `moveToNextState(ReadingBody, read(n))` | `read(n)`, `setTimeout(0)` |
| 3 | n 바이트 도착 | ReadingBody | `from(ReadingBody).when(READ)` | `moveToNextState(Processing, startWork())` | `startWork()` |
| 4 | 연결이 떨어짐 | Processing | `from(Processing).when(WORK_STARTED)` | `stayInCurrentState()` | 아무것도 안 함. Work 는 worker 가 가짐 |
| 5 | worker 가 `finishWithWrite` | Processing | (이벤트 아님) | | 응답을 보냄 |
| 6 | 응답 송신 완료 | Processing | `from(Processing).when(WRITTEN)` | `moveToNextState(ReadingHeader, read(4) + 30 초)` | 1 과 같음 |
| 7 | 30 초 동안 조용함 | ReadingHeader | `when(TIMEOUT)` | `stayInCurrentState(close())` | `close(KIOTTY_SUCCESS)` |
| 8 | 끊김 | 아무 상태 | `when(CLOSED)` | 무시됨 | Slot 반납 |

- `.to(...)` 는 그 행이 갈 수 있는 다음 상태 목록이다. debug 빌드에서 transition action 이 목록 밖으로 가면 `KIOTTY_ILLEGAL_TRANSITION` 으로 끊는다. `.to()` 가 없는 행과 `stayInCurrentState` 는 검사하지 않는다.
- 표에 없는 (상태, 이벤트) 가 오면 `KIOTTY_UNHANDLED_EVENT` 로 끊는다. 예를 들어 ReadingBody 에서 TIMEOUT 은 `when(TIMEOUT)` (ANY 행) 이 받는다. 정확한 행이 ANY 행보다 우선한다.
- 행 순서는 아무렇게나 써도 된다. `KiottyNet_rules` 가 컴파일 타임에 정렬하고, 같은 (상태, 이벤트) 가 두 번 나오면 컴파일 에러다.

### C

C 에는 template 이 없으므로 같은 것을 손으로 묶는다. 순서는 같다. State enum, Session struct, transition action 함수, 표.
그 다음 **`KiottyNetStateMachine` 은 "표 + 초기 상태 + Session 크기" 를 Runner 에 넘기기 위한 묶음일 뿐이다.** C 의 Runner 는 Session 타입을 모르므로 크기를 알려 줘야 연결마다 그만큼을 pool 에서 잘라 줄 수 있다. C++ 은 template 이 이 정보를 알고 있어서 이 struct 가 필요 없다.

```c
enum EchoState
{
    ECHO_READING_HEADER,
    ECHO_READING_BODY,
    ECHO_PROCESSING
};

typedef struct EchoSession
{
    uint32_t body_length;
    char     body[1024];
} EchoSession;

static uint32_t decode_length(KiottyNetSpan data)
{
    const unsigned char* p = (const unsigned char*)data.data;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void post_to_worker(EchoSession* session, KiottyNetWorkHandle work);

static KiottyNetTransition read_header(void)
{
    return KiottyNet_moveToNextState(ECHO_READING_HEADER, KiottyNet_setEntryActionTimeout(KiottyNet_makeReadEntryAction(4), 30000));
}

static KiottyNetTransition on_connected(void* session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return read_header();
}

static KiottyNetTransition on_header(void* session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    EchoSession* s = (EchoSession*)session;
    (void)connection;
    s->body_length = decode_length(event.as.read.data);
    if (s->body_length == 0 || s->body_length > sizeof(s->body))
    {
        return KiottyNet_stayInCurrentState(KiottyNet_makeCloseEntryAction());
    }
    return KiottyNet_moveToNextState(ECHO_READING_BODY, KiottyNet_makeReadEntryAction(s->body_length));
}

static KiottyNetTransition on_body(void* session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    EchoSession* s = (EchoSession*)session;
    (void)connection;
    memcpy(s->body, event.as.read.data.data, event.as.read.data.length);
    return KiottyNet_moveToNextState(ECHO_PROCESSING, KiottyNet_makeStartWorkEntryAction());
}

static KiottyNetTransition on_work_started(void* session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)connection;
    post_to_worker((EchoSession*)session, event.as.work_started.work);
    return KiottyNet_stayInCurrentState(KiottyNet_makeNoEntryAction());
}

static KiottyNetTransition on_responded(void* session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return read_header();
}

static KiottyNetTransition on_timeout(void* session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return KiottyNet_stayInCurrentState(KiottyNet_makeCloseEntryAction());
}

static KiottyNetTransition on_closed(void* session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return KiottyNet_stayInCurrentState(KiottyNet_makeNoEntryAction());
}

static const KiottyNetRule ECHO_RULES[] = {
    KIOTTY_RULE(KIOTTY_ANY_STATE,    KIOTTY_CONNECTION_EVENT_CONNECTED,    on_connected, ECHO_READING_HEADER),
    KIOTTY_RULE(ECHO_READING_HEADER, KIOTTY_CONNECTION_EVENT_READ,         on_header,    ECHO_READING_BODY),
    KIOTTY_RULE(ECHO_READING_BODY,   KIOTTY_CONNECTION_EVENT_READ,         on_body,      ECHO_PROCESSING),
    KIOTTY_RULE_UNCHECKED(ECHO_PROCESSING, KIOTTY_CONNECTION_EVENT_WORK_STARTED, on_work_started),
    KIOTTY_RULE(ECHO_PROCESSING,     KIOTTY_CONNECTION_EVENT_WRITTEN,      on_responded, ECHO_READING_HEADER),
    KIOTTY_RULE_UNCHECKED(KIOTTY_ANY_STATE, KIOTTY_CONNECTION_EVENT_TIMEOUT, on_timeout),
    KIOTTY_RULE_UNCHECKED(KIOTTY_ANY_STATE, KIOTTY_CONNECTION_EVENT_CLOSED,  on_closed),
};

static const KiottyNetStateMachine ECHO_MACHINE = { ECHO_RULES, KIOTTY_RULE_COUNT(ECHO_RULES), ECHO_READING_HEADER, sizeof(EchoSession) };

static char pool[KIOTTY_FSM_POOL_SIZE(sizeof(EchoSession), 32)];

int start_echo_server(void)
{
    KiottyNetServerConfig    config;
    KiottyNetFsmRunnerHandle runner;
    KiottyNetServerHandle    server;

    if (KiottyNet_initializeTcpServerConfig(&config, "0.0.0.0", 9000) != KIOTTY_SUCCESS)
    {
        return 1;
    }
    runner = KiottyNet_initFsmRunner(&ECHO_MACHINE, pool, 32);
    if (runner == NULL)
    {
        return 1;
    }
    server = KiottyNet_createServer(&config, NULL, KiottyNet_getFsmHandler(runner));
    if (server == NULL)
    {
        return 1;
    }
    return KiottyNet_runServer(server) == KIOTTY_CLOSED ? 0 : 1;
}
```

C 에서 표를 Runner 에 주는 단계만 정리하면 이렇다.

1. `static const KiottyNetRule ECHO_RULES[] = { KIOTTY_RULE(...), ... };` : 표. `KIOTTY_RULE(상태, 이벤트, transition action, 갈 수 있는 다음 상태...)`, 검사 없이 쓰려면 `KIOTTY_RULE_UNCHECKED(상태, 이벤트, transition action)`.
2. `static const KiottyNetStateMachine ECHO_MACHINE = { 표, 행 수, 초기 상태, sizeof(Session) };`
3. `static char pool[KIOTTY_FSM_POOL_SIZE(sizeof(Session), 연결 수)];` : 연결마다 `{ state, in_use, Session }` 이 들어갈 저장소. 런타임 할당 없음.
4. `KiottyNet_initFsmRunner(&ECHO_MACHINE, pool, 연결 수)` : 표에 중복 행이 있으면 NULL.
5. `KiottyNet_createServer(&config, NULL, KiottyNet_getFsmHandler(runner))` : 나머지는 보통 Server 와 같다.

### 예제를 쓰면서 드러난 열린 질문

1. **push 와 Runner 의 startWork 경쟁.** 앱 thread 가 `KiottyNet_startWork(connection)` 로 push 하는 순간에 표가 `startWork()` entry action 을 돌려주면, 늦게 부른 쪽이 `KIOTTY_BUSY` 다. 지금 Runner 는 startWork 실패를 그 코드로 연결을 끊는 것으로 처리하므로, push 가 정상 연결을 끊을 수 있다. 또 그때 오는 WORK_STARTED 는 push 의 것인데 Processing 행이 받는다. transition action 이 Session 에 남긴 표시 (예: 보낼 push 메시지가 쌓여 있는지) 로 누구의 Work 인지 구분해야 하는지, Runner 가 BUSY 를 재시도해야 하는지 정해야 한다.
2. **소유자 close 와 worker 의 Session 접근.** `server.close()` 는 Work 중인 연결에도 바로 CLOSED 를 내고 Runner 는 Slot 을 반납한다. 그 뒤에도 worker 가 `s->response` 에 쓰고 있으면, `run()` 을 다시 해서 같은 Slot 이 새 연결에 재사용될 때 덮어쓴다. Work 가 끝날 때까지 Slot 반납을 미룰지 정해야 한다.
3. **union 이름 `as`.** C99 에는 이름 없는 union 이 없어서 `event.as.read.data` 처럼 한 단계가 더 붙는다. C11 을 요구하면 `event.read.data` 로 줄일 수 있다.
4. **`stayInCurrentState` 의 entry action 은 엄밀히는 entry action 이 아니다.** UML 에서 상태에 머무는 internal transition 은 entry action 을 실행하지 않는다. 여기서는 머물 때도 같은 struct 로 IO 하나를 시킨다. 이름은 이대로 두되 이 차이는 알고 쓴다.

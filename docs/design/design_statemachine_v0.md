# kiotty_net_statemachine 구현 설계 v0

2026-11-09. 기준은 `docs/requirement/requirement_v0.md` 8절 (StateMachine), `docs/requirement/implementation_guide_v0.md` 2절, `docs/requirement/user_guide_http.md`, `docs/requirement/user_guide_websocket.md`, `docs/design/design_base_v5.html`, `docs/deviations.md` (DV6, DV12, DV13) 이다. base 는 끝났고 (`docs/progress/261109_progress_base_8.md`) 다음 층이 StateMachine 이다. 이 문서는 세 가지를 정한다.

1. StateMachine 층의 코드 모양. 공개 헤더 `include/kiotty_net_statemachine.h` 는 이미 있고, 구현은 `KiottyNet_moveToNextState` 하나뿐이다.
2. HTTP 와 WebSocket 이 이 층 위에 얹히는 자리. 둘의 설계는 다음 문서지만, 둘이 필요로 하는 것을 이 층이 지금 갖추어야 한다.
3. DV12 (`const char*` 가 내부 경계를 지난다) 를 닫고, DV13 (공개 API 의 bool 인자, 약어, wrapper 의 인자와 메서드 수) 에 공개 API 판 v1 을 제안한다. 공개 헤더는 수칙이 아니라 사용자가 지정한 모양을 따르므로 (수칙 0.2) DV13 은 사용자가 정하는 만큼만 바뀐다. 헤더 넷과 그 줄 수는 그대로 둔다.

두 문서가 다르면 requirement 가 우선한다. requirement 8절과 구현 가이드 2절이 다른 자리는 14절에 모았다.

설계 방침 (이 문서가 지키는 세 가지)

1. **StateMachine 은 새 계층이 아니라 Handler 의 한 모양이다.** 지금 Connection 은 `Handler` (사용자 콜백 표 + app_context + owner) 를 받아 `handler.callbacks.onRead(...)` 를 부른다. 이 자리를 인터페이스로 바꾸고 두 구현을 둔다. `CallbackHandler` 는 지금 하는 일 그대로 사용자의 `KiottyNetConnectionHandler` 를 부르고, `StatefulHandler` 는 표를 찾아 transition action 을 부른다. Connection, IoLoop, Server 는 어느 쪽인지 모른다.
2. **라이브러리가 기억하는 것은 둘이다.** state 는 연결마다 하나이고 Connection 안의 `int32_t` 다 (지금은 DV6 의 `ReceiveFlow::machineState`). machine 은 run 마다 하나이고 run 을 부른 함수의 stack (`StatefulHandler`) 에 있다. 풀 안의 객체 (Server, Connection) 는 run 바깥 (사용자의 표, Session) 을 가리키는 멤버를 갖지 않는다 (설계 v5 19.1, `docs/learning/owner-destroy-deadlock.html`).
3. **HTTP 와 WebSocket 은 이 층의 사용자다.** 둘은 표 하나, Session 저장소 하나, transition action 함수들로만 만들어진다. 둘이 base 에 더 요구하는 것은 없고, 이 층에 요구하는 것은 11절의 다섯 줄이다.

```
KiottyNet_runServer(server, callbacks, app)            KiottyNet_runStatefulServer(server, stateful)
    │ CallbackHandler handler(*callbacks, app, *server)      │ StatefulHandler handler(*stateful, *server)
    └──────────────┬─────────────────────────────────────────┘
                   v
          Server::run(const Handler& handler)            run 의 stack 에 LoopParts{ ..., handler }
                   │
          IoLoop ── admit : handler.admit(info) → Decision
                 ── step  : Connection::dispatch → ... → handler.dispatch(caller, event) → KiottyNetConnectionAction
                 ── error : handler.report(info, code, message)
                   │
          Connection ── _machine_state (int32_t). Caller 가 참조를 든다. CallbackHandler 는 보지 않는다
```

## 0. 한눈에

| 것 | 지금 | 이 문서 |
|---|---|---|
| `domain/entity/kiotty_handler.h` 의 `Handler` | struct { callbacks&, app_context, owner& } | 가상 함수 다섯의 인터페이스. `owner&` 만 데이터 |
| 콜백을 부르는 줄 (Connection 5, ReceiveFlow 1, Caller 1, IoLoop 4) | `handler.callbacks.onX(_context, handle, ...)` | `handler.dispatch(caller, event)` 하나, `handler.admit(info)`, `handler.report(info, code, message)` |
| `ReceiveFlow::machineState / setMachineState` (DV6) | ReceiveFlow 의 atomic | Connection 의 private `int32_t _machine_state`. Caller 가 `int32_t&` 로 든다. DV6 닫힘 |
| `Connection::onWorkReady` | `onWorkStarted(_context, stream)` 부르고 끝 | dispatch 의 return 이 `thenWork()` 가 아니면 그 자리에서 `_work_flow.closeStream(generation, returned)` |
| `src/kiotty_net_statemachine.cpp` | `moveToNextState` 하나 | + `runStatefulServer / runStatefulEventClient` (StatefulHandler 를 만들어 `run`) |
| `domain/usecases/statemachine/` | 없음 | `StatefulHandler`, 표 찾기 / 검사 |
| `core/` | `Span`, `Result`, `checked` | + `FixedString<N>`, `parseText`, `parseUnsigned`, `parseHexadecimal` (DV12) |
| 내부 `const char*` (hostname, path) | 다섯 자리 (DV12) | 전부 `Span<const char>`. OS 에 C 문자열이 필요한 backend 안에서만 `FixedString<N>` 으로 복사 |
| 공개 API | v0 이름 + DV13 | 판 v1 (12절). bool 인자 → enum, 약어 → 단어 (제안). 헤더 넷과 `KiottyNet_HttpServer` 의 메서드 17 개는 그대로 |

## 1. 폴더

```
include/
    kiotty_net_base.h, kiotty_net_statemachine.h, kiotty_net_http.h, kiotty_net_websocket.h
                                    넷 그대로. 나누지 않는다 (수칙 0.2, 12.3)
src/
    kiotty_net_base.cpp             번역 (설계 v5 15절). run 은 CallbackHandler 를 stack 에 만들어 넘긴다 (6절)
    kiotty_net_statemachine.cpp     번역. moveToNextState, runStateful* (StatefulHandler 를 stack 에)
    implementation/
        core/
            kiotty_fixed_string.h   FixedString<N> (8.1)
            kiotty_text.h/.cpp      parseText, equalsIgnoreCase (8.2)
            kiotty_parse_integer.h/.cpp   parseUnsigned, parseHexadecimal (8.2)
        domain/
            entity/
                kiotty_handler.h            Handler 인터페이스 (3절)
                kiotty_connection_event.h   KiottyNetConnectionEvent 를 만드는 자유 함수 여섯 (3.2)
            usecases/
                io_loop/                    그대로. admit / 오류 보고 줄만 Handler 의 메서드로 (5절)
                statemachine/
                    kiotty_stateful_handler.h/.cpp   StatefulHandler (4절)
                    kiotty_rule_table.h/.cpp         findRule, checkRules, isAllowed (4.2, 4.3)
        data/
            repository/connection/
                kiotty_callback_handler.h/.cpp   CallbackHandler (3.3). 사용자 콜백 표를 아는 유일한 자리
                kiotty_connection.*              _machine_state, onWorkReady (3.4)
                kiotty_caller.h                  machine_state 참조 (3.4)
                kiotty_receive_flow.*            machineState / setMachineState 삭제
        context/
            kiotty_server.*, kiotty_event_client.*   run(const Handler&) 하나 (6절)
            kiotty_configuration.*                   const char* 를 parseText 로 (8.3)
```

StatefulHandler 가 `domain/usecases/` 에 있는 이유: 표 (사용자 값) 와 Connection 의 state (repository) 를 합쳐 다음 action 을 만드는 흐름이고, repository 와 entity 를 쓴다 (수칙 7.3). CallbackHandler 는 `data/repository/connection/` 이다. Caller 와 같은 층에서 사용자 콜백 표를 읽기만 한다. Handler 인터페이스 자체는 값 (`KiottyNetConnectionEvent`, `KiottyNetConnectionAction`) 만 알아 `domain/entity/` 다.

## 2. 공개 API (statemachine.h) 점검

`include/kiotty_net_statemachine.h` 는 requirement 8절 그대로다. 바뀌는 것은 넷이고 전부 12절의 판 v1 에 들어간다.

| 자리 | 지금 | v1 | 이유 |
|---|---|---|---|
| `KiottyNetStatefulAppContext` | `app_context, machine, createConnectionContext, releaseConnectionContext` | + `onError(app_context, info, code, message)` (NULL 허용) | base 의 `onError` 가 알리던 것 (TLS 실패, 거부된 action, accept 실패) 과 이 층의 `UNHANDLED_EVENT / ILLEGAL_TRANSITION`, Session 이 없어 거절한 연결이 갈 곳이 없다. 14절 1 |
| `kiotty::net::Rule` 의 두 번째 생성자 | 인자 8 개 (`state, event, action, next_count, n0 ~ n3`) | `Rule(state, event, action, KiottyNetNextStates next)`. `KiottyNetNextStates { int32_t at[KIOTTY_NET_MAX_NEXT]; uint32_t count; }` 는 `KiottyNetRule` 의 `next / next_count` 를 대신하는 값 | 수칙 4.1 인자 4 개. C 의 `KIOTTY_NET_RULE` 매크로는 같은 struct 를 채운다 |
| `KIOTTY_NET_RULE` 매크로 | 복합 리터럴 `(int32_t[]){ __VA_ARGS__ }` | `#if !defined(__cplusplus)` 안으로 | 복합 리터럴은 C99 다. C++ 은 `KiottyNet_rules` 를 쓴다 |
| `KiottyNetConnectionEvent.as.work_started.stream` | `KiottyNetConnectionStream` | 그대로 | `WORK_STARTED` 의 action 이 `thenWork()` 를 돌려주면 stream 을 worker 에게 넘겼다는 뜻 (3.4) |

이 밖의 뜻은 requirement 8절의 규칙 그대로다. 표에 (state, event) 가 없으면 `close(KIOTTY_NET_CODE_UNHANDLED_EVENT)`, 정확 일치 행이 ANY 행보다 먼저, `.to()` 밖으로 가면 `ILLEGAL_TRANSITION`, `CLOSED` 의 Transition 은 무시, `event.as.read.data` 는 return 하면 무효, C 표의 중복은 run 이 `DUPLICATE_RULE`.

## 3. Handler : 콜백을 부르는 자리 하나

### 3.1 인터페이스

```cpp
// domain/entity/kiotty_handler.h
class Handler
{
public:
    explicit Handler(KiottyNetEndpoint& connection_owner);
    virtual ~Handler() = default;
    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;

    virtual KiottyNetCode               validate() const = 0;                      // run 의 첫 줄. 표 검사 (4.3). CallbackHandler 는 SUCCESS
    virtual int32_t                     initialState() const = 0;                  // admit 마다 Connection 의 _machine_state 초기값. CallbackHandler 는 0
    virtual KiottyNetConnectionDecision admit(const KiottyNetConnectionInfo& info) const = 0;           // 옛 onConnected
    virtual KiottyNetConnectionAction   dispatch(const Caller& caller, const KiottyNetConnectionEvent& event) const = 0;
    virtual void                        report(const KiottyNetConnectionInfo& info, KiottyNetCode code, const char* message) const = 0;   // 옛 onError

    KiottyNetEndpoint& owner;
};
```

- public 은 다섯이다 (수칙 4.1 의 7 개 안). 옛 콜백 여덟 (onConnected, onReady, onRead, onWritten, onTimeout, onWorkStarted, onDisconnected, onError) 중 여섯이 `dispatch` 하나로 모인다. 이벤트의 종류가 `KiottyNetConnectionEventType` 여섯 (CONNECTED, READ, WRITTEN, WORK_STARTED, TIMEOUT, CLOSED) 과 정확히 같기 때문이다. `onReady` 가 `CONNECTED` 다 (requirement 8절 "CONNECTED 는 5절의 onReady 에 해당").
- `dispatch` 는 언제나 `KiottyNetConnectionAction` 을 돌려준다. 옛 `onWorkStarted` 와 `onDisconnected` 는 void 였다. `WORK_STARTED` 의 return 은 stream 에 적용되고 (`thenWork()` 는 "stream 은 worker 의 것"), `CLOSED` 의 return 은 Connection 이 버린다. 그래서 두 Handler 의 계약이 같아진다.
- `report` 는 `Caller::reject` 와 IoLoop 의 accept 실패 보고가 부른다. 연결이 없는 오류 (accept 실패) 는 `info` 가 빈 값이다 (지금과 같다).
- Handler 는 run 을 부른 thread 의 stack 에 있고 `LoopParts::handler` 가 참조한다 (설계 v5 10절). 멤버는 `owner&` 하나다. 두 구현이 사용자의 표를 참조로 들지만 그것도 stack 이다. 풀 안의 객체는 Handler 를 가리키지 않는다.
- 가상 함수는 RTTI 없이 된다 (`IoSource` 와 같다). `dispatch` 한 번은 가상 호출 하나와 switch 하나다. 콜백 한 번에 syscall 이 한 번 이상 따르므로 잴 수 없는 차이다. 그래도 10절의 벤치마크 `EchoRoundtrip` 을 전후로 비교한다.

### 3.2 KiottyNetConnectionEvent 를 만드는 자유 함수

```cpp
// domain/entity/kiotty_connection_event.h. 공개 struct 를 채우는 것뿐이라 entity 다
KiottyNetConnectionEvent connectedEvent();
KiottyNetConnectionEvent readEvent(KiottyNetSpan data);
KiottyNetConnectionEvent writtenEvent(size_t length);
KiottyNetConnectionEvent workStartedEvent(KiottyNetConnectionStream stream);
KiottyNetConnectionEvent timeoutEvent();
KiottyNetConnectionEvent closedEvent(KiottyNetCode reason);
```

### 3.3 CallbackHandler : 지금 하는 일 그대로

```cpp
// data/repository/connection/kiotty_callback_handler.h
class CallbackHandler : public Handler
{
public:
    CallbackHandler(const KiottyNetConnectionHandler& callbacks, KiottyNetAppContext app_context, KiottyNetEndpoint& owner);
    KiottyNetCode               validate() const override;        // SUCCESS
    int32_t                     initialState() const override;    // 0
    KiottyNetConnectionDecision admit(const KiottyNetConnectionInfo& info) const override;      // callbacks.onConnected(app_context, info)
    KiottyNetConnectionAction   dispatch(const Caller& caller, const KiottyNetConnectionEvent& event) const override;
    void                        report(const KiottyNetConnectionInfo& info, KiottyNetCode code, const char* message) const override;   // callbacks.onError
private:
    const KiottyNetConnectionHandler& _callbacks;
    KiottyNetAppContext               _app_context;
};
```

`dispatch` 는 `event.type` 의 switch 여섯 줄이다 (수칙 2.8, default 없음).

| event.type | 부르는 콜백 | 돌려주는 것 |
|---|---|---|
| CONNECTED | `onReady(caller.context, caller.handle())` | 그 return |
| READ | `onRead(context, handle, event.as.read.data)` | 그 return |
| WRITTEN | `onWritten(context, handle, event.as.written.length)` | 그 return |
| TIMEOUT | `onTimeout(context, handle)` | 그 return |
| WORK_STARTED | `onWorkStarted(context, event.as.work_started.stream)` | `KiottyNet_thenWork()`. stream 은 사용자가 가져갔다 |
| CLOSED | `onDisconnected(app_context, context, handle, event.as.closed.reason)` | `KiottyNet_thenHold(0)`. 버려진다 |

### 3.4 Connection 이 바뀌는 자리

Connection 의 public 아홉 (DV15) 과 IoSource 일곱 (DV9) 은 그대로다. 바뀌는 것은 private 과 Caller 다.

```cpp
// kiotty_connection.h (바뀌는 줄만)
private:
    int32_t _machine_state;             // ReceiveFlow 에서 왔다 (DV6 닫힘). IO thread 만 읽고 쓴다. initialize 가 configuration.machine_state 로 놓는다
// kiotty_connection_configuration.h
struct ConnectionConfiguration { ActiveSocket socket; ConnectionLimits limits; KiottyNetConnectionContext context; Span<const char> hostname; TlsProfile& profile; int32_t machine_state; };
// kiotty_caller.h
struct Caller { const Handler& handler; const KiottyNetConnectionInfo& info; StageBase& send; int32_t& machine_state; KiottyNetConnectionContext context; PoolHandle id; ... };
```

콜백을 부르던 다섯 줄이 `dispatch` 한 줄씩이 된다.

| 자리 | 지금 | 이 문서 |
|---|---|---|
| `Connection::onReady` | `handler.callbacks.onReady(_context, caller.handle())` | `handler.dispatch(caller, connectedEvent())` |
| `Connection::onWritten` | `onWritten(_context, handle, length)` | `dispatch(caller, writtenEvent(length))` |
| `Connection::onTimeout` | `onTimeout(_context, handle)` | `dispatch(caller, timeoutEvent())` |
| `ReceiveFlow::deliver` | `caller.handler.callbacks.onRead(caller.context, caller.handle(), data)` | `caller.handler.dispatch(caller, readEvent(data))` |
| `Connection::enterClosing` | `onDisconnected(app_context, _context, handle, reason)` | `(void)handler.dispatch(caller, closedEvent(reason))` |
| `Caller::reject` | `handler.callbacks.onError(handler.app_context, info, code, message)` | `handler.report(info, code, message)` |

`onWorkReady` 만 모양이 바뀐다. 옛 `onWorkStarted` 는 void 였고 stream 은 사용자가 가져갔다. 이제 return 이 `thenWork()` 가 아니면 "stream 을 그 자리에서 닫고 다음 action 을 건다" 는 뜻이다 (requirement 8절: WORK_STARTED 의 entry action 이 HOLD / READ / READ_UNTIL / WRITE / CLOSE 면 `closeConnectionStream(stream, entry_action)`).

```cpp
IoAction Connection::onWorkReady(const Handler& handler)
{
    _work.start(_id.generation);
    const Caller caller = callerFor(handler);
    const KiottyNetConnectionAction returned = handler.dispatch(caller, workStartedEvent(caller.stream()));
    if (returned.type == KIOTTY_NET_ACTION_TYPE_START_WORK) { return IoAction::none(); }    // worker 가 closeStream 한다
    const KiottyNetCode closed = _work_flow.closeStream(_id.generation, returned);           // 공개 closeConnectionStream 과 같은 길. desk 와 wake 는 필요 없다 (IO thread 다)
    if (closed != KIOTTY_NET_CODE_SUCCESS) { caller.reject(closed, WORK_CLOSE_MESSAGE); }
    return IoAction::none();                                                                 // outcome 은 다음 stepLocalEvents 의 WORK_DONE 이 집는다
}
```

- `WorkFlow::closeStream` 은 `verify(generation)` → `adoptAction` → `_reporter.finish(outcome)` 이다. IO thread 에서 WORKING 직후에 부르면 verify 가 통과하고 outcome 이 WorkContext 에 놓인다. 다음 `stepLocalEvents` 가 `isFinished()` 를 보고 `WORK_DONE` 으로 `ReceiveFlow::drive` 를 돌린다. 공개 `KiottyNet_closeConnectionStream` 이 worker 에서 가는 길과 같은 길이므로 "WORK_STARTED 의 entry action 은 stream 에 적용" 이 분기 없이 성립한다 (설계 방침 2 의 "엣지 케이스는 프로세스가 삼킨다").
- `closeStream` 이 실패하는 경우는 `adoptAction` 의 INVALID_ARGUMENT / BUFFER_FULL 뿐이다 (START_WORK 는 위에서 걸렀다). `reject` 로 알리고 stream 은 남는다. 사용자 프로토콜이라면 사용자 action 의 잘못이고, HTTP 는 `send_capacity` 로 자른 조각만 넘기므로 생기지 않는다.
- ConnectionState 의 snapshot 에 machine_state 는 넣지 않는다. 다른 thread 가 볼 이유가 없다 (설계 v5 14절의 "machineState 는 아무 thread (atomic)" 줄이 없어진다).

## 4. StatefulHandler

### 4.1 인터페이스

```cpp
// domain/usecases/statemachine/kiotty_stateful_handler.h
class StatefulHandler : public Handler
{
public:
    StatefulHandler(const KiottyNetStatefulAppContext& stateful, KiottyNetEndpoint& owner);
    KiottyNetCode               validate() const override;        // checkRules(*_stateful.machine) (4.3)
    int32_t                     initialState() const override;    // _stateful.machine->initial
    KiottyNetConnectionDecision admit(const KiottyNetConnectionInfo& info) const override;
    KiottyNetConnectionAction   dispatch(const Caller& caller, const KiottyNetConnectionEvent& event) const override;
    void                        report(const KiottyNetConnectionInfo& info, KiottyNetCode code, const char* message) const override;   // _stateful.onError 가 있으면
private:
    KiottyNetConnectionAction refuse(const Caller& caller, KiottyNetCode code) const;
    void                      release(const Caller& caller) const;
    const KiottyNetStatefulAppContext& _stateful;
};
```

멤버는 사용자의 struct 참조 하나다. 사용자의 `KiottyNetStatefulAppContext` 는 run 이 return 할 때까지 살아 있어야 한다 (requirement 8절 사용 예의 "run 동안만 살아 있으면 된다"). 라이브러리가 따로 잡는 per-run 객체는 없다 (구현 가이드 2.1).

```cpp
KiottyNetConnectionDecision StatefulHandler::admit(const KiottyNetConnectionInfo& info) const
{
    if (_stateful.createConnectionContext == nullptr) { return KiottyNet_acceptConnection(nullptr); }
    const KiottyNetConnectionContext session = _stateful.createConnectionContext(_stateful.app_context, info);
    if (session == nullptr)
    {
        report(info, KIOTTY_NET_CODE_SESSION_EXHAUSTED, SESSION_EXHAUSTED_MESSAGE);
        return KiottyNet_ignoreConnection();           // 받은 socket 을 바로 닫는다. Session 이 없으므로 CONNECTED 도 CLOSED 행도 release 도 없다. 14절 2
    }
    return KiottyNet_acceptConnection(session);
}

KiottyNetConnectionAction StatefulHandler::dispatch(const Caller& caller, const KiottyNetConnectionEvent& event) const
{
    const RuleResult found = findRule(*_stateful.machine, caller.machine_state, event.type);
    if (!found) { return refuse(caller, KIOTTY_NET_CODE_UNHANDLED_EVENT); }
    const KiottyNetRule& rule = found.value();
    const KiottyNetTransition transition = rule.transition_action(caller.context, caller.handle(), event);
    if (event.type == KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED) { release(caller); return KiottyNet_thenHold(0); }   // Transition 은 무시. 마지막 이벤트
    const bool allowed = isAllowed(rule, transition.next);
    if (!allowed) { return refuse(caller, KIOTTY_NET_CODE_ILLEGAL_TRANSITION); }
    caller.machine_state = transition.next;
    return transition.entry_action;
}

KiottyNetConnectionAction StatefulHandler::refuse(const Caller& caller, KiottyNetCode code) const
{
    report(caller.info, code, messageOf(code));
    return KiottyNet_thenClose(code);                  // WORK_STARTED 면 Connection 이 stream 을 이 action 으로 닫는다 (3.4). CLOSED 는 여기 오지 않는다
}

void StatefulHandler::release(const Caller& caller) const
{
    if (_stateful.releaseConnectionContext == nullptr) { return; }
    if (caller.context == nullptr) { return; }
    _stateful.releaseConnectionContext(_stateful.app_context, caller.context, caller.handle());
}
```

- `CLOSED` 행이 없으면 (`UNHANDLED_EVENT`) 어떻게 되나: `findRule` 이 비면 `refuse` 가 `thenClose` 를 돌려주지만 Connection 은 `CLOSED` 의 return 을 버린다. 그러나 `release` 가 불리지 않게 된다. 그래서 `CLOSED` 만은 행이 없어도 `release` 는 한다: `dispatch` 의 첫 줄에서 `event.type == CLOSED && !found` 면 `release(caller)` 뒤 `thenHold(0)`. 표에 `CLOSED` 행이 없는 것은 허용이다 (정리할 것이 release 뿐인 Session). 이것이 `UNHANDLED_EVENT` 가 report 되지 않는 유일한 자리다.
- `ILLEGAL_TRANSITION` 검사는 release 빌드에서도 한다. 최대 4 번의 정수 비교다. requirement 8절의 "debug 빌드는" 을 "항상" 으로 바꾸자는 제안이다 (14절 3). 사용자가 debug 로 고르면 `#if !defined(NDEBUG)` 한 줄이다.
- `machine_state` 는 `caller.machine_state` (Connection 의 `int32_t&`) 다. IO thread 만 만진다.

### 4.2 표 찾기

```cpp
// domain/usecases/statemachine/kiotty_rule_table.h
using RuleResult = Result<const KiottyNetRule&, KiottyNetCode>;
RuleResult    findRule(const KiottyNetStateMachine& machine, int32_t state, KiottyNetConnectionEventType event);   // 정확 일치가 먼저, 없으면 ANY. 둘 다 없으면 UNHANDLED_EVENT
bool          isAllowed(const KiottyNetRule& rule, int32_t next);                                                  // next_count 0 이면 true
KiottyNetCode checkRules(const KiottyNetStateMachine* machine);                                                     // 4.3
```

`findRule` 은 선형 탐색이다. 표는 행이 수십 개이고 한 이벤트마다 한 번 돌며, 그 이벤트는 syscall 하나 뒤에 온다. C++ 의 `KiottyNet_rules` 가 정렬해 두지만 C 의 표는 정렬되어 있지 않으므로 이진 탐색을 전제하지 않는다. 한 바퀴에 `state == KIOTTY_NET_ANY_STATE` 행을 기억해 두고 정확 일치가 없을 때 돌려준다 (구현 가이드 2.3 그대로).

### 4.3 표 검사 : run 의 첫 줄

`Server::run(handler)` 와 `EventClient::run(handler)` 의 첫 줄이 `handler.validate()` 다. 실패하면 listen 도 connect 도 하지 않고 그 code 를 돌려준다 (requirement 8절 "표 검사에 실패하면 열지 않고 바로 돌아온다").

| 검사 | code |
|---|---|
| `machine == NULL`, `rules == NULL`, `count == 0` | INVALID_ARGUMENT |
| 어떤 행의 `transition_action == NULL` | INVALID_ARGUMENT |
| 어떤 행의 `event` 가 enum 범위 밖, `next_count > KIOTTY_NET_MAX_NEXT`, `state` 나 `next[i]` 가 `KIOTTY_NET_ANY_STATE` 보다 작음 | INVALID_ARGUMENT |
| 같은 (state, event) 가 두 번 | DUPLICATE_RULE |

중복 검사는 O(n²) 비교다. `KiottyNetStatefulAppContext` 자체가 NULL 인 것은 번역 층 (`kiotty_net_statemachine.cpp`) 이 거른다.

## 5. IoLoop 이 바뀌는 자리

`domain/usecases/io_loop/kiotty_io_loop_step.cpp` 의 네 줄이다. 그 밖은 그대로다.

| 자리 | 지금 | 이 문서 |
|---|---|---|
| accept 뒤 admit | `parts.handler.callbacks.onConnected(parts.handler.app_context, info)` | `parts.handler.admit(info)` |
| `ConnectionConfiguration` 을 채우는 줄 | `{ socket, limits, decision.connection_context, hostname, profile }` | + `parts.handler.initialState()` |
| accept 실패, host 가 찼을 때의 보고 | `parts.handler.callbacks.onError(parts.handler.app_context, info, code, message)` | `parts.handler.report(info, code, message)` |
| `LoopParts::handler` | `const Handler&` | 그대로 (타입이 인터페이스가 됐을 뿐) |

`IoLoop::admit` (EventClient 가 run 전에 부르는 것) 도 같은 길이다.

## 6. Server, EventClient, 번역 층

Server 와 EventClient 의 `run` 은 하나다.

```cpp
KiottyNetCode Server::run(const Handler& handler)
{
    const KiottyNetCode checked = handler.validate();
    if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
    // 이하 지금의 run. LoopParts 의 handler 자리에 그대로
}
```

지금 `run(const KiottyNetConnectionHandler& callbacks, KiottyNetAppContext app_context)` 가 안에서 `Handler handler(callbacks, app_context, *this)` 를 만들던 것을 번역 층이 한다. Server 의 public 은 그대로 일곱 (initialize, reset, run, close, await, broadcast, lifetime) 이고 `runStateful` 같은 두 번째 run 은 없다.

```cpp
// src/kiotty_net_base.cpp
KiottyNetCode KiottyNet_runServer(KiottyNetServerHandle server, const KiottyNetConnectionHandler* handler, KiottyNetAppContext app_context)
{
    if (server == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
    if (handler == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
    const CallbackHandler callbacks(*handler, app_context, *server);
    return server->run(callbacks);
}
// src/kiotty_net_statemachine.cpp
KiottyNetCode KiottyNet_runStatefulServer(KiottyNetServerHandle server, const KiottyNetStatefulAppContext* stateful)
{
    if (server == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
    if (stateful == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
    const StatefulHandler handler(*stateful, *server);
    return server->run(handler);
}
```

- 번역 층이 객체 하나를 stack 에 만드는 것은 설계 v5 15절의 "번역" 범위다. 검사도 분기도 없다 (NULL 거르기는 지금도 번역 층의 일이다).
- Handler 가 run 을 부른 thread 의 stack 에 있으므로 run 이 return 하기 전까지 살아 있다. 모든 `CLOSED` dispatch 는 run 안에서 끝난다 (Owner::shutdown 이 WORKING 연결의 closeStream 을 기다린다. 설계 v5 12절).
- `KiottyNet_runEventClient / runStatefulEventClient` 도 같다. StreamClient 는 Handler 가 없다 (동기 API).
- 사용자가 같은 Server 를 어떤 run 은 callbacks 로, 어떤 run 은 stateful 로 돌려도 된다. Server 는 run 사이에 아무것도 기억하지 않는다.

## 7. 시나리오

echo 서버 (구현 가이드 3절의 표) 로 한 연결의 흐름을 적는다. 표: `(ANY, CONNECTED) → onConnected .to(ReadingHeader)`, `(ReadingHeader, READ) → onHeader .to(ReadingBody)`, `(ReadingBody, READ) → onBody .to(Writing)`, `(Writing, WRITTEN) → onWritten .to(ReadingHeader)`, `(ANY, TIMEOUT) → onTimeout`, `(ANY, CLOSED) → onClosed`. 초기 상태 ReadingHeader.

| 바퀴 | 일어나는 일 | Handler 가 하는 일 | Connection 의 `_machine_state` |
|---|---|---|---|
| accept | IoLoop 이 `handler.admit(info)` | `createConnectionContext(app, info)` → Session. NULL 이면 `report(SESSION_EXHAUSTED)` 뒤 socket 을 닫는다 | initialize 가 `initialState()` = ReadingHeader 로 |
| onOpened (평문) / 핸드셰이크 DONE (TLS) | `Connection::onReady` → `dispatch(connectedEvent())` | `findRule(ReadingHeader, CONNECTED)` 는 ANY 행 → `onConnected(session, handle, event)` → `{ ReadingHeader, thenRead(4, 5000) }` | ReadingHeader |
| RECEIVE 완료, 4 바이트 | `ReceiveFlow::deliver` → `dispatch(readEvent(data))` | `(ReadingHeader, READ)` → `onHeader` → `{ ReadingBody, thenRead(length, 5000) }` | ReadingBody |
| RECEIVE 완료, body | 같은 길 | `(ReadingBody, READ)` → `onBody` 가 Session 에 복사하고 `{ Writing, thenWrite(frame, 5000) }` | Writing |
| SEND 완료 (USER) | `Connection::onWritten` → `dispatch(writtenEvent(length))` | `(Writing, WRITTEN)` → `{ ReadingHeader, thenRead(4, 5000) }` | ReadingHeader |
| 5 초 동안 아무것도 안 옴 | `Connection::onTimeout` → `dispatch(timeoutEvent())` | ANY 행 → `onTimeout` → `{ Closing, thenClose(TIMEOUT) }`. READ 가 걸려 있어도 CLOSE 는 바로 닫는다 (설계 v5 11절) | Closing |
| CLOSE 뒤 enterClosing | `dispatch(closedEvent(reason))` | `(ANY, CLOSED)` → `onClosed` (Transition 무시) → `releaseConnectionContext(app, session, handle)` | 그대로. reset 이 0 으로 |

다른 길들.

| 상황 | 흐름 |
|---|---|
| 표에 (Writing, READ) 가 없는데 READ 가 왔다 | 생길 수 없다. READ 는 `thenRead` 를 돌려준 상태에서만 온다. 생기는 것은 TIMEOUT (어느 상태에서나) 과 CLOSED (어느 상태에서나) 이고, 그 둘의 ANY 행을 빠뜨리면 `UNHANDLED_EVENT` 다. TIMEOUT 은 `report` 뒤 `thenClose(UNHANDLED_EVENT)` → onDisconnected 대신 CLOSED dispatch → 행이 없으면 release 만 (4.1) |
| `.to(ReadingBody)` 인데 action 이 Writing 을 돌려줬다 | `isAllowed` 가 false → `report(ILLEGAL_TRANSITION)` 뒤 `thenClose(ILLEGAL_TRANSITION)`. state 는 바뀌지 않은 채 닫힌다 |
| onTimeout 이 READ 가 걸린 채 `thenRead` 를 돌려줬다 | `Connection::onTimeout` 의 `isUserActionPending` → `extend`. Handler 는 모른다. state 는 action 이 돌려준 next 로 이미 바뀌었다 (연장 뒤 READ 가 오면 그 state 의 READ 행이 돈다) |
| action 이 `thenWork()` 를 돌려줬다 | `apply(START_WORK)` → `stepLocalEvents` 의 WORK_READY → `onWorkReady` → `dispatch(workStartedEvent(stream))`. `(S, WORK_STARTED)` 행의 action 이 stream 을 Session 에 넣고 worker 에게 넘긴 뒤 `{ Working, thenWork() }` 를 돌려주면 stream 은 worker 의 것. worker 가 `KiottyNet_closeConnectionStream(stream, thenWrite(...))` 로 끝내면 WORK_DONE → drive → WRITTEN 이 `(Working, WRITTEN)` 행으로 온다 |
| WORK_STARTED 행의 action 이 `thenRead(n)` 을 돌려줬다 | Connection 이 그 자리에서 `_work_flow.closeStream(generation, thenRead(n))`. worker 는 없었던 것과 같다. 다음 `stepLocalEvents` 가 WORK_DONE 으로 drive 한다 (3.4) |
| WORK_STARTED 행이 없다 | `refuse` → `thenClose(UNHANDLED_EVENT)` → 같은 `closeStream` 길로 닫힌다 |
| `createConnectionContext` 가 NULL 함수 | session 은 NULL. action 은 `connection_context == NULL` 을 받는다. release 는 부르지 않는다 |
| 소유자의 `close()` | 모든 연결에 `enterClosing(CLOSED)` → CLOSED dispatch → release. WORKING 연결은 worker 의 closeStream 뒤 |

## 8. core : FixedString 과 parse (DV12)

### 8.1 FixedString<N>

```cpp
// core/kiotty_fixed_string.h
template <size_t N>
class FixedString                                  // 늘 null 종료. N 은 종료 문자를 포함한 칸 수. 수칙 2.6
{
public:
    static constexpr size_t MAX_LENGTH = N - 1;
    FixedString();                                 // 빈 문자열
    bool              assign(Span<const char> text);   // 길이 > MAX_LENGTH 면 false 이고 비운다 (수칙 2.6 "assign 실패 시 비운다")
    bool              append(Span<const char> text);   // 자리가 모자라면 false 이고 바꾸지 않는다
    Span<const char>  span() const;                // 종료 문자 제외
    const char*       c_str() const;               // OS / OpenSSL 에 넘기는 자리에서만
    size_t            length() const;
    bool              empty() const;
    void              clear();
private:
    std::array<char, N> _text;
    size_t              _length;
};
```

- public 일곱이다. 복사는 `Span::copyFrom` 으로 한다 (`memcpy` 는 Span 안의 한 자리).
- `c_str()` 을 부르는 자리는 `data/infra/*/backend/` 와 OpenSSL 호출 (`SSL_set_tlsext_host_name`, `SSL_CTX_use_certificate_chain_file`) 뿐이다. 그 밖에서 `c_str()` 을 쓰면 `check_rules.py` 가 BANNED 로 잡는다 (`c_str` 을 BANNED 목록에, backend 와 tls backend 는 예외).
- 스택의 FixedString 은 `KIOTTY_NET_MAX_PATH_LENGTH` (260), `KIOTTY_NET_MAX_HOST_LENGTH` (256) 크기다. 수칙 1.7 의 1024 바이트 안이다.

### 8.2 parse 셋

```cpp
// core/kiotty_text.h
using TextResult = Result<Span<const char>, KiottyNetCode>;
TextResult parseText(const char* text, size_t max_length);        // C 경계의 유일한 입구. NULL 이면 INVALID_ARGUMENT. strnlen(text, max_length + 1) 가 max_length 를 넘으면 INVALID_ARGUMENT. UTF-8 이 아니면 INVALID_ENCODING (사용자 결정 2026-11-09)
bool       equalsIgnoreCase(Span<const char> a, Span<const char> b);  // ASCII. HTTP 헤더 이름, "chunked"
// core/kiotty_parse_integer.h
using UnsignedResult = Result<uint64_t, KiottyNetCode>;
UnsignedResult parseUnsigned(Span<const char> text);               // 10 진. 빈 입력, 숫자 아닌 글자, 20 자리 초과, 오버플로는 INVALID_ARGUMENT. Content-Length
UnsignedResult parseHexadecimal(Span<const char> text);            // 16 진. chunk 크기. 같은 규칙, 16 자리 초과
```

- `parseText` 가 `const char*` 를 받는 유일한 함수다. 수칙 2.1 의 "경계의 값은 parse* 하나를 지나야 들어온다" 의 그 하나다. 경계는 공개 C 함수의 `const char*` 인자 (host, pipe_name, cert_file, key_file, key_password, trust_store_path, 파일 path, HTTP 의 path / name / value / content_type, WebSocket 의 path / protocols / reason) 전부다.
- 수칙 2.10 의 다섯 입력 (빈 입력, 최대 길이, 최대 길이 + 1, 중간 null 바이트, 잘못된 UTF-8) 을 셋 모두 테스트한다. `parseText` 의 "중간 null 바이트" 는 C 문자열이라 그 자리에서 끝난 것으로 본다 (길이가 짧아질 뿐 실패가 아니다). 이 해석을 테스트 이름에 적는다.
- 문자열 상수 (`TlsFailure::message`, `Caller::reject` 의 message, `onError` 의 message) 는 경계 밖에서 온 값이 아니다. 라이브러리 안의 리터럴이고 공개 API 가 `const char* message` 로 받으므로 그대로 둔다. DV12 의 "TLS 실패 메시지 상수" 줄은 편차가 아니라 수칙 2.6 의 대상이 아닌 것으로 본다 (14절 5).

### 8.3 내부 경계에서 `const char*` 가 사라지는 자리

| 자리 | 지금 | 이 문서 |
|---|---|---|
| `context/kiotty_configuration.*` 의 `initialize*Configuration(config, const char* host / pipe_name)`, `measureText`, `writeText`, `hostnameOf` | `strnlen` + 상한 | `parseText(host, KIOTTY_NET_MAX_HOST_LENGTH - 1)` 한 번 → `Span<char>{ configuration.address.host, N }.copyFrom` + 종료 문자. `measureText / writeText / hostnameOf` 삭제. 설정 struct 의 `char[]` 를 읽는 자리는 `textOf(const char (&field)[N]) → Span<const char>` 하나 (`strnlen(field, N)`. 설정 struct 는 initialize 가 채웠으므로 종료가 보장된다) |
| `context/kiotty_tls_configuration.*` 의 cert / key / password / trust_store | 같다 | `parseText(..., KIOTTY_NET_MAX_PATH_LENGTH - 1)` |
| `ConnectionConfiguration::hostname` (`const char*`) | Server 는 빈 문자열, EventClient 는 설정의 host | `Span<const char> hostname` |
| `TlsEngine::open(profile, const char* hostname, capacity)` | SNI 와 hostname 검증 | `Span<const char> hostname`. backend 안에서 `FixedString<KIOTTY_NET_MAX_HOST_LENGTH>` 로 복사해 `c_str()` |
| `TlsProfile` 의 파일 경로 | `KiottyNetTlsIdentity` 의 `char[]` 를 backend 가 그대로 | `textOf` 로 `Span<const char>` → backend 안에서 `FixedString<KIOTTY_NET_MAX_PATH_LENGTH>` |
| `InputFile::open(const char* path)`, `ConnectionStream::writeFile(const char* path, ...)`, `Endpoint::writeStreamFile(id, const char* path, FileRange, timeout)`, `ConnectionAccess::writeStreamFile` | `const char*` 가 세 층을 지난다 | 번역 층 (`KiottyNet_writeConnectionStreamFile`) 이 `parseText(path, KIOTTY_NET_MAX_PATH_LENGTH - 1)`. 실패면 INVALID_ARGUMENT. 안쪽은 전부 `Span<const char> path`. `kiotty_posix_file.cpp / kiotty_windows_file.cpp` 가 `FixedString<KIOTTY_NET_MAX_PATH_LENGTH>` 로 복사해 `open / CreateFileA` |
| `Caller::reject(code, const char* message)`, `Handler::report(..., const char* message)`, `TlsFailure::message` | 리터럴 | 그대로 (8.2) |

`Endpoint` 의 가상 함수 일곱은 개수가 그대로이고 `writeStreamFile` 의 둘째 인자 타입만 바뀐다. DV12 는 코드가 이 표대로 되면 닫는다.

## 9. 메모리와 thread

| 것 | 크기 | 수명 |
|---|---|---|
| `Connection::_machine_state` | 4 바이트 (ReceiveFlow 에서 옮긴 것. Connection 크기 변화 없음) | 연결. initialize ~ reset |
| `CallbackHandler` | 참조 둘 + 포인터 하나 | run 의 stack |
| `StatefulHandler` | 참조 둘 | run 의 stack |
| `KiottyNetStatefulAppContext`, 표, Session 저장소 | 사용자 것 | 사용자가 run 동안 유지 |
| `FixedString<N>` | N + 8 | backend 함수의 stack (≤ 268) |

할당은 없다. DV1 의 표에 더해지는 것이 없다.

| 클래스 | 부르는 thread |
|---|---|
| Handler 의 다섯 | `validate` 는 run 의 첫 줄 (IO thread 가 되는 thread). 나머지 IO thread |
| `Connection::_machine_state` | IO thread (Caller 를 거쳐 Handler 가) |
| 사용자의 transition action, create / release / onError | IO thread. 안에서 할 수 있는 것은 base 콜백과 같다 (`KiottyNet_getConnectionInfo`, `KiottyNet_notifyConnection`) |

## 10. 테스트

테스트 계획 문서 (`docs/test/test_base_v0.html`) 에 16절 "StateMachine" 과 5절의 core 케이스를 더한다. 이름 규칙은 그 문서 3절이다.

| 층 | 케이스 |
|---|---|
| core (unit) | `FixedString`: `AssignLongerThanMaxLengthFailsAndClears`, `AppendWithoutRoomFailsAndKeepsTheText`, `CStrIsAlwaysTerminated`. `parseText / parseUnsigned / parseHexadecimal` 각각 수칙 2.10 의 다섯 + 대표값 (`ParseUnsignedReadsContentLength`, `ParseHexadecimalReadsChunkSize`, `ParseUnsignedRejectsOverflow`) |
| rule table (unit) | `FindRulePrefersExactStateOverAny`, `FindRuleFallsBackToAny`, `FindRuleWithoutARowIsUnhandledEvent`, `IsAllowedWithoutNextListIsTrue`, `CheckRulesRejectsNullMachineRulesAndZeroCount`, `CheckRulesRejectsDuplicateStateEvent`, `CheckRulesRejectsNullAction`, `CheckRulesAcceptsTheSortedCppTable` |
| StatefulHandler (unit, Caller 는 테스트 더블의 Handler 없이 값으로 조립) | `DispatchMovesStateAndReturnsEntryAction`, `DispatchClosedIgnoresTransitionAndReleases`, `DispatchClosedWithoutARowStillReleases`, `IllegalTransitionReportsAndCloses`, `UnhandledEventReportsAndCloses`, `AdmitWithoutCreateAcceptsNullSession`, `AdmitWithNullSessionReportsSessionExhaustedAndIgnores`, `WorkStartedReturningReadIsAppliedToTheStream` (Connection 과 함께, 9절의 TestHandler 로) |
| CallbackHandler (unit) | 기존 Connection 테스트 (9절 평문 96, TLS 23) 가 `TestHandler` 를 `CallbackHandler` 로 바꿔 그대로 통과한다. 새 케이스는 `WorkStartedCallbackLeavesTheStreamToTheUser` 하나 |
| C API (integration) | 구현 가이드 3절의 echo (서버 표 + EventClient 표) 를 C++ 과 C 로: `StatefulEchoRoundTrip`, `StatefulEchoRoundTripInC`. `RunStatefulRejectsNullAndDuplicateRules`, `RunStatefulClosesOnUnhandledTimeout`, `StatefulWorkStartedRowHandsTheStreamToAWorker`, `SessionNullIgnoresTheConnectionAndReportsOnError`, `OnErrorNullIsAllowed` |
| 헤더 | `tests/unit/kiotty_test_headers_c.c` 가 `KIOTTY_NET_RULE` 로 표를 하나 만든다 (C99 복합 리터럴). C++ 쪽은 `KiottyNet_rules` 의 중복 행이 compile 되지 않는 것을 `static_assert` 가 아니라 별도의 compile-fail 검사로 (CMake `try_compile`, 12.3) |
| 벤치마크 | `EchoRoundtrip` 을 callbacks 판과 stateful 판으로. 가상 호출의 차이가 잡음 안인지 본다 |

## 11. HTTP 와 WebSocket 이 얹히는 자리

둘의 설계는 다음 문서 (`design_http_v0.md`) 다. 여기서는 이 층이 둘을 위해 갖추는 것과, 둘이 이 층을 어떻게 쓰는지의 모양만 정한다. 구현 가이드 4절과 user_guide 둘이 기준이다.

**HTTP 와 WebSocket 이 이 층에 요구하는 다섯 줄.**

1. transition action 이 `KiottyNetConnectionHandle` 을 받는다 (`KiottyNet_notifyConnection` 으로 pong / close frame / push 를 보내기 위해). 있다.
2. `WORK_STARTED` 행의 action 이 stream 을 Session 에 넣고 `thenWork()` 를 돌려주면 worker 가 `KiottyNet_closeConnectionStream(stream, thenWrite(첫 조각))` 으로 끝낼 수 있다 (HTTP 의 `finishHttpWorkWithResponse`). 3.4 로 성립한다.
3. `TIMEOUT` 이 어느 상태에서나 오고 action 이 그 상태에 맞는 다음 action 을 돌려준다 (HTTP header / keep-alive timeout, WebSocket ping / pong / close 타이머). 표의 ANY 행 또는 상태별 행. 있다.
4. Session 이 없으면 연결을 받지 않는다 (HttpSession pool 이 비었을 때). `admit` 의 ignore + `onError(SESSION_EXHAUSTED)`. 4.1.
5. 표와 Session 저장소가 라이브러리 안에 있어도 사용자 표와 같은 길을 간다. `StatefulHandler` 는 `KiottyNetStatefulAppContext` 가 누구 것인지 모른다. 6절.

**HttpServer 의 모양 (고려만).**

```
context/kiotty_http_server.h/.cpp
    HttpServer : KiottyNetHttpServer 의 base. 멤버 : Server& (Context 의 풀에서 받은 것), HttpSessionPool (max_clients, create 때), RouteTable (max_routes, create 때), KiottyNetHttpLimits
    run() : stack 에 KiottyNetStatefulAppContext{ this, &HTTP_MACHINE, &acquireSession, &releaseSession, &reportHttpError } → StatefulHandler → _server.run(handler)
domain/usecases/http/
    HTTP_MACHINE (static constexpr, KiottyNet_rules 로 만든다), transition action 들 (구현 가이드 4.3 의 상태 표), 요청 파서 (parseRequestLine, parseHeaderLine → core 의 parseUnsigned / parseHexadecimal / equalsIgnoreCase)
data/repository/http/
    HttpSession (요청 버퍼, 헤더 span 배열, body 버퍼, 응답 버퍼. 구현 가이드 4.2), HttpSessionPool (SizedReusablePool<HttpSession>)
```

- HttpSession 이 `createConnectionContext` 가 돌려주는 Session 이다. 사용자 프로토콜과 똑같은 자리이고 저장소가 라이브러리 안에 있을 뿐이다 (구현 가이드 2.6).
- `KiottyNetHttpResponseHandle` / `KiottyNetHttpWorkHandle` 은 지금 `int64_t` 다. base 의 handle 이 `{ owner, id }` struct 가 된 이유 (소유자를 handle 에서 찾는다. 설계 v5 3절) 가 여기에도 그대로 있다. 구현 가이드 4.5 의 미결 `ownerOf(index)` 는 `{ KiottyNetHttpServerHandle server; int64_t id; }` 로 닫는 것을 제안한다 (14절 6). 12절의 판 v1 에 넣는다.
- 사용자 콜백 (`onRequest`, `onWorkStarted`) 은 IO thread 에서 transition action 안에서 불린다. 콜백이 받는 것은 HTTP 의 handle 뿐이고 Connection handle 은 `request.connection` 으로만 보인다.

**WebSocket 과 "표 바꿔 타기".** 구현 가이드 5절과 user_guide_websocket 9절의 미결 ("HTTP 표에서 WebSocket 표로 넘어가는 방법. 두 표를 한 표로 합치는지, machine 포인터를 덮어쓰는지") 을 여기서 정한다. **한 표다.** machine 은 run 마다 하나라는 규칙 (설계 방침 2) 을 지키고, state 는 `int32_t` 라 두 프로토콜의 상태를 한 enum 에 둘 수 있다.

```cpp
enum class ProtocolState : int32_t
{
    HTTP_READING_REQUEST_LINE = 0, HTTP_READING_HEADER_LINE, HTTP_READING_BODY, HTTP_READING_CHUNK_SIZE, HTTP_READING_CHUNK_DATA, HTTP_WORKING, HTTP_SENDING, HTTP_CLOSING,
    WEB_SOCKET_OPENING = 100,       // 101 을 보내는 중. WRITTEN 이 오면 onOpen
    WEB_SOCKET_READING_HEADER, WEB_SOCKET_READING_EXTENDED_LENGTH, WEB_SOCKET_READING_MASK, WEB_SOCKET_READING_PAYLOAD, WEB_SOCKET_CLOSING
};
```

- 사용자는 표를 보지 않는다. 사용자가 넘기는 것은 HTTP 의 route (`onRequest`, `onWorkStarted`) 와 Upgrade 경로의 WebSocket 콜백 (`onOpen`, `onMessage`, `onClose`) 이고, 라이브러리의 transition action 이 그 콜백을 부른다. 표는 라이브러리 안의 `static constexpr` 하나다.
- HttpServer 의 표는 HTTP 행 + WebSocket 행이다. Upgrade 경로가 없으면 WebSocket 행에 닿는 일이 없을 뿐이다. Upgrade 요청을 받은 HTTP 행의 action 이 `{ WEB_SOCKET_OPENING, thenWrite(101) }` 을 돌려주면 그 연결은 그 뒤로 WebSocket 행만 밟는다. WebSocketServer (WebSocket 만 쓰는 앱) 의 표는 HTTP 행 중 Upgrade 에 필요한 것 (request line, header line) + WebSocket 행이다. WebSocketClient 의 표는 Upgrade 요청을 보내고 101 을 읽는 행 둘 + WebSocket 행 (mask 를 씌우는 쪽).
- Session 도 하나다. Session 배열은 Connection 풀과 같은 크기 (`max_clients`) 이고 Connection 슬롯 번호를 그대로 index 로 쓴다 (14절 2). 그래서 HTTP / WebSocket 에는 `SESSION_EXHAUSTED` 가 없다.
- Session 의 큰 버퍼는 `core/` 의 `Stage` 하나다. `Stage` 는 이미 프로토콜을 모르는 버퍼다 (Provider 가 쓰고 Consumer 가 읽는다). HTTP 는 이것을 요청 body 로, Upgrade 뒤의 WebSocket 은 같은 Stage 를 메시지 조립 버퍼로 쓴다. 한 연결에서 둘이 동시에 쓰이지 않으므로 슬롯마다 하나다. 새 버퍼 클래스는 만들지 않는다. 각 프로토콜이 Stage 를 자기 뜻으로 읽는 것은 `domain/usecases/http/`, `domain/usecases/websocket/` 의 함수 몫이다 (body 를 채우기, frame payload 를 unmask 하며 채우기).
- Stage 의 크기는 `max(max_body_bytes, max_message_bytes)` 다. 크기는 create 때 정해지므로 WebSocket 의 `max_message_bytes` 도 create 때 알아야 한다. `KiottyNet_addWebSocketUpgrade(http_server, &websocket)` 는 create 뒤라 할당할 수 없으므로, Upgrade 경로의 `KiottyNetWebSocketConfiguration.max_message_bytes` 가 `max_body_bytes` 를 넘으면 `INVALID_ARGUMENT` 로 거절한다 (14절 7).
- 송신 control frame (pong, ping, close) 과 사용자의 `send` 는 전부 push lane (`KiottyNet_notifyConnection`) 이다 (user_guide_websocket 2절). IO thread 의 action 안에서 부르면 그 자리에서 복사된다. 다른 thread 의 `closeWebSocket` 은 close frame 을 notify 로 넣고 Session 의 atomic 플래그를 세운다. 다음 이벤트 (WRITTEN 은 notify 에 없으므로 READ / TIMEOUT) 에서 action 이 플래그를 보고 CLOSING 으로 간다. 이것은 다음 설계의 일이다.

## 12. 공개 API 판 v1 (DV13)

DV13 이 미룬 것의 제안이다. 공개 헤더는 사용자가 지정한 모양을 따르므로 (수칙 0.2) 아래의 수칙 번호는 차이를 가리킬 뿐이고, 무엇을 바꿀지는 사용자가 정한다. 이름 표는 `tools/rename_public_api.tsv` 에 이어 적는다 (단계 2 의 120 쌍 뒤). enum 값의 번호는 바뀌지 않는다.

### 12.1 bool 인자 → enum (수칙 4.6)

| 지금 | v1 |
|---|---|
| `KiottyNet_configureTlsServerVersions(config, bool tls_1_2, bool tls_1_3)`, `...TlsClientVersions` | `(configuration, uint32_t versions)`. `KIOTTY_NET_TLS_VERSION_V12 \| V13` 의 비트 합. 이미 그런 enum 이고 struct 의 `versions` 도 그 값이다. 0 이면 INVALID_ARGUMENT |
| `KiottyNet_configureTlsClientVerification(config, bool verify_server, bool verify_hostname)` | `(configuration, KiottyNetTlsVerification verification)`. `enum { KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME, _SERVER, _NONE }`. 0 이 가장 엄격한 값이라 0 으로 채운 struct 도 검증한다 (전의 `skip_*` 필드와 같은 성질). hostname 만 검증하는 조합은 뜻이 없어 셋이다. struct 의 `int skip_server_verification, skip_hostname_verification` → `KiottyNetTlsVerification verification` 하나 |
| `KiottyNet_configureServerNoDelay(config, bool)`, `...ClientNoDelay` | `(configuration, KiottyNetNoDelay no_delay)`. `enum { KIOTTY_NET_NO_DELAY_OFF, _ON }`. struct 의 `bool no_delay` → `KiottyNetNoDelay no_delay`. 내부의 `enum class NoDelay` 와 맞는다 |
| C++ builder 의 `versions(bool, bool)`, `verification(bool, bool)`, `noDelay(bool)` | 같은 enum / 비트 합 |

bool 을 돌려주는 것 (`KiottyNet_isConnectionStreamOpen`, `KiottyNet_isHttpWorkConnected`, `onUpgrade` 의 return), struct 의 bool 필드 (`KiottyNetConnectionDecision.accepted`) 는 수칙 4.6 의 대상 (bool 인자) 이 아니다. 그대로 둔다.

### 12.2 약어 (수칙 4.5)

| 지금 | v1 | 자리 |
|---|---|---|
| 인자 `config` | `configuration` | base.h 의 initialize / configure 함수 전부, create* 셋, C++ builder 와 wrapper 의 생성자 |
| 인자 `cert_file` | `certificate_file` | TLS 설정 둘, `KiottyNetTlsIdentity.cert_file` 필드도 |
| 인자 `ws` | `websocket` | websocket.h 의 handle 함수와 콜백 |
| `KiottyNetAppContext`, `app_context` | 그대로 | `App` 은 약어가 아니라 소프트웨어로서의 application 을 뜻하는 한 단어로 본다 (사용자 결정 2026-11-09, 수칙 4.5 의 예외) |
| `tls` (인자), `Tls` (타입), `Http`, `Tcp`, `Url`, `Id` | 그대로 | 약어가 아니라 이름이다 (설계 v5 3절의 cipher suite 와 같은 이유). `del` (C++ wrapper 의 DELETE) 도 예약어 때문의 예외로 둔다 |

### 12.3 헤더 넷은 그대로

공개 헤더는 `kiotty_net_base.h`, `kiotty_net_statemachine.h`, `kiotty_net_http.h`, `kiotty_net_websocket.h` 넷이고 나누지 않는다 (사용자 결정 2026-11-09). 공개 헤더는 수칙이 아니라 사용자가 지정한 모양을 따르고 (수칙 0.2), 400 줄 한계와의 차이는 DV13 에 남긴다. `tools/check_rules.py` 는 지금처럼 `include/` 를 보지 않는다.

- 위에 C, 아래에 C++ 이라는 모양과 "이 헤더 하나만 include 하면 된다" (user_guide_http) 는 그대로다.
- `kiotty::net::OwnedHandle` 을 http.h 와 websocket.h 가 글자까지 같게 되풀이하는 규칙 (user_guide_http 헤더 구성) 도 그대로다.
- `KiottyNet_rules` 의 중복 행이 compile 되지 않는 것은 `tests/unit/compile_fail/` 의 작은 소스와 CMake `try_compile` 로 본다 (10절).

### 12.4 인자 5 개 이상 (수칙 4.1)

| 지금 | v1 |
|---|---|
| `KiottyNet_writeConnectionStreamFile(stream, path, offset, length, timeout)` | 그대로 (사용자 결정 2026-11-09). offset 과 length 를 따로 받고 7 개 한계와의 차이는 DV13 에 둔다 |
| `kiotty::net::Rule` 의 8 인자 생성자 | 2절. `KiottyNetNextStates` |
| `KiottyNet_HttpServer::route(method, path, on_request, on_work_started, app_context)` 와 메서드별 별칭의 5 인자 overload | 없앤다. 남는 overload 는 `(method, path, KiottyNetHttpRoute)` 와 `(method, path, KiottyNetHttpRequestAction)` (context 없고 Work 없는 라우트) 둘. 셋 다 필요하면 `KiottyNet_makeHttpRoute(context, onRequest, onWorkStarted)` 로 값을 만들어 넘긴다 (C 와 같은 모양) |

그 밖의 공개 함수는 모두 4 개 이하다 (`onDisconnected`, `onError`, `onWorkStarted` 의 콜백 넷, `createWebSocketServer / Client` 넷, `configureServerBufferCapacity` 넷).

### 12.5 wrapper 의 public 메서드 (수칙 4.1, DV11 의 셈법)

| 클래스 | 지금 | v1 |
|---|---|---|
| `KiottyNet_EventClient` | 8 (valid, code, handle, run, close, await, connection, notify) | `valid()` 를 `explicit operator bool() const` 로 → 7. 연산자는 세지 않는다 (DV11). `KiottyNet_Server`, `KiottyNet_StreamClient`, `KiottyNet_Context`, HTTP / WebSocket 의 소유 wrapper 도 같은 모양으로 맞춘다 (일관성) |
| `KiottyNet_HttpServer` | 17 | 그대로 (사용자 결정 2026-11-09). 라우트와 파일 등록이 한 객체에 모이는 모양을 유지하고 7 개와의 차이는 DV13 에 둔다 |
| `KiottyNet_HttpResponse` | 7 | 그대로 |
| `KiottyNet_WebSocket` | 6 | 그대로 |
| `KiottyNet_WebSocketClient` | 7 (valid, handle, run, close, await, socket, send) | valid → operator bool 로 6 |
| `kiotty::net::Rule`, `From`, `FromWhen`, `Table` | 값 객체. `to` 네 overload 는 하나로 센다 | 그대로 |

### 12.6 HTTP handle

`KiottyNetHttpResponseHandle`, `KiottyNetHttpWorkHandle` 을 `int64_t` 에서 `{ KiottyNetHttpServerHandle server; int64_t id; }` 두 struct 로 (11절). `KiottyNetWebSocketHandle` 도 `{ owner, id }` 로 같은 모양이 되어야 하지만 WebSocket 의 소유자가 둘 (HttpServer 위의 Upgrade, WebSocketServer, WebSocketClient) 이라 다음 설계에서 정한다.

### 12.7 이 판이 바꾸지 않는 것

핸들 struct 둘, `KiottyNetConnectionHandler` 의 콜백 여덟과 그 인자, action 함수 여섯, `KiottyNetCode` 의 값 (M2 에서 enum 끝에 `INVALID_ENCODING` 하나를 더했다. 있던 값은 그대로다), 설정 struct 의 필드 배치 (12.1 의 둘 제외), `initialization_mark`.

## 13. 사이클

base 의 사이클 정의 (`plan_base_v0.md` 0절: 로컬 두 환경 초록, commit 하나, progress 문서) 그대로다. 순서는 의존 순서이고 이름 변경 (M1) 을 먼저 해 새 코드가 처음부터 v1 이름으로 쓰이게 한다.

| 사이클 | 하는 것 | 완료 |
|---|---|---|
| M1 공개 API 판 v1 (끝, 2026-11-09) | 2절의 statemachine.h 넷, 12.1, 12.2, 12.4 (파일 쓰기 제외), 12.5, 12.6. 이름 표, 테스트와 user_guide 의 이름 | 매트릭스 초록. 네 헤더가 C++17 과 C99 로 syntax check 통과 |
| M2 core (DV12) (끝, 2026-11-09) | 8절. FixedString, parse 셋, 8.3 의 표 | 수칙 2.10 의 다섯 × 3 통과. `grep "const char\*" src/implementation` 이 리터럴 message 와 parseText 뿐 |
| M3 Handler (끝, 2026-11-09) | 3절, 5절, 6절의 `run(const Handler&)`, CallbackHandler, Connection 의 `_machine_state`, onWorkReady | 기존 테스트 전부 그대로 통과 (동작 변화 없음). `EchoRoundtrip` 전후 비교 |
| M4 StatefulHandler (끝, 2026-11-09) | 4절, statemachine.cpp, 10절의 unit + integration, 구현 가이드 3절의 echo 예제가 테스트로 | 매트릭스 초록 |
| M5 문서 (끝, 2026-11-10) | deviations 의 DV6 / DV12 닫힘 (DV13 은 유지), requirement 1절의 코드 주석과 8절에 `onError`, `KiottyNetNextStates`, SESSION_EXHAUSTED, release 에서도 하는 ILLEGAL_TRANSITION 검사, 구현 가이드 2절 (11-09 에 먼저 끝), 테스트 계획 16절, progress 문서. user_guide 둘은 M1 이 이름을 바꿨고 나머지는 HTTP / WebSocket 재설계가 다시 쓴다 | 문서만. 코드 변경 없음 |

M1 의 이름 변경은 `tools/rename_public_api.tsv` 와 `wrap_parameters.py` 로 단계 2 때처럼 기계적으로 한다.

## 14. 정할 것

1. (정했다, 2026-11-09) **`KiottyNetStatefulAppContext` 에 `onError` 를 더한다** (2절). NULL 이면 부르지 않는다.
2. (정했다, 2026-11-09) **Session 이 NULL 이면 Session 이 다 닳은 것이고 연결을 닫는다.** `onError(SESSION_EXHAUSTED)` 로 알리고 받은 socket 을 바로 닫는다 (`ignoreConnection` 의 동작). Session 이 없으므로 CONNECTED 도 CLOSED 행도 release 도 없다 (4.1). 라이브러리가 Session 을 갖는 HTTP / WebSocket 은 Session 배열을 Connection 슬롯과 같은 크기, 같은 index 로 두어 이 일이 생기지 않게 하는 것을 다음 설계에서 본다 (11절).
3. (정했다, 2026-11-09) **`ILLEGAL_TRANSITION` 검사는 release 빌드에서도 한다.**
4. (정했다, 2026-11-09) **`KiottyNetAppContext` 는 그대로.** `App` 은 소프트웨어로서의 application 을 뜻하는 한 단어로 보고 약어 금지 (수칙 4.5) 의 예외로 둔다.
5. (정했다, 2026-11-09) **문자열 상수는 수칙 2.6 의 대상이 아니다** (8.2). DV12 의 "TLS 실패 메시지 상수" 를 편차 목록에서 뺀다. 경로와 hostname 은 사용자가 넘긴 값이라 믿지 않고 `parseText` 를 지난다.
6. (정했다, 2026-11-09) **HTTP 의 handle 둘을 `{ server, id }` struct 로** (12.6). 구현 가이드 4.5 의 `ownerOf` 미결이 닫힌다.
7. (정했다, 2026-11-09) **HTTP body 와 WebSocket 메시지는 Session 의 `Stage` 하나를 같이 쓴다** (11절). Stage 가 프로토콜을 모르는 상위 버퍼이고 HTTP / WebSocket 의 함수가 자기 뜻으로 쓴다. Upgrade 경로의 `max_message_bytes` 가 `max_body_bytes` 보다 크면 `addWebSocketUpgrade` 가 INVALID_ARGUMENT.
8. (정했다, 2026-11-09) **HTTP 와 WebSocket 은 한 표, 한 Session** (11절). 사용자는 HTTP route 와 WebSocket 콜백을 넘기고, 라이브러리의 표 하나가 Upgrade 전후를 다 밟는다.
9. (정했다, 2026-11-09) **`KiottyNet_HttpServer` 의 메서드 17 개는 그대로** (12.5). 7 개와의 차이는 DV13 에 둔다.
10. (정했다, 2026-11-09) **공개 헤더는 넷 그대로** (12.3). 나누지 않고 400 줄과의 차이는 DV13 에 둔다. `include/` 는 수칙이 아니라 사용자가 지정한 모양을 따른다 (수칙 0.2).

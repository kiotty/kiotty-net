# 2026-10-26 진행 정리 : 인터페이스 1~7절과 Work 모델

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
- C++ Builder 는 외부 struct 를 참조로 받아 C 함수만 부르고 처음 실패한 코드를 `build()` 로 돌려준다. 서버 / 클라이언트는 `KiottyNet_ServerConfigBuilder::tcp(config, host, port)` / `::pipe(config, name)`.

### 수명

- loop 를 가진 객체는 `run / close / await(timeout_ms)` 셋뿐. `run` 이 열고 loop 를 돈다. `close` 는 기다리지 않는다. `await` 는 loop 가 돌기 시작할 때까지 기다리고, run 이 이미 끝났으면 run 의 반환값. `KIOTTY_CLOSED` 코드 추가.

### Work (suspend / resume 대체)

- `KiottyNet_startWork(handle, payload)` 는 어느 thread 에서나. IO Thread 는 송신 Stage 를 비우고 (`onWritten` 까지), 걸린 read 요청과 남은 deadline 을 기억한 뒤 **다음 loop 에서** `onWorkStarted(ctx, handle, work, payload)`.
- 그 뒤 finish 까지 콜백 없음. IO Thread 는 수신 Stage 채우기와 peer 끊김 기록만 한다. deadline 멈춤. 라이브러리는 Work 에 timeout 을 두지 않는다.
- finish 다섯 가지 : `finishWork` (요청 없이, 이전 read 요청과 deadline 복원), `finishWorkWithRead(exact, timeout_ms)`, `finishWorkWithReadUntil(delim, max, timeout_ms)`, `finishWorkWithWrite(data)` (그 자리에서 송신 Stage 로 복사, 이전 read 복원), `finishWorkWithClose(reason)`. 한 번만. 두 번째는 `KIOTTY_BUSY`.
- `KiottyNet_isWorkConnected(work)` : IO Thread 가 기록한 플래그를 읽을 뿐 socket 을 건드리지 않는다.
- Work 중 peer 가 끊으면 slot 은 유지, finish 뒤 `onDisconnected(KIOTTY_PEER_CLOSED)`. 소유자 `close()` 는 Work 중인 연결에도 바로 `onDisconnected(KIOTTY_CLOSED)`, 뒤늦은 finish 는 `KIOTTY_PEER_CLOSED`.
- C++ `KiottyNet_Work` 는 이동만, 소멸자가 finish 하지 않은 Work 를 `finish()` 로 돌려준다.
- lock-free 구조 (atomic 상태 `RUNNING → DRAINING → WORKING → DONE`, `max_clients` 칸 MPSC done ring, eventfd / PQCS) 는 구현 가이드 1.5.

### broadcast (초안, 인터페이스만)

- `KiottyNet_broadcastServer(server, data)` / `KiottyNet_Server::broadcast`, `KiottyNet_sendEventClient(client, data)` / `KiottyNet_EventClient::send`. 아무 thread 에서나. Server 의 broadcast 버퍼 (send_capacity 크기) 로 복사한 뒤 IO Thread 가 모든 연결의 송신 Stage 로 보낸다. Work 중인 연결에 어떻게 줄지는 미결.

## 열린 질문 (2026-10-26 마지막 대화)

1. **broadcast 와 Work 중인 연결.** 건너뛸지, 연결마다 밀린 broadcast 를 기억할지, Stage 를 SPSC 로 만들어 IO Thread 가 Work 중에도 송신 Stage 에 쓸 수 있게 할지.
2. **`finishWork` (요청 없이 돌려주기) 가 정말 필요한가.** 지금 용도는 C++ 소멸자의 안전망과 push 취소뿐이다. 대안은 소멸자가 `finishWithClose` 를 부르는 것.
3. **연결 확인.** FIN 은 IO Thread 의 recv 가 잡는다. FIN 없이 죽은 peer 는 select 로도 못 잡고 TCP keepalive 나 앱 ping 뿐이다. `configureServerKeepAlive` 같은 설정을 둘지.
4. **WebSocket 의 pong.** Work 중 송신 Stage 는 worker 것이라 IO Thread 전용 control 버퍼로 보내기로 했다. 10절 고칠 때 반영.

## 다음 할 일

1. requirement 8절 StateMachine : `Request::suspend()` + `RESUMED` → `Request::startWork()` + `WORK_STARTED(work)`.
2. 9절 HttpServer : `defer` + `resumed` 콜백 → HTTP 전용 Work (`finishWithBody` 등).
3. 10절 WebSocket : push → `startWork` 또는 broadcast. control frame 은 IO Thread.
4. 11절 HttpClient / WebSocketClient 의 열린 질문 4개.
5. 헤더 `kiotty_net_statemachine.h`, `kiotty_net_http.h`, `kiotty_net_websocket.h` 작성. `src/` 는 아직 비어 있다. Stage 는 내부 구현이라 아직 안 짰다.
6. `implementation_guide_v0.md` 의 2, 3절은 옛 이름 (`NetCode`, `fsm::`, `resume`) 그대로다.

## 검증 상태

- `include/kiotty_net_base.h` 는 MSVC 에서 C++14 `/W4 /WX /EHsc` 와 C11 `/TC /W4 /WX` 로 경고 없이 컴파일된다. MSVC 에 C++11 모드가 없어 C++11 은 미확인. gcc / clang 미확인.
- 검사용 소스는 `include` 만 포함하는 `use.cpp` / `use.c` 였고 저장소에는 넣지 않았다. 구현이 없으므로 링크는 하지 않았다.

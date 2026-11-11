# 2026-11-11 진행 정리 : base 사이클 B2 (주소와 TLS 는 run / connect 때)

기준 문서: `docs/design/design_http_v0.html` 1절 결정 18, 2.3절, 11절의 B2 행. WebSocket 은 `design_websocket_v0.html` 결정 13.

## 왜

DICOM 라이브러리가 자기 Context 를 만들 때 Server / Client 를 전부 만들고, Association (connect, bind, listen) 은 그 자원 위에서 연다. 그런데 접속할 원격 AE 의 주소와 TLS 설정 (사용 여부, trust store, 인증서, 버전, cipher) 은 실행 중에 정해지고 바뀐다. B1 까지는 주소가 설정 struct 의 `address` 에, TLS 설정이 create 의 인자에 묶여 있어서, 목적지를 바꾸려면 endpoint 를 destroy 하고 다시 create 해야 했다.

사용자의 결정 (같은 날의 문답): 주소와 TLS 설정은 run / connect 가 받는다 (서버도). TLS 인자가 NULL 이면 평문. TLS 설정 struct 와 builder 는 지금 것 그대로. endpoint 는 TLS Stage 를 늘 최대치로 잡는다. SSL_CTX 는 run / connect 마다 새로 만든다. 그 전에 나온 두 안 (미리 만드는 TlsProfile 핸들, endpoint 마다의 TLS init / builder 함수) 은 버렸다.

## 바꾼 것

### 공개 API

| 자리 | 전 | 후 |
|---|---|---|
| 설정 struct | `KiottyNetServerConfiguration` / `KiottyNetClientConfiguration` 에 `KiottyNetAddress address` | 없음. 자원만 |
| 설정 initialize | `initializeTcp / PipeServerConfiguration(cfg, host, port / name)`, Client 도 둘 | `initializeServerConfiguration(cfg)`, `initializeClientConfiguration(cfg)` |
| 주소 | 설정의 initialize 가 채움 | `initializeTcpAddress(address, host, port)`, `initializePipeAddress(address, name)`. 검사는 옛 initialize 의 것 그대로 |
| create | `createServer / createStreamClient / createEventClient (context, cfg, tls)` | `(context, cfg)` |
| run / connect | `runServer(server, handler, app)`, `connectStreamClient(client)`, `runEventClient(client, handler, app)` | `runServer(server, address, tls, handler, app)`, `connectStreamClient(client, address, tls)`, `runEventClient(client, address, tls, handler, app)` |
| StateMachine | `runStatefulServer(server, stateful)`, `runStatefulEventClient(client, stateful)` | `(endpoint, address, tls, stateful)` |
| C++ | builder 의 `tcp(...) / pipe(...)`, wrapper 의 TLS 생성자 | builder 는 `explicit` 생성자 하나. `Server::run(address, [tls,] handler, app)`, `StreamClient::connect(address [, tls])`, `EventClient::run(address, [tls,] handler, app)`. TLS 생성자 없음 |
| http.h / websocket.h (구현 전) | create 가 tls | create 는 tls 없음, run 이 `(address, tls)`. 선언과 wrapper 만. H1 / W1 이 이 위에서 판 v2 로 간다 |

### 안

| 자리 | 내용 |
|---|---|
| `kiotty_configuration` | `initializeTcpAddress / initializePipeAddress`, `isAddressValid` (transport, NUL 로 끝나는 비지 않은 host, TCP 의 port), `hostnameOf(address)`. 인자 없는 설정 initialize |
| Owner 셋 | `configuration_type` 이 공개 설정 struct. `ServerConfiguration` / `ClientConfiguration` 묶음과 `kiotty_client_configuration.h` 를 지웠다. initialize 는 TLS 프로파일을 열지 않는다 |
| TLS Stage | `MemoryLimits::TLS` 이면 언제나 잡는다. Server / EventClient 의 `cipherOf(limits)`, StreamClient 의 `reserveTlsStages`. 공식은 그대로 |
| run | handler 검사 → 주소 검사 → TLS 설정 mark → `RunGate::enter` → `openProfile(tls)` (먼저 `close`, tls 가 있으면 새 SSL_CTX) → 서버 `listen(address)` / EventClient `connectWithRetry(cfg, address, closing)`. 실패는 `leave(code)` |
| connect | `disconnect` → 주소 / mark 검사 → `openProfile` → `connectWithRetry` → `secure(address)` |
| SNI, hostname 검증 | run / connect 의 주소의 host |
| 번역 층 | 주소를 run / connect 의 stack 에 복사해서 넘긴다. `serverMemorySize / clientMemorySize` 가 TLS 빌드에서 cipher Stage 를 더한다 |

### 문서

| 문서 | 내용 |
|---|---|
| `design_http_v0.html` | 1절 결정 18, 2.3절, 0 / 4.9 / 5 / 6 / 9 / 11절 (B2 행) (commit `8246ac1`) |
| `design_websocket_v0.html` | 결정 13, 결정 6 의 SNI 출처, 3.3, 3.4, 4, 5, 8, W4 (같은 commit) |
| `requirement_v0.md` | 3절의 initialize 규칙, 5 / 6번 (주소, TLS 는 run / connect), 예제, StreamClient / EventClient / Server 의 선언, 수명 표의 run, 다시 run 할 때의 await, runStateful 둘 |
| `implementation_guide_v0.md` | 1.7 (새), 2.1 의 runStatefulServer, 3절 예제 넷, 4.1 / 4.8 의 HttpServer |
| `user_guide_http.md`, `user_guide_websocket.md` | 예제와 1.1 의 설정 설명 |
| `test_base_v0.html` | 18절 (새). 목차에 17, 18 |
| `.claude/agents/cpp-developer.md` | "TLS 는 create 에 따로" → "주소와 TLS 는 run / connect 에" |

## 테스트

MSVC 기준 501 → 511. 새 13 개, 옮긴 것 3 개 (설정 initialize 의 주소 검사 → `Address.*`).

- `Address` 다섯: TCP / Pipe 주소의 값과 hostname, 빈 / 긴 / port 0 / UTF-8 아닌 이름, `isAddressValid` (모르는 transport, port 0, 빈 host, NUL 없는 host).
- `Configuration.InitializeWithNullIsInvalidArgument`.
- `Destination` (새 파일 `tests/integration/kiotty_test_destination.cpp`, 공개 C API): StreamClient 하나가 평문 → TLS → 평문 서버로, 같은 클라이언트가 trust store `ca.pem` 성공 → `other_ca.pem` 으로 `TLS_VERIFY_FAILED` → `ca.pem` 성공, 서버 하나가 평문으로 run 한 뒤 close 하고 다른 port 와 TLS 로 다시 run, EventClient 하나가 평문 뒤 TLS, 잘못된 주소 / mark 없는 TLS 설정이 소켓을 열기 전에 `INVALID_ARGUMENT`, TLS 없는 빌드의 `TLS_NOT_SUPPORTED`, memory size 의 cipher Stage.
- 고친 것: 설정과 주소를 쓰던 integration 테스트 다섯 (BaseApi, Context, Endpoint, Stateful, ConnectWithRetry) 과 unit 의 Configuration, benchmark. Context 의 "mark 없는 TLS 설정을 create 가 거부" 는 run / connect 의 테스트로 옮겼다.

테스트를 쓰다 본 것: 서버를 close 한 뒤 다시 run 하면, 새 run 이 `RunGate::enter` 에 들어가기 전의 짧은 동안 `await` 가 지난 run 의 `CLOSED` 를 돌려준다. B2 전부터 있던 성질이고 Endpoint 테스트는 이미 `await(1) == SUCCESS` 를 기다린다. 새 테스트도 그렇게 했고 requirement 7절에 한 줄 적었다.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF (IOCP) | 511 / 511 (Destination 의 TLS 넷은 SKIP) |
| WSL gcc Debug, TLS OFF / ON | 512 / 512, 512 / 512 |
| WSL clang Debug, TLS OFF / ON | 512 / 512, 512 / 512 |
| WSL gcc Release TLS ON | 511 / 511 |
| WSL asan,undefined TLS ON | 512 / 512 |
| WSL thread TLS ON | 512 / 512 |
| benchmark 빌드 (MSVC Release, WSL Release) | 컴파일만. 돌리지 않았다 |

asan,undefined 의 `runtime error` 다섯 줄은 B1 때와 같은 googletest 내부 (vptr) 다. `tools/check_rules.py` 는 B1 과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 메모리

TLS 를 넣어 빌드하면 TLS 를 쓰지 않는 endpoint 도 cipher Stage 를 잡는다 (결정 18). 기본 버퍼 (4 KiB 셋) 에서 연결 하나에 receive 4096 + cipher send `alignStage(cipherSendBytes(4096 + 4096 + 64))` = 8832, 합 12928 바이트가 는다. 기본 `max_clients` 1024 인 서버는 약 12.6 MiB 다. 평문만 쓰는 서버는 `max_clients` 를 줄이거나 TLS 를 빼고 빌드하면 된다.

## 하지 않은 것

- benchmark 를 돌리지 않았다. 송수신 경로는 바뀌지 않았고 바뀐 것은 run / connect 의 앞부분 (프로파일, 주소) 뿐이다. `TlsHandshakeRate` 는 이제 connect 마다 SSL_CTX 를 만들므로 전보다 느릴 것이다 (결정 18 이 받아들인 값).
- HTTP / WebSocket 은 헤더 선언과 wrapper 만. 구현은 H1 ~ H6, W1 ~ W5.
- `requirement_v0.md` 11절의 HttpClient 초안 (`address.host 가 Host 헤더`) 은 그대로 두었다 (http 설계 결정 10 으로 다음 판).

## 다음

H1 (`design_http_v0.html` 11절). 판 v2 의 http.h 는 이 판의 `createHttpServer(context, cfg, limits)` / `runHttpServer(server, address, tls, handler, app)` 위에 선다.

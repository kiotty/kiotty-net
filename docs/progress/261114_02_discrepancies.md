# 2026-11-14 진행 정리 : W5 / H6 에 적어 둔 어긋난 곳 정리, WebSocketClient 외부 서버 확인

## 외부 확인 (반대 방향)

`build/interop/` (gitignore) 의 `interop_client.cpp` (공개 C API 의 WebSocketClient) 를 Python `websockets` 17.4 서버 (`ws_server.py`, ws + wss, keepalive ping 0.2 s / pong 0.5 s) 에 붙였다. 21 / 21.

- UTF-8 텍스트, mask 한 65528 바이트 바이너리 echo, 서버가 조각 셋으로 보낸 메시지 합치기, 서버의 60000 바이트 메시지, 서버 ping 에 pong (1.2 s 동안), 우리 close 1000 → `onClose(SUCCESS, 1000)` + run `PEER_CLOSED`, 서버 close 4001 "bye", 70000 바이트 → 우리가 1009 + `REQUEST_TOO_LARGE`, 403 → run `PROTOCOL_ERROR`. ws 와 wss 둘 다.
- 기록만 하는 것: 1009 처럼 받은 frame 때문에 닫을 때 우리는 close frame 을 보낸 뒤 상대의 답을 기다리지 않고 TCP 를 닫는다 (구현 가이드 5.3). Python 은 답을 보내다 broken pipe 를 만났다. RFC 6455 7.1.7 (Fail the WebSocket Connection) 은 허용한다. 상대 close 에 대한 답 뒤에 클라이언트가 TCP 를 먼저 닫는 것도 같다 (7.1.1 은 클라이언트가 서버의 close 를 기다리기를 권한다, SHOULD). 지금은 고치지 않는다.

## 고친 것 (코드)

| # | 어긋난 곳 | 고친 것 |
|---|---|---|
| H6-1 | `HttpSessionPool::find` 가 generation 을 lock 없이 읽고, IO thread (`beginExchange`) 나 worker 의 finish 가 쓴다. 다른 thread 가 늦게 옛 handle 로 setter 를 부르는 것은 허용된 사용 (`PEER_CLOSED`) 이라 규칙 위반만의 일이 아니었다 | Session 마다 `std::atomic<int64_t> published` (풀의 `_published` 배열, Session 은 참조. `HttpSession` 은 `std::vector` 에 있어 atomic 을 직접 들 수 없다). `find` 는 그것과 비교한다. generation 을 바꾸는 넷 (`beginExchange`, `release`, `acceptUpgrade`, 클라이언트 `onConnected`) 은 `advanceSession` 하나로 |
| H6-6 | `response_bytes` 의 하한이 configure 는 256 + 64, `HttpServer::prepare` 는 256 + 2 | `MIN_HTTP_RESPONSE_BYTES` (`kiotty_http_configuration.h`) 하나를 둘이 쓴다. 테스트: struct 에 직접 쓴 하한 - 1 은 create 가 `INVALID_ARGUMENT` |

## 고친 것 (문서)

| # | 어긋난 곳 | 정한 것 |
|---|---|---|
| H6-2 | 두 번째 finish 는 다음 요청 전 `BUSY`, 뒤 `PEER_CLOSED` 인데 문서는 `BUSY` 만 | 둘 다 적었다 (user guide http 1.10) |
| H6-3 | 서버 close 의 끊김 action (`CLOSED`) 이 worker 의 finish 보다 먼저 온다 | 그 순서를 적었다. `closeHttpServer` 표의 "Work 중인 연결도 바로 닫힌다" 를 "FIN, finish 뒤에 닫힌다" 로 (같은 문서 1.5 와 맞춤) |
| H6-4 | `writeHttpWorkChunk` 는 남은 실패보다 인자 검사가 먼저 | 순서를 적었다. 인자 오류를 먼저 알리는 것이 맞다 |
| H6-5 | `setHttpFile` 은 파일을 type 보다 먼저 연다 | 순서를 적었다. 고치려면 response store 의 private `checkBodyType` 을 public 으로 해야 해서 (메서드 7 개 규칙) 두었다. 둘 다 틀렸을 때 어느 코드가 먼저인지의 차이뿐이다 |
| H6-7 | user guide 1.10 이 `setHttpNotFound` 의 `TABLE_FULL` 을 적는다 | 지웠다 (표를 쓰지 않는다) |
| H6-8 | 설계 http 7절이 `setHttpBodyStream` 은 worker 만 의미가 있다고 적는다 | "onRequest (startHttpWork 와 함께) 또는 worker" 로. 4절 헤더 초안의 주석도 |
| W5-2 | "메시지는 body 의 Stage 에 모인다" | user guide websocket 과 설계 websocket 의 문장을 "HttpSession 의 body 버퍼" 로 (Stage 에서 복사하며 unmask) |

## 두는 것

- W5-1: 클라이언트의 handler struct 는 run 이 return 할 때까지 살아 있어야 한다는 규칙. 구현은 run 이 값으로 복사하지만 복사는 run 을 부른 thread 안에서 일어나고 다른 thread 의 호출자는 그 시점을 알 수 없으므로, 규칙을 그대로 두는 것이 맞다.
- W5-3: `design_statemachine_v0.md` 502 줄의 "구현 가이드 5절의 미결" 은 역사 기록이다.

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 663 / 663 |
| WSL gcc / clang Debug, TLS OFF / ON | 664 / 664 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 664 / 664, 664 / 664 |
| WSL clang Release TLS ON | 663 / 663 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

# 2026-11-14 진행 정리 : push_capacity 상한 16 KiB → 64 KiB

## 왜

외부 클라이언트 검사 (Python `websockets` 17.4, `build/interop/`) 에서 서버가 16 KiB 보다 큰 WebSocket 메시지를 보낼 수 없었다. WebSocket 의 `send` / `broadcast` 는 frame 하나를 push lane 에 전부 아니면 전무로 복사하므로 `push_capacity` 가 송신 메시지의 상한이고, 그 상한이 `MemoryLimits::PUSH_CAPACITY` = 16384 였다. 수신 쪽은 `receive_capacity` 조각으로 읽어 body 버퍼 (`max_body_bytes`, 기본 64 KiB) 에 모으므로 묶이지 않는다.

## 정한 것

- **push 상한만 64 KiB.** `MemoryLimits::PUSH_CAPACITY` 65536. 송신 (`SEND_CAPACITY`) 은 16 KiB 로 둔다. HTTP 완성 응답, 파일, Work 스트림은 `send_capacity` 조각으로 나가고 WebSocket 은 write lane 을 핸드셰이크와 close frame 에만 쓰므로 키울 이유가 없다 (사용자 결정).
- 기본값 (4 KiB) 은 그대로다. 메모리는 상한까지 설정한 연결에서만 는다.
- TLS 는 바꿀 것이 없었다. 암호문 송신 버퍼는 이미 `평문 / 16384 + 1` 레코드로 잡고 (`cipherSendBytes`), `SSL_write_ex` 가 레코드로 나눈다.
- **너무 큰 메시지는 `INVALID_ARGUMENT`.** 문서는 그렇게 적었지만 서버는 base desk 의 `BUFFER_FULL` 을 돌려주고 (클라이언트는 scratch 검사로 `INVALID_ARGUMENT`), `broadcast` 는 소켓마다 실패한 뒤 `SUCCESS` 를 돌려주었다. `WebSocketAccess::checkData` 가 `HttpSite::push_capacity` 와 frame 머리 (클라이언트는 + mask key) 로 먼저 거른다. `HttpSite` 에 `push_capacity` 를 더했다.

## 테스트

- `WebSocketServerTest.PushCapacityOfSixtyFourKibibytesSendsTheLargestMessage`: 상한 + 1 거절, 65532 바이트 echo, + 1 은 send / broadcast 모두 `INVALID_ARGUMENT`, 그 뒤 echo.
- `WebSocketClientTest.PushCapacityOfSixtyFourKibibytesCarriesTheLargestMaskedMessage`: 65528 바이트 왕복 (ws, TLS 빌드면 wss), + 1 은 `INVALID_ARGUMENT`.
- unit 의 설정 검사는 상수를 참조하므로 그대로 새 상한을 본다.

## 문서

requirement 3절 (상한), 설정 struct 주석, `configureServerBufferCapacity` 주석, 10절 (송신 메시지 상한과 머리 크기, `send` 코드 표), user guide WebSocket 3 / 5 / 10절, 구현 가이드 5.5, 테스트 계획 20.2 / 20.3.

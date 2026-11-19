# 2026-11-19 진행 정리 : 상대에 따른 설정 사이클 D1

기준 문서: `docs/design/design_peer_settings_and_cancel_v0.html` 2절, 7절의 D1 행. 배경은 DICOM 라이브러리에 적용하면서 들어온 변경 요청 (연결 시도와 송신의 설정은 Endpoint 가 아니라 상대에 따라 정해진다).

## 정한 것 (2026-11-19, 사용자)

- 다섯 설정 (`connect_timeout_milliseconds`, `max_trial`, `trial_interval_milliseconds`, `no_delay`, `send_timeout_milliseconds`) 은 `KiottyNetAddress` 로 옮긴다. create 의 설정은 메모리 크기만. 세 번째 설정 구조체는 만들지 않는다.
- address 에 `initialization_mark`. `initialize*Address` 는 host / port (파이프는 이름) 를 반드시 받고 다섯 값은 기본값으로 채운다. 바꾸려면 `KiottyNet_setAddress*`.
- 서버 run 은 address 의 `no_delay` 와 `send_timeout_milliseconds` 만 읽고 connect 셋은 읽지 않는다 (거절하지 않음).
- 이름: `KiottyNet_setAddressConnectTimeout` / `setAddressRetry` / `setAddressNoDelay` / `setAddressSendTimeout`, C++ `KiottyNet_AddressBuilder`.

## 공개 API

- `KiottyNetAddress` 에 `initialization_mark` 와 다섯 필드. `KiottyNetServerConfiguration` 은 `max_clients` 와 세 capacity, `KiottyNetClientConfiguration` 은 세 capacity 만.
- 없앤 함수: `KiottyNet_configureServerNoDelay`, `configureServerSendTimeout`, `configureClientConnectTimeout`, `configureClientRetry`, `configureClientNoDelay`, `configureClientSendTimeout`. C++ builder 의 같은 메서드도 없앴다. 부르던 코드는 컴파일 오류로 드러난다.
- 새 함수: `KiottyNet_setAddress*` 넷. `initialize*Address` 를 거치지 않은 address 면 `INVALID_ARGUMENT`.
- 새 C++ `KiottyNet_AddressBuilder(address, host, port)`, `KiottyNet_AddressBuilder::pipe(address, name)`, `connectTimeout` / `retry` / `noDelay` / `sendTimeout` / `build` (처음 실패한 코드).
- connect / run (Server, EventClient, StreamClient, 그 위의 HTTP / WebSocket / statemachine) 은 `initialize*Address` 를 거치지 않은 address 를 `INVALID_ARGUMENT` 로 거절한다 (`isAddressValid` 가 표시를 본다).

## 안에서

- `connectWithRetry(address, stop[, deadline])`: 설정 인자를 없애고 시도 횟수, 간격, 시도 시간, `no_delay` 를 address 에서 읽는다. StreamClient 의 TLS handshake 시간도 address 의 연결 시간.
- 서버의 `no_delay` 는 `Server::listen` 이 address 에서 읽는다.
- 송신 시간: run 마다 만드는 `LoopParts` 에 `send_timeout_milliseconds` 를 더했다. IoLoop 가 연결을 받을 때 `ConnectionLimits` 의 송신 시간으로 넣고, `ConnectionHost::acquire` 는 메모리 셋만 create 때의 값으로 채운다. EventClient 도 같다.
- Server / EventClient 의 `_configuration` 멤버는 읽는 곳이 없어 뺐다.

## 설계와 다른 것 (확인 부탁)

1. 설계 2.4 는 송신 시간을 `ConnectionConfiguration` 의 새 칸으로, `ConnectionLimits` 를 메모리 셋만으로 적었다. 구현은 `ConnectionLimits` 를 그대로 두고 값을 `LoopParts` 로 넘긴다. 처음에는 `ConnectionHost::setSendTimeout` 을 두었는데 ConnectionHost 의 공개 메서드가 9 개가 되어 (수칙 PUBLIC) run 마다 만드는 `LoopParts` 로 옮겼다. 설계 문서에 메모를 붙였다.
2. 설계 2.3 은 직접 채우는 테스트 다섯 파일을 고친다고 적었다. 공개 connect / run 을 거치는 것과 `connect_with_retry` 만 고쳤다. `kiotty_test_acceptor` 와 `kiotty_test_connection_stream` (포트 0 으로 듣기, `initializeTcpAddress` 는 포트 0 을 거절), `active_socket`, `event_listener`, `io_loop` 은 표시를 보지 않는 내부 함수만 써서 그대로 두었다.

## 테스트

- unit `kiotty_test_configuration.cpp`: 클라이언트 / 서버 기본값은 capacity 만, 새로 address 의 기본값 (TCP, 파이프), 직접 채운 address 는 유효하지 않고 setter 도 거절 (`NULL` 포함), address 의 송신 / 연결 시간, `AddressBuilder` (모든 값, 파이프, 첫 오류에서 멈춤), 모르는 `no_delay`, 시도 0 거절. 21 → 23.
- integration `kiotty_test_destination.cpp`: 표시가 없는 address 를 runServer / connectStreamClient / runEventClient 가 거절.
- 옮긴 것: `base_api` 의 송신 시간 (서버 address), `http_client_close` / `http_client_retry` 의 재시도와 연결 시간 (address), `websocket_client` 의 재시도 (fixture 의 값을 address 에), `connect_with_retry` (address 에 시도 횟수 / 간격).
- unit 의 `ConnectionBench` 는 acquire 때 자기 송신 시간을 넘긴다 (실제 IoLoop 와 같은 길).

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC (`build/msvc`, TLS OFF) | 879 | 881 |
| WSL gcc / clang × TLS ON / OFF, asan-tls, tsan-tls | 880 | 882 |
| WSL release-tls | 879 | 881 |

수칙 검사 기준선 그대로 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 문서

- `requirement_v0.md` 7절: address 와 설정 구조체, setter, C++ builder, 예제.
- `user_guide_http.md`, `user_guide_websocket.md`, `implementation_guide_v0.md`: 예제의 재시도 / 연결 시간을 address 로.

## 남은 것

D2 (`KiottyNet_cancelStreamClient`). 단일 헤더는 commit 뒤 다시 만든다.

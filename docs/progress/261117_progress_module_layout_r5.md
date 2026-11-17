# 2026-11-17 진행 정리 : 모듈 정리 R5 client State

기준 문서: `docs/design/design_module_layout_v0.html` 7절 (HttpClient 의 State), 6절 (클라이언트: 요청 하나). 계획: `docs/plan/plan_base_261116.md`. 동작 바뀜 없음.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `HttpClient` 가 store 둘, body, upload, 주소, host, 플래그들을 멤버로 가졌고 `HttpClientExchange` 가 friend 로 그것을 만졌다 | `http/data/state/kiotty_http_client_state.h` 의 `HttpClientState` (shared, reply, body, upload, target, head_area, max_headers, persistence, busy). `HttpClient` 는 메모리 (vector 넷) 와 State 만. friend 0 |
| `HttpClientExchange(HttpClient&)` | `HttpClientExchange(HttpClientState&, StreamClient&)`. StreamClient 의 공개 메서드만 쓴다. `HttpClientHeaders(state)`, `HttpClientLink(state, line)` 도 같다 |
| `StreamClient` 가 HttpClient 용 protected (prepare, reconnect, isConnected, readLine, peekReceived, consumeReceived, stagedBytes, stopSignal) 와 공개 read / write 를 가졌다 | 공개 7: initialize, reset, connect, `reconnect(address, deadline)`, disconnect, `stream()`, `stop()`. `stream()` 은 `StreamResult` (socket 이 없으면 `CLOSED`). protected 없음. C ABI 의 read / write 는 `stream().read` / `write` |
| `ConnectionStream::readLine`, `fill` | `peek(wanted, timeout)` (Stage 에 wanted 바이트가 있을 때까지 받는다. `peek(0, 0)` 은 있는 것만) 과 `consume(length)`. `readLine(stream, timeout)` 은 자유 함수 (peek / consume 위). 공개 메서드 7 그대로 |
| HttpClient 의 private 보내기 / 받기 메서드 열넷 | `client/kiotty_http_client_send` (`writeHead` → 머리 span, `ensureConnected` → `Connected`, `sendBytes`, `writeAll`, `settleStop`, `timeLeftOf`), `client/kiotty_http_client_reply` (`receiveReply` → `ReplyHead`, `pumpBody`, `replyOf`, `dropUnreadReply`). 값 둘은 entity `kiotty_http_client_step.h` |
| `_has_reply_bytes` 플래그, transmit 안의 숨은 다시 연결 | `ReplyHead::saw_bytes` (줄을 하나라도 받았거나 실패한 때 Stage 에 바이트가 있으면), `Connected { code, is_reused }` |

- 설계 7절과 다른 것 셋. (1) 이름은 `HttpClientStream` 대신 지금의 `HttpClientExchange` (`StreamClient::stream()` 과 헷갈린다). (2) `reconnect(deadline)` 대신 `reconnect(address, deadline)`. 주소는 `HttpClientTarget` 에 하나만 두고 StreamClient 는 저장하지 않는다. (3) State 에 `max_headers` 가 있다.
- "이쪽이 열려 있나" 는 `stream()` 의 성공 여부다. `ConnectionStream::connected()` 는 peer 가 닫았는지 probe 하므로 `is_reused` 와 `settleUpload` 에 쓰면 POST 의 재시도 규칙 (test `PostOrPatchOnAnIdleConnectionTheServerClosedIsPeerClosedAndNotSent`) 이 어긋난다.
- `connect` 가 `INVALID_ARGUMENT` (주소 / TLS 설정 검사) 면 target 을 두지 않는다. 그 밖의 실패 (거절, timeout) 는 target 을 두어 다음 send 가 다시 연결한다 (test `refused_connect` 그대로).
- `peek` 은 Stage 에 이미 있으면 flush 없이 돌려준다 (전의 `peekReceived` 와 같다). `readLine` 이 처음에 하던 flush 도 받을 때만 한다.
- 테스트: ConnectionStream 테스트의 `readLine` / `fill` 호출을 자유 함수 / `peek` 로 (`FillAppendsToTheStagedBytes` → `PeekReceivesUntilTheWantedBytesAreStaged`), endpoint 테스트의 `client.read` / `write` 를 `stream()` 위의 helper 로.
- 구현 가이드 6절 (표, 6.1 ~ 6.6 의 조각), 설계 문서의 R5 note.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로). friend: http 0 (남은 셋은 base 의 PassiveSocket / TlsPipe / WorkReporter).

## 다음

R6 usecases: 모든 모듈의 C ABI 를 usecases 함수로, `src/kiotty_net_*.cpp` 는 인자 변환만.

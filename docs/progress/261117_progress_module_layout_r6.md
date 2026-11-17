# 2026-11-17 진행 정리 : 모듈 정리 R6 usecases

기준 문서: `docs/design/design_module_layout_v0.html` 1절 (층의 역할), 5절 (usecases 파일). 계획: `docs/plan/plan_base_261116.md`. 동작 바뀜 없음 (공개 API, 오류 code, 테스트 그대로).

## 어댑터와 usecases 의 경계

| 어댑터 (`src/kiotty_net_*.cpp`) | usecases (`<module>/domain/usecases`) |
|---|---|
| handle / 포인터의 null 검사 | handle 안으로 두 걸음 이상 들어가는 일: registry / notices 를 꺼내 등록, `HttpWork(host)`, `exchange()`, `headers()`, `link()`, `stream()`, `registration()` 과 `handlerOf` |
| C 문자열을 `parseText` 로 Span 에 | 규칙 검사 (`isClientMethod`, `isUpgradeTarget`, `isProtocolNameList`, `hasParentSegment`) |
| C 구조체를 C++ 값으로 (`CallbackHandler`, `ClientRequest`, `ClientRun`), 주소 복사, 결과를 C 구조체로 | 크기 계산 (`serverMemorySize`, `clientMemorySize`) |
| handle 메서드 하나나 usecase 함수 하나 부르기 | |

- 오류 code 의 순서가 걸린 문자열은 usecase 가 C 문자열로 받는다: 응답 헤더 이름 / 값 (Session 을 찾은 뒤 `response_bytes` 상한), 파일 경로 (Session 을 찾은 뒤), close reason (owner 를 본 뒤), protocol 목록.
- statemachine 은 R2 에서 이미 이 모양이다 (`runStatefulServer`, `runStatefulEventClient`).

## 바뀐 것

| 파일 | 함수 |
|---|---|
| `base/domain/usecases/kiotty_server_usecases` | `serverMemorySize` |
| `base/domain/usecases/kiotty_client_usecases` | `clientMemorySize`, `readStreamClient`, `writeStreamClient`, `notifyEventClient` |
| `http/domain/usecases/kiotty_http_server_usecases` | `findHttpHeader` (`findReplyHeader` 를 다시 씀), 응답 setter 일곱, Work 다섯, `addHttpRoute`, `setHttpNotFound`, `onHttpDisconnected`, `onHttpError`, `serveHttpDirectory` / `File` / `Memory` |
| `http/domain/usecases/kiotty_http_client_usecases` | `clientRequestOf`, `setHttpClientHeader`, `clearHttpClientHeaders`, `disconnectHttpClient`, `closeHttpClient`, `sendHttpRequest`, `readHttpReplyBody`, `beginHttpRequest`, `writeHttpRequestBody`, `finishHttpRequest` |
| `websocket/domain/usecases/kiotty_websocket_server_usecases` | `sendWebSocket`, `closeWebSocket`, `addWebSocket`, `onWebSocketUpgrade` / `Open` / `Message` / `Close`, `broadcastWebSocket` |
| `websocket/domain/usecases/kiotty_websocket_client_usecases` | `setWebSocketClientProtocols`, `onWebSocketClientOpen` / `Message` / `Close` / `Error`, `runWebSocketClient`, `byeWebSocketClient`, `sendWebSocketClient` |

- 메모리 크기: `stageBytesOf(receive, send, push)` 를 `base/data/datasource/kiotty_memory_layout.h` 로 (어댑터의 `stagesOf` / `cipherStagesOf`). 크기는 base 타입 (`Server`, `EventClient`, `StreamClient`) 으로 세고, `context/kiotty_handles.h` 에 handle 이 base 타입과 크기가 같다는 `static_assert` 셋.
- 어댑터가 다른 C ABI 함수를 부르던 것 (`byeWebSocketClient` → `KiottyNet_closeWebSocket`, `sendWebSocketClient` → `KiottyNet_sendWebSocket`) 은 usecase 끼리.
- 그대로 둔 것: 설정 / limits 함수 (repository 함수 하나), `then*`, `make*`, 메서드별 HTTP 함수 (요청을 만들어 `sendHttpRequest`), run (`CallbackHandler` 로 바꾼 뒤 `run` 하나), create / destroy, close / await, create 의 limits 기본값 고르기.
- 수칙 7.1 에 어댑터가 하는 것을 적었다. 구현 가이드의 "번역 층이 …" 다섯 곳.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로).

## 다음

R7 http ↔ websocket 경계 (설계 먼저).

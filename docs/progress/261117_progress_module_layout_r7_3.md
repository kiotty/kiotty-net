# 2026-11-17 진행 정리 : 모듈 정리 R7-3 클라이언트 자기 state

기준 문서: `docs/design/design_http_websocket_boundary_v0.html` 3절. 계획: `docs/plan/plan_base_261116.md`. 동작 바뀜 없음.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `WebSocketClient` 가 `HttpSessionHost` 하나 (session 풀 1 칸, 라우트 표 1 칸, notices) 를 가졌다 | `WebSocketClientState` (`websocket/data/state/kiotty_websocket_client_state.h`) 와 그 메모리 (응답 머리 버퍼, 헤더 칸, message 버퍼) 만 |
| 101 응답 헤더를 http 의 **요청** 저장소에 `takeHeader` 로 담았다 | `HttpReplyStore::takeHeader` (HttpClient 와 같은 저장소). 상태줄은 저장소에 넣지 않고 지금처럼 `parseStatusLine` 으로만 본다 (헤더 버퍼의 상한이 전과 같다) |
| http 의 `readLine`, `startHeaderClock` / `headerTimeLeft`, `advanceSession`, `sessionOf` | 클라이언트 기계의 `readReplyLine` (같은 상한 `min(receive_capacity, max_header_bytes + 2)`), `header_deadline` 과 `replyTimeLeft`, `nextClientGeneration`, `clientStateOf` |
| `createSession` 이 풀에서 session 을 받았다 | 연결 context 가 `&state` (연결은 하나) |
| onError 가 `HttpNotices` 에 | `state.on_error` |
| `registration()` 이 `HttpSessionHost&` | `WebSocketClientState&`. usecase 가 `state.handler->client.events` 와 `state.on_error` 를 채운다 |
| `isUpgradeResponse(const KiottyNetHttpRequest&, ...)` | `isUpgradeResponse(Span<const KiottyNetHttpHeader>, ...)`, `headerOf` 에 헤더 목록판 |
| `WebSocketHandler` 의 `handlerOf(host / session)`, `socketOf(session)`, `linkOf(session)` | 지움. handler 헤더가 http 의 host 를 include 하지 않는다 |
| `WebSocketAccess(const HttpRegistry&, ...)` | `const HttpRegistry*`. 클라이언트는 NULL (path 를 준 broadcast 는 서버만) |

- 상태 이름은 `HttpState` 를 그대로 쓴다. 서버와 함께 쓰는 frame 단계가 `UPGRADED` / `CLOSING` 으로 옮기기 때문이다 (설계 3절의 자기 상태 이름은 하지 않았다).
- 메모리: 전의 1 칸 풀이 잡던 응답 버퍼 (`response_bytes`) 가 빠졌다.
- websocket 에 남은 http host 이름은 `WebSocketServer : HttpServer` (R3) 가 access 와 broadcast 에 `host().registry` / `host().push_capacity` 를 넘기는 곳뿐이다. 그 밖의 websocket 파일은 http 의 session / host / 풀을 include 하지 않는다.
- 구현 가이드 5.8, 5.9. 설계 문서의 R7-3 note.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로).

## 다음

R7-4 `WebSocketHandler` 의 공개 필드 (owner, lane, client, role, mask_state) 정리.

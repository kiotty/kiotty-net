# 2026-11-17 진행 정리 : 모듈 정리 R7-2 access

기준 문서: `docs/design/design_http_websocket_boundary_v0.html` 2절 (다른 thread 의 access 는 websocket 의 `SocketLink` 를 본다). 계획: `docs/plan/plan_base_261116.md`.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `WebSocketAccess(HttpSessionHost&)` 가 `host.sessions.find(id)`, `session.info`, `session.state.phase`, `host.registry`, `host.push_capacity` 를 봤다 | `WebSocketAccess(const HttpRegistry&, WebSocketHandler&, size_t push_capacity)`. session 풀을 보지 않는다 |
| info: `sessions.find(id)` 뒤 `socket.is_open`, 연결 정보는 `session.info` | `openLink(id)` (`SocketLink::socket == id`) 뒤 `socket.is_open`, 연결 정보는 open 때 복사한 `SocketState::connection`. `UpgradeSession` 에 `info` (연결 정보 참조) 를 더했다 |
| header / select: `sessions.find(id)` 뒤 `phase == UPGRADING` 이 아니면 `BUSY` | answer 가 on_upgrade 를 부르는 동안만 `SocketState::upgrading` 이 지금의 `UpgradeSession` 을 가리킨다. id 가 같으면 그것, 열린 socket 의 id 면 `BUSY`, 아니면 `PEER_CLOSED` |

- **달라진 것 하나.** on_close 안이나 닫힌 뒤 같은 handle 로 `selectWebSocketProtocol` 을 부르면 전에는 `BUSY` (session 이 아직 그 id 를 가짐), 이제는 `PEER_CLOSED` 다. 둘 다 실패이고 header 는 둘 다 빈 span 이다. 테스트는 이 경우를 보지 않는다 (on_upgrade 안 성공, on_open 안 `BUSY` 는 그대로).
- 서버와 클라이언트는 cpp 의 helper (`serverAccessOf`, `clientAccessOf`) 로 access 를 만든다. usecase 의 broadcast 도 같은 세 인자.
- `kiotty_websocket_access.h` 의 http include 는 `server/kiotty_http_registry.h` 하나.
- 구현 가이드 5절의 access 설명과 파일 표, 설계 문서의 R7-2 note.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로).

## 다음

R7-3 클라이언트 자기 state: `WebSocketClientState`, websocket 의 상태 기계, 101 응답은 `HttpReplyStore`, `HttpSessionHost` 를 가지지 않음.

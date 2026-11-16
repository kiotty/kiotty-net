# 2026-11-16 진행 정리 : 모듈 정리 R0, R1

기준 문서: `docs/design/design_module_layout_v0.html` 2절, 9절. 계획: `docs/plan/plan_base_261116.md`.

## R0 파일 정리 (`2b9aa04`, 단일 헤더 `d74963e`)

- `src/implementation` 의 242 개 파일을 `git mv` 로 `base / statemachine / http / websocket` 의 `domain/{entity,usecases}`, `data/{repository,datasource}` 로 옮겼다. `core/` 는 그대로, `context/` 에는 Context 와 handles 만.
- include 경로, CMake 의 소스 목록, 수칙 7.1 의 트리, 구현 가이드 / deviations / 테스트 계획의 경로를 새 자리로. progress / design 문서는 기록이라 그대로.
- 동작 바뀜 없음. MSVC 847, WSL 848 × 6, release-tls 847.

## R1 작은 방향

| 것 | 고침 |
|---|---|
| base → websocket | `~KiottyNetWebSocketEndpoint()` 를 `base/.../kiotty_endpoint.cpp` 에서 새 파일 `websocket/data/repository/kiotty_websocket_endpoint.cpp` 로 |
| base → statemachine 공개 헤더 | `KiottyNetConnectionEventType`, `KiottyNetConnectionEvent` 의 정의를 `kiotty_net_statemachine.h` 에서 `kiotty_net_base.h` (`KiottyNetConnectionStream` 뒤) 로. statemachine 헤더는 base 를 include 하므로 앱 코드는 그대로. base 의 세 헤더 (`kiotty_handler.h`, `kiotty_connection_event.h`, `kiotty_callback_handler.h`) 가 statemachine 헤더를 include 하지 않는다 |
| http → context | `HttpServer::prepare` 가 `static_cast<KiottyNetHttpServer*>(this)` 로 공개 handle 을 만들려고 `context/kiotty_handles.h` 를 include 했다. `HttpServer::run(self, address, tls, app_context)` 로 C ABI 번역이 handle 을 넘기고 run 이 `site.owner` 에 둔다 (owner 는 요청을 처리하는 동안에만 쓰인다) |
| 검사 | `tools/check_rules.py` 에 `LAYER`: 모듈 순서 `core ← base ← statemachine ← http ← websocket ← context` 를 거꾸로 include 하면 위반. 공개 헤더 `kiotty_net_<모듈>.h` 도 같은 순서로 본다 (`core` 의 `kiotty_net_base.h` 는 code 때문에 허용, 수칙 7.4) |

수칙 7.1 의 의존 방향 문단을 고쳤다. 남은 `LAYER` 9 줄은 모두 http → websocket (서버의 upgrade) 이고 R3 에서 없앤다.

수칙 검사: FILE 1, LAYER 9, LINE 4, MACRO 18, PUBLIC 3. 기준선에 LAYER 9 가 새로 들어간다 (R3 에서 0).

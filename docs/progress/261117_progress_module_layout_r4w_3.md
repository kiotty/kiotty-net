# 2026-11-17 진행 정리 : 모듈 정리 R4w-3 이름 HttpSite → HttpSessionHost

기준 문서: `docs/design/design_module_layout_v0.html` 5.1절. 계획: `docs/plan/plan_base_261116.md`. 동작 바뀜 없음, 이름만.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `struct HttpSite` (`server/kiotty_http_site.h`) | `struct HttpSessionHost` (`server/kiotty_http_session_host.h`, `git mv`) |
| `session.site`, `HttpServer::site()`, `_site` | `session.host`, `HttpServer::host()`, `_host` |
| 인자 이름 `site` | `host` |

- base 의 `Connection` / `ConnectionHost` 와 같은 짝이다. session 들과 그들이 함께 쓰는 것 (registry, notices, limits, app_context, upgrade handler) 을 가진다.
- src 31 파일과 구현 가이드. 테스트 support 의 `HttpSite` (테스트용 서버 fixture, 다른 namespace) 는 그대로.
- 계획에 R4w 의 네 단계와 새 사이클 R7 (http ↔ websocket 경계) 을 적었다. "websocket 이 http 의 session / host 를 include 하지 않음" 과 클라이언트가 host 를 통째로 빌리지 않는 것은 경계 인터페이스가 바뀌는 일이라 R5 / R6 뒤 R7 로 미룬다.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로).

## 다음

R4w-4 `HttpSessionState`: session 의 진행 칸 (`framing`, `body`, `sent`, `persistence`, `phase` 와 같은 성격의 것) 을 한 값으로.

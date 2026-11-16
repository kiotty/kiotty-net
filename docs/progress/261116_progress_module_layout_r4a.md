# 2026-11-16 진행 정리 : 모듈 정리 R4a (http 를 관심사별 폴더로, entity 는 값만)

기준 문서: `docs/design/design_module_layout_v0.html` 5.1절. 구현은 C++ 구현 에이전트, 검토와 전체 빌드는 이 세션. 동작 바뀜 없음 (이름, 시그니처, 로직 그대로).

## http/ (`kiotty_http_` 접두 생략)

```
domain/entity/        body_step client_request constants content_type message_framing reply_framing
                      request_head request_line response_layout response_text route_match route_path
                      state status_line                                     (모두 .h, 값만)
data/repository/
  machine/   machine exchange static rule_names.h
  header/    header_line_parser token_rules reply_headers_parser shared_headers content_type_rules
  body/      message_framing_parser body_reader file_body upload_rules
  request/   request_line_parser request_head_builder request_store request_check_rules
  response/  status_line_parser response_text_builder response_store reply_store reply_framing_rules
  upgrade/   upgrade_handler.h
  server/    server work registry notices configuration site.h route_path_rules
  client/    client client_exchange client_headers client_link
data/state/       session
data/datasource/  session_pool
```

- 이동 67 개 (git 이 rename 으로 따라감), 새 entity 헤더 14 개.
- 이름: entity 는 값의 이름, 함수 파일은 하는 일을 붙인다 (`_parser` 바이트 → 값, `_builder` 값 → 바이트, `_rules` 검사 / 결정). 클래스 (store, reader, registry, server, client ...) 는 이름 그대로.
- entity 는 함수 선언이 없고 core, 다른 entity, 공개 헤더만 include 한다. `Result` 별칭 (`HeadLengthResult` 등) 은 그것을 돌려주는 함수 쪽에 남았다 (entity 가 `kiotty_result.h` 를 모른다).
- 값의 이름이 repository 의 클래스 파일과 겹치는 것은 다른 이름으로: `state.h` (HttpState, exchange 에서), `body_step.h` (BodyStep / BodyState, body_reader 에서), `route_match.h` (MatchKind, StaticTarget, RouteMatch ..., registry 에서), `client_request.h`, `response_layout.h` (response_store 에서).
- content_type 은 표와 파서가 같은 익명 namespace 를 써서 통째로 `_rules`. 값 `TextKind` 만 entity.
- 한 파일이 일을 둘 하는 것 (그대로 둠): request_line_parser 의 `methodTextOf`, message_framing_parser 의 `decidePersistence` / `expectsContinue`, reply_headers_parser 의 `allowedMethodsOf`, upload_rules 의 `idleUpload` / `startUpload`.
- request_check 는 통째로 `request/` (설계 5.1 은 헤더 쪽을 `header/` 로 나눴다. R4b 에서 볼 것).
- 값만 쓰는 헤더는 파서 대신 entity 를 include 하게 좁혔다. 그래서 `.cpp` 넷과 테스트 하나에 파서 include 를 더했다.
- 문서: 구현 가이드의 경로.

## R4b 가 알아야 할 것

- `machine/kiotty_http_exchange.h` 의 함수가 모두 `HttpSession&` 을 받아 몰래 바꾼다 (`beginExchange`, `readLine`, `readBody`, `reject`, `dispatch`, `startWork`, `sendNext`, `startSending`, `startHeaderClock`). 값으로 바꿀 주 대상.
- `answerStatic`, file_body 의 `attachFile / detachFile / pieceAt` 도 `session.response`, `session.file` 을 바꾼다.
- `HttpSession` 이 store (repository 의 클래스) 를 가져서 `state/` 가 `repository/request`, `repository/response` 를 include 한다.
- websocket 의 entity 파일이 아직 `data/state/kiotty_http_session.h`, `server/kiotty_http_site.h` 를 include 한다 (R4w).

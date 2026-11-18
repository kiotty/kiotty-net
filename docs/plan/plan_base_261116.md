# 계획 (2026-11-16)

이전 plan 문서 (`plan_base_v0.md`, `plan_base_v1.md`, `plan_base_v2.md`, `plan_refactor_v0.md`) 는 2026-11-16 에 지웠다 (모듈 정리로 폴더와 순서가 모두 바뀜). 기록은 git 과 `docs/progress/` 에 남아 있다. 이 문서 하나가 지금의 계획이다.

## 0. 한 사이클

```text
설계 (docs/design) ─▶ 사용자 확인 ─▶ 구현 ─▶ 전체 빌드 ─▶ 보고 (한국어) ─▶ 사용자 승인 ─▶ commit ─▶ push ─▶ CI
```

- 사이클 하나는 설계의 한 덩어리와 그 테스트 전부다.
- **전체 빌드**: WSL `Ubuntu-24.04` 일곱 벌 (gcc / clang × TLS OFF / ON, release-tls, asan-tls, tsan-tls, `build/b2_wsl.sh <이름들>`) 과 MSVC (`build/b2_all.bat msvc`). 경고 0. 방법은 `.claude/skills/build/SKILL.md`.
- **수칙 검사**: WSL 에서 `python3 -I tools/check_rules.py`. 기준선 FILE 1, LINE 4, MACRO 18, PUBLIC 3 보다 늘지 않는다.
- **단일 헤더**: 번역 단위 하나로 컴파일되는지 본다. 소스를 commit 한 뒤 `python3 tools/make_single_header.py` 로 다시 만들어 따로 commit 한다 (머리의 버전이 `+modified` 없이 찍히게).
- commit / push 는 사용자 승인 뒤. main 에 바로. commit 메시지 끝에 `Co-Authored-By` 줄. push 뒤 `gh run watch` 로 CI.
- 작업 트리를 바꾸는 git 명령 (`stash`, `checkout -- .`, `reset`) 은 쓰지 않는다.
- 사이클이 끝나면 `docs/progress/YYMMDD_progress_<주제>_<사이클>.md` 를 쓰고 이 문서 3절에 한 줄 더한다.
- docs 는 CRLF. 소스도 CRLF 다.

## 1. 사이클 이름표

사이클 이름은 설계 문서마다 머리글자를 따로 붙인다.

| 머리글자 | 설계 문서 | 무엇 | 상태 |
|---|---|---|---|
| 0 ~ 8, R4 ~ R8, 4b, 5a, 5b, 7b | `design_base_v5.html` | base (core, socket, event, tls, connection, io_loop, server / client, C API) | 끝 |
| M1 ~ M5 | `design_statemachine_v0.md` | 상태 기계 | 끝 |
| H1 ~ H5 | `design_http_v0.html` | HTTP 서버 | 끝 |
| W, V1 ~ V4, B3 | `design_websocket_v0.html`, `design_websocket_v1.html` | WebSocket | 끝 |
| C1 ~ C5 | `design_http_client_v0.html` 11절 | HttpClient v0 | 끝 |
| U1 ~ U3 | `design_http_client_v1.html` 9절 | HttpClient v1 (요청 body 조각 올리기) | U1, U2 끝, U3 남음 |
| R0 ~ R7, R4w | `design_module_layout_v0.html` 9절 | 모듈 정리 (base / statemachine / http / websocket) | R0 ~ R7 끝 |
| A1 ~ A3 | (쓸 것) write 중 도착 설계 | write 가 상대 바이트를 보면 멈추고 `DATA_ARRIVED_WHILE_WRITING` | 설계 전 |

## 2. 앞으로의 순서

```text
R0 → R1 → R2 → R3 → R4a → R4b → R4w → R5 → R6 → R7 → U2 → (설계) → A1 → A2 → A3 → U3
```

| 사이클 | 하는 것 | 완료 |
|---|---|---|
| R0 파일 정리 | 242 개 파일을 `base / http / websocket / statemachine` 로 옮김, include / CMake / 살아 있는 문서의 경로, 수칙 7.1 의 트리 | 끝 (`2b9aa04`) |
| R1 작은 방향 | websocket endpoint 소멸자를 websocket 으로, `KiottyNetConnectionEvent` 를 `kiotty_net_base.h` 로, http → context, 수칙 검사의 `LAYER` | 끝 (`75b66c1`) |
| R2 statemachine | entity `StateMachine` (앱 배열을 가리킴), repository 에 규칙 찾기와 `StatefulHandler`, usecases 에 run 의 조합 | 끝 (`2cfc0a4`) |
| R3 upgrade | http 의 `HttpUpgradeHandler`, `UPGRADED` 상태, websocket 이 frame 상태 / 연결별 병렬 배열 / 라우트 대상을 가짐, `WebSocketServerEndpoint` | 끝 (`4b8c635`) |
| R4a http 폴더 | repository 하위 폴더 8, state/session, entity 는 값만 (`_parser` / `_builder` / `_rules`) | 끝 (`880ec3f`) |
| R4b http 흐름 | R4b-1 요청 머리 (`RequestFraming`), R4b-2 body (`BodyPlan`), R4b-3 답 고르기 (`Answer`), R4b-4 보내기 (`ResponseFrame`, `AfterResponse`). 숨은 쓰기를 돌려주는 값으로 | http 서버 테스트 그대로, 숨은 쓰기 목록 비움. 끝 (R4b-1, R4b-2 `27e0e8d`, R4b-3 `1fb8238`, R4b-4 `79b1297`) |
| R4w websocket | R4w-1 자리 옮기기 (entity 는 값만, repository 하위 폴더 8, state 둘, datasource `SocketTable`), R4w-2 frame 읽기를 값으로 (`UPGRADED` 한 상태, `stepFrame`, `WebSocketState`), R4w-3 이름 `HttpSite` → `HttpSessionHost` (`Connection` / `ConnectionHost` 와 짝), R4w-4 `HttpSessionState` (session 의 진행 칸을 한 값으로) | websocket 테스트 그대로. 끝 (R4w-1 `b1bb486`, R4w-2 `ad3e91b`, R4w-3 `69f2c9d`, R4w-4 `6db8054`) |
| R5 client State | `HttpClientState`, `ConnectionStream` 의 peek / consume, StreamClient 공개 메서드 정리, `HttpClientExchange(state, line)` (friend 제거), 값으로 잇는 client 흐름 (`Connected`, `ReplyHead`) | HttpClient 테스트 그대로, friend 0. 끝 (`5a615f5`) |
| R6 usecases | 모든 모듈의 C ABI 를 usecases 함수로, `src/kiotty_net_*.cpp` 는 인자 변환만 | adapter 에 조합 로직 없음. 끝 (`ce65bd8`) |
| R7 http ↔ websocket 경계 | 설계 `design_http_websocket_boundary_v0.html` (2026-11-17, 이름 `UpgradeSession`). R7-1 `UpgradeSession` 과 answer / open / step / end, R7-2 access 가 `SocketLink` 만, R7-3 클라이언트 자기 state, R7-4 handler 공개 필드. `HttpUpgradeHandler` 의 answer / step / end 가 session 통째 대신 필요한 것 (요청 view, 응답 store, body 버퍼, 한도) 만 받음. 클라이언트가 `HttpSessionHost` 를 통째로 빌리지 않고 자기 state 를 가짐. `WebSocketHandler` 의 공개 필드 (owner, lane, client, role, mask_state) 정리 | websocket 이 http 의 session / host 를 include 하지 않음 (남은 것: `WebSocketServer : HttpServer` 의 `host().registry` / `push_capacity`). websocket 테스트 그대로. 끝 (R7-1 `4828405`, R7-2 `1c2e99e`, R7-3 `cd92ede`, R7-4 `ffc708e`) |
| U2 연결과 취소 | begin 의 살아 있는지 보기, close 로 깨우기, 송신 오류 정리, TLS, 먼저 온 응답 (`design_http_client_v1.html` 정할 것 1) | integration: idle 연결 뒤 begin, close 가 걸린 write 를 깨움, 413, https 올리기. 끝 (Windows 의 먼저 온 응답은 A3) |
| A1 base | `KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING` (enum 끝), 소켓 backend 의 읽기 또는 쓰기 기다림, `ConnectionStream` 의 멈추는 write (시작할 때 한 번 + 막힐 때마다 확인, 수신 Stage 로 옮김, Stage 가 차면 OUT 만, TLS 는 평문이 나올 때만), StreamClient 와 서버 work stream | 평문 / TLS 멈춤, FIN / RST 구별, Stage 가 찼을 때 헛돌지 않음, NewSessionTicket 에 멈추지 않음, Windows 에서 RST 앞의 바이트를 지킴 |
| A2 HTTP 서버 work | work 층이 도착을 다음 요청으로 보고 계속 씀, `failureOf` 의 `isStreamOpen` 짐작 없앰, `isHttpWorkConnected` 문서 | pipelining 중 긴 응답, SSE 에서 클라이언트가 떠나면 다음 write 가 `PEER_CLOSED` |
| A3 HttpClient 올리기 | 멈추면 응답 머리를 읽어 1xx 는 버리고 이어 보냄, 최종 응답이면 `REFUSED` 와 새 code, finish 가 그 응답 | Windows 에서도 413 / 401, U2 의 두 테스트를 다시 엄격하게 |
| U3 도우미, C++, 문서 | `sendHttpFile`, C++ begin / write / finish, user guide / requirement / 구현 가이드 / 테스트 계획, Python `http.server` 외부 확인 | wrapper 테스트, 매트릭스 |

미결 (사이클 없음): WebSocket streaming (보내기 begin / write / finish, 받기는 조각마다 `onMessage`). `requirement_v0.md` 의 메모. 이름 / 켜는 단위 / 조각 크기를 정해야 사이클이 된다.

## 3. 기록

2026-11-16 HttpClient v1 U1 (`9ad71d9`, 단일 헤더 `6548b85`, `docs/progress/261116_progress_http_client_u1.md`): 요청 body 를 begin / write / finish 로 조각 올리기, Content-Length 또는 chunked. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-16 모듈 정리 설계 (`docs/design/design_module_layout_v0.html`): 의존 방향 `core ← base ← statemachine ← http ← websocket`, 층의 역할, 값으로 잇는 흐름, 정할 것 여섯 모두 닫음.

2026-11-16 모듈 정리 R0 (`2b9aa04`, 단일 헤더 `d74963e`), R1 (`75b66c1`) (`docs/progress/261116_progress_module_layout_r0_r1.md`): 파일 242 개를 모듈로, 거꾸로 된 의존 셋 (base → websocket, base → statemachine 헤더, http → context) 을 없앰, 수칙 검사 `LAYER`. 남은 LAYER 9 은 http → websocket (R3). MSVC 847, WSL 848 × 6, release-tls 847. 수칙 검사 기준선 FILE 1, LAYER 9, LINE 4, MACRO 18, PUBLIC 3.

2026-11-16 모듈 정리 R2 (`2cfc0a4`, `docs/progress/261116_progress_module_layout_r2.md`): statemachine 을 entity (`StateMachine` view) / repository (규칙 찾기, `StatefulHandler`) / usecases (run) 로. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-16 모듈 정리 R3 (`4b8c635`, `docs/progress/261116_progress_module_layout_r3.md`): http 는 `HttpUpgradeHandler` 만 안다, websocket 상태 / 라우트 대상은 `WebSocketHandler`, `WebSocketServer : HttpServer`. LAYER 0. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-16 모듈 정리 R4a (`880ec3f`, `docs/progress/261116_progress_module_layout_r4a.md`): http 를 관심사별 폴더로, entity 는 값만. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-16 모듈 정리 R4b-1, R4b-2 (`27e0e8d`, `docs/progress/261116_progress_module_layout_r4b_1_2.md`): 요청 머리를 `RequestFraming` 값으로, body 남은 길이를 `BodyPlan` 규칙으로. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-16 모듈 정리 R4b-3 (`1fb8238`, `docs/progress/261116_progress_module_layout_r4b_3.md`), R4b-4 (`79b1297`, `docs/progress/261116_progress_module_layout_r4b_4.md`): 라우트가 아닌 답을 `Answer` 로, 콜백 뒤 갈래를 `RouteOutcome -> AfterRoute` 로, 보내기를 `SendStep` / `AfterResponse` 로. R4b 끝. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-17 모듈 정리 R4w-1 (`b1bb486`, `docs/progress/261117_progress_module_layout_r4w_1.md`), R4w-2 (`ad3e91b`, `..._r4w_2.md`), R4w-3 (`69f2c9d`, `..._r4w_3.md`), R4w-4 (`6db8054`, `..._r4w_4.md`): websocket 을 관심사별 폴더로 (entity 는 값만, state 둘, datasource `SocketTable`), http 기계의 frame 상태를 `UPGRADED` 하나로 하고 서버와 클라이언트가 `stepFrame` 을 함께 씀 (frame / message / control 규칙이 값을 돌려줌), `HttpSite` → `HttpSessionHost`, `HttpSessionState`. http ↔ websocket 경계 (session 통째 대신 필요한 것만, 클라이언트의 자기 state) 는 새 사이클 R7 로. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-17 모듈 정리 R5 (`5a615f5`, `docs/progress/261117_progress_module_layout_r5.md`): `HttpClientState` (state/), `HttpClientExchange` / Headers / Link 가 (state, StreamClient) 를 받고 StreamClient 공개 메서드만 씀 (friend 0), StreamClient 공개 7 (`stream()` → `StreamResult`, `reconnect(address, deadline)`, `stop()`), `ConnectionStream::peek(wanted, t)` / `consume` 과 자유 함수 `readLine`, send / reply 자유 함수가 `Connected` / `ReplyHead` 값을 돌려줌. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-17 모듈 정리 R6 (`ce65bd8`, `docs/progress/261117_progress_module_layout_r6.md`): usecases 여섯 파일 (base / http / websocket 의 server, client), 어댑터는 null 검사, 인자 변환, 결과 변환과 부르기 하나만. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-17 모듈 정리 R7-1 (`4828405`, `docs/progress/261117_progress_module_layout_r7_1.md`): `UpgradeSession` 과 answer / open / step / end, http 가 101 을 보냄, websocket 의 frame / message / control / handshake 가 http session 을 include 하지 않음. MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-17 모듈 정리 R7-2 (`1c2e99e`), R7-3 (`cd92ede`), R7-4 (`ffc708e`), `docs/progress/261117_progress_module_layout_r7_{2,3,4}.md`: access 가 websocket 의 칸만 봄, 클라이언트가 자기 `WebSocketClientState` (`HttpReplyStore`), `WebSocketHandler` 공개 필드 0 (`SocketSide`). MSVC 847, WSL 848 × 6, release-tls 847.

2026-11-18 HttpClient v1 U2 (`docs/progress/261118_progress_http_client_u2.md`): begin 이 다시 쓰는 연결을 1 ms peek 로 확인 (닫힘, 남은 바이트, TLS close_notify 면 다시 연결), write 가 연결을 잃으면 `REFUSED` 로 두고 finish 가 먼저 온 응답 (413, 401) 을 돌려줌, close 가 걸린 write 를 깨움, https 올리기. MSVC 857, WSL 858 × 6, release-tls 857.

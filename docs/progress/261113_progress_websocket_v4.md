# 2026-11-13 진행 정리 : WebSocket 설계 v1 의 사이클 V4 (문서)

기준 문서: `docs/design/design_websocket_v1.html` 11절의 V4 행. 코드는 바뀌지 않았다.

## 고친 문서

- **`docs/requirement/user_guide_websocket.md`** 를 헤더 판 v3 으로 다시 썼다.
  - 1절 한눈에: 등록이 HTTP 모양 (`KiottyNet_on<Layer><Event>`), app_context 는 run 에 한 번, 서버 / 클라이언트의 차이 표.
  - 4절 서버: `addWebSocket(server, path)` 의 route 사슬, `route.code` 의 규칙, onMessage 필수 (run 거절), 콜백 넷의 모양 (Close 에 handle 없음과 그 이유).
  - 5절 보내기, **받기 (새 절)**, 닫기, broadcast. receive 함수가 없는 이유와 "답을 기다리는" 모양은 앱의 큐로 놓는다는 것. 닫기의 세 장면 (받은 내용을 보고, 바깥 사건, 정상 종료), bye, close status 의 범위, onClose 표에 `PEER_NOT_READING`.
  - 6절 클라이언트: 등록 넷, Error 의 표 (열리기 전 한 번, Close 와 둘 중 하나), subprotocol 후보, run 의 path / tls / app_context, 반환값 표 (1024 바이트), bye 와 close.
  - 7절 서버 예제: `KiottyNet_WebSocketRoute` 사슬, websocket_context 에 handle 을 담아 Close 에서 목록에서 빼기, `onHttpError`. C 예제도 route 로.
  - **8절 클라이언트 예제 (새 절)**: 요청을 보내고 답을 기다리기 (onMessage → 큐 + condition_variable, `wait_for` 3 초, onClose 가 기다림을 놓아 줌), protocols / onError / bye.
  - 9절 안에서 어떻게 도는가: 역할 값이 부르는 콜백도 가른다, 클라이언트의 101 의 후보 검사, run 끝의 onError, mask key 생성기.
  - 10절 판 v2 와 달라진 점, 11절 두지 않은 것 (receive 함수, 콜백 하나로 모든 이벤트).
- **`docs/requirement/requirement_v0.md`** 10절 (서버의 등록, app_context, onClose 의 handle, receive 함수 없음, write lane 의 close 는 `thenCloseWith`, `PEER_NOT_READING`, subprotocol, 헤더 초안의 typedef 넷과 Route, C 함수, C++ `KiottyNet_WebSocketRoute`, 사용 예) 와 11절 WebSocketClient (create, 등록, Error, 후보, run, Upgrade 요청, 101 검사, mask, 닫기 둘, 반환값, 헤더 초안, C++, 사용 예), 미결.
- **`docs/requirement/implementation_guide_v0.md`** 5절: 파일 표, `WebSocketTarget` / `ClientEvents` / `ClientUpgrade`, 등록 (registry 의 index 와 `find(index)`, run 의 onMessage 검사), `openSocket` 과 `callOpen / callMessage / callClose`, 클라이언트의 등록 / 후보 / onError 의 두 길 / bye / mask key, 미결 하나 닫음.
- **`docs/requirement/user_guide_http.md`** 1.11: WebSocket 경로의 안내를 새 등록으로.
- **`docs/test/test_base_v0.html`** 20절: Headers 행 (handler 대신 route), 서버의 경로 구분 (websocket_context), 등록 규칙 행, 클라이언트의 요청 검사 행과 인자 검사 행을 V2 / V3 의 모양으로. 19절은 V1 에서 22절로 정리했다.
- **`docs/design/design_websocket_v1.html`** 11절: 모두 끝났다는 기록과 구현에서 정한 것 (V1 의 필수 app_context, V2 의 `find(index)`, V3 의 onError 규칙과 1024 바이트).

## 매트릭스

문서만 바꿨으므로 빌드는 돌리지 않았다. 마지막 코드 커밋 (V3, `a87af46`) 의 매트릭스: MSVC 667 / 667, WSL 넷 668 / 668, asan / tsan 668 / 668, Release 667 / 667.

## 다음

설계 v1 의 사이클은 모두 끝났다. 남은 열린 것은 requirement 의 미결 (CORS, streaming 모드, permessage-deflate) 과 HttpClient 초안이다.

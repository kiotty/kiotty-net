# 2026-11-17 진행 정리 : 모듈 정리 R4w-1 websocket 자리 옮기기

기준 문서: `docs/design/design_module_layout_v0.html` 5.2절. 계획: `docs/plan/plan_base_261116.md`. 구현은 C++ 구현 에이전트, 검토와 전체 빌드는 이 세션. 동작 바뀜 없음.

## websocket/ (`kiotty_websocket_` 접두 생략)

```
domain/entity/        frame.h close.h handshake.h target.h                     (값만)
data/repository/
  frame/      frame_parser frame_builder mask_rules frame_reader
  message/    message
  control/    close_rules control timing
  handshake/  key_rules upgrade opening
  send/       lane
  server/     handler server access
  client/     client client_machine
  endpoint/   endpoint
data/state/       socket_state client_state.h
data/datasource/  socket_table
```

- `rows.cpp` (393 줄) 를 넷으로: `frame_reader` (onFrame*, readNext, nextFrame), `message` (finishData, isInOrder, storePiece), `control` (finishControl, answerClose, failSocket, writeClose, onSocketTimeout, endSocket), `opening` (onSocketWritten, openSocket).
- `frame.cpp` 를 `frame_parser`, `frame_builder`, `mask_rules` 로. `FrameHead::span()` 은 `headBytesOf(const FrameHead&)` 로 (entity 는 메서드가 없다).
- `close.cpp` → `control/close_rules`, `handshake.cpp` → `handshake/key_rules`. `Result` 별칭은 함수 쪽.
- `socket.h` 를 셋으로: `SocketRole`, `WebSocketTarget`, `ClientEvents`, `ProtocolText` → `entity/target.h`, `ClientUpgrade` → `state/client_state.h`, `PushLane` → `send/lane.h`. `SocketState`, `SocketLink` 는 `state/socket_state`.
- `SocketTable` (datasource) 이 `SocketState[]`, `SocketLink[]` 를 가진다. `WebSocketHandler` 는 `_targets` 와 `SocketTable` 하나.
- frame 머리의 비트 상수 (`FINAL_BIT`, `MASK_BIT`, 길이 코드와 길이 바이트) 는 parser 와 builder 가 함께 써서 `entity/frame.h` 로 (단일 헤더는 번역 단위 하나라 두 벌을 둘 수 없다).
- 익명 이름: `frame_reader` 의 `TOO_BIG` 을 `FRAME_TOO_BIG` 으로 (control 의 것과 겹침).
- 구현 가이드의 websocket 파일 표를 새 경로로.

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로).

## 남은 것

- repository 의 흐름 함수는 아직 모두 `HttpSession&` 을 받는다 (R4w-2, R4w-4).
- `UPGRADED` 한 상태로 줄이기, 서버와 클라이언트가 함께 쓰는 `stepFrame` (R4w-2).
- 클라이언트가 `HttpSite` / `HttpSession` 을 통째로 빌림, `WebSocketHandler` 의 공개 필드 (owner, lane, client, role, mask_state) (R4w-3).

## 다음

R4w-2 frame 읽기를 값으로.

# 2026-11-17 진행 정리 : 모듈 정리 R4w-2 frame 읽기를 값으로

기준 문서: `docs/design/design_module_layout_v0.html` 4절, 5.2절, 6절. 계획: `docs/plan/plan_base_261116.md`.

## 바뀐 것

| 전 | 뒤 |
|---|---|
| `HttpState` 에 `SOCKET_OPENING`, `FRAME_START`, `FRAME_LENGTH`, `FRAME_MASK`, `FRAME_PAYLOAD` 다섯. http 기계에 frame 행 9 개, 클라이언트 기계에 8 개 | `HttpState::UPGRADED` 하나. http 기계는 `UPGRADED` 의 WRITTEN / READ / TIMEOUT 세 행이 `handler.step` 으로, 클라이언트 기계는 READ / TIMEOUT 두 행이 `stepFrame` 으로 |
| `socket.read` 가 `SocketRead { length, int32_t state }` (HttpState 를 int 로) | `FrameRead { length, WebSocketState }`. `WebSocketState { OPENING, FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD }` 는 websocket 의 entity |
| 서버 handler 의 `onFrameRead` 와 클라이언트 기계의 frame 행이 따로 갈랐다 | 둘 다 `stepFrame(session, event)` (`frame/frame_reader`) 하나. `OPENING` 이면 WRITTEN → `sendOpening`, 아니면 READ → 단계, TIMEOUT → `timeoutSocket` |
| frame 행 안에서 마스크 / 순서 / 크기 / 다음 read 를 바로 판단하고 `socket` 을 고쳤다 | 규칙이 값을 돌려준다: `frame_rules` (`isAcceptedStart`, `frameOf`, `isOversized`, `frameEndOf` → `PAYLOAD / CONTROL / DATA`, `payloadReadOf`, `advanceFrame`), `message_rules` (`isInOrder(message, opcode)`, `messageStepOf` → `MessageStep { message, delivered, is_done }`, `isBrokenMessage`, `appMessageOf`), `control_rules` (`controlActionOf` → `ANSWER_CLOSE / SEND_PONG / CLEAR_PING / NOTHING`, `timeoutActionOf` → `CLOSE / SEND_CLOSE / SEND_PING`). 단계 함수는 값을 `socket` 에 한 번 저장한다 |
| `onSocketWritten`, `onSocketTimeout`, `onFrame*` 가 행 모양 (context, handle, event) | `sendOpening(session, written)`, `timeoutSocket(session)`, 익명 `readFrame*(session, bytes)` |

- 새 entity `kiotty_websocket_frame_step.h`: `WebSocketState`, `FrameRead`, `SocketFrame` (state 에서 옮김), `FrameEnd`, `MessageStep`, `ControlAction`, `TimeoutAction`.
- 규칙 함수는 session 을 받지 않는다.
- 흐름: `FrameStart -> isAcceptedStart / frameOf -> SocketFrame`, `SocketFrame -> frameEndOf -> 다음 read 또는 끝`, `SocketFrame + message -> MessageStep -> on_message`, `opcode -> ControlAction`, `ping_deadline -> TimeoutAction`.

## 동작

테스트로 볼 수 있는 동작은 그대로다. 생길 수 없는 사건 조합만 달라졌다. 예전에는 행이 없어 statemachine 이 `UNHANDLED_EVENT` 로 거절했다 (onError 보고). 이제 `stepFrame` 이 `closeWith(UNHANDLED_EVENT)` 로 닫는다. 이런 조합은 OPENING 의 READ, frame 단계의 WRITTEN 이다. OPENING 의 TIMEOUT 은 예전처럼 `TIMEOUT` 이다 (101 의 write 는 timeout 0 이라 생기지 않는다).

## 확인

MSVC 847, WSL 848 × 6, release-tls 847. 단일 헤더 `single done 0`. 수칙 검사 FILE 1, LINE 4, MACRO 18, PUBLIC 3, LAYER 0 (그대로). 구현 가이드 5.3 / 5.4 / 5.5 / 5.8 의 상태와 이름.

## 다음

R4w-3 클라이언트가 `HttpSite` / `HttpSession` 을 통째로 빌리지 않게, `WebSocketHandler` 의 공개 필드 정리.

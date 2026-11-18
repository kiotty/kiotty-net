# kiotty-net

빌드 / 테스트 방법은 `.claude/skills/build/SKILL.md`. 계획은 `docs/plan/plan_base_261116.md`, 사이클 기록은 `docs/progress/`.

## 다음에 할 일 (2026-11-18 퇴근 때 적음)

U2 (HttpClient v1 의 연결과 취소) 는 commit / push 했다. 남은 순서:

```text
설계 문서 → 사용자 확인 → A1 (base) → A2 (HTTP 서버 work) → A3 (HttpClient 올리기) → U3
```

1. **설계 문서 쓰기**: `docs/design/` 에 "write 중 도착" 설계 (이름 예: `design_write_arrival_v0.html`). 배경은 `docs/learning/early-reply-and-rst.html` (올리는 중 서버가 413 / 401 을 먼저 보내고 RST 로 닫으면 Windows 는 커널 수신 버퍼의 응답을 버림). 사용자와 정한 것:
   - write 는 poll(IN | OUT | stop) 로 기다린다. 상대 바이트가 오면 보내기를 멈추고 recv 로 수신 Stage 에 옮긴 뒤 `{ 보낸 만큼, KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING }` 을 돌려준다. 앱은 바로 read 하고, read 는 Stage 에서 먼저 준다. poll / 기다리지 않는 read 는 앱에 공개하지 않는다.
   - 확인 시점은 A: write 를 시작할 때 한 번 (기다리지 않음) 과 막힐 때마다.
   - EOF → `PEER_CLOSED`, RST → `SOCKET_ERROR`.
   - TLS 는 평문이 나올 때만 멈춘다 (NewSessionTicket 같은 제어 레코드는 멈추지 않음).
   - Stage 에 바이트를 실제로 옮겼을 때만 알린다. Stage 가 차 있으면 OUT 만 기다린다 (헛돌기 방지).
   - StreamClient 와 서버 work stream 모두 적용. work stream 은 `WorkFlow::isReady` 가 이벤트가 쉴 때만 시작하므로 IoLoop 와 바이트를 다투지 않는다. WebSocket 은 이벤트 기반이라 바뀌지 않는다.
   - 새 code 는 enum 맨 끝에 붙인다.
   - FIONREAD / MSG_PEEK 로 길이만 보는 방법은 데이터와 닫힘은 구별하지만 Windows 의 RST 버림과 TLS 를 풀지 못해서 쓰지 않는다 (설계 문서에 적을 것).
   - 정할 것: HttpClient 올리기 write 의 code 이름 (후보 `KIOTTY_NET_CODE_REPLY_ARRIVED_WHILE_WRITING`), 설계 문서 이름과 사이클 머리글자 (지금은 A1 ~ A3).
2. **A1 ~ A3**: 계획 문서 2절의 표. A3 에서 U2 의 두 테스트 (`FinishShowsTheReplyOfAServerThatRefusedTheBodyEarly`, `FinishAfterAWriteThePeerCutOffShowsTheReplyItSentFirst`) 의 Windows 예외 (`RESET_DROPS_RECEIVED_BYTES`) 를 없앤다.
3. **U2 에서 사용자 확인을 기다리는 것** (`docs/progress/261118_progress_http_client_u2.md` 의 "설계와 다른 것"): begin 의 살아 있는지 보기는 `connected()` 대신 1 ms peek, 먼저 온 응답이 없을 때 finish 는 write 의 code 대신 읽기의 code.

이 절은 다음 일을 시작하면 지우거나 고친다.

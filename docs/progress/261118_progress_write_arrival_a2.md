# 2026-11-18 진행 정리 : write 중 도착 사이클 A2 (HTTP 서버 work)

기준 문서: `docs/design/design_write_arrival_v0.html` 4절 ("A2 에서 볼 것"), 9절의 A2 행. 앞 사이클은 `261118_progress_write_arrival_a1.md`.

## 정한 것 (2026-11-18, 사용자)

- **half-close 는 떠남이다.** 클라이언트가 요청 뒤 쓰기 쪽만 닫으면 (FIN) work 의 다음 write 가 EOF 를 보고 `PEER_CLOSED` 로 응답을 끊는다 (설계 결정 3 그대로). 일반 응답 경로는 응답을 다 보낸 뒤 다음 읽기에서 EOF 를 보므로 그런 클라이언트에게도 끝까지 보낸다. 이 차이는 문서에 적었다. 브라우저와 curl 은 half-close 를 하지 않는다.

## 안에서

- **도착은 다음 요청이다.** `writeHttpWorkChunk` 의 송신 (`sendSpan`) 은 A1 에서 이미 도착이면 보낸 만큼 넘기고 이어 보낸다. 받아 둔 바이트는 연결의 수신 Stage 에 있고, work 가 끝나 연결이 읽기로 돌아가면 `ReceiveFlow::drive` 가 Stage 를 먼저 보아 다음 요청으로 처리한다 (edge-triggered 이벤트를 기다리지 않음). 새 코드는 필요 없었고 테스트로 확인했다.
- **`failureOf` 의 짐작을 없앴다.** 전에는 송신이 `PEER_CLOSED` / `SOCKET_ERROR` 로 끝나면 `isStreamOpen` 을 보아 닫혔으면 `PEER_CLOSED` 로 바꿨다 (RST 도 `PEER_CLOSED` 가 됨). 이제 송신이 EOF 와 RST 를 직접 구별하므로 그대로 돌려준다. 서버가 close 중이면 `CLOSED` 는 그대로.
- `isHttpWorkConnected` 는 남긴다. 문서를 "이벤트를 기다리는 worker 가 다음 write 전에 끊김을 미리 볼 때" 로 고쳤다 (`requirement_v0.md`, `user_guide_http.md`).

## 테스트

- integration `kiotty_test_http_work.cpp`:
  - `PeerThatLeavesEndsTheStreamWithPeerClosed` 를 둘로: 클라이언트가 쓰기 쪽을 닫으면 (FIN) 다음 write 가 `PEER_CLOSED`, reset (RST) 이면 `SOCKET_ERROR`. 전의 테스트는 읽지 않은 핑을 남긴 채 닫아 실제로는 RST 였고 짐작이 `PEER_CLOSED` 로 바꿔 주었다.
  - 새: 긴 응답 (SSE) 중에 보낸 다음 요청 (pipelining) 을 write 가 받아 두고, 응답이 끝난 뒤 그 요청이 답을 받음.

| 빌드 | 전 | 후 |
|---|---|---|
| MSVC (`build/msvc`, TLS OFF) | 866 | 871 (A2 와 A3 를 함께 돌린 수, A2 만은 gcc 의 HttpWork 테스트로 확인) |
| WSL gcc / gcc-notls / clang / clang-notls / asan / tsan | 867 | 872 (A2 와 A3 를 함께 돌린 수, A2 만은 gcc 의 HttpWork 테스트로 확인) |
| WSL clang-release | 866 | 871 (A2 와 A3 를 함께 돌린 수, A2 만은 gcc 의 HttpWork 테스트로 확인) |

수칙 검사 기준선 그대로 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 남은 것

A3 (HttpClient 올리기), U3.

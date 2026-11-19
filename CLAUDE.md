# kiotty-net

빌드 / 테스트 방법은 `.claude/skills/build/SKILL.md`. 계획은 `docs/plan/plan_base_261116.md`, 사이클 기록은 `docs/progress/`.

## 다음에 할 일 (2026-11-18 고침)

write 중 도착 (A1 ~ A3, `docs/design/design_write_arrival_v0.html`) 과 HttpClient v1 (U1 ~ U3) 은 끝났다. 기록은 `docs/progress/261118_progress_write_arrival_a1.md` ~ `_a3.md`, `261118_progress_http_client_u3.md`. U2 에서 확인을 기다리던 두 가지 (begin 의 1 ms peek, finish 가 읽기의 code) 는 A1 설계 확인 때 사용자가 받아들였다 (2026-11-18).

계획 문서의 남은 미결은 WebSocket streaming (사이클 없음, 이름 / 켜는 단위 / 조각 크기를 정해야 함) 하나다.

DICOM 적용 변경 요청 D1 ~ D2 (2026-11-19, 설계 `docs/design/design_peer_settings_and_cancel_v0.html`) 도 끝났다. 기록은 `docs/progress/261119_progress_peer_settings_d1.md`, `261119_progress_stream_client_cancel_d2.md`.

- D1 (끝): 상대에 따라 정해지는 다섯 설정 (`connect_timeout_milliseconds`, `max_trial`, `trial_interval_milliseconds`, `no_delay`, `send_timeout_milliseconds`) 을 `KiottyNetAddress` 로 옮기고, create 의 설정은 메모리 크기 (`max_clients`, 세 capacity) 만 남긴다. 세 번째 설정 구조체는 만들지 않는다.
- D2 (끝): `KiottyNet_cancelStreamClient` (다른 thread 에서 `StopSignal::raise`, read / write / connect 를 깨우고 연결은 살려 둠, `KIOTTY_NET_CODE_CANCELLED`). 서버 work stream 에는 cancel 을 넣지 않는다.

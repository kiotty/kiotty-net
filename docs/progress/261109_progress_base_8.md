# 2026-11-09 진행 정리 : 6 ~ 8 (io_loop, owner, base.cpp) 과 7b

기준 문서: `docs/plan/plan_base_v2.md` 6, 7, 8, `docs/design/design_base_v5.html`, `docs/test/test_base_v0.html` 10 ~ 12절, `docs/learning/owner-destroy-deadlock.html`.

## 커밋

| 커밋 | 내용 |
|---|---|
| ff38d02, deef453 | 6. IoLoop 의 바퀴, admit, accept 실패 재시도 |
| 42e09a1 | 7. Server / EventClient / StreamClient, static Context |
| e4d977d | 7. destroy 교착 수정: join 을 Context mutex 밖으로, loop thread 의 자기 destroy 는 다음 create 가 회수, release 는 RELEASING 단계 |
| 166ebc5 | 7b. Owner 개수 상한 없음 (`std::vector<std::unique_ptr<T>>`), Server 의 연결 풀은 `max_clients` (기본 1024) 로 create 때, EventListener 의 placement new 와 완료 배열의 raw new 제거 |
| db5b6fa | 7b. `KiottyNetEndpoint` → `KiottyNetAddress`, `KiottyNetConnectionOwner` → `KiottyNetEndpoint` |
| (이 커밋) | 8. `src/kiotty_net_base.cpp`, C API 테스트, 벤치마크, CI `benchmark` job |

## 8 의 코드

- `context/kiotty_handles.h`: `struct KiottyNetServer final : Server {}` 와 StreamClient, EventClient 둘. `KiottyNetContextImpl final : Context` 는 `kiotty_context.h` 끝 (DV16). Context 의 풀과 `create*` / `destroy` 가 이 이름을 쓰므로 base.cpp 에 cast 가 없다.
- `src/kiotty_net_base.cpp`: 함수마다 NULL 검사 (context, config, handle, handler, `owner`) 뒤 내부 함수 하나. 로직 없음. `KiottyNet_notifyEventClient` 는 `notify(connection().id, data)`.
- `KiottyNet_serverMemorySize` 는 기본 설정 (`max_clients` 1024) 의 Server 객체 + 연결 수 × `sizeof(Connection)`, `KiottyNet_clientMemorySize` 는 두 client 중 큰 쪽. 인자가 없는 함수라 설정마다의 값은 내지 못한다 (아래 정할 것).

## 테스트

- `tests/integration/kiotty_test_base_api.cpp` (공개 헤더만): 6 개. Context 두 번, NULL 인자 전부, notify 의 handle 종류 ({NULL, 0}, {owner, 0}, 랜덤 id, 유효, 지난 generation, destroy 된 Owner), Server + EventClient 왕복, StreamClient ↔ worker 와 IO thread 의 `BUSY`, 메모리 크기.
- 전체 420 개.

## 벤치마크

`tests/benchmark/kiotty_bench_base.cpp` 12 개 (공개 C API 만). 첫 수치는 `docs/test/test_base_v0.html` 12.1. 추정 대비 밖인 것: EchoRoundtrip/1 (253 µs, CPU 19 µs), ConnectionChurn (645 /s), TlsHandshakeRate (666 /s). CI `benchmark` job 은 main push 와 `workflow_dispatch` 에서 JSON 을 artifact 로 남긴다. 회귀 판정은 아직 없다.

## 정할 것

1. `await(0)` 은 "영원히 기다림" 이다 (`RunGate` 의 `WAIT_FOREVER = 0`). 테스트를 쓰다가 `KiottyNet_awaitEventClient(client, 0)` 이 멈췄다. "0 = 바로 확인" 이 C API 사용자에게 자연스럽다.
2. `KiottyNet_serverMemorySize(void)` 를 설정을 받는 모양 (`const KiottyNetServerConfiguration*`) 으로 바꿀지.
3. echo 왕복 253 µs 의 원인 (WSL2 의 wake 지연인지, 바퀴의 wait 인지) 조사.
4. `docs/requirement/requirement_v0.md` 의 compile time 상한 (128, 최대 1024) 문장.

## 다음 할 일

base 완료 뒤: kqueue (macOS, iOS 의 event backend), io_uring, HTTP 층.

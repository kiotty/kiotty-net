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
- `KiottyNet_serverMemorySize(config)` 는 그 설정의 Server 객체 + `max_clients` × `sizeof(Connection)` (NULL 이나 초기화 안 된 설정은 0), `KiottyNet_clientMemorySize` 는 두 client 중 큰 쪽.

## 테스트

- `tests/integration/kiotty_test_base_api.cpp` (공개 헤더만): 6 개. Context 두 번, NULL 인자 전부, notify 의 handle 종류 ({NULL, 0}, {owner, 0}, 랜덤 id, 유효, 지난 generation, destroy 된 Owner), Server + EventClient 왕복, StreamClient ↔ worker 와 IO thread 의 `BUSY`, 메모리 크기.
- 전체 421 개.

## 벤치마크

`tests/benchmark/kiotty_bench_base.cpp` 12 개 (공개 C API 만). 수치는 `docs/test/test_base_v0.html` 12.1 (CI 의 ubuntu 와 WSL2). CI 기준으로 echo 78 µs, TLS handshake 1.09 k/s 는 목표 안. WSL2 는 맨 TCP loopback echo 도 197 µs 라 그 환경의 지연이 대부분이다. 처음 판의 대기 helper 가 1 ms sleep 이라 ConnectionChurn, WorkHandoff 가 낮게 나왔다 (yield 로 바꿨다). 목표 밖으로 남은 것은 ReadHeavy (16 KiB 마다 콜백, 약 40 µs) 와 ConnectionChurn. CI `benchmark` job 은 main push 와 `workflow_dispatch` 에서 JSON 을 artifact 로 남긴다. 회귀 판정은 아직 없다.

## 정할 것

1. timeout 0 의 뜻. 라이브러리 전체 (`await`, stream read / write, connect, action 의 timeout) 가 0 = 제한 없음이다 (`SO_RCVTIMEO` 와 같은 관례). `await` 만 0 = 바로 확인으로 바꾸면 어긋난다.
2. ReadHeavy 와 ConnectionChurn 의 원인 조사.

## 다음 할 일

base 완료 뒤: kqueue (macOS, iOS 의 event backend), io_uring, HTTP 층.

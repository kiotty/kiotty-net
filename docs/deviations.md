# 편차 기록 (Deviation Record)

개발 수칙 v2 (`docs/requirement/development_rule.md`) 와 MISRA C++:2023 에서 벗어난 곳을 모은다. 새 편차는 이 표에 한 줄을 더하고, 그 코드가 있는 파일 이름을 "자리" 열에 적는다. 편차가 사라지면 줄을 지우지 않고 "상태" 를 "닫힘" 으로 바꾼다.

MISRA 규칙 번호는 2023 원문과 맞춰 보지 않은 것이 있다. 확인한 번호만 "확인" 으로 적는다.

| # | 수칙 / 규칙 | 편차 | 이유 | 완화 | 자리 | 상태 |
|---|---|---|---|---|---|---|
| DV1 | MISRA 21.6.1 (미확인), 수칙 1.1 | `create*` 의 `new (std::nothrow)`, EventListener 의 완료 배열 | 메모리를 보장하는 시점이 create 다. 초기화 단계에서만 할당한다 | run 중 할당 0. 실패는 `KIOTTY_NET_CODE_OUT_OF_MEMORY` | `context/owner`, `infra/event/backend` | 열림 |
| DV2 | 수칙 1.1 | OpenSSL 의 자기 malloc | OpenSSL 내부 할당은 막을 수 없다 (설계 v4 19.5) | 연결당 상한 (128 KiB) 을 테스트 `TlsMemory` 가 본다 | `infra/tls/backend/openssl_*` | 열림 |
| DV3 | 수칙 1.10, 3.2 | backend 의 raw pointer: OVERLAPPED 의 owner, epoll `data.ptr`, ready 목록의 `next`, IoEvent 의 listener 포인터 | OS 가 주소를 들고 있어야 한다 | backend 파일 안에만 둔다. 수칙 2.3 의 `reinterpret_cast` 예외와 같은 범위 | `infra/*/backend` | 열림 |
| DV4 | 수칙 5.6 | `SocketCode` 는 공개 enum 이 아닌 내부 enum 이다 | WOULD_BLOCK / PENDING 같은 I/O 진행 상태를 담는다. 에러 enum 이 아니다 | 경계를 넘을 때 `toNetCode` 로 공개 `KiottyNetCode` 가 된다 | `infra/error` | 열림 |
| DV5 | 수칙 4.14 | `KIOTTY_NET_USE_TLS` 로 tls backend 파일을 고르는 `#if` | 소스가 backend 를 고른다는 빌드 규칙 (CMake 는 OS 로 소스를 나누지 않는다) | tls backend 두 파일에만 | `infra/tls/backend` | 열림 |
| DV6 | 수칙 4.16 | `ReceiveFlow::machineState / setMachineState` 쌍 (옛 `Connection::state / setState`) | StateMachine 이 쓰는 int32 상태 값이다. 6단계에서 `ConnectionState` 와 이름이 겹치지 않게 바꾸고, Connection 의 public 7 개 한계 때문에 받는 맥락 `ReceiveFlow` 로 옮겼다 | 값은 `std::atomic<int32_t>` 하나. initialize / reset 이 0 으로 돌린다 | `repository/connection` | 열림 |
| DV7 | 수칙 1.x (static 금지) | Context 하나를 process 에 static 으로 둔다 | 설계 v4 13절. process 에 하나 | `Context::open` 한 자리에서만 만든다 | `context/` | 열림 |
| DV8 | 수칙 2.9 | 테스트 타깃은 예외를 켠다 | GoogleTest 가 예외로 실패를 보고한다 | `-fno-exceptions` (MSVC `/EHs-c- _HAS_EXCEPTIONS=0`) 는 라이브러리 타깃 (`kiotty_net`) 에만. RTTI 는 둘 다 끈다 (gcc / clang 테스트도 `-fno-rtti`. 라이브러리의 다형 타입을 테스트가 상속하므로 맞춰야 링크된다) | `tests/CMakeLists.txt` | 열림 |
| DV9 | 수칙 4.1 (public 메서드 7 개) | 한계를 클래스마다 세되, 구현하는 인터페이스의 메서드는 빼고 센다. `Connection` 은 자기 메서드 7 개와 `IoSource` override 7 개 | 사용자 결정 (2026-11-07). 인터페이스의 메서드는 그 인터페이스가 정한 계약이라 클래스가 줄일 수 없다 | 인터페이스 자체 (`IoSource`) 도 7 개 이하 | `repository/connection` | 열림 |

# 2026-10-29 진행 정리 : base 설계 확정, 테스트 계획, 사이클 0

기준 문서: `docs/design/design_base_v2.md` (설계), `docs/test/test_base_v0.html` (테스트 계획), `docs/plan/plan_base_v0.md` (사이클).

## 오늘 결정된 것

- base 설계 v1 검토의 열 답과 남은 결정 다섯을 모두 확정했다 (`design_base_v1.html` 0절 표). 이름: `TlsEngine`, `StageSet`, `Stage` 그대로, `IoEvent::pending()`, `NotificationContext::queued`. cancel 없음. `onConnected(app, info)` 에 handle 없음. `Phase::IDLE` 이 빈 slot. `extend()` 가 TIMEOUT 의 걸린 경우.
- 테스트 라이브러리는 GoogleTest 1.15.2 + Google Benchmark 1.9.1. 이름은 `TEST(모듈, 문장PascalCase)`. 층은 unit (소켓 없음) / integration (loopback) / benchmark (ctest 밖).
- 한 사이클 = 구현 → 로컬 (Windows MSVC + WSL Ubuntu-24.04) → commit → push → CI 매트릭스 초록.

## 사이클 0 결과

- `CMakeLists.txt`, `tests/{unit,integration,benchmark}`, `.github/workflows/ci.yml`, `.claude/skills/build/SKILL.md`.
- CI 8 job 초록: msvc, gcc, clang, apple-clang, clang ASan+UBSan, clang TSan, android-arm64 (빌드만), ios-arm64 (빌드만). commit `0e45ee5`.
- 고친 것: `kiotty_net_statemachine.cpp` 가 헤더보다 오래됐었다 (`KIOTTY_SAME_STATE`, `stayInCurrentState`). Apple clang 이 GoogleTest 의 `[[maybe_unused]]` 를 C++14 + `-Wpedantic -Werror` 에서 거부해 테스트 타깃만 `-Wno-c++17-attribute-extensions` 를 `-Wpedantic` 뒤에 둔다 (라이브러리는 그대로).
- 로컬 도구: Windows 는 VS 18 Insiders 의 CMake 4.3.1 + Ninja (`vcvars64.bat` 뒤), WSL 은 cmake 3.28 / gcc 13 / clang 18. `libssl-dev` 는 아직 없다 (사이클 4 전에 설치).

## 발견

- `src/implementation/core/kiotty_stage.h` 는 빈 파일이다. Stage 는 사이클 2 에서 짠다.
- `src/kiotty_net_base.cpp`, `kiotty_net_http.cpp`, `kiotty_net_websocket.cpp` 도 빈 파일이라 CMake 소스 목록에 넣지 않았다. 내용이 생기는 사이클에 넣는다.

## 다음 할 일

사이클 1: `include/kiotty_net_base.h` 를 설계 v2 3절대로 고치고 `kiotty_net_statemachine.h` / `.cpp` 를 `KiottyNetConnectionAction` 으로 맞춘다. http.h / websocket.h 가 바뀐 타입을 쓰면 같이. 테스트는 헤더 컴파일 검사 (C++ 와 C 파일 둘을 unit 타깃에). requirement_v0.md 5 ~ 7절도 같은 commit 에서 맞춘다.

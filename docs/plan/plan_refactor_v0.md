# 개발 수칙 v2 리팩터링 계획 v0

2026-11-07. 기준은 `docs/requirement/development_rule.md` (개발 수칙 v2) 다. 지금까지의 코드 (사이클 0 ~ 5a, commit `71e151b`) 를 수칙에 맞게 고치고, 바뀐 결과를 `docs/design/design_base_v5.html` 에 정리한다. `plan_base_v2.md` 의 5b 이후는 이 계획이 끝난 뒤 새 구조 위에서 이어 간다.

## 0. 진행 방식

- 한 단계 = 한 세션 = 한 commit. 단계마다 새 세션에서 이 문서만 읽고 시작한다. 이전 대화의 맥락에 기대지 않는다.
- 단계가 끝나면 이 문서 8절의 "상태" 열과 "남은 것" 을 고친다.
- 각 단계는 그 단계가 옮긴 코드의 테스트도 함께 옮긴다. 단계 끝에 MSVC Debug (TLS ON / OFF) 와 WSL gcc (TLS ON) 가 초록이어야 commit 한다. 전체 매트릭스는 7단계에서 한 번 돈다.
- 옛 파일과 새 파일은 단계 사이에 함께 있을 수 있다. 옛 파일은 그것을 쓰는 코드가 모두 옮겨진 단계에서 지운다. CMake 의 소스 목록은 단계마다 맞춘다.
- WSL 배포판 이름은 이 머신에서 `Ubuntu` 다. 저장소 경로는 `C:\repository\private\kiotty-net` 이다.

## 1. 확정한 결정 (2026-11-07)

| # | 주제 | 결정 |
|---|---|---|
| D1 | 언어 표준 | C++17 로 옮긴다. `CMAKE_CXX_STANDARD 17`, `REQUIRED ON`, `EXTENSIONS OFF`. 라이브러리와 테스트 모두. MSVC 의 C++14 분기를 없앤다. `.claude/agents/cpp-developer.md` 와 `.claude/skills/build/SKILL.md` 의 C++11 문구도 고친다 |
| D2 | 인자 4개 한계 | 넘는 함수는 인자를 struct 로 묶는다. `IoLoop::run / step / stepLocalEvents / access / shutdown` 의 (host, listener, acceptor, profile, handler) 는 `LoopParts` 같은 참조 묶음 하나로. 묶음 struct 는 참조 멤버만 갖고 run 의 stack 에 산다 |
| D3 | 매크로 금지 | 메모리 상한은 CMake `configure_file` 이 만드는 헤더 (`kiotty_memory_limits_configuration.h`) 의 `inline constexpr` 상수로 받는다. 소스는 `KIOTTY_NET_MAX_*` 매크로를 읽지 않는다. 플랫폼 선택 `#if defined(_WIN32)` 류와 `#pragma once` 만 남긴다 |
| D4 | 설계 문서의 주석 | 설계 문서 (HTML / md) 의 코드 블록 안 주석은 검사 대상이 아니다. 소스에만 수칙 4.9 를 적용한다 |
| D5 | 세부 해석 | 아래 2절의 표를 따른다 |
| D6 | 파일 이름 | 지금의 `kiotty_xxx.h / .cpp` 를 유지한다. 수칙 4.5 를 `<organization>_snake_case(클래스명).h / .cpp` 로 고쳤다 (2026-11-07). 6절의 초안 (`result.hpp` 등) 은 옮길 때 `kiotty_result.h` 처럼 이름을 바꾼다 |
| D7 | 공개 API 이름 (Q1) | 이번 리팩터링에서 수칙 5.2 로 바꾼다. 단계 2 |
| D8 | Q2 ~ Q7 | 3절의 추천대로 |

## 2. 수칙을 코드에 옮기는 방법

| 수칙 | 지금 | 바꿀 모양 |
|---|---|---|
| 4.5 namespace | `kiotty` (core), `kiotty::net` | 전부 `kiotty::net::implementation` |
| 4.5 파일 | `kiotty_xxx.h/.cpp`, include guard | 이름은 그대로 `kiotty_snake_case(클래스명).h/.cpp` (D6). 내부 헤더는 `#pragma once`, 파일 하나 = 클래스 하나. 공개 C 헤더 `include/*.h` 는 include guard 를 남긴다 |
| 4.5 이름 | `timeout_ms`, `_begin_pos`, `recv` | 단위와 단어를 다 쓴다: `timeout_milliseconds`, `_begin_position`, `receive`. 공개 헤더도 (D7, 2.3) |
| 5.1 Result | `Result<Code, Value>`, `isOk()`, `code()`, `value()` | `Result<Value, Code>` struct. `code`, `value` 공개 필드, `explicit operator bool`, `success / failure`. `Result<Value&, Code>` 는 `value()` 하나만 함수 |
| 2.2 Span | `Span { data, length }` 에 begin / end / [] | `skip`, `fill`, `copyFrom` 을 더한다. 직접 `memcpy / memset` 은 `Span::copyFrom / fill` 로 |
| 2.4 정수 | 덧셈 검사 없음 | `core/kiotty_checked.h` 의 `checkedAdd / checkedMultiply`. 외부 길이의 덧셈은 이것으로 |
| 1.3 Handle 조회 | `ReusablePool::find` 가 포인터 | `get(PoolHandle)` 가 `Result<T&, Code>` (`INVALID_HANDLE` 대신 지금 있는 `KIOTTY_PEER_CLOSED` 등으로, 3절 Q3) |
| 4.15 템플릿은 core 만 | `ConnectionHost<N>` 이 template | `ReusablePoolBase<T>` (N 없는 base, Span 셋을 든다) 를 core 에 두고, `ConnectionHost` 는 구체 클래스가 `ReusablePoolBase<Connection>&` 를 받는다. N 을 아는 것은 Owner 의 멤버 `ReusablePool<Connection, N>` 하나 |
| 4.6 출력 인자 | `sendAll(..., size_t& sent)`, `sendFile(..., uint64_t& sent)` | 보낸 길이와 code 를 담은 `Result` 를 돌려준다 |
| 4.6 bool 인자 | `setNoDelay(bool)`, `stop_when_empty` | `enum class NoDelay`, `enum class StopPolicy` |
| 2.8 switch | `default:` 있는 switch 몇 곳 (epoll `checkRequest`, `progress`, `kindOf`, iocp `start`, `toNetCode`) | 모든 enum 멤버를 적고 default 를 지운다 |
| 1.9 초기화 | `T x;` 뒤 memset | `T x{};` |
| 4.3 / 4.4 | 여러 줄 if 블록, 조건식 안 호출 | 에러 검사는 `if (!result) { return ...; }` 한 줄. 호출 결과는 const 지역 변수로 받은 뒤 검사 |
| 4.1 한계 | 100 자 넘는 줄 다수, 30 줄 넘는 함수 (`drive`, `decode`, `handshake`, `writeFile`, epoll `poll` 등), public 메서드 30 개 넘는 `Connection` | 함수는 보조 함수로 나누고 익명 namespace 에 둔다. 클래스는 4.4 의 분할 |
| 2.3 금지 함수 | `strlen` (openssl backend, socket backend), 직접 `memcpy` | `strnlen` + 상한, `Span::copyFrom` |
| 2.9 빌드 옵션 | `-Wall -Wextra -Wpedantic -Werror`, `/W4 /WX` | 더한다: `-Wconversion -Wsign-compare -Wshadow -Wswitch -fno-rtti -fstack-protector-strong -D_FORTIFY_SOURCE=2` (Release). `-fno-exceptions` 는 3절 Q4. MSVC 는 `/GR-` 와 `/sdl` |
| 3.4 배치 | 순서 무관 | 멤버를 크기 내림차순으로. 새 struct 와 값 struct 에 `static_assert(sizeof)` |
| 0 편차 | 없음 | `docs/deviations.md` 를 만든다 (5절) |

### 2.1 디렉토리 (수칙 7.1)

```
src/implementation/
    core/                 result, span, checked, stage, stage_set, clock, deadline, parker,
                          pool/ (pool_handle, reusable, reusable_pool, lazy_reusable_pool)
    domain/entity/        io_action, io_completion, io_payload, socket_code, phase, tls_step,
                          connection_limits, handler, connection_configuration
    domain/usecases/      io_loop/ (6 사이클 뒤. 이번에는 자리만)
    data/datasource/      memory_limits (생성 헤더를 감싼다), memory_layout
    data/infra/           error/, socket/, event/, tls/, file/ (InputFile, sendFile). 각자 backend/
    data/repository/      connection/ (Connection 과 그 조각, WorkContext, Sender, IoSource, ConnectionHost),
                          stream/ (ConnectionStream)
    context/              owner (KiottyNetConnectionOwner)
```

옛 위치와 새 위치 (표와 위 그림은 `kiotty_` 접두사와 `.h / .cpp` 를 줄여 적었다. 실제 파일은 D6 대로 `kiotty_active_socket.h` 다):

| 옛 | 새 |
|---|---|
| `core/kiotty_result.h`, `kiotty_span.h`, `kiotty_stage*.h/.cpp`, `kiotty_clock.h/.cpp`, `kiotty_parker.h`, `core/pool/*` | `core/` 같은 이름 (`clock` 에서 `Deadline` 을 `deadline` 으로 분리) |
| `base/event/kiotty_io_action.h/.cpp`, `kiotty_event.h` | `domain/entity/io_action`, `io_completion`, `io_payload` |
| `base/error/*` | `domain/entity/kiotty_socket_code.h` (enum, toNetCode) + `data/infra/error/` (OS 표) |
| `base/pool/kiotty_memory_limits.h`, `kiotty_memory_layout.h` | `data/datasource/kiotty_memory_limits.h`, `kiotty_memory_layout.h` |
| `base/socket/*` | `data/infra/socket/` (`active_socket`, `passive_socket`, `socket_transfer`, `connect_with_retry`, backend 는 `posix_* / windows_*` 로 클래스별 분리) |
| `base/event/kiotty_event_listener.h`, `backend/*` | `data/infra/event/kiotty_event_listener.h`, `backend/kiotty_{iocp,epoll,no}_event.h`, `..._event_listener.cpp` |
| `base/tls/*` | `data/infra/tls/tls_profile`, `tls_engine` (4.4 분할), `backend/openssl_*`, `stub_*` |
| `base/stream/kiotty_input_file.h`, `backend/*_file.cpp` | `data/infra/file/input_file`, `send_file`, backend |
| `base/stream/kiotty_connection_stream.*` | `data/repository/stream/connection_stream` |
| `base/connection/*` | `data/repository/connection/` |
| `base/context/kiotty_owner.*` | `context/owner` |
| `base/io_loop/kiotty_io_loop.h` (빈 파일) | 지운다 |

### 2.2 큰 클래스 나누기 (수칙 4.1 의 public 7 개)

6단계 시작 때 이 표를 확정한다. 나누는 기준은 "맥락은 맥락끼리" (설계 v4 방침 1) 다.

| 클래스 | 지금 public | 나누는 안 |
|---|---|---|
| `Connection` | 약 35 | `Connection` (수명: initialize / reset / dispatch / pickWrite / close 와 IoSource), 받는 맥락 `ReceiveFlow` (drive / refill / satisfied), 보내는 맥락 `SendFlow` (select / encoded / resolve), Work 맥락 `WorkFlow` (verify / stream / closeStream). 상태 읽기 (deadline, workReady 등) 는 `ConnectionState` 값 하나를 돌려주는 메서드로 모은다 |
| `TlsEngine` | 약 17 | `TlsEngine` (open / close / handshake / decode / encode) 와 동기 통로용 `TlsPipe` (write / receiveRoom / received / outgoing / sent) |
| `ConnectionHost` | 약 13 | IO thread 쪽 (acquire / release / at / capacity / notificationMutex) 과 아무 thread 쪽 `NotificationDesk` (notify / broadcast / info / working / closeStream) |
| `ActiveSocket` | 13 | 끝남 (초안): RAII 7 개 + 자유 함수 `send / receive / peek / waitReadable / waitWritable / shutdownWrite / configureNoDelay` |

### 2.3 공개 API 이름 (수칙 5.2, D7)

함수 `KiottyNet_xxx` 와 타입 `KiottyNetXxx` 는 이미 접두사가 맞다. 바꿀 것은 셋이다.

| 대상 | 지금 | 바꿀 모양 |
|---|---|---|
| enum 멤버 | `KIOTTY_SUCCESS`, `KIOTTY_ACTION_READ`, `KIOTTY_HTTP_GET`, `KIOTTY_CLIENT_AUTH_NONE`, `KIOTTY_TLS13_AES_128_GCM_SHA256` 등 | `KIOTTY_NET_<TYPE>_<MEMBER>`. 예: `KIOTTY_NET_CODE_SUCCESS`, `KIOTTY_NET_CONNECTION_ACTION_READ`, `KIOTTY_NET_HTTP_METHOD_GET`. TYPE 은 enum 타입 이름에서 `KiottyNet` 을 뺀 UPPER_SNAKE |
| 상수 매크로 | `KIOTTY_MAX_URL_LENGTH`, `KIOTTY_ANY_STATE`, `KIOTTY_MAX_NEXT` | `KIOTTY_NET_MAX_URL_LENGTH` 처럼 `KIOTTY_NET_` 접두사 |
| 약어 | 타입 `...Config`, `...ClientAuth`, 필드 `timeout_ms` 등 `_ms` 11 종, `recv_capacity` | `...Configuration`, `...ClientAuthentication`, `_milliseconds`, `receive_capacity`. 함수 이름 안의 `ConfigBuilder` 도 `ConfigurationBuilder` |

방법: 옛 이름 → 새 이름 표 (`tools/rename_public_api.tsv`) 를 먼저 만들어 사용자에게 보이고, 확인 받은 뒤 그 표로 `include/`, `src/`, `tests/`, `docs/requirement/*.md` 를 sed 로 한 번에 바꾼다. enum 값의 번호는 바꾸지 않는다. design 문서의 옛 판 (`design_base_v0 ~ v4`, feedback) 은 기록이므로 두고, v5 만 새 이름을 쓴다.

## 3. 정한 것 (2026-11-07 답)

Q1 은 추천과 다르게 "이번에 바꾼다" 로 정했다 (D7). Q2 ~ Q7 은 추천대로다 (D8).

| # | 질문 | 결정 | 적용 단계 |
|---|---|---|---|
| Q1 | 공개 헤더 (`include/*.h`) 의 이름도 수칙 5.2 로 바꾸나 | 바꾼다. 2.3 | 2 |
| Q2 | `SocketCode` 는 수칙 5.6 의 "새 에러 enum" 인가 | 아니다. WOULD_BLOCK / PENDING 같은 I/O 진행 상태를 담는 내부 상태 enum 이고, 경계를 넘을 때 `toNetCode` 로 공개 enum 이 된다. `deviations.md` 에 적는다 | 4 |
| Q3 | 풀의 handle 실패 code | 새 값을 만들지 않는다. 호출자가 정하는 공개 값 (`KIOTTY_NET_CODE_PEER_CLOSED` 등) 을 T 의 `INVALID_HANDLE` 상수로 둔다 | 1 |
| Q4 | `-fno-exceptions` | 라이브러리 target 에만. 할당은 이미 `new (std::nothrow)` 라 `bad_alloc` 을 잡을 자리가 없다. MSVC 는 `/EHs-c- /D_HAS_EXCEPTIONS=0`. GoogleTest 는 예외를 켠 채로 둔다. 표준 라이브러리 헤더 경고가 나면 그 줄만 편차로 | 1 |
| Q5 | backend 의 raw pointer (OVERLAPPED owner, epoll `data.ptr`, ready 목록의 `next`, IoEvent 의 listener 포인터) | OS 가 주소를 들고 있어야 하므로 `infra/*/backend` 의 예외로 `deviations.md` 에 적는다 (수칙 2.3 의 `reinterpret_cast` 예외와 같은 범위). epoll ready 목록은 그대로 | 5 |
| Q6 | `Connection::state() / setState()` 쌍 (수칙 4.16) | StateMachine 이 쓰는 값이라 남긴다. 이유를 PR 에 적는다 (수칙 9 의 13) | 6 |
| Q7 | 테스트 코드에 수칙 4.1 (30 줄, 100 자) 을 적용하나 | 이름과 namespace 만 적용하고 길이 한계는 적용하지 않는다 | 1 |

## 4. 단계

| 단계 | 범위 | 테스트 | commit |
|---|---|---|---|
| 0 | 이 문서. 수칙 4.5 의 파일 이름 (D6). 메모리의 MISRA 노트를 C++17 로 | 없음 | `docs: refactor plan v0 for development rule v2` |
| 1 | CMake C++17 와 경고 / 보안 옵션 (2.9), `configure_file` 상한 헤더 (D3), `core/` 전부 (Result 순서와 필드화 포함), `docs/deviations.md` 첫판, agent / build skill 문서. 6절의 core 초안을 옛 `core/kiotty_*.h` 자리에 덮어쓴다 (이름이 같으므로 include 경로는 거의 그대로). 이 단계에서 **모든 namespace 와 Result 호출을 새 것으로 바꾼다** (기계적 치환. 2.1 의 다른 폴더 이동은 하지 않는다) | core 테스트 전부 (`result`, `result_ref`, `span` + copyFrom, `stage`, `stage_set`, `clock`, `parker`, `pool_handle`, `reusable_pool`, `lazy_reusable_pool`) 를 새 API 로. 새로: `Checked`, `Span::copyFrom` 이 짧은 목적지를 거부 | `core: C++17, kiotty::net::implementation, Result<Value, Code>, Span copyFrom, checked arithmetic` |
| 2 | 공개 API 이름 (2.3, D7). 이름 표를 먼저 보이고 확인 받는다. `include/`, `src/`, `tests/`, `docs/requirement/` 를 표로 치환. 6절 초안도 같이 | 기존 테스트 전부가 새 이름으로 초록 | `api: public names under rule 5.2 (KIOTTY_NET_<TYPE>_<MEMBER>, no abbreviations)` |
| 3 | `domain/entity/`, `data/datasource/`. IoAction / IoCompletion 의 배치 (3.4), static_assert | `io_action`, `memory_layout` | `domain: entities and memory limits under the rule v2 layout` |
| 4 | `data/infra/error`, `data/infra/socket` (초안 재사용). NoDelay enum, 출력 인자 제거 | `socket_error`, integration `active_socket`, `passive_socket`, `connect_with_retry` | `infra: socket and error split per class, free transfer functions` |
| 5 | `data/infra/event`, `tls` (TlsEngine 분할), `file`. backend 함수 30 줄 맞춤, switch default 제거 | `tls`, integration `event_listener` | `infra: event, tls and file backends under rule v2` |
| 6 | `data/repository/connection`, `stream`, `context/owner`. 2.2 의 분할 확정 후 적용. ConnectionHost 비 template | `sender`, `work_context`, `connection`, `connection_host`, integration `connection_stream`, test support | `repository: connection split by context, non-template ConnectionHost` |
| 7 | 옛 파일 삭제, 수칙 9절 체크리스트 15 항목을 전체에 한 번, 로컬 매트릭스 일곱 벌, push, CI | 전부 | `refactor: remove the old layout, checklist pass` |
| 8 | `docs/design/design_base_v5.html`: v4 의 내용을 새 이름 / 폴더 / 분할 / 공개 API 이름으로 다시 쓴 판. 수칙과 v4 가 달라진 곳의 표, deviations 요약 | 없음 | `docs: design base v5 after the rule v2 refactor` |

단계 1 과 2 가 가장 넓다 (모든 파일). 토큰을 아끼려면 치환은 sed 스크립트로 하고, 파일을 하나씩 읽지 않는다. Result 의 `isOk()` → `operator bool`, `code()` → `code`, `value()` → `value` (참조 특수화는 `value()` 유지), 템플릿 인자 순서 바꿈은 grep 으로 자리를 모은 뒤 손으로 고친다. 단계 2 의 치환은 긴 이름부터 (`KIOTTY_TLS13_...` 가 `KIOTTY_TLS` 보다 먼저) 하고, 단어 경계 (`\b`) 를 건다.

## 5. `docs/deviations.md` 에 처음 적을 것

| 수칙 | 편차 | 이유 | 완화 |
|---|---|---|---|
| MISRA 21.6.1, 수칙 1.1 | `create*` 의 `new (std::nothrow)`, listener 의 완료 배열 | 초기화 단계에서만, 메모리 보장 시점이 create | run 중 할당 0. 실패는 `KIOTTY_NET_CODE_OUT_OF_MEMORY` |
| 수칙 1.1 | OpenSSL 의 자기 malloc | OpenSSL 내부 할당은 막을 수 없다 (설계 v4 19.5) | 연결당 상한을 테스트가 본다 (128 KiB) |
| 수칙 1.10, 3.2 | backend 의 raw pointer 와 epoll ready 목록 | OS 가 주소를 들고 있다 | backend 파일 안에만 |
| 수칙 5.6 | `SocketCode` | Q2 | 경계에서 `toNetCode` |
| 수칙 4.14 | `KIOTTY_NET_USE_TLS` 로 tls backend 파일을 고르는 `#if` | 소스가 backend 를 고른다는 빌드 규칙 | tls backend 두 파일에만 |
| 수칙 4.16 | `Connection::state / setState` | Q6 | |
| static Context | 설계 v4 13절 | process 에 하나 | `Context::open` 한 자리 |

## 6. 이미 쓴 초안 (2026-11-07, 컴파일하지 않음, CMake 에 없음)

새 세션은 이것을 버리지 말고 1, 3, 4 단계의 출발점으로 쓴다. 각 단계에서 다시 읽고 빌드로 확인한다.

초안은 D6 이전에 써서 이름이 `result.hpp` 꼴이다. 옮길 때 `kiotty_result.h` 꼴로 바꾸고 초안 안의 `#include "....hpp"` 도 같이 고친다 (sed 한 줄: `s#\([a-z_/]*/\)\?\([a-z_]*\)\.hpp#\1kiotty_\2.h#`). core 초안은 같은 이름의 옛 파일을 덮어쓴다. 초안의 공개 enum 이름은 옛 이름 (`KIOTTY_SUCCESS`) 이므로 단계 2 의 치환에 포함한다.

- `core/`: `result.hpp`, `span.hpp` (skip / fill / copyFrom), `checked.hpp` (checkedAdd 만. checkedMultiply 를 더할 것), `stage.hpp/.cpp`, `stage_set.hpp`, `clock.hpp/.cpp`, `deadline.hpp/.cpp`, `parker.hpp`, `pool/pool_handle.hpp`, `pool/reusable.hpp` (`std::void_t`), `pool/reusable_pool.hpp` (`ReusablePoolBase<T>` + `ReusablePool<T, N>`, `get` 이 Result, `T::INVALID_HANDLE` 필요), `pool/lazy_reusable_pool.hpp` (`find` 제거됨. Context 가 필요하면 `get` 을 더한다)
- `domain/entity/`: `io_action.hpp/.cpp`, `io_completion.hpp`, `io_payload.hpp`, `socket_code.hpp`, `phase.hpp`, `tls_step.hpp`, `connection_limits.hpp`, `handler.hpp`
- `data/datasource/`: `memory_limits.hpp` (아직 매크로를 읽는다. D3 의 생성 헤더로 바꿀 것), `memory_layout.hpp`
- `data/infra/error/`: `socket_error.hpp/.cpp`, `backend/posix_socket_error.cpp`, `backend/windows_socket_error.cpp`
- `data/infra/socket/`: `socket_handle.hpp`, `active_socket.hpp`, `passive_socket.hpp`, `socket_transfer.hpp`, `connect_with_retry.hpp/.cpp`, `backend/{posix,windows}_{address.hpp/.cpp, active_socket.cpp, passive_socket.cpp, socket_transfer.cpp}`

알려진 문제: 줄 길이 100 자를 넘는 줄이 남아 있다. `IoAction` 의 `static_assert(sizeof)` 값은 플랫폼별로 확인하지 않았다. `ReusablePoolBase` 의 `static_cast<code_type>(T::INVALID_HANDLE)` 는 Connection 에 그 상수가 아직 없다.

## 7. 끝난 뒤

- `plan_base_v2.md` 5b 부터 이어 간다. 5b 의 Acceptor 는 `data/repository/connection/kiotty_acceptor.h` 다.
- `plan_base_v2.md` 와 진행 문서 속 옛 공개 이름은 고치지 않는다 (기록). 새 문서부터 새 이름.

## 8. 상태

1단계에서 한 것 (2026-11-07):

- 모든 초안 (`core`, `domain`, `data`) 을 `kiotty_*.h / .cpp` 로 이름을 바꿨다. core 의 옛 파일은 지웠다. `domain/`, `data/infra/` 초안은 아직 CMake 에 없다.
- `base/pool/kiotty_memory_limits.h` 를 지우고 `data/datasource/kiotty_memory_limits.h` 로 옮겼다. 이 헤더는 생성 헤더 `kiotty_memory_limits_configuration.h` (`cmake/*.h.in`, `<build>/generated/`) 의 `CONFIGURED_*` 를 읽는다. CMake 캐시 이름 `KIOTTY_NET_MAX_RECV_CAPACITY` 는 `KIOTTY_NET_MAX_RECEIVE_CAPACITY` 가 됐다. 테스트는 기대값을 `KIOTTY_TEST_EXPECTED_*` 정의로 받는다.
- `ReusablePool` 은 `ReusablePoolBase<T>` 위에 선다. `find` 대신 `get` (Result). `full / empty` 를 지웠다 (`count / capacity`). `Connection::INVALID_HANDLE = KIOTTY_PEER_CLOSED`. `LazyReusablePool` 에는 `get` 이 없다 (쓰는 곳이 없다).
- 테스트 타깃도 gcc / clang 에서 `-fno-rtti` (라이브러리의 다형 타입을 테스트가 상속한다). 예외는 테스트에서만 켠다 (deviations DV8).
- 확인: MSVC Debug TLS OFF 303, TLS ON 303, WSL gcc Debug TLS ON 304 모두 초록. clang 과 나머지 매트릭스는 7단계.

| 단계 | 상태 | 남은 것 |
|---|---|---|
| 0 | 끝 | |
| 1 | 끝 | 아래 "1단계에서 한 것" |
| 2 | 시작 안 함 | 이름 표 만들고 확인 받기 |
| 3 | 초안 일부 | ConnectionConfiguration, 테스트 |
| 4 | 초안 | 빌드와 테스트 |
| 5 | 시작 안 함 | |
| 6 | 시작 안 함 | 2.2 분할 확정 |
| 7 | 시작 안 함 | |
| 8 | 시작 안 함 | |

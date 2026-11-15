# kiotty-net 사용자 가이드 : 단일 헤더

`single_header/kiotty_net.hpp` 하나로 라이브러리 전체 (base, StateMachine, HTTP 서버 / 클라이언트, WebSocket) 를 쓴다. stb 방식이다: 어디서나 include 하면 공개 API 가 보이고, C++17 파일 **하나** 에서만 구현이 컴파일된다. CMake 로 라이브러리를 빌드하지 않아도 된다.

## 1. 쓰는 법

```cpp
// kiotty_net_impl.cpp : 이 파일에는 이 두 줄만 둔다
#define KIOTTY_NET_IMPLEMENTATION
#include "kiotty_net.hpp"
```

```c
/* 다른 파일 (C99 또는 C++17) */
#include "kiotty_net.hpp"
```

- 구현 파일에는 define 과 include 만 둔다. 그 파일 안에서는 내부 이름 (`kiotty::net::implementation`) 과 전역 별칭 `net` 이 보인다.
- 구현은 C++17 로만 컴파일된다. C 파일에서 `KIOTTY_NET_IMPLEMENTATION` 을 define 하면 `#error`.
- 공개 API 는 C99 에서도 C++17 에서도 같다 (`kiotty_net_*.h` 네 개를 합친 것).

## 2. 켜는 것 (include 전에 define)

| 매크로 | 기본 | 뜻 |
|---|---|---|
| `KIOTTY_NET_USE_TLS=1` | 0 (TLS 없음) | OpenSSL 을 쓴다. OpenSSL include 경로와 libssl / libcrypto 링크가 필요하다. 없으면 TLS 함수는 `KIOTTY_NET_CODE_TLS_NOT_SUPPORTED` |
| `KIOTTY_NET_WITH_IO_URING` | 없음 (epoll) | Linux 에서 io_uring. **값과 상관없이 define 만으로 켜진다** (`=0` 도 켠다). kernel header 5.11 이상, liburing 은 필요 없다 |
| `KIOTTY_NET_WITH_LIBKQUEUE` | 없음 | Linux 에서 libkqueue 로 kqueue backend (시험용) |
| `KIOTTY_NET_SHARED`, `KIOTTY_NET_BUILD` | 없음 | DLL 로 만들거나 쓸 때. 만드는 쪽은 둘 다, 쓰는 쪽은 `KIOTTY_NET_SHARED` 만 |

모든 파일에서 같은 값으로 define 한다 (공개 API 의 모양은 바뀌지 않지만 DLL 매크로는 선언에 걸린다).

## 3. 링크

| 플랫폼 | 링크 |
|---|---|
| Windows | `ws2_32`, `ntdll` (MSVC 는 header 의 `#pragma comment(lib)` 로 자동) |
| Linux / macOS / Android | `-pthread` |
| TLS 를 켰을 때 | `libssl`, `libcrypto` |

## 4. Windows

- `windows.h` 보다 **먼저** include 하거나, `windows.h` 전에 `NOMINMAX` 와 `WIN32_LEAN_AND_MEAN` 을 define 한다. 구현 부분은 둘이 없으면 스스로 define 한다.
- `min` / `max` 가 이미 매크로면 구현 부분이 `#error` 로 멈춘다 (알 수 없는 컴파일 오류 대신).

## 5. 다시 만들기와 이름 바꾸기

```sh
python3 tools/make_single_header.py                       # single_header/kiotty_net.hpp (저장소의 이름)
python3 tools/make_single_header.py --organization Acme --project Link --output out/acme_link.hpp
```

- 이름 바꾸기는 **만들어지는 파일에만** 적용된다. 저장소는 그대로다.
- `--organization` 과 `--project` 는 PascalCase (대문자 하나 + 영문자 / 숫자). 기본은 `Kiotty`, `Net`. 세 모양을 만든다.

| 모양 | 규칙 | 예 |
|---|---|---|
| Pascal | 조직 + 프로젝트 | `KiottyNet` → `AcmeLink` (`KiottyNet_getHttp` → `AcmeLink_getHttp`, `KiottyNet_HttpClient` → `AcmeLink_HttpClient`) |
| UPPER | `조직_프로젝트` 대문자 | `KIOTTY_NET` → `ACME_LINK` (`KIOTTY_NET_IMPLEMENTATION` → `ACME_LINK_IMPLEMENTATION`) |
| lower | 조직, 프로젝트 소문자 | `kiotty::net` → `acme::link` |

- 대소문자만 바꾸므로 이름 안의 대문자는 단어를 나누지 않는다: `MyCo` → `MYCO`, `myco`.
- 소문자 모양이 C++ 키워드거나 `std`, `posix`, `linux`, `unix` 면 거절한다. 조직 이름 `Net` 도 거절한다 (전역 별칭 `net` 과 겹친다).
- 바꾼 뒤 kiotty 이름이 하나라도 남으면 도구가 오류로 멈춘다. OpenSSL 과 googletest 의 이름은 건드리지 않는다.
- `--line-directives` 를 주면 `#line` 을 넣어 컴파일 오류가 저장소의 원래 파일을 가리킨다.
- 파일 머리의 주석에 버전 (`git describe`) 이 들어간다. 저장소에 commit 하지 않은 변경 (CMakeLists, include, src, 도구) 이 있으면 `+modified` 가 붙는다.

## 6. 확인

`tests/single_header/` 가 빌드 때 기본 이름과 `Acme` / `Link` 두 벌을 만들어, 구현 파일 하나 + 구현 없이 include 하는 C++ 파일 둘 + C99 파일 하나로 실행 파일을 만들고 HttpServer / HttpClient 왕복 (pipe 주소) 과 WebSocket echo 를 돌린다. `Acme` 판에는 kiotty 이름이 남지 않았는지 보는 검사가 붙는다. Python 이 없으면 commit 된 `single_header/kiotty_net.hpp` 로 기본 판만 돌린다. commit 된 파일이 지금 소스와 같은지는 `fresh` label 의 테스트가 본다 (기본 matrix 에는 넣지 않았다).

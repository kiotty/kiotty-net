---
name: build
description: kiotty-net 을 로컬 (Windows MSVC, WSL Ubuntu gcc / clang) 에서 configure, build, ctest 하는 방법. 빌드하거나 테스트를 돌리라는 요청이면 이 순서를 따른다.
---

# kiotty-net 빌드

CMake 프로젝트다. 옵션은 `KIOTTY_NET_USE_TLS` (기본 ON. ON 이면 configure 때 openssl-cmake 로 OpenSSL 3.6 static 을 받는다, OFF 면 stub), `KIOTTY_NET_OPENSSL_PREBUILT` (기본 ON. OFF 면 OpenSSL 을 소스에서 빌드, 10 ~ 30 분), `KIOTTY_NET_BUILD_TESTS`, `KIOTTY_NET_BUILD_BENCHMARKS`, `KIOTTY_NET_SANITIZER` (address, thread, undefined 또는 콤마 목록), 메모리 상한 `KIOTTY_NET_MAX_OWNERS` / `_CONNECTIONS_PER_SERVER` / `_RECV_CAPACITY` / `_SEND_CAPACITY` / `_PUSH_CAPACITY` / `_TLS_CONTEXTS` (설계 v4 19.3). 빌드 디렉터리는 `build/<이름>` 이고 `.gitignore` 에 있다. ctest 라벨은 `unit` 과 `integration` 이다.

## Windows (MSVC, IOCP)

cmake 와 ninja 가 PATH 에 없다. VS 18 Insiders 의 `vcvars64.bat` 가 둘을 PATH 에 올린다. PowerShell 도구에서 `.bat` 하나를 만들어 `cmd /c` 로 돌린다 (Git Bash 에서 `cmd /c` 는 `/c` 를 `C:\` 로 바꾸므로 쓰지 않는다).

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\repo\kiotty-net
cmake -B build\msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKIOTTY_NET_BUILD_TESTS=ON -DKIOTTY_NET_USE_TLS=OFF
cmake --build build\msvc
ctest --test-dir build\msvc -L "unit|integration" --output-on-failure
```

TLS 를 켜면 `-DKIOTTY_NET_USE_TLS=ON`. 시스템 OpenSSL 은 쓰지 않는다. prebuilt 는 `build/<이름>/_deps/openssl-cmake-build/prebuilt/<triple>/` 에 한 번만 받는다 (Windows x64 MSVC, Linux x64, Android 만. macOS / iOS 는 `-DKIOTTY_NET_OPENSSL_PREBUILT=OFF` 나 TLS OFF).

## WSL Ubuntu 24.04 (배포판 이름 `Ubuntu-24.04`, gcc 13 / clang 18, epoll)

```bash
wsl -d Ubuntu-24.04 -- bash -lc 'cd /mnt/d/repo/kiotty-net && cmake -B build/wsl-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKIOTTY_NET_BUILD_TESTS=ON -DKIOTTY_NET_USE_TLS=OFF && cmake --build build/wsl-gcc && ctest --test-dir build/wsl-gcc -L "unit|integration" --output-on-failure' | tr -d '\0'
```

clang 은 `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` 와 `build/wsl-clang`. sanitizer 는 `-DKIOTTY_NET_SANITIZER=address,undefined` 또는 `thread`. WSL 의 출력에는 NUL 이 섞이므로 `tr -d '\0'` 를 붙인다. TLS 는 시스템 OpenSSL 없이 ON 이 된다 (openssl-cmake 의 Linux x64 prebuilt). 로컬 matrix 는 gcc / clang × TLS ON / OFF, Release TLS, asan,undefined TLS, thread TLS 일곱 벌이다.

## 규칙

- CMake 는 소스를 운영체제로 나누지 않고 전부 나열한다. backend `.cpp` 는 파일 전체를 `#if defined(_WIN32)` / `#if !defined(_WIN32)` / `#if defined(__linux__)` 로 감싼다. 테스트가 backend 유무를 볼 때는 헤더의 매크로 (`KIOTTY_NET_HAS_EVENT_BACKEND`) 를 쓴다. 링크 라이브러리 (`ws2_32`, `ntdll`) 만 CMake 의 `if(WIN32)` 다.

- WSL 의 `/mnt/d` 에서 openssl-cmake 의 prebuilt 압축 해제 뒤 `file(RENAME)` 이 가끔 실패한다 (DrvFs). 그 빌드 디렉터리를 지우고 다시 configure 하면 된다.

- integration 테스트의 Unix domain socket 경로는 POSIX 에서 `/tmp` 아래다. WSL 의 `/mnt/c` (DrvFs) 는 socket 파일을 만들 수 없다 (NOT_SUPPORTED).
- Git Bash 에서 `wsl -- bash -lc '...'` 로 넘기면 `$변수` 가 사라질 수 있다. 긴 명령은 스크립트 파일로 만들고 `MSYS_NO_PATHCONV=1 wsl -d Ubuntu-24.04 -- bash /mnt/c/.../script.sh` 로 돌린다.

- 경고는 오류다 (`/W4 /WX`, `-Wall -Wextra -Wpedantic -Werror`). 경고를 끄지 말고 코드를 고친다.
- 라이브러리는 gcc / clang 에서 C++11, MSVC 는 C++14 (`/permissive-`). 테스트 타깃만 C++14.
- 한 사이클 (`docs/plan/plan_base_v0.md` 0절) 은 Windows 와 WSL 둘 다 초록이어야 push 한다. push 뒤 `gh run watch` 로 CI 를 본다.
- 헤더만 검사할 때는 memory 의 `header-compile-check` 레시피 (`/Zs`, `-fsyntax-only`).

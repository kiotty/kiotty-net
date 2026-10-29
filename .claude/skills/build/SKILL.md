---
name: build
description: kiotty-net 을 로컬 (Windows MSVC, WSL Ubuntu gcc / clang) 에서 configure, build, ctest 하는 방법. 빌드하거나 테스트를 돌리라는 요청이면 이 순서를 따른다.
---

# kiotty-net 빌드

CMake 프로젝트다. 옵션은 `KIOTTY_NET_USE_TLS` (기본 ON, OpenSSL 3 필요), `KIOTTY_NET_BUILD_TESTS`, `KIOTTY_NET_BUILD_BENCHMARKS`, `KIOTTY_NET_SANITIZER` (address, thread, undefined 또는 콤마 목록). 빌드 디렉터리는 `build/<이름>` 이고 `.gitignore` 에 있다. ctest 라벨은 `unit` 과 `integration` 이다.

## Windows (MSVC, IOCP)

cmake 와 ninja 가 PATH 에 없다. VS 18 Insiders 의 `vcvars64.bat` 가 둘을 PATH 에 올린다. PowerShell 도구에서 `.bat` 하나를 만들어 `cmd /c` 로 돌린다 (Git Bash 에서 `cmd /c` 는 `/c` 를 `C:\` 로 바꾸므로 쓰지 않는다).

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\repo\kiotty-net
cmake -B build\msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKIOTTY_NET_BUILD_TESTS=ON -DKIOTTY_NET_USE_TLS=OFF
cmake --build build\msvc
ctest --test-dir build\msvc -L "unit|integration" --output-on-failure
```

TLS 를 켜면 `-DKIOTTY_NET_USE_TLS=ON -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL"`.

## WSL Ubuntu 24.04 (배포판 이름 `Ubuntu`, gcc 13 / clang 18, epoll)

```bash
wsl -d Ubuntu -- bash -lc 'cd /mnt/c/repository/private/kiotty-net && cmake -B build/wsl-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKIOTTY_NET_BUILD_TESTS=ON -DKIOTTY_NET_USE_TLS=OFF && cmake --build build/wsl-gcc && ctest --test-dir build/wsl-gcc -L "unit|integration" --output-on-failure' | tr -d '\0'
```

clang 은 `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` 와 `build/wsl-clang`. sanitizer 는 `-DKIOTTY_NET_SANITIZER=address,undefined` 또는 `thread`. WSL 의 출력에는 NUL 이 섞이므로 `tr -d '\0'` 를 붙인다. TLS 는 `sudo apt install libssl-dev` 뒤 ON.

## 규칙

- integration 테스트의 Unix domain socket 경로는 POSIX 에서 `/tmp` 아래다. WSL 의 `/mnt/c` (DrvFs) 는 socket 파일을 만들 수 없다 (NOT_SUPPORTED).
- Git Bash 에서 `wsl -- bash -lc '...'` 로 넘기면 `$변수` 가 사라질 수 있다. 긴 명령은 스크립트 파일로 만들고 `MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/.../script.sh` 로 돌린다.

- 경고는 오류다 (`/W4 /WX`, `-Wall -Wextra -Wpedantic -Werror`). 경고를 끄지 말고 코드를 고친다.
- 라이브러리는 gcc / clang 에서 C++11, MSVC 는 C++14 (`/permissive-`). 테스트 타깃만 C++14.
- 한 사이클 (`docs/plan/plan_base_v0.md` 0절) 은 Windows 와 WSL 둘 다 초록이어야 push 한다. push 뒤 `gh run watch` 로 CI 를 본다.
- 헤더만 검사할 때는 memory 의 `header-compile-check` 레시피 (`/Zs`, `-fsyntax-only`).

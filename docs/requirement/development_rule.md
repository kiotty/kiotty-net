# Kiotty 개발 수칙 v2 — 권장안

의료기기 소프트웨어 등급의 C++ 라이브러리를 만들기 위한 규칙이다.
규칙은 "~한다 / ~하지 않는다" 한 문장으로 쓴다. 코드는 형태를 보여줄 때만 붙인다. 예외는 `예외:` 로만 적는다.

## 0. 원칙

모든 규칙은 아래 네 가치 중 하나에 속한다. 규칙끼리 충돌하면 순위가 높은 가치가 이긴다.

| 순위 | 가치 | 한 줄 요약 |
| --- | --- | --- |
| 1 | 메모리 예측 가능성 | 할당은 초기화 때 한 번. 런타임 힙 0. 총량은 설정값으로 계산된다. |
| 2 | 보안 | 외부에서 온 바이트는 검증 함수 하나를 통과해야 내부 타입이 된다. 길이 없는 포인터는 없다. |
| 3 | 지역성 | 함께 읽는 데이터는 붙여 두고, 인덱스 순서로 순회한다. |
| 4 | 가독성 | 함수 30줄, 중첩 2단, 인자 4개. 한 줄에 한 동작. |

참조 표준: MISRA C++:2023, CERT C++, IEC 62304. 편차는 `docs/deviations.md` 에 규칙 번호·이유·완화책으로 기록한다.

### 0.1 언어 표준은 C++17 이다.

- Windows(MSVC), Linux(GCC), macOS(Clang), Android(NDK Clang + libc++) 가 모두 지원하는 가장 낮은 표준이다.
- C++14 이하는 쓰지 않는다. `if constexpr`, `inline` 변수, `[[nodiscard]]` 가 필요하다.
- C++20 이상 기능은 쓰지 않는다.
- CMake 는 `CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF` 로 설정한다.
- 네 플랫폼 툴체인 모두에서 빌드가 통과해야 PR 을 병합한다.

---

## 1. 메모리 예측 가능성

### 1.1 힙 할당은 초기화 함수 안에서만 한다.

- 최대 개수(서버, 클라이언트, 연결, 세션)는 `Configuration` 으로 받아 초기화 때 전부 확보한다.
- 초기화 이후 `new`, `malloc`, `std::vector` 크기 변경, `std::string` 생성은 없다.
- `std::bad_alloc` 은 초기화 함수에서 잡아 `OUT_OF_MEMORY` 로 반환한다. 그 밖의 코드는 `-fno-exceptions` 로 빌드한다.
- 초기화는 단일 스레드에서 한 번 호출한다. 재호출은 `ALREADY_INITIALIZED`.
- 사용자 정의 `operator new/delete`, placement new, 사용자 정의 할당자는 쓰지 않는다.
- 위 항목은 MISRA C++:2023 21.6.1 편차로 문서화한다.

### 1.2 전역 Context 가 모든 인스턴스를 소유하고, 소멸자가 전부 해제한다.

```cpp
struct Context
{
    explicit Context(const Configuration& configuration); // 모든 풀을 여기서 확보
    ~Context() = default;                                  // 멤버 소멸로 전부 해제

    LazyPool<Server> servers;
    LazyPool<Client> clients;
};
```

- 사용 순서: `createContext → create*(context, configuration) → run → release* → releaseContext`.
- `release*` 를 호출하지 않아도 `releaseContext` 하나로 누수 없이 정리된다.

### 1.3 반복 생성·소멸되는 인스턴스는 풀에서 꺼내고, Handle 로만 접근한다.

- 개수가 컴파일 타임에 정해지면 `Pool<Value, N>`, 설정값이면 `LazyPool<Value>` (생성자에서 확보, 이후 크기 불변).
- Handle 은 `{index, generation}`. generation 불일치는 `INVALID_HANDLE`. 슬롯 재사용 시 generation 을 올린다.
- generation·free list 는 값 배열과 분리해 둔다. 값 배열만 연속으로 스캔할 수 있다.

```cpp
template<typename Value>
class LazyPool
{
public:
    explicit LazyPool(size_t capacity);

    Handle acquire();                              // 가득 차면 invalid Handle
    Result<Value&, ErrorCode> get(Handle handle);  // generation 검증
    void release(Handle handle);
    size_t capacity() const;

private:
    std::vector<Value>    _values;       // 초기화 후 크기 불변
    std::vector<uint32_t> _generations;  // _values 와 같은 index
    std::vector<uint32_t> _free_indices;
    size_t                _free_count;
};
```

### 1.4 Entity 는 값 객체다. 포인터 없음, 고정 크기, 1024 바이트 이하.

```cpp
struct Entity
{
    FixedString<32> name;
    FixedString<64> id;
    int32_t         value;
};
static_assert(sizeof(Entity) <= 1024, "Entity must fit on the stack");
```

### 1.5 대용량 데이터와 다른 Entity 참조는 Handle 로 보관한다.

```cpp
struct Handle
{
    uint32_t index;       // INVALID_INDEX 면 비어 있음
    uint16_t generation;
    uint16_t type;        // 어떤 풀의 Handle 인지
};
template<uint16_t Type> struct TypedHandle : Handle { TypedHandle() : Handle{INVALID_INDEX, 0, Type} {} };

using ImageHandle = TypedHandle<HandleType::IMAGE>;  // entity/ 에서 정의

struct Entity
{
    ImageHandle                       image;      // 대용량 데이터
    FixedVector<OtherEntity, MAX_CHILDREN> children; // 직접 보유
    FixedVector<OtherEntityHandle, MAX_LINKS> links; // 참조만 보유
};
```

### 1.6 컨테이너는 `core/` 의 고정 크기 컨테이너만 쓴다.

| 용도 | 사용 | 쓰지 않음 |
| --- | --- | --- |
| 가변 길이 배열 | `FixedVector<T, N>` | `std::vector` (초기화 함수 제외) |
| 문자열 | `FixedString<N>`, `StringView` | `std::string` |
| 키 조회 | `FixedMap<K, V, N>` (정렬 배열 + 이진 탐색) | `std::map`, `std::unordered_map` |
| 바이트 범위 | `Span<T>` = `{pointer, length}` | `T*` 단독 |

### 1.7 스택 지역 버퍼는 1024 바이트 이하다. 더 크면 호출자가 넘긴 작업 Context 의 버퍼를 쓴다.

```cpp
void doSomething(WorkContext& work, int32_t parameter)
{
    Span<char> buffer = work.scratch();  // 초기화 때 확보된 영역
}
```

### 1.8 재귀는 쓰지 않는다. 깊이 제한이 있는 명시적 스택(`FixedVector`)으로 바꾼다.

### 1.9 모든 변수는 선언과 동시에 `{}` 로 초기화한다.

### 1.10 Raw pointer 는 쓰지 않는다.

- 비소유 참조는 `&` 멤버로 두고 생성자에서 받는다.
- 기본 생성자가 필요하면 메서드 인자로 매번 받는다.
- 예외: 스트림 I/O(`Span` 내부), `core/` 자료구조 내부, C interface.

```cpp
class Some
{
public:
    explicit Some(Dependency& dependency) : _dependency(dependency) {}
private:
    Dependency& _dependency;
};
```

---

## 2. 보안

### 2.1 신뢰 경계 밖에서 온 값은 `parse*` 함수 하나를 지나야 내부로 들어온다.

- 경계: C interface, 파일, 소켓, DB, 환경 변수.
- `parse*` 는 `Span<const char>` 를 받아 Entity 를 `Result` 로 돌려준다. 내부 함수는 검증이 끝난 Entity 만 받고 다시 검증하지 않는다.

```cpp
Result<Configuration, ErrorCode> parseConfiguration(Span<const char> input);                     // 경계: 검증
Result<ServerHandle, ErrorCode> createServer(Context& context, const Configuration& configuration); // 내부: 검증 없음
```

### 2.2 포인터는 항상 길이와 함께 다닌다. `Span<T>` 만 넘긴다.

- 모든 인덱스 접근은 범위를 검사하고, 실패는 `Result` 로 돌려준다.
- `operator[]` 는 `length()` 로 끝나는 for 루프 안에서만 쓴다.
- 복사는 `Span::copyFrom` 으로만 한다. 목적지가 짧으면 실패를 반환하며, 잘라서 넣지 않는다.

### 2.3 금지 함수와 대체

| 쓰지 않음 | 대신 |
| --- | --- |
| `strcpy` `strcat` `strlen` `gets` `sprintf` | `FixedString::assign/append`, `Span.length` |
| 비상수 포맷의 `printf` 계열 | 상수 포맷 + `core/format` (`{0}` 치환) |
| `atoi` `atol` `strtol` | `core/parseInteger` → `Result` |
| 직접 `memcpy` `memset` | `Span::copyFrom`, `Span::fill` |
| `new` `delete` `malloc` `free` | 초기화 함수 안의 `std::vector`, `unique_ptr` |
| C 스타일 캐스트, `reinterpret_cast` | `static_cast`. `reinterpret_cast` 는 `infra/*/backend` 만 |
| `rand` | `infra/random` (OS CSPRNG) |

### 2.4 정수는 범위를 검사한 뒤에 쓴다.

- 크기·길이·오프셋은 `size_t`.
- 외부에서 온 정수는 `[min, max]` 검사 후 사용한다. enum 으로 바꿀 때도 같다.
- 덧셈·곱셈은 `core/checked.hpp` 의 `checkedAdd`, `checkedMultiply` 를 쓴다. 넘치면 `Result` 실패.
- 부호가 다른 비교는 컴파일 에러로 막는다 (`-Wsign-compare -Werror`).

### 2.5 외부에는 포인터를 내보내지 않는다. 외부는 Context 와 Handle 만 가진다.

- 모든 Handle 은 풀에서 generation 을 검증한다. 해제 뒤 접근은 `INVALID_HANDLE`.

### 2.6 외부 문자열은 null 종료를 가정하지 않는다.

- 경계에서는 `Span<const char>` 로 받고 UTF-8 유효성을 검사한다.
- `FixedString` 은 항상 null 종료를 보장하고 `assign` 실패 시 비운다.

### 2.7 민감 데이터(키, 토큰, 환자 식별자)는 쓰고 나서 `secureZero` 로 지운다.

- 로그·에러 메시지에 넣지 않는다.
- 비교는 상수 시간 `constantTimeEquals` 를 쓴다.

### 2.8 enum 분기는 `switch` 로 전수 처리하고 `default` 를 두지 않는다.

- 알 수 없는 값은 거부한다 (deny-by-default). `-Wswitch -Werror` 로 누락을 컴파일 에러로 만든다.

```cpp
switch (kind)
{
case Kind::READ:
case Kind::READ_UNTIL:
    break;
case Kind::WRITE:
    write();
    break;
}
```

### 2.9 빌드 옵션

```
-Wall -Wextra -Werror -Wconversion -Wsign-compare -Wshadow -Wswitch
-fno-exceptions -fno-rtti -fstack-protector-strong -D_FORTIFY_SOURCE=2
```

- 테스트 빌드는 ASan + UBSan 을 켠다. `parse*` 함수는 fuzz 테스트를 붙인다.

### 2.10 모든 `parse*` 함수는 다음 다섯 입력을 테스트한다.

빈 입력, 최대 길이, 최대 길이 + 1, 중간 null 바이트, 잘못된 UTF-8.

---

## 3. 지역성

### 3.1 같은 시점에 함께 읽는 필드만 같은 struct 에 둔다.

- 매 루프 읽는 hot 필드와 가끔 읽는 cold 필드(이름, 설명)는 같은 index 를 쓰는 두 배열로 나눈다.

```cpp
struct SessionState { uint32_t last_seen_milliseconds; uint16_t flags; };  // 매 틱 스캔
struct SessionInfo  { FixedString<64> user; FixedString<32> device; };    // 조회 시만

struct SessionRepository
{
    LazyPool<SessionState> states;   // Handle.index 공유
    std::vector<SessionInfo> infos;  // 초기화 때 states.capacity() 로 확보
};
```

### 3.2 포인터로 연결된 자료구조는 쓰지 않는다. 배열 + index 로 만든다.

- 연결 리스트, 포인터 트리, 노드 할당 map 금지. 트리는 `parent_index`, `first_child_index` 배열로 표현한다.

### 3.3 순회는 index 순서로 풀 전체를 훑는다.

- Handle 목록을 따라가며 흩어진 슬롯을 읽지 않는다.
- 다른 풀의 데이터가 필요하면 index 를 먼저 모으고(1차 패스) 그 풀을 순서대로 읽는다(2차 패스).

### 3.4 멤버는 크기 내림차순으로 배치하고 `static_assert(sizeof)` 로 패딩을 고정한다.

```cpp
struct Record
{
    uint64_t id;
    uint32_t count;
    uint16_t flags;
    uint8_t  kind;
    uint8_t  _padding;
};
static_assert(sizeof(Record) == 16, "unexpected padding");
```

### 3.5 다른 스레드가 쓰는 데이터는 `alignas(64)` 로 캐시 라인을 분리한다.

### 3.6 16 바이트 이하의 trivially copyable 타입은 값으로, 나머지는 `const&` 로 넘긴다.

---

## 4. 가독성

### 4.1 수치 한계. 넘으면 나눈다.

| 대상 | 한계 |
| --- | --- |
| 함수 본문 | 30 줄 |
| 중첩 깊이 | 2 단 |
| 인자 | 4 개 |
| 파일 | 400 줄 |
| 한 줄 | 100 자 |
| 클래스 public 메서드 | 7 개 |

- 예외: `core/` 의 템플릿 헤더는 파일 길이 제한을 두지 않는다.

### 4.2 한 함수는 한 추상 수준이다. 흐름 함수는 호출과 에러 검사만 나열한다.

```cpp
using ServerResult = Result<ServerHandle, ErrorCode>;

ServerResult createServer(Context& context, const ServerConfiguration& configuration)
{
    Handle handle = context.servers.acquire();
    if (!handle.valid()) { return ServerResult::failure(ErrorCode::POOL_IS_FULL); }

    auto server = context.servers.get(handle);
    if (!server) { return ServerResult::failure(server.code); }

    ErrorCode code = server.value.initialize(configuration);
    if (code != ErrorCode::SUCCESS) { return ServerResult::failure(code); }

    return ServerResult::success(handle);
}
```

### 4.3 early return 을 쓴다. 에러 검사는 `if (!res) { return ...; }` 한 줄 블록으로 통일한다. `return` 뒤에 `else` 를 두지 않는다.

### 4.4 한 줄에 한 동작이다. 조건식 안에서 대입·호출을 하지 않는다. 선언·계산·호출을 한 줄에 섞지 않는다.

### 4.5 이름

| 대상 | 규칙 | 예 |
| --- | --- | --- |
| 함수 | 동사 + 목적어, camelCase | `openCursor`, `parseConfiguration` |
| bool | `is` / `has` / `can` | `isValid`, `hasChildren` |
| 변수 | snake_case, 단위 포함 | `timeout_milliseconds`, `buffer_length` |
| private 멤버 | `_` 접두 | `_free_count` |
| 상수, enum 멤버 | UPPER_SNAKE | `MAX_SESSIONS`, `POOL_IS_FULL` |
| 클래스, enum, 타입 | PascalCase | `LazyPool`, `ErrorCode` |
| 파일 | snake_case(클래스명), 파일 하나 = 클래스 하나 | `lazy_pool.hpp` |
| namespace | `<organization>::<project>::implementation`. 예: `kiotty::spec::implementation`. 조직·프로젝트 이름은 사용자가 정한다. | |

- 약어를 쓰지 않는다. 이름은 네 단어 이하.

### 4.6 bool 인자를 쓰지 않는다. `enum class` 로 바꾼다. 출력 인자를 쓰지 않는다. 리턴값으로 돌려준다.

### 4.7 `auto` 는 우변에 타입이 보일 때(생성자 호출, `Result` 반환)만 쓴다. 숫자·bool 은 타입을 적는다.

### 4.8 매직 넘버를 쓰지 않는다. `constexpr` 상수에 이름을 붙인다.

### 4.9 작업을 완료하여 Release 를 하기 전까지 주석을 쓰지 않는다.

- Release 준비 단계에서만 "왜" 를 설명하는 주석을 추가한다. "무엇" 은 이름과 함수 분리로 드러낸다.
- `TODO` 를 남기지 않는다. 이슈로 기록한다.
- 이 문서의 코드 예시 주석은 설명용이며 소스에 옮기지 않는다.

### 4.10 읽는 순서 = 호출 순서. 상위 흐름 함수가 위, 보조 함수가 아래.

- 헤더: `public` → `private`.
- 소스: 익명 namespace 에 보조 함수 선언 → 공개 함수 정의 → 보조 함수 정의.

### 4.11 멤버 변수를 쓰는 계산은 멤버를 인자로 받는 보조 함수로 뺀다. 메서드는 멤버를 모아 넘기고 결과를 저장하는 일만 한다.

```cpp
namespace {
int32_t sumWithOffset(int32_t a, int32_t b, int32_t offset) { return a + b + offset; }
}

bool A::doSomething(int32_t a, int32_t b)
{
    _c = sumWithOffset(a, b, _d);
    return _c > 10;
}
```

### 4.12 부정 조건을 피한다. `if (!isEmpty())` 대신 `if (hasItems())`. 이중 부정 금지.

### 4.13 삼항 연산자는 값 선택 한 단만 쓴다. 중첩하지 않는다.

### 4.14 매크로를 쓰지 않는다. 예외: 플랫폼 선택 `#ifdef`, `#pragma once`.

### 4.15 템플릿은 `core/` 에만 둔다. 그 밖에서는 구체 타입을 쓴다.

### 4.16 호출자가 없는 메서드를 만들지 않는다. getter/setter 쌍을 만들지 않는다. 값 객체는 public 필드다.

### 4.17 `const` 가 기본이다. 바뀌는 변수만 non-const 로 둔다.

### 4.18 method 는 그 개념이 할 수 있는 동작을 나타내는 짧은 동사로 쓴다.

- 통상적으로 이해되는 동사를 쓴다. 예: `acquire`, `release`, `open`, `fetch`, `append`, `parse`.
- `getXxx`, `doXxx`, `handleXxx` 처럼 동작을 드러내지 않는 이름을 쓰지 않는다.
- 멤버 변수에 접근하는 method 는 꼭 필요한 경우에만 만든다. 다른 layer 가 값을 읽어야 하고 동작으로 대신할 수 없을 때가 필요한 경우다.
- 예외: `core/` 의 자료구조 클래스는 `length()`, `capacity()` 같은 접근 method 를 쓸 수 있다.

---

## 5. 에러 처리

### 5.1 에러는 `Result<Value, Code>` 리턴값으로만 전달한다. 예외, `errno`, 전역 상태는 쓰지 않는다.

- `Code` 는 `include/` 의 공개 에러 enum 이다. 값 0 이 성공이다.
- 예외, `errno`, 전역 상태는 쓰지 않는다.

```cpp
template<typename Value, typename Code>
struct Result
{
    Code  code;   // 0 이면 성공
    Value value;  // default constructible

    explicit operator bool() const { return code == Code{}; }

    static Result success(Value content) { return Result{Code{}, std::move(content)}; }
    static Result failure(Code error_code) { return Result{error_code, Value{}}; }
};
// Result<Value&, Code> 특수화는 내부적으로 포인터를 보관한다.
```

### 5.6 에러 코드는 `include/` 에서만 정의한다.

- 에러 enum 은 공개 헤더 `include/kiotty_spec.h` 의 하나뿐이다. `core/`, `domain/`, `data/`, `context/` 는 에러 enum 을 새로 만들지 않는다.
- 새 에러가 필요하면 공개 enum 끝에 값을 추가한다. 기존 값의 번호는 바꾸지 않는다.
- 내부 모듈은 공개 enum 값을 그대로 `Result` 와 `failure` 에 넣는다.

### 5.2 C interface 는 `<Organization><Project>_` 접두사를 붙인다.

- 함수: `<Organization><Project>_functionName`. 예: `KiottySpec_createServer`.
- 타입: `<Organization><Project><TypeName>`. 예: `KiottySpecServer`.
- enum 멤버: `<ORGANIZATION>_<PROJECT>_<TYPE>_<MEMBER>`. 예: `KIOTTY_SPEC_STATUS_SUCCESS`. C enum 은 이름 공간이 없으므로 타입 이름을 넣어 충돌을 막는다.
- 매크로: `<ORGANIZATION>_<PROJECT>_<NAME>`. 예: `KIOTTY_SPEC_API`.
- 약어 금지(4.5)는 C interface 에도 적용한다. 예: `Db` 대신 `DATABASE`, `InvalidArg` 대신 `INVALID_ARGUMENT`.
- 조직·프로젝트 이름은 사용자가 정한다. 임의로 정하지 않는다.
- C interface 는 프로젝트의 모든 에러를 덮는 C enum 하나를 가진다.

```c
ServerResult KiottySpec_createServer(GlobalContext* context, const ServerConfiguration configuration);
```

### 5.3 스택에 담기 큰 결과는 Context 안의 멤버 참조를 `Result<Value&, Code>` 로 돌려준다.

### 5.4 스트림(파일, 소켓, USB) 읽기·쓰기만 `Span` 출력 인자를 허용한다.

```cpp
Result<size_t, ErrorCode> read(FileDescriptor file, Span<char> output);
Result<size_t, ErrorCode> write(SocketDescriptor socket, Span<const char> input);
```

### 5.5 에러 코드를 문장으로 바꾸는 일은 presentation 에서만 한다.

---

## 6. 설계

### 6.1 작업 순서는 타입으로 강제한다. A→B→C 는 A 의 출력 타입이 B 의 입력이다.

```cpp
auto opened = openCursor(context, query);
auto page   = fetchPage(context, opened.value, 0);  // CursorHandle 없이는 호출 불가
closeCursor(context, opened.value);
```

### 6.2 함수는 인자와 리턴값으로만 세상과 만난다.

- 전역 상태를 읽거나 쓰지 않는다.
- 인자는 `const&` 가 기본이다.
- 상태 변경이 목적인 함수는 그 객체를 유일한 non-const 인자로 받고, 이름이 동사로 그것을 말한다 (`appendToStream(Stream&, Span<const char>)`).

### 6.3 한 흐름은 한 맥락만 다룬다. 여러 맥락이 필요하면 상위 맥락 함수가 조합한다.

### 6.4 엣지 케이스는 흐름이 삼킨다. 빈 입력·경계값도 같은 루프로 처리되게 설계하고, 조건 분기로 특수 처리하지 않는다.

### 6.5 같은 층위의 흐름은 서로 호출하지 않는다.

### 6.6 여러 인스턴스 조회는 커서를 열고, 고정 크기 페이지를 가져오고, 닫는다.

```cpp
struct RecordPage
{
    size_t length;
    Record elements[MAX_PAGE_ELEMENTS];
};

Result<CursorHandle, ErrorCode> openCursor(Context& context, const Query& query);
Result<RecordPage&, ErrorCode>  fetchPage(Context& context, CursorHandle cursor, size_t page_index);
void                            closeCursor(Context& context, CursorHandle cursor);
```

### 6.7 크로스 플랫폼 클래스는 backend 헤더를 선택하고 Payload 멤버로 구현을 감춘다.

```cpp
#ifdef _WIN32
#include "backend/windows_socket.hpp"
#else
#include "backend/posix_socket.hpp"
#endif

class Socket
{
public:
    Result<size_t, ErrorCode> read(Span<char> output);
private:
    SocketPayload _payload;  // backend 에서 정의
};
```

### 6.8 외부와 주고받는 문자열은 UTF-8 이다. 다른 인코딩은 presentation 의 변환기에서 UTF-8 로 바꾼다.

---

## 7. 디렉토리 구조와 의존 방향

### 7.1 C++ 라이브러리

```
include/
    library_name.h          # C interface
src/
    library_name.cpp        # C interface ↔ implementation 얇은 어댑터
    implementation/
        context/            # 모든 인스턴스 소유 (1.2), C 경계의 커서 상자 (7.4)
        core/               # 범용 자료구조와 유틸 (7.4)
        domain/
            entity/         # 값 객체. 하드웨어 제약을 모른다
            usecases/       # 사용자 기능 단위. function1/, function2/
        data/
            repository/     # 기능 단위 보관 클래스. module1/, module2/
            states/         # repository 의 상태
            datasource/     # 메모리 공급. 고정 크기 풀, Local DB, Remote DB
            infra/          # 플랫폼 구현. class1/backend/, class1/class1.hpp
single_header/              # 스크립트로만 생성
```

### 7.2 GUI · CLI · 서버 데몬

```
src/
    main.cpp
    core/                   # 자료구조. single header
    di/                     # 종속성 조립
    i18n/                   # 언어별 문장
    presentation/
        GUI:    view/ viewModel/ service/
        CLI:    commands/ input/ handler/(main_handler + function별) output/
        데몬:   context/ handler/(endpoint별 → function별)
    domain/
        entity/ usecases/(function별) repository/(module별 인터페이스) states/
    datalayer/
        repository/(module별 Native·Mock·Remote 구현) datasource/
    infra/                  # OS 종속 기능, third party wrapper, logger
```

| 디렉토리 | 역할 |
| --- | --- |
| `core/` | 여러 레이어가 쓰는 자료구조. 의존성 없음 |
| `di/` | 구현체를 조립하는 유일한 곳. 모든 레이어를 알 수 있음 |
| `i18n/` | Key 에 대한 언어별 문장 표. 조립하지 않음 |
| `presentation/view` | 화면 구성. 사용자 입력을 받고 결과를 그림 (GUI) |
| `presentation/viewModel` | 화면 상태 보관, 문장 조립, 바인딩 데이터 생성 (GUI) |
| `presentation/service` | 플랫폼 위젯 전용 모듈. 필요한 경우에만 둠 (GUI) |
| `presentation/commands` | Handler 의 Input·Output 객체 정의 (CLI) |
| `presentation/input` | 명령줄 문자열을 Input 객체로 파싱 (CLI) |
| `presentation/handler` | Input 을 usecase 호출로 매핑. 여러 handler 를 `main_handler` 가 조합 (CLI, 데몬) |
| `presentation/output` | Handler 결과를 사용자에게 보일 문장으로 조립 (CLI) |
| `presentation/context` | Handler 와 Server Loop 를 보관하는 전역 객체 (데몬) |
| `domain/entity` | 프로젝트 개념 값 객체. 하드웨어 제약을 모름 |
| `domain/usecases` | 기능 하나를 함수 하나로 표현. 사용자 기능 단위로 폴더 구성 |
| `domain/repository` | 저장소 **인터페이스**. 구현은 가지지 않음 |
| `domain/states` | 여러 viewModel·handler 가 구독하는 앱 전체 상태 |
| `datalayer/repository` | 저장소 **구현**. Native·Mock·Remote 중 데이터 원천에 따라 선택 |
| `datalayer/datasource` | 영속 데이터를 읽고 쓰는 원천 (DB, 파일, 네트워크) |
| `infra` | OS 종속 기능, third party 래퍼, logger, statistics. 레이어 밖에서 전역 관찰용 |

서버와 클라이언트를 함께 만들면 `app/project_name`, `library/server`, `library/client`, `library/protocol` 으로 나눈다.

### 7.3 의존 방향 (왼쪽을 오른쪽이 쓴다)

```
core      ← infra, domain, datalayer, presentation
domain    ← presentation, datalayer
datalayer ← domain/usecases
infra     ← datalayer, presentation (Socket, CLI 같은 presentation 용 third party 만)
i18n      ← presentation
모두      ← di
```

### 7.4 `core/` 는 범용 자료구조와 유틸만 둔다.

- `core/` 는 도메인 용어(항목, 체크포인트, 링크, 마일스톤 등)와 도메인 레코드를 담지 않는다.
- `core/` 는 C 인터페이스의 레코드 타입(`KiottySpec*` 구조체)을 담지 않는다. 에러 enum 만 `include/` 에서 가져온다.
- 도메인 레코드를 C 구조체로 바꾸는 코드(커서 상자 등)는 `context/` 에 둔다.
- `core/` 는 `include/kiotty_spec.h` 의 에러 enum 외에는 프로젝트 고유 헤더에 의존하지 않는다.

---

## 8. i18n

- 보이는 모든 문장은 Key 를 가진다. Key → 언어별 Value 는 `i18n/` 에 둔다.
- 문장 조립은 View·ViewModel 에서만 한다. domain·datalayer 는 문장을 만들지 않는다.
- 치환은 `core/format` 의 `{0}` 자리표시자로 한다. `printf` 포맷 문자열을 번역 데이터로 쓰지 않는다 (2.3).

```cpp
void SomeViewModel::setLabel(StringView parameter)
{
    StringView format = _locale.get(Key::WARNING_MESSAGE_1, currentLanguage());
    formatInto(_label, format, parameter);  // FixedString<256> _label
}
```

---

## 9. 리뷰 체크리스트

PR 마다 아래 열다섯 가지에 모두 "예" 여야 한다.

1. 초기화 함수 밖에 힙 할당이 없다.
2. 모든 Handle 접근이 generation 을 검증한다.
3. 외부 입력은 `parse*` 하나를 지나고, 내부에서 다시 검증하지 않는다.
4. 길이 없는 포인터 인자가 없다.
5. 2.3 의 금지 함수가 없다.
6. 정수 덧셈·곱셈이 `checked*` 를 쓰거나 넘칠 수 없음이 자명하다.
7. 새 struct 는 `static_assert(sizeof)` 가 있고 hot/cold 가 나뉘어 있다.
8. 함수 30줄, 중첩 2단, 인자 4개를 넘지 않는다.
9. `switch` 에 `default` 가 없고 모든 enum 멤버가 있다.
10. 새 `parse*` 함수는 2.10 의 다섯 입력을 테스트한다.
11. 새 C interface 함수는 `<Organization><Project>_` 접두사를 가진다.
12. 추가된 소스 줄에 주석이 없다. Release 준비 PR 은 예외다.
13. 새 method 는 동사로 시작하고, 멤버 접근 method 는 필요한 이유가 PR 에 적혀 있다. `core/` 는 예외다.
14. `core/` 파일이 도메인 용어나 `KiottySpec*` 레코드 타입을 담지 않는다.
15. 새 에러 enum 이 없고, 모든 에러는 `include/kiotty_spec.h` 의 값을 쓴다.

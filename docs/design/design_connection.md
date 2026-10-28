# Pseudo 코드를 통한 인터페이스 도출

## 1. Handler 변경

### C 인터페이스 변경

```c
typedef int64_t KiottyNetConnectionHost;

// Opaque struct; 

typedef struct _KiottyNetConnectionHostAction* KiottyNetConnectionHostAction
struct _KiottyNetConnectionHostAction
{
    bool is_connected;
    KiottyNetConnectionContext handle;
}
typedef struct _KiottyNetConnectionAction* KiottyNetConnectionAction;
struct _KiottyNetConnectionAction
{
    IoAction action;
}

KiottyNetConnectionHostAction KiottyNet_acceptConnection(KiottyNetConnectionContext connectino_context);
KiottyNetConnectionHostAction KiottyNet_ignoreConnection();

/* KiottyNetConnectionAction을 호출하도록 변경 */
KiottyNetConnectionAction KiottyNet_readConnection(KiottyNetConnectionHandle connection, size_t exact);
KiottyNetConnectionAction KiottyNet_readConnectionUntil(KiottyNetConnectionHandle connection, KiottyNetSpan delimiter, size_t max);
KiottyNetConnectionAction KiottyNet_writeConnection(KiottyNetConnectionHandle connection, KiottyNetSpan data);
KiottyNetConnectionAction KiottyNet_closeConnection(KiottyNetConnectionHandle connection, KiottyNetCode reason);
KiottyNetConnectionAction KiottyNet_setConnectionTimeout(KiottyNetConnectionHandle connection, uint32_t ms);
KiottyNetConnectionAction KiottyNet_startWork(KiottyNetConnectionHandle connection); 
 
typedef struct KiottyNetConnectionHandler
{
    // 1. Connection Host에 충분한 Connection을 할당할 수 있을만큼 충분한 공간이 없다면, 호출하지 않는다.
    // 2. 안에서는 acceptConnection, ignoreConnection 또는 return 하여 함수를 끝내야 한다.
    // 3. KiottyNetConnectionInfo를 주는 이유는 black list 또는 white list로 인해 연결이 될 수도 있고 안 될 수도 있기 때문이다.
    // 4. 외부에서 KiottyNetConnectionInfo의 값을 조작할 수도 있으므로, 값 복사로 info를 넘긴다.
    KiottyNetConnectionHostAction (*onAccept)(KiottyNetAppContext app_context, KiottyNetConnectionInfo info);
    void (*onDisconnected)(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, KiottyNetCode reason);
    
    // TLS 적용했을 경우 내부적으로 Handshake까지 다 된 connection을 수행한다.
    // 여기서부터 read할지 write할지 결정된다.
    // KiottyNetConnectionAction을 return하게 만든 이유는 이 함수 안에서는
    // 무조건 마지막에 한 read, write, close, setTimeout, startWork만 반영하며 이것으로 return해야함을 강조하기 위해서다.
    // 단, notify는 예외다.
    KiottyNetConnectionAction (*onConnected)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle handle);
    KiottyNetConnectionAction (*onRead)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, KiottyNetSpan data);
    KiottyNetConnectionAction (*onWritten)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, size_t length);
    KiottyNetConnectionAction (*onWorkStarted)(KiottyNetConnectionContext connection_context, KiottyNetWorkHandle work);
    KiottyNetConnectionAction (*onTimeout)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection);
    
    // 서버 내부적으로 할당 에러 등이 발생했을 떄 return된다.
    void (*onError)(KiottyNetCode code, const char* error_str);
} KiottyNetConnectionHandler;

```


```cpp
enum class IoKind : uint8_t
{
    NONE,
    ACCEPT,
    CONNECT,
    RECEIVE,
    SEND
};

typedef IOOwner void*;
struct IoCompletion
{
    IoKind     kind;
    IOOwner owner;
    SocketCode code;
    size_t length;
}

enum class IoActionKind
{
    NONE,
    READ,
    READ_UNTIL,
    WRITE,
    START_WORK,
    CLOSE
}

struct IoAction
{
    IoActionKind type;    
    PoolHandle connection_handle; // Connection의 Handle
    union
    {
        struct
        {
            size_t exact;
        } read;
        struct
        {
            KiottyNetSpan delimiter;
            size_t        max;
        } read_until;
        struct
        {
            KiottyNetSpan data;
        } write;
        struct
        {
            KiottyNetCode reason;
        } close;
        struct {
            uint32_t ms;
        } timeout;
    } as;
}
```



```cpp

// 조건에 맞는 Element들을 return해주기 위한 클래스

#include <cstddef>

// 참조 결과를 순회하기 위한 간접 반복자
template<typename T>
class KiottyIndirectIterator
{
public:
    explicit KiottyIndirectIterator(T** ptr)
        : ptr_(ptr)
    {
    }

    T& operator*() const
    {
        return **ptr_;
    }

    T* operator->() const
    {
        return *ptr_;
    }

    KiottyIndirectIterator& operator++()
    {
        ++ptr_;
        return *this;
    }

    bool operator!=(const KiottyIndirectIterator& other) const
    {
        return ptr_ != other.ptr_;
    }

    bool operator==(const KiottyIndirectIterator& other) const
    {
        return ptr_ == other.ptr_;
    }

private:
    T** ptr_;
};


// 1. 값 저장 버전
template<typename Element, std::size_t MAX_SIZE>
struct KiottySearchView
{
    static_assert(MAX_SIZE > 0, "MAX_SIZE must be greater than zero");

    using iterator = Element*;

    iterator begin()
    {
        return elements;
    }

    iterator end()
    {
        return elements + size;
    }

    template<typename Source>
    void find(
        const Source* sources,
        std::size_t length,
        Element (*convert)(const Source&),
        bool (*filter)(const Source&))
    {
        size = 0;

        for (std::size_t i = 0; i < length; ++i)
        {
            if (filter(sources[i]))
            {
                elements[size++] = convert(sources[i]);

                if (size == MAX_SIZE)
                    break;
            }
        }
    }

    template<typename Source>
    void find(
        const Source* sources,
        std::size_t length,
        Element (*convert)(const Source&))
    {
        find(sources, length, convert, &acceptAll<Source>);
    }

    template<typename Source, std::size_t SourceLength>
    void find(
        const Source (&sources)[SourceLength],
        Element (*convert)(const Source&),
        bool (*filter)(const Source&))
    {
        find(sources, SourceLength, convert, filter);
    }

    template<typename Source, std::size_t SourceLength>
    void find(
        const Source (&sources)[SourceLength],
        Element (*convert)(const Source&))
    {
        find(sources, SourceLength, convert);
    }

private:
    template<typename Source>
    static bool acceptAll(const Source&)
    {
        return true;
    }

    std::size_t size = 0;
    Element elements[MAX_SIZE];
};


// 2. 참조 저장 버전
template<typename Element, std::size_t MAX_SIZE>
struct KiottySearchView<Element&, MAX_SIZE>
{
    static_assert(MAX_SIZE > 0, "MAX_SIZE must be greater than zero");

    using iterator = KiottyIndirectIterator<Element>;

    iterator begin()
    {
        return iterator(elements);
    }

    iterator end()
    {
        return iterator(elements + size);
    }

    template<typename Source>
    void find(
        const Source* sources,
        std::size_t length,
        Element& (*convert)(const Source&),
        bool (*filter)(const Source&))
    {
        size = 0;

        for (std::size_t i = 0; i < length; ++i)
        {
            if (filter(sources[i]))
            {
                elements[size++] = &convert(sources[i]);

                if (size == MAX_SIZE)
                    break;
            }
        }
    }

    template<typename Source>
    void find(
        const Source* sources,
        std::size_t length,
        Element& (*convert)(const Source&))
    {
        find(sources, length, convert, &acceptAll<Source>);
    }

    template<typename Source, std::size_t SourceLength>
    void find(
        const Source (&sources)[SourceLength],
        Element& (*convert)(const Source&),
        bool (*filter)(const Source&))
    {
        find(sources, SourceLength, convert, filter);
    }

    template<typename Source, std::size_t SourceLength>
    void find(
        const Source (&sources)[SourceLength],
        Element& (*convert)(const Source&))
    {
        find(sources, SourceLength, convert);
    }

private:
    template<typename Source>
    static bool acceptAll(const Source&)
    {
        return true;
    }

    std::size_t size = 0;
    Element* elements[MAX_SIZE];
};

```

```cpp
class Acceptor {
    Acceptor() {
    }

private:
    
}




void* app_context;
EventListener listener;
ConnectionHost host;
ConnectionHandler handler;
(PassiveSocket or ActiveSocket) host_socket;

listener.submitAccept();

KiottyResult<KiottySearchView<IoCompletion>&, KiottyNetCode> wait_result = listener.wait(host.aggregateDeadline());

IoAction action;
if (wait_result.code == TIMEOUT) {
    KiottySearchView<Connection&>& expired_connections = host.findExpiredConnections();

    for (auto& expired_connection : expired_connections)
    {
        action = onTimeout(expired_connection);
    } 
}

for (auto& completion : wait_result.value)
{
    switch (completion.type)
    {
        case ACCEPT:
            // accept -> connection 생성 -> handler 수행 -> handler로부터 다음 action 받아옴 -> 다음 action처리 하면서 listener에 다음 동작 등록            

            action = onConnected(connection);
            break;
        case READ:
            action = onRead(connection);
            break;
        case WRITE:
            action = onWritten(connection);
            break;
        case DISCONNECTED:
            onDisconneted(host);
            continue; // loop를 빠져나가기
    }

    auto& target_connection = host.find(action.connection_handle);
    switch(action.type)
    {
        case READ: 
            listener.read(
                target_connection.event(), 
                action.as.read.exact);
            break;
        case READ_UNTIL: 
            listener.readUntil(
                target_connection.event(), 
                action.as.read_until.delimeter, 
                action.as.read_until.max);
            break;
        case WRITE: 
            listener.write(
                connection.event(), 
                action.as.write.data);
            break;
        case START_WORK: 
            listener.startWork(connection.event()); 
            // reactor 모델에서는 더 이상 connection event를 받지 않음
            // proactive 모델에서는 다음 요청을 하지 않으니까 아무 동작도 하지 않으면 됨.
            onWorkStarted(connection);
            break;
        case TIMEOUT:
            // aggregate
            host.setTimeout(action.connection_handle, action.as.timeout.ms);
        case CLOSE: 
            listener.close(connection.event());
            break;        
    }
}

Acceptor& asAcceptor(IoOnwer owner);
Connection& asConnection(IoOwner owner);

KiottyResult<Connection&, KiottyNetCode> access(ConnectionHost& host, IoCompletion& completion)
{
    if (completion.type == ACCEPT) {
        auto& acceptor = asAcceptor(completion.owner);
        KiottyResult<ActiveSocket, KiottyNetCode> acceptor.accept();
    }
    return Ok(*reinterpret_cast<Connection&>)
}

```

# 2026-11-05 진행 정리 : 4b (ConnectionStream)

기준 문서: `docs/plan/plan_base_v2.md` 4b, `docs/design/design_base_v4.md` 9.7.

## 코드

- `base/stream/kiotty_connection_stream.h/.cpp`: `ConnectionStream(ActiveSocket&, TlsEngine&, StageBase& receive, StageBase& send)`. 참조 넷을 드는 값이다. 복사 생성만 되고 대입은 없다.
  - 공개: `read`, `write`, `writeFile`, `connected`, `handshake`.
  - private: `flush`, `send`, `sendAll`, `waitReadable`, `receiveCipher`, `takeReceived`.
- `base/stream/kiotty_input_file.h`, `backend/kiotty_posix_file.cpp` / `kiotty_windows_file.cpp`: `InputFile` 의 `open`, `read(offset, buffer, length)`, `close`, `size`.
  - POSIX: `open(O_RDONLY | O_CLOEXEC)`, `fstat` (일반 파일만), `pread`.
  - Windows: UTF-8 경로를 `MultiByteToWideChar` 로 바꿔 `CreateFileW`, `GetFileSizeEx`, `ReadFile` (OVERLAPPED 의 offset).
- `ActiveSocket`: `peek` (`recv(MSG_PEEK)`), `waitReadable(timeout_ms)` / `waitWritable(timeout_ms)`. POSIX 는 poll 이고 connect 의 대기와 같은 함수 (`waitFor`) 다. Windows 는 select 다.
- `core/kiotty_clock.h`: `Deadline` (`limited`, `expired`, `remaining`). timeout 0 은 한도 없음.

## 정한 것 (설계 9.7 에 적었다)

- `timeout_ms` 는 호출 전체의 한도다. 0 이면 없다.
- read: buffer 가 nullptr 이거나 length 0 이면 INVALID_ARGUMENT. 평문은 사용자 buffer 로 바로 recv 한다. TLS 는 cipherReceive → decode → receive Stage → 복사다. length 는 `INT32_MAX` 로 자른다.
- write: 전부 보내야 SUCCESS 다. 실패하면 `bytes` 가 그때까지 보낸 평문 바이트 수다. TLS 는 send Stage 의 capacity 씩 encode 한다.
- writeFile:
  - 없는 파일, 일반 파일이 아닌 것, 범위 밖, 읽다가 짧아진 것은 전부 INVALID_ARGUMENT 다.
  - send Stage 를 읽기 버퍼로 쓰므로, send Stage 에 데이터가 남아 있으면 BUSY 다.
- handshake 의 timeout 은 `KIOTTY_TLS_HANDSHAKE_TIMEOUT` 이다.

## 테스트

`kiotty_test_connection_stream.cpp` 에 13 개가 있다. plan v2 의 10 개와 더한 셋 (`ReadReturnsPeerClosedWhenThePeerShutsDown`, `ReadAndWriteRejectEmptyOrMissingBuffers`, `HandshakeWithoutAnOpenEngineIsInvalidArgument`) 이다. 테스트 계획 9절 끝에 표를 더했다. TLS 케이스는 TLS OFF 에서 skip 한다.

## 로컬

- MSVC Debug: TLS ON / OFF 183/183. ConnectionStream 13 개 10 회 반복 초록.
- WSL: gcc / clang TLS ON / OFF, clang ASan+UBSan, TSan 184/184, clang Release TLS 183/183. ConnectionStream 13 개를 gcc / ASan / TSan 에서 각 10 회 반복 초록.

## 고친 것 (같은 날): 복사 줄이기, writeFile 은 sendfile / TransmitFile

- TLS read: socket 에서 BIO pair 안으로 바로 recv 한다 (`TlsEngine::receiveRoom` / `received`, BIO_nwrite0). 그다음 `TlsEngine::decode(char*, length)` 가 SSL_read 로 사용자 buffer 에 바로 쓴다. cipherReceive 와 receive Stage 를 거치지 않는다.
- TLS write: `TlsEngine::write` (SSL_write) 가 BIO 에 암호문을 만들고, `outgoing` / `sent` (BIO_nread0) 로 BIO 안에서 바로 send 한다. cipherSend 를 거치지 않는다. 16 KiB 씩 쓴다 (send capacity 와 무관).
- 평문 writeFile: Linux `sendfile` (EAGAIN 이면 waitWritable. 그동안 그 thread 의 SIGPIPE 를 막는다), Windows `TransmitFile` (WSAIoctl 로 얻는다. hEvent 하위 비트로 IOCP 를 피하고 event 를 기다린다. 시간이 지나면 CancelIoEx). 그 밖의 POSIX 는 4 KiB pread + send.
- TLS writeFile 은 그대로 send Stage 로 읽어 보낸다. 커널이 암호화를 못 하므로 복사 하나는 남는다.
- 테스트: `WriteTlsEncodesInSendCapacityChunks` → `WriteTlsSendsFullRecordsStraightFromTheEngine` (40000 B → 레코드 셋). 더한 것: `ReadTlsDecodesStraightIntoTheCallersBuffer`, `WriteFileOverTlsSendsExactRange`. 모두 15 개.
- 로컬: MSVC Debug TLS ON / OFF 185/185, WSL gcc / clang TLS ON / OFF, ASan+UBSan, TSan 186/186, clang Release TLS 185/185. ConnectionStream 15 개를 MSVC / gcc / ASan / TSan 에서 각 10 회 반복 초록.

## 다음 할 일

5a (Sender, WorkContext, IoSource, Connection 평문, ConnectionHost).

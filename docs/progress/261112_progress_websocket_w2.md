# 2026-11-12 진행 정리 : WebSocket 사이클 W2 (frame codec)

기준 문서: `docs/design/design_websocket_v0.html` 6절, 10절의 W2 행. RFC 6455 5절 (framing), 4.2.2 (Sec-WebSocket-Accept), 7.4 (close status).

## 만든 것

| 층 | 파일 | 하는 일 |
|---|---|---|
| core | `kiotty_sha1` | SHA-1 (FIPS 180-1). `sha1Of(first, second)` 는 두 조각을 이어 해시한다 (키 + GUID 를 복사하지 않으려고). 할당 없음 |
| core | `kiotty_base64` | `encodeBase64` (목적지가 짧으면 `BUFFER_FULL`), `base64LengthOf`, `isBase64Text` (길이 4 의 배수, 알파벳, 끝의 `=` 둘까지) |
| domain/usecases/websocket | `kiotty_websocket_frame` | `parseFrameStart` (2 바이트: FIN, RSV 는 0, 아는 opcode, MASK, 길이 7 비트. 제어 frame 은 FIN 이고 125 이하), `extendedLengthBytesOf`, `parseExtendedLength` (최소 표현이 아니거나 최상위 비트가 서면 1002), `parseMaskKey`, `isControl`, `buildFrameHead` / `buildMaskedFrameHead` (FIN 하나짜리, 최대 14 바이트), `applyMask` (offset 으로 조각마다 이어서) |
| | `kiotty_websocket_close` | `parseClosePayload` (빈 payload 는 1005, 1 바이트나 125 초과는 1002, 보낼 수 없는 status 는 1002, 이유가 UTF-8 아니면 1007), `buildClosePayload`, `isSendableCloseStatus` (1000 ~ 1014 중 1004 / 1005 / 1006 제외, 3000 ~ 4999) |
| | `kiotty_websocket_handshake` | `isWebSocketKey` (16 바이트의 base64 = 24 글자, 끝이 `==`), `acceptOf` (SHA-1(key + GUID) 의 base64) |

실패의 코드는 `KiottyNetWebSocketStatus` 다 (HTTP 파서의 실패가 곧 자동 응답 status 인 것과 같은 모양). 송신 쪽 (`buildClosePayload`) 은 사용자 인자를 받으므로 `KiottyNetCode` 다.

## 테스트

`tests/unit/kiotty_test_websocket_codec.cpp` 11 개:

- SHA-1: 빈 입력, `abc` (한 조각과 두 조각), 두 블록 벡터, `a` 백만 개.
- base64: RFC 4648 의 벡터, 목적지 부족, 텍스트 검사 (길이, `=` 뒤의 글자, 알파벳 밖, `=` 셋).
- handshake: RFC 6455 의 키 → `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`, 빈 키, 23 / 25 글자, `==` 아님, 가운데 NUL.
- frame: RFC 6455 5.7 의 예 (`Hello` 평문 / mask, 조각 둘, ping, mask 된 pong, 256 바이트, 64 KiB) 의 머리 파싱과 만들기, mask 의 offset 이어 쓰기, 깨진 머리 (빈 입력, 1 / 3 바이트, RSV, 모르는 opcode 둘, 조각난 제어 frame, 126 길이의 제어 frame), 길이 경계 (125, 126, 65535, 65536, 최소 표현이 아닌 것, 최상위 비트).
- close: 빈 payload, status + 이유, 사설 status, 가운데 NUL 이 있는 이유, 1 바이트, 1005 / 1006 / 1004 / 0 / 5000, 최대 길이와 + 1, 깨진 UTF-8, 만들기 (status 의 0 바이트, 이유 상한, 보낼 수 없는 status, 깨진 UTF-8).

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 644 / 644 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 645 / 645 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 645 / 645, 645 / 645 |
| WSL clang Release TLS ON | 644 / 644 |

asan 에서 처음에 하나가 실패했다 (`WebSocketClose.ParsesStatusAndReason`). 테스트가 임시 문자열로 `parseClosePayload` 를 불러 돌아온 reason span 이 사라진 버퍼를 가리켰다 (파서는 설계대로 입력을 복사하지 않는다). 입력을 지역 변수로 두어 고쳤다.

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 다음

W3 (서버): HTTP 표에 WebSocket 행, 라우트 표의 Upgrade 항목, onUpgrade / onOpen / onMessage / onClose, ping / pong / close 타이머, 다른 thread 의 send / close, `broadcastWebSocket`.

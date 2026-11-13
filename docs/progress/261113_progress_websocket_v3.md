# 2026-11-13 진행 정리 : WebSocket 설계 v1 의 사이클 V3 (클라이언트)

기준 문서: `docs/design/design_websocket_v1.html` 5절, 결정 6 ~ 9, 12, 15, 8.3 / 8.4, 9절, 11절의 V3 행.

## 바뀐 API

```c
typedef void (*KiottyNetWebSocketClientOpenAction)(KiottyNetAppContext app_context);
typedef void (*KiottyNetWebSocketClientMessageAction)(KiottyNetAppContext app_context, KiottyNetWebSocketMessage message);
typedef void (*KiottyNetWebSocketClientCloseAction)(KiottyNetAppContext app_context, KiottyNetWebSocketClose close);
typedef void (*KiottyNetWebSocketClientErrorAction)(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message);

KiottyNetWebSocketClientResult KiottyNet_createWebSocketClient(KiottyNetContext context, const KiottyNetClientConfiguration* configuration, const KiottyNetHttpLimits* limits);   /* path 가 빠졌다 */
KiottyNetCode KiottyNet_setWebSocketClientProtocols(KiottyNetWebSocketClientHandle client, const char* protocols);
KiottyNetCode KiottyNet_onWebSocketClientOpen / Message / Close / Error(KiottyNetWebSocketClientHandle client, ... );
KiottyNetCode KiottyNet_runWebSocketClient(KiottyNetWebSocketClientHandle client, const KiottyNetAddress* address, const char* path, const KiottyNetTlsClientConfiguration* tls, KiottyNetAppContext app_context);
KiottyNetCode KiottyNet_byeWebSocketClient(KiottyNetWebSocketClientHandle client, uint16_t status, const char* reason);
```

`KiottyNetWebSocketHandler` 는 없어졌다 (서버는 V2 에서 route 로 옮겼다). C++ `KiottyNet_WebSocketClient(context, configuration, limits)`, `protocols`, `onOpen / onMessage / onClose / onError`, `run(address, path, app_context)`, `run(address, path, tls, app_context)`, `bye(status = 1000, reason = nullptr)`, `close`.

## 정한 것 (구현에서)

- **등록.** run 전에만, run 중이면 `BUSY`. NULL 콜백 / client 는 `INVALID_ARGUMENT` (서버 route 와 같다). 다시 부르면 바꾸고 다음 run 에도 남는다. onMessage 없이 run 하면 `INVALID_ARGUMENT` (onError 없이).
- **후보 목록.** 쉼표로 나눈 token 들, 127 바이트까지. 빈 항목이나 token 이 아닌 항목은 `INVALID_ARGUMENT`. NULL 이나 빈 문자열은 지운다. 요청에 `Sec-WebSocket-Protocol: <목록 그대로>`. 101 의 값은 후보 중 하나와 정확히 같아야 하고, 없으면 통과하고 `info.protocol` 은 빈 문자열.
- **path** 는 run 마다 검사한다 (`/` 로 시작, 보이는 글자, 255 바이트까지). Upgrade 요청 버퍼는 768 에서 1024 바이트로 늘렸다 (path + Host + 후보 목록).
- **onError 는 열리기 전에 한 번.** 두 길이 있다. (1) base 의 오류 보고 (TLS handshake 실패 등) 가 client machine 의 `reportError` 로 오면 넘긴다. (2) run 이 끝날 때 아직 열리지 않았고 알리지 않았으면 run 의 결과 code 로 알린다 (재시도 포기, 101 아님, Accept 틀림, 후보 밖, 101 timeout, 열리기 전 상대가 끊음). `SUCCESS`, `CLOSED` (앱의 멈추기), `INVALID_ARGUMENT` 는 알리지 않는다. 열린 뒤의 base 오류 보고는 넘기지 않는다 (onClose 의 code 로 온다). info 는 그 run 의 연결 상대 (연결 전 실패면 빈 값). 둘은 `ClientUpgrade::has_opened` / `has_failed` 로 겹치지 않는다. onError 의 저장은 `HttpNotices::setError` 를 그대로 쓴다 (모양이 `KiottyNetHttpErrorAction` 과 같다).
- **bye** 는 `KiottyNet_closeWebSocket(client->socket(), status, reason)`. 열린 소켓이 없으면 `PEER_CLOSED`.
- **mask key.** 생성기를 `nextMaskKey(std::atomic<uint64_t>&)` (frame 모듈) 로 옮기고 state 를 `HttpSite::mask_state` 에 둔다. push lane 의 frame 과 write lane 의 close frame 이 같은 state 에서 뽑는다. 단조 시계로 만들던 close frame 의 key 가 없어졌다 (결정 15).

## 안

- frame 행의 콜백 자리 셋 (`callOpen`, `callMessage`, `callClose`) 이 역할 값 (`site.role`) 으로 서버의 `WebSocketTarget` 과 클라이언트의 `ClientUpgrade::events` 를 갈라 부른다. 클라이언트의 websocket_context 는 늘 NULL 이다.
- `ClientUpgrade` 는 `{ events, request, accept, protocols, peer, opened, failure, has_opened, has_failed }`. `target` 은 빠졌다.
- `isUpgradeResponse(response, accept, offered)` 가 후보를 받는다. 고른 값은 `onResponseHeader` 가 `socket.protocol` 에 담는다 (`openSocket` 이 요청 저장소를 다시 쓰기 전에).
- `WebSocketClient` 의 public 은 `registration()` (run 중 `BUSY`, 아니면 site) 과 `setProtocols` 가 더해졌다. 번역 층이 site 의 events / notices 를 채운다.

## 테스트

테스트 계획 24절. 클라이언트 integration 은 새 등록으로 바뀌었고 셋을 더했다 (`OffersSubprotocolsAndAcceptsOneOfThemOrNone`, `GivingUpOnConnectingIsReportedOnceThroughOnError`, `CppClientRegistersRunsSendsAndSaysBye`). `CreateRejectsBadArgumentsAndRunRejectsAMissingHandler` 는 `RegistrationAndRunRejectBadArguments` 가 됐다. 단위 테스트 `MaskKeysComeFromTheSharedStateAndChangeEveryDraw`. C / C++ 헤더 layout 에서 handler 를 뺐다.

하지 않은 것: TLS 실패가 onError 로 한 번만 오는 것을 따로 재현하지 않았다 (코드의 길은 `has_failed` 로 막혀 있다).

## 매트릭스

| 빌드 | 결과 |
|---|---|
| Windows MSVC Debug, TLS OFF | 667 / 667 (TLS 는 SKIP) |
| WSL gcc / clang Debug, TLS OFF / ON | 668 / 668 넷 |
| WSL clang asan,undefined TLS ON, thread TLS ON | 668 / 668, 668 / 668 |
| WSL clang Release TLS ON | 667 / 667 |

`tools/check_rules.py` 는 전과 같은 수 (FILE 1, LINE 4, MACRO 18, PUBLIC 3).

## 문서

requirement, user guide, 구현 가이드는 V4 에서 한 번에. 이 사이클은 테스트 계획 24절과 이 progress.

## 다음

V4: 문서 (user guide 를 새 API 로, "받기" 절과 요청 / 응답 예제, requirement 9 ~ 11절, 구현 가이드 4 / 5절, 테스트 계획 19 / 20절).

1.1 B로 가보자
1.2 코드를 직접 보지 않으니 이해하기 힘들군. 일단 스킵

2.1 헉. 그건 몰랐다. 우선 오케이
2.2 
- TLS 관련 error는 정말 세밀하게 짜줬으면 좋겠어. 어디서 원인이 발생했는지 모르면 고치기가 어려워..
- 받기는 늘 걸려있다고? HandshakeStep::MORE: 이면 내가 더 보내야 할 때도 있고 내가 더 받아야할 때도 있는데 receive 요청이나 write 요청을 통해 handshake에 필요한 packet을 더 보내거나 받아야 되는거 아니야? hold를 호출하니 혼란스러운데. 심지어 이건 proactive 모델이라 계속해서 send나 recv를 하지 않으면 자동으로 멈추게 되는데?
- Socket Event를 IoAction을 통해서 보내는 것과 IoCompletion으로 받아오는 걸 처리하는게 있는데
IoCompletion으로 받아오는 걸 처리하는 로직이면 on으로 통일하고 싶어. 그래서 stepHandshake를 onHandshake로 바꾸고 싶음.
2.3. 식별을 Phase로 하는 거 동의야.
Handshaking -> Ready -> Closing (이렇게 3개겠지?)

2.5 
- 이거 보니 생각났는데 StreamClient의 TLS에 대해서는 전혀 생각하지 않은 것 같아..
여기도 TLSProfile과 TLSPipe로 충분할까?

전반적으로 내가 생각한 부분이랑 다른 부분이 있어.
1) IoAction은 실제로 submit할 때 사용하기 위해 만든거야. 뭔가 다른 목적으로 사용되고 있어. IoAction의 결과가 IoCompletion으로 나와야 돼. 왜 내가 이걸 했냐면.. Write를 위한 Segment 같은 건 안 만들고 싶어서..!!
2) 그래서 dispatch의 결과로 IoAction이 나오고, 그걸 submit에 먹이는 결과가 나와야 돼.
3) submit 단계에서도 START_WORK와 관련된 작업을 해야돼. submit은 listener와 연관된 작업을 하는데, listener
4) 아무래도 이걸 위해서 IoAction을 조금 바꿔야될 것 같아..
IoAction의 as.read와 as.readUntil에서 listener용 정보와 handler용 정보를 모두 포함해야 할 것 같아.
ConnectionHandler에서 계속 read나 readUntil을 통해 read를 요청하면 우선 ReceiveStage에 값이 있으면 그것을 다시 ConnectionHandler에 넘기고 더 이상 ReceiveStage에 읽을 게 없으면 IoAction에 가장 마지막으로 사용자가 요청한 read, readUntil의 parameter와 ReceiveStage가 읽어야 할 용량과 ReceiveStage에서 읽기 시작할 수 있는 pointer를 넘기는 거지. 그리고 submit 단계에서 IoAction의 type을 보고 _listener에 read를 보내는 것임. 아무래도 Connection 내 IoEvent는 READ용 Event와 Write용 Event를 따로둬야 할 것 같아.
5) IoAction의 as.write에도 이게 notify용인지 그냥 write용인지 구분하는 게 있어야 될 것 같아. write용이면 dispatch 단계에서 이미 송신용 TxStage에서 보낼 위치에 대한 span을 IoAction에 넣을테고 submit 단계에서 그걸 그대로 보내겠지. 

6) 
notify / broadcast는 이렇게 하는 게 어떨까 싶어.
1. Connection마다 NotificationContext에 접근할 수 있음.
2. Connection.notify(const char* buffer, size_t length, bool instant);
  // IoThread 내부 일때는
  //   TLS일 경우, TLS 암호화문으로 변환
  //   Notification Stage에 값을 넣음. Notificiation 공간이 부족하면 Error Message
  //   PendingIoAction를 NotificationStage에 값이 시작하는 지점과 값이 찬 전체 길이를 가지고 있도록 갱신한다.
  //   instant가 true면, 감수하고 동기 Write.
  //   일반적으로 다른 Connection의 동작을 지연시키기 때문에 권하지 않지만,
  //   지연 전송을 하면 안 되는 경우에 사용한다.
  // IoThread 바깥일 때는
  //   NotificationContext.slot에 buffer의 주소와 길이를 저장
  //   IoThread
  









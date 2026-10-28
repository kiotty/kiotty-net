// Connection의 Lifecycle을 담당함.
// 실질적인 Pool 역할.
// std::vector<>로 kiott_connection.h 관련 객체를 담당하고 KiottyConnectionHandle로
// connection을 찾거나 해제할 수 있음. 그리고 할당도 가능함.
// Server 또는 EventClient가 초기화되면 같이 초기화됨.
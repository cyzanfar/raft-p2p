
#include <unistd.h>

#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <random>
#include "main.hh"

enum node_status nodeStatus;
struct node_state nodeState;
struct leader_state leaderState;


ChatDialog::ChatDialog()
{

	textview = new QTextEdit(this);
	textview->setReadOnly(true);
	textline = new QLineEdit(this);

	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(textview);
	layout->addWidget(textline);
	setLayout(layout);

	// Create a UDP network socket
	socket = new NetSocket();
	if (!socket->bind())
		exit(1);

	//	 Randomize local origin
	qsrand((uint) QDateTime::currentMSecsSinceEpoch());

	local_origin = QString::number(qrand()) + socket->localPort();
	setWindowTitle(local_origin);

	qDebug() << "LOCAL ORIGIN: " << local_origin;

	// set init currentTerm
	nodeState.currentTerm = 0;

	// set the nodes id
	nodeState.id = local_origin;

	// set waiting for a status to 0 (false) since instance just launched
	nodeStatus = WAITING;

	// last log applied to state
	nodeState.lastApplied = 0;

	// index of highest log entry known to be committed
	nodeState.commitIndex = 0;


	// // Initialize timer for heartbeat timeout
	heartbeatTimer = new QTimer(this);
	connect(heartbeatTimer, SIGNAL(timeout()), this, SLOT(timeoutHandler()));

	socket->pingList = socket->PeerList();

	// Register a callback on the textline's returnPressed signal
	// so that we can send the message entered by the user.
	connect(textline, SIGNAL(returnPressed()),
		this, SLOT(gotReturnPressed()));

	// Callback fired when message is received
	connect(socket, SIGNAL(readyRead()), this, SLOT(readPendingMessages()));
	
}

void ChatDialog::readPendingMessages()
{

	while (socket->hasPendingDatagrams()) {
		QByteArray datagram;
		datagram.resize(socket->pendingDatagramSize());
		QHostAddress sender;
		quint16 senderPort;

		socket->readDatagram(datagram.data(), datagram.size(),
								&sender, &senderPort);

		qDebug() << "\nRECEIVING MESSAGE";

		processIncomingData(datagram, sender, senderPort, socket);
	}
}

void ChatDialog::processReceivedMessage(
		QMap<QString,
		QVariant> messageReceived,
		QHostAddress sender,
		quint16 senderPort
		)

{
	QString msg_origin = messageReceived.value("Origin").toString();
	quint32 msg_seqnum = messageReceived.value("SeqNo").toUInt();

	// If localStatusMap[msg_origin] = 0, this is a new peer not on list
	// Set expected msg_num to 1 for comparison
	if (localStatusMap.value(msg_origin) == 0) {
		localStatusMap[msg_origin] = 1;
	}

	// Modify localStatusMap before executing sendStatusMessage
	// If origin already in localStatusMap, seqno+1
	if (localStatusMap.value(msg_origin) == msg_seqnum) {
		
		messageList[msg_origin][msg_seqnum] = messageReceived;

		localStatusMap[msg_origin] = msg_seqnum+1;
		
		// Show in chatdialog textview
		textview->append(msg_origin + ": " + messageReceived.value("ChatText").toString());

		sendRandomMessage(serializeMessage(messageReceived));
	}
	else if (localStatusMap.value(msg_origin) > msg_seqnum) {
		// If expected message number greater than one being received
		// Message has already been seen, ignore
		qDebug() << "message already seen: " << msg_seqnum;
	}
	else {
		qDebug() << "waiting for msg with msgnum: " << localStatusMap.value(msg_origin);
	}

	socket->sendStatusMessage(sender, senderPort,
			localStatusMap);
}

void ChatDialog::processStatusMessage(QMap<QString, QMap<QString, quint32> > peerWantMap, QHostAddress sender, quint16 senderPort) {
	
	QMap<QString, QVariant> messageToSend;
	// Unwrap peerWant
	QMap<QString, quint32> peerStatusMap = peerWantMap.value("Want");

	// Create enum for differences in status
	enum Status {INSYNC, AHEAD, BEHIND};

	qDebug() << "\nmessage contains want:" << peerStatusMap;

			// check if the want matches the one we are waiting an ACK for
//	if (currentState.waitingForStatus == 1) {
//		if (last_message_sent.contains(senderPort)) {
//			QString lms_origin = last_message_sent[senderPort].value("Origin").toString();
//			quint32 lms_seqno = last_message_sent[senderPort].value("SeqNo").toUInt();
//
//			if ((peerStatusMap.contains(lms_origin)) && (peerStatusMap[lms_origin] == lms_seqno+1)) {
//				last_message_sent.remove(senderPort);
//				currentState.waitingForStatus = 0;
//				qDebug() << "in processstatusmessage about to stop timer";
//				timer->stop();
//			}
//		}
//	}

	// Set initial status
	Status status = INSYNC;
	// Compare statusMaps using localStatus keys
	for (auto originKey : localStatusMap.keys()) {
		if (!peerStatusMap.contains(originKey)){
			status = AHEAD;
			// Add message to message buffer from messageList
			messageToSend = messageList[originKey][1];
			break;
		}
		else if (peerStatusMap.value(originKey) < localStatusMap.value(originKey)) {
			status = AHEAD;
			// Add message to message buffer from messageList
			messageToSend = messageList[originKey][peerStatusMap.value(originKey)];
			break;
		}
		else if (peerStatusMap.value(originKey) > localStatusMap.value(originKey)) {
			// Send status message
			status = BEHIND;
			break;
		}
	}

	// Check for keys in peerStatusMap that are not in localStatusMap
	for (auto originKey : peerStatusMap.keys()) {
		if (!localStatusMap.contains(originKey)){
			status = BEHIND;
			break;
		}
	}

	switch (status) {
		case INSYNC:
			// Coin flip and randomly send, or stop
			srand(time(0));
			int coin_flip;
			coin_flip = (qrand() % 2);
			qDebug() << "[INSYNC]";
			
			if(coin_flip) {
				getRandomNeighbor();
				int index = qrand() % neighborList.size();

				qDebug() << "COIN FLIP ABOUT TO SEND";
				
				socket->sendStatusMessage(QHostAddress::LocalHost, neighborList[index], localStatusMap);
			}
			break;
		case AHEAD:
			qDebug() << "[AHEAD] about to send MESSAGE";

			sendMessage(serializeMessage(messageToSend), senderPort);
			break;
		case BEHIND:
			qDebug() << "[BEHIND] about to send STATUS";

			socket->sendStatusMessage(sender, senderPort, localStatusMap);
			break;
	}
}

void NetSocket::processPingMessage(QHostAddress sender, quint16 senderPort) {
	// Send ping reply
	sendPingReply(sender, senderPort);
}

void NetSocket::processPingReply(quint16 senderPort, QList<quint16> neighborList)
{

	// Check timer, addprocessPingReply to pingTimes, and remove port from pingList
	quint16 pingTime = pingTimer.elapsed();

	pingTimes[senderPort] = pingTime;

	pingList.removeOne(senderPort);

	QList<quint16> peerList;
	int index;

	if (neighborList.size() == 2) {
		switch (pingTimes.size()) {
			case 1:
				for (quint16 port : pingTimes.keys()) {
					neighborList.append(port);		
				}

				peerList = this->PeerList();
				
				peerList.removeOne(neighborList[0]);

				index = (rand() % peerList.size());
				neighborList.append(peerList[index]);
				break;
			case 2:
				for (quint16 port : pingTimes.keys()) {
					neighborList.append(port);
				}
				break;
			default:
				QList<int> ping_rtt;
				QList<quint16> ports;

				ping_rtt = pingTimes.values();
				ports = pingTimes.keys();

				qSort(ping_rtt);
				pingTimes.clear();

				QList<int>::iterator i;
				QList<short unsigned int>::iterator j;

				i = ping_rtt.begin();
				j = ports.begin();

				// insert back the sorted values and map them to keys in QMap container
				while(i != ping_rtt.end() && j != ports.end()){
					pingTimes.insert(*j, *i);
					i++;
					j++;
				}
				
				for (quint16 port : pingTimes.keys()) {
					if (neighborList.size() < 2) {
						neighborList.append(port);
					}
				}
				break;
		}
	}
}

// Process the message read from pending messages from sock
void ChatDialog::processIncomingData(
		QByteArray datagramReceived,
		QHostAddress sender,
		quint16 senderPort,
		NetSocket *socket
		)
{
	// Stream for both msg and want, as stream is emptied on read
	QMap<QString, QVariant> messageReceived;
	QDataStream stream_msg(&datagramReceived,  QIODevice::ReadOnly);

	stream_msg >> messageReceived;

	if (messageReceived.value("term") < nodeState.currentTerm) {
		// reply false
	}

	if (nodeState.lastApplied <= messageReceived.value("lastLogIndex").toUInt()) {
		// send message
		nodeState.votedFor = messageReceived.value("candidateId").toString();
	}

	if (messageReceived.contains("ChatText")) {
		qDebug() << "messageReceived: " << messageReceived["ChatText"];
		processReceivedMessage(messageReceived, sender, senderPort);
	}
	else if (peerWantMap.contains("Want")) {
		qDebug() << "wantMap: " << peerWantMap["Want"];
		processStatusMessage(peerWantMap, sender, senderPort);
	}
	else if (pingMap.contains("Ping")) {
		socket->processPingMessage(sender, senderPort);
	}
	else if (pingMap.contains("PingReply")) {
		socket->processPingReply(senderPort, neighborList);
	}
	
}

QByteArray ChatDialog::serializeLocalMessage(QString messageText)
{
	QVariantMap messageMap;

	messageMap.insert("ChatText", messageText);
	messageMap.insert("Origin", local_origin);
	messageMap.insert("SeqNo", currentSeqNum);

	messageList[local_origin][currentSeqNum] = messageMap;

	QByteArray buffer;
	QDataStream stream(&buffer,  QIODevice::ReadWrite);

	stream << messageMap;

	return buffer;
}

QByteArray ChatDialog::serializeMessage(QMap<QString, QVariant> messageToSend)
{
	QVariantMap messageMap;

	messageMap.insert("ChatText", messageToSend.value("ChatText"));
	messageMap.insert("Origin", messageToSend.value("Origin"));
	messageMap.insert("SeqNo", messageToSend.value("SeqNo"));

	QByteArray buffer;
	QDataStream stream(&buffer,  QIODevice::ReadWrite);

	stream << messageMap;

	return buffer;
}

void ChatDialog::sendRequestVoteRPC()
{
	quint32 lastLogTerm = 0;

	QVariantMap requestVoteMap;
	requestVoteMap.insert("term", nodeState.currentTerm);
	requestVoteMap.insert("candidateId", nodeState.id);
	requestVoteMap.insert("lastLogIndex", nodeState.lastApplied);

	QByteArray buffer;
	QDataStream stream(&buffer,  QIODevice::ReadWrite);


	if (nodeState.lastApplied != 0) {
		lastLogTerm = nodeState.logEntries[nodeState.lastApplied]["Term"].toUInt();
	}

	requestVoteMap.insert("lastLogTerm", lastLogTerm);

	stream << requestVoteMap;

	QList<quint16> peerList = socket->PeerList();

	for (int p = 0; p < peerList.size(); p++) {
		sendMessage(buffer, p);
	}


}

void ChatDialog::timeoutHandler()
{
	qDebug() << "TIMEOUT OCCURED!!!";

	// Timeout occurred change state to CANDIDATE send RequestVote
	nodeStatus = CANDIDATE;

	heartbeatTimer->stop();
}

void ChatDialog::antiEntropyHandler() 
{
	qDebug() << "ANTIENTROPY kicked in";

	QList<quint16> peerList = socket->PeerList();

	int index = rand() % peerList.size();

	socket->sendStatusMessage(QHostAddress::LocalHost, peerList[index], localStatusMap);
}

void ChatDialog::gotReturnPressed()
{
	QString text = textline->text();

//	textview->append(local_origin + ": " + textline->text());

	checkCommand(text);

	// Append to localStatusMap
	localStatusMap[local_origin] = currentSeqNum+1;

	// about to send message so set current waitingForStatus to true
//	currentState.waitingForStatus = 1;

	// about to send a message from chat diaglog, start a timer
//	timer->start(1000);

	sendRandomMessage(serializeLocalMessage(text));

	// If local message being forwarded, increment, else don't
	currentSeqNum++;

	// Clear the textline to get ready for the next input message.
	textline->clear();
}

void ChatDialog::checkCommand(QString text) {

	std::random_device rd; // obtain a random number from hardware
	std::mt19937 eng(rd()); // seed the generator
	std::uniform_int_distribution<> distr(150, 300); // define the range

	if (text.contains("START", Qt::CaseSensitive)) {
		qDebug() << "COMMAND START";

		// change state to follower and start timer
		nodeStatus = FOLLOWER;

		// waiting for heartbeat
		heartbeatTimer->start(distr(eng));

		// if timer runs out change state to CANDIDATE
		// else respond to heatbeats

	}
	else if (text.contains("MSG", Qt::CaseSensitive)) {
		qDebug() << "COMMAND MSG";

	}
	else if (text.contains("GET_CHAT", Qt::CaseSensitive)) {
		qDebug() << "COMMAND GET_CHAT";

	}
	else if (text.contains("STOP", Qt::CaseSensitive)) {
		qDebug() << "COMMAND STOP";


	}
	else if (text.contains("DROP", Qt::CaseSensitive)) {
		qDebug() << "COMMAND DROP";

	}
	else if (text.contains("RESTORE", Qt::CaseSensitive)) {
		qDebug() << "COMMAND RESTORE";

	}
	else if (text.contains("GET_NODES", Qt::CaseSensitive)) {
		qDebug() << "COMMAND GET_NODES";

	}
	else {
		qDebug() << "Did not recognize valid command";
	}
	return;
}

void ChatDialog::getRandomNeighbor() {

	QList<quint16> peerList;
	int index;

	peerList = socket->PeerList();

	if (neighborList.size() == 0) {
		
		index = rand() % peerList.size();
		neighborList.append(peerList[index]);

		peerList.removeOne(peerList[index]);
		
		index = rand() % peerList.size();
		neighborList.append(peerList[index]);
	}
}

void ChatDialog::sendMessage(QByteArray buffer, quint16 senderPort)
{
	qDebug() << "Sending to port: " << senderPort;

	socket->writeDatagram(buffer, buffer.size(), QHostAddress::LocalHost, senderPort);

}

void ChatDialog::sendRandomMessage(QByteArray buffer)
{
	int index;

	getRandomNeighbor();

	qDebug() << "Neighbor List: " << neighborList;

	index = rand() % neighborList.size();
	
	sendMessage(buffer, neighborList[index]);
}

void ChatDialog::cacheLastSentMessage(quint16 peerPort, QByteArray buffer)
{
	QMap<QString, QVariant> lastMessageSentMap;
	QDataStream stream(&buffer,  QIODevice::ReadOnly);
	stream >> lastMessageSentMap;

	last_message_sent[peerPort] = lastMessageSentMap;
}

void NetSocket::sendPingMessage(QHostAddress sendto, quint16 port)
{
	QByteArray ping;
	QDataStream stream(&ping,  QIODevice::ReadWrite);
	
	QMap<QString, QString> pingMsg;
	pingMsg["Ping"] = "Ping";

	stream << pingMsg;

	this->writeDatagram(ping, ping.size(), sendto, port);
}

void NetSocket::sendPingReply(QHostAddress sendto, quint16 port)
{
	QByteArray pingreply;
	QDataStream stream(&pingreply,  QIODevice::ReadWrite);

	QMap<QString, QString> pingReply;
	pingReply["PingReply"] = "PingReply";

	stream << pingReply;

	this->writeDatagram(pingreply, pingreply.size(), sendto, port);
}

void ChatDialog::Ping(NetSocket *socket) {
	int attempts = 3;

	while (attempts > 0) {
		if(neighborList.size() == 2) {
			break;
		}
		
		int timeout = 1000;
	
		if(socket->pingTimer.elapsed() > 0) {
			socket->pingTimer.restart();
		}
		else {
			socket->pingTimer.start();
		}

		for (int i = 0; i < socket->pingList.size(); i++) {
			socket->sendPingMessage(QHostAddress::LocalHost, socket->pingList[i]);
		}

		int remainingTime = timeout - socket->pingTimer.elapsed();
		while (remainingTime > 0) {
			remainingTime = timeout - socket->pingTimer.elapsed();
		}

		attempts--;
	}
}

void NetSocket::sendStatusMessage(QHostAddress sendto, quint16 port, QMap<QString, quint32> localStatusMap)
{
	QByteArray buffer;
	QDataStream stream(&buffer,  QIODevice::ReadWrite);
	QMap<QString, QMap<QString, quint32>> statusMessage;

	// Define message QMap
	statusMessage["Want"] = localStatusMap;

	qDebug() << "\nSending statusMessage: " << statusMessage;
	qDebug() << "sending status to peer: " << port;

	stream << statusMessage;

	this->writeDatagram(buffer, buffer.size(), sendto, port);
}

NetSocket::NetSocket()
{
	// Pick a range of four UDP ports to try to allocate by default,
	// computed based on my Unix user ID.
	// This makes it trivial for up to four P2Papp instances per user
	// to find each other on the same host,
	// barring UDP port conflicts with other applications
	// (which are quite possible).
	// We use the range from 32768 to 49151 for this purpose.
	myPortMin = 32768 + (getuid() % 4096)*4;
	myPortMax = myPortMin + 3;
}

QList<quint16> NetSocket::PeerList()
{
    QList<quint16> peerList;
	for (int p = myPortMin; p <= myPortMax; p++) {
	    if (this->localPort() != p) {
            peerList.append(p);
	    }
	}
    return peerList;
}

bool NetSocket::bind()
{
	// Try to bind to each of the range myPortMin..myPortMax in turn.
	for (int p = myPortMin; p <= myPortMax; p++) {
		if (QUdpSocket::bind(p)) {
			qDebug() << "bound to UDP port " << p;
			return true;
		}
	}

	qDebug() << "Oops, no ports in my default range " << myPortMin
		<< "-" << myPortMax << " available";
	return false;
}

int main(int argc, char **argv)
{
	// Initialize Qt toolkit
	QApplication app(argc,argv);

	// Create an initial chat dialog window
	ChatDialog dialog;
	dialog.show();

	// Enter the Qt main loop; everything else is event driven
	return app.exec();
}


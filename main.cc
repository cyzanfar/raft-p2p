
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

	// set vote empty string
	nodeState.votedFor = "";

	// // Initialize timer for heartbeat timeout
	heartbeatTimer = new QTimer(this);
	connect(heartbeatTimer, SIGNAL(timeout()), this, SLOT(handleHeartbeatTimeout()));

   	electionTimeout = new QTimer(this);
   	connect(electionTimeout, SIGNAL(timeout()), this, SLOT(handleElectionTimeout()));
	// socket->pingList = socket->PeerList();

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
		datagram.resize((int)socket->pendingDatagramSize());
		QHostAddress sender;
		quint16 senderPort;

		socket->readDatagram(datagram.data(), datagram.size(),
								&sender, &senderPort);

		qDebug() << "\nRECEIVING MESSAGE";

		processIncomingData(datagram, socket, senderPort);
	}
}

void ChatDialog::processRequestVote(QMap<QString, QVariant> voteRequest, quint16 senderPort)
{

	// If the logs have last entries with different terms,
	// then the log with the later term is more up-to-date.
	// If the logs end with the same term, then whichever log
	// is longer is more up-to-date.

	quint32 candidateTerm = voteRequest.value("term").toUInt();

	quint32 candidateLastLogIndex = voteRequest.value("lastLogIndex").toUInt();
    quint32 candidateLastLogTerm = voteRequest.value("lastLogTerm").toUInt();

    int localLastLogIndex = nodeState.lastApplied; // the last log index
    int localLastLogTerm = getLastTerm(); // the last log index


	if ((candidateTerm == nodeState.currentTerm) && (nodeState.votedFor != ""))
	{
		sendVote(0, senderPort);
	}
	else if (candidateTerm < nodeState.currentTerm) 
	{
		sendVote(0, senderPort);
	}
	else if ((candidateLastLogTerm < localLastLogTerm) || \
		(nodeState.votedFor != ""))
	{
		sendVote(0, senderPort);
	}
	else if ((candidateLastLogTerm == localLastLogTerm) && \
        (candidateLastLogIndex >= localLastLogIndex)) 
	{
		qDebug() << "Vote granted";
		nodeState.votedFor = voteRequest.value("candidateId").toString();
		sendVote(1, senderPort);

	}
	else
	{
		nodeState.currentTerm = candidateTerm;
		sendVote(0, senderPort);
	}
}

void ChatDialog::sendVote(quint8 vote, quint16 senderPort)
{
	QMap<QString, QMap<QString, QVariant>> voteToSend;
	QByteArray buffer;
	QDataStream stream(&buffer,  QIODevice::ReadWrite);

	voteToSend["VoteReply"].insert("vote", vote);

	stream << voteToSend;

	sendMessage(buffer, senderPort);

}

void ChatDialog::processAppendEntries(
		AppendEntryRPC appendEntry,
		quint16 senderPort)
{
	quint32 rcvTerm = appendEntry.term;
	QString rcvId = appendEntry.leaderId;
	quint32 rcvPrevLogIndex = appendEntry.prevLogIndex;
	quint32 rcvPrevLogTerm = appendEntry.prevLogTerm;
	quint32 rcvCommitIndex = appendEntry.leaderCommit;
	QMap <quint32, QMap<QString, QVariant>> entries = appendEntry.entries;

	// build response from append entries
	QMap<QString, QMap<QString, QVariant>> ackToSend;
	QByteArray buffer;
	QDataStream stream(&buffer, QIODevice::ReadWrite);


	if (nodeStatus == CANDIDATE)
	{

		if (rcvTerm >= nodeState.currentTerm)
		{
			// you recognize the leader and return to follower state because you're weak
			nodeStatus = FOLLOWER;
		}

	}

	if (rcvTerm < nodeState.currentTerm)
	{
		// reply false -> leader update its currenterm to 
		// rcv term and set itself to follower
		ackToSend["ACK"].insert("originid", nodeState.id);
		ackToSend["ACK"].insert("term", nodeState.currentTerm);
		ackToSend["ACK"].insert("success", 0);

		stream << ackToSend;
		
		sendMessage(buffer, senderPort);
		
		return;
	}

	if (nodeState.logEntries.contains(rcvPrevLogIndex)) {

		QMap<QString, QVariant> localEntry;

		localEntry = nodeState.logEntries[rcvPrevLogIndex];
		
		if (rcvPrevLogTerm != localEntry["term"]) 
		{
			// reply false
			ackToSend["ACK"].insert("originid", nodeState.id);
			ackToSend["ACK"].insert("term", nodeState.currentTerm);
			ackToSend["ACK"].insert("success", 0);

			stream << ackToSend;
			
			sendMessage(buffer, senderPort);

			for (quint32 i = rcvPrevLogIndex; i <= nodeState.lastApplied; i++) {
				nodeState.logEntries.remove(i);
			}

			nodeState.lastApplied = rcvPrevLogIndex-1;
			return;
		}
		else 
		{
			if (!entries.isEmpty()){
				for (int e = 0; e < entries.size(); e++){
					for (auto index : entries.keys()) {
						nodeState.logEntries[index] = entries[index];
					}
				}

				ackToSend["ACK"].insert("originid", nodeState.id);
				ackToSend["ACK"].insert("term", nodeState.currentTerm);
				ackToSend["ACK"].insert("success", 1);

				stream << ackToSend;
				
				sendMessage(buffer, senderPort);
			}
		}
	}

	if (rcvCommitIndex > nodeState.commitIndex)
	{
		if (rcvCommitIndex > nodeState.lastApplied) {
			nodeState.commitIndex = nodeState.lastApplied;
		}
		else {
			nodeState.commitIndex = rcvCommitIndex;
		}
	}
}

// Process the message read from pending messages from sock
void ChatDialog::processIncomingData(QByteArray datagramReceived, NetSocket *socket, quint16 senderPort)
{

	QMap<QString, QMap<QString, QVariant>> messageReceived;
	QDataStream stream_msg(&datagramReceived,  QIODevice::ReadWrite);
	stream_msg >> messageReceived;

	qDebug() << "Data received: " << messageReceived;

	if (messageReceived.contains("RequestVote"))
	{
        qDebug() << "MESSAGE CONTAINS REQUEST_VOTE";

		processRequestVote(messageReceived.value("RequestVote"), senderPort);
	}
	else if (messageReceived.contains("AppendEntries"))
	{
		qDebug() << "MESSAGE CONTAINS APPEND_ENTRIES";

		QMap<QString, QMap<QString, QMap<quint32 , QMap<QString, QVariant>>>> appendEntryMessage;
		QDataStream entries_msg(&datagramReceived,  QIODevice::ReadWrite);
		entries_msg >> appendEntryMessage;

		processAppendEntries(
				messageReceived.value("AppendEntries"),
				appendEntryMessage["AppendEntries"].value("entries"),
				senderPort);

	}
	else if (messageReceived.contains("VoteReply"))
	{
		qDebug() << "MESSAGE CONTAINS VOTE_REPLY";

        addVoteCount((quint8)messageReceived["VoteReply"]["vote"].toUInt());
	}

	else if (messageReceived.contains("ACK"))

	{
		processACK(messageReceived.value("ACK"), senderPort);
	}
	else {
		qDebug() << "Unsupported message RPC type";
	}
}

void ChatDialog::processACK(QMap<QString, QVariant> ack, quint16 senderPort)
{
	quint32 rcvAckTerm = ack.value("term").toUInt();
	quint32 rcvAckSuccess = ack.value("success").toUInt();
	// • If command received from client: append entry to local log, respond after entry applied to state machine (§5.3)
	// • If last log index ≥ nextIndex for a follower: send AppendEntries RPC with log entries starting at nextIndex
	// • If successful: update nextIndex and matchIndex for
	// follower (§5.3)
	// • If AppendEntries fails because of log inconsistency:
	// decrement nextIndex and retry (§5.3)
	// • If there exists an N such that N > commitIndex, a majority
	// of matchIndex[i] ≥ N, and log[N].term == currentTerm: set commitIndex = N (§5.3, §5.4).


	QString candidateId = ack.value("candidateId").toString();
	quint32 candidateNextIndex =  leaderState.nextIndex.value(candidateId).toUInt();
	

	if ((rcvAckTerm > nodeState.currentTerm) && (rcvAckSuccess == 0))
	{
		// BECOME follower
		nodeStatus = FOLLOWER;
		nodeState.currentTerm = rcvAckTerm;
		return;		
	}

	if (rcvAckSuccess == 0) 
	{
		QByteArray buffer;
		QDataStream stream(&buffer, QIODevice::ReadWrite);
		QMap<QString, AppendEntryRPC> appendEntryToSend;
		AppendEntryRPC appendEntry;


		leaderState.nextIndex[candidateId]= candidateNextIndex - 1;

		appendEntry.term = nodeState.currentTerm;
		appendEntry.leaderId = nodeState.id;
		appendEntry.prevLogIndex = nodeState.lastApplied;
		appendEntry.prevLogTerm = getLastTerm();
		appendEntry.leaderCommit = nodeState.commitIndex;

		
		for (int i = leaderState.nextIndex[candidateId].toUInt(); i <= nodeState.lastApplied; i++)
		{
			appendEntry.entries.insert("term", nodeState.logEntries[i].value("term"));
			appendEntry.entries.insert("command", nodeState.logEntries[i].value("command"));
		}


		appendEntryToSend.insert("AppendEntries", appendEntry);

		stream << appendEntryToSend;

		sendMessage(buffer, senderPort);
		
	}
	else
	{
		leaderState.nextIndex[candidateId] = nodeState.lastApplied + 1;
		leaderState.matchIndex[candidateId] = nodeState.lastApplied;

	}	
	
						
}



void ChatDialog::addVoteCount(quint8 vote)
{
     numberOfVotes += vote;

     // we know there are 5 nodes
     if (numberOfVotes >= 3)
     {
        // become leader and send heartbeat

        // set vote to 0
        numberOfVotes = 0;
        qDebug() << "BECAME FUCKING LEADER";
        // set status to LEADER
        nodeStatus = LEADER;

		// init nextIndex + 1 for each node
		// also for matchindex ?


     }
}

void ChatDialog::sendRequestVoteRPC()
{

	QMap<QString, QMap<QString, QVariant>> requestVoteMap;
	QByteArray buffer;
	QDataStream stream(&buffer, QIODevice::ReadWrite);

	requestVoteMap["RequestVote"].insert("term", nodeState.currentTerm);
	requestVoteMap["RequestVote"].insert("candidateId", nodeState.id);

	requestVoteMap["RequestVote"].insert("lastLogIndex", nodeState.lastApplied);
	requestVoteMap["RequestVote"].insert("lastLogTerm", getLastTerm());

	stream << requestVoteMap;

	QList<quint16> peerList = socket->PeerList();

	for (int p = 0; p < peerList.size(); p++) {
		sendMessage(buffer, peerList[p]);
	}
}

void ChatDialog::sendHeartbeat(quint16 port, QList<quint32>)
{

	QByteArray buffer;
	QDataStream stream(&buffer, QIODevice::ReadWrite);
	AppendEntryRPC appendEntry;
	QMap<QString, AppendEntryRPC> heartbeat;

	appendEntry.term = nodeState.currentTerm;
	appendEntry.leaderId = nodeState.id;
	appendEntry.prevLogIndex = nodeState.lastApplied;
	appendEntry.prevLogTerm = getLastTerm();
	appendEntry.leaderCommit = nodeState.commitIndex;

	heartbeat.insert("AppendEntries", appendEntry);

	stream << heartbeat;

	sendMessage(buffer, port);

}

int ChatDialog::getLastTerm()
{
	int response = 0;

	if (!nodeState.logEntries.isEmpty())
	{
		response = nodeState.logEntries[nodeState.lastApplied].value("term")
	}

    return response;
}

void ChatDialog::handleHeartbeatTimeout()
{
	qDebug() << "HEARTBEAT TIMEOUT OCCURED!!!";

	// when trasmitioning to candidate state
	nodeState.currentTerm++;

	nodeStatus = CANDIDATE;

	numberOfVotes = 1;
	
	heartbeatTimer->stop();

	nodeState.votedFor = nodeState.id;

	sendRequestVoteRPC();
	
    numberOfVotes++;

	heartbeatTimer->start(generateRandomTimeRange(150, 300));

	electionTimeout->start(generateRandomTimeRange(300, 450));
}

void ChatDialog::handleElectionTimeout()
{
   qDebug() << "REQUESTVOTE TIMEOUT OCCURED!!!";

   numberOfVotes = 0;

   electionTimeout->stop();

   sendRequestVoteRPC();

   electionTimeout->start(generateRandomTimeRange(300, 400));

}

void ChatDialog::gotReturnPressed()
{
	QString text = textline->text();

//	textview->append(local_origin + ": " + textline->text());

	checkCommand(text);
	// Clear the textline to get ready for the next input message.
	textline->clear();
}

int ChatDialog::generateRandomTimeRange(int min, int max)
{
    std::random_device rd; // obtain a random number from hardware
    std::mt19937 eng(rd()); // seed the generator
    std::uniform_int_distribution<> distr(min, max); // define the range
    return distr(eng);
}

void ChatDialog::checkCommand(QString text) {


	if (text.contains("START", Qt::CaseSensitive)) {
		qDebug() << "COMMAND START";

		// change state to follower and start timer
		nodeStatus = FOLLOWER;

 		// waiting for heartbeat
		heartbeatTimer->start(generateRandomTimeRange(150, 300));

		// if timer runs out change state to CANDIDATE
		// else respond to heatbeats

	}
	else if (text.contains("MSG", Qt::CaseSensitive)) {
		QMap<>QVariantMap logItem;

		logItem.insert("command", "MSG");
		logItem.insert("term", nodeState.currentTerm);
//		messageMap.insert("Origin", messageToSend.value("Origin"));

		QByteArray buffer;
		QDataStream stream(&buffer,  QIODevice::ReadWrite);

		stream << logItem;

		qDebug() << "COMMAND MSG";

		if (leaderPort){
			sendMessage(buffer, leaderPort);
		}
		else {
			QList<quint16> peerList = socket->PeerList();

			randomPeer = generateRandomTimeRange(peerList[0], peerList[peerList.size()-1]);
			
			sendMessage(buffer, peerList[p]);
		}
	}
	else if (text.contains("GET_CHAT", Qt::CaseSensitive)) {
		// Print current chat history of the selected node
		qDebug() << "COMMAND GET_CHAT";

	}
	else if (text.contains("STOP", Qt::CaseSensitive)) {
		qDebug() << "COMMAND STOP";

	}
	else if (text.contains("DROP", Qt::CaseSensitive)) {
		qDebug() << "COMMAND DROP";

		// Drop incoming packets from specified node
	}
	else if (text.contains("RESTORE", Qt::CaseSensitive)) {
		qDebug() << "COMMAND RESTORE";

		// Restore (reverse drop) to accept packets from specified node
	}
	else if (text.contains("GET_NODES", Qt::CaseSensitive)) {
		qDebug() << "COMMAND GET_NODES";
		// Get all node ids
		// show raft state, if leader elected, show id
		// State of current node
	}
	else {
		qDebug() << "Did not recognize valid command";
	}
	return;
}

void ChatDialog::sendMessage(QByteArray buffer, quint16 senderPort)
{
	qDebug() << "Sending to port: " << senderPort;

	socket->writeDatagram(buffer, buffer.size(), QHostAddress::LocalHost, senderPort);

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
	myPortMax = myPortMin + 4;
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


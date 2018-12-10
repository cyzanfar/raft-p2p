
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

//	socket->pingList = socket->PeerList();

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


//
//void NetSocket::processPingMessage(QHostAddress sender, quint16 senderPort) {
//	// Send ping reply
//	sendPingReply(sender, senderPort);
//}

void ChatDialog::processRequestVote(QMap<QString, QVariant> voteRequest, quint16 senderPort)
{

	// If the logs have last entries with different terms,
	// then the log with the later term is more up-to-date.
	// If the logs end with the same term, then whichever log
	// is longer is more up-to-date.

	quint32 candidateTerm = voteRequest.value("term").toUInt();
	quint32 candidateLastAppliedIndex = voteRequest.value("lastLogIndex").toUInt();

	if ((candidateTerm < nodeState.currentTerm) || \
		(nodeState.votedFor != NULL))
	{
		sendVote(0, senderPort);
	}
	else if ((candidateTerm == nodeState.currentTerm) && \
        candidateLastAppliedIndex >= nodeState.lastApplied)
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
	QMap<QString, QMap<QString, quint8>> voteToSend;
	QByteArray buffer;
	QDataStream stream(&buffer,  QIODevice::ReadWrite);

	voteToSend["VoteReply"]["vote"] = vote;

	stream << voteToSend;

	sendMessage(buffer, senderPort);

}

void ChatDialog::processAppendEntries(QMap<QString, QVariant> AppendEntries)
{
	// do
}

// Process the message read from pending messages from sock
void ChatDialog::processIncomingData(QByteArray datagramReceived, NetSocket *socket, quint16 senderPort)
{

	QMap<QString, QMap<QString, QVariant>> messageReceived;
	QDataStream stream_msg(&datagramReceived,  QIODevice::ReadOnly);
	stream_msg >> messageReceived;

	if (messageReceived.contains("RequestVote"))
	{
		processRequestVote(messageReceived.value("RequestVote"), senderPort);
	}
	else if (messageReceived.contains("AppendEntries"))
	{
		processAppendEntries(messageReceived.value("AppendEntries"));
	}
	else if (messageReceived.contains("VoteReply"))
	{
        addVoteCount((quint8)messageReceived["VoteReply"]["vote"].toUInt());
	}
	else {
		qDebug() << "Unsupported message RPC type";
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
        // set status to LEADER
        nodeStatus = LEADER;

     }
}

void ChatDialog::sendRequestVoteRPC()
{

	QMap<QString, QMap<QString, QVariant>> requestVoteMap;
	QByteArray buffer;
	QDataStream stream(&buffer,  QIODevice::ReadWrite);

	requestVoteMap["RequestVote"].insert("term", nodeState.currentTerm);
	requestVoteMap["RequestVote"].insert("candidateId", nodeState.id);

	requestVoteMap["RequestVote"].insert("lastLogIndex", getLastEntryFor(nodeState.logEntries, 0));
	requestVoteMap["RequestVote"].insert("lastLogTerm", getLastEntryFor(nodeState.logEntries, 1));

	stream << requestVoteMap;

	QList<quint16> peerList = socket->PeerList();

	for (int p = 0; p < peerList.size(); p++) {
		sendMessage(buffer, p);
	}
}

int ChatDialog::getLastEntryFor(QList<std::tuple<quint16, quint16, QString>> logEntries, int pos)
{
	std::tuple<quint16, quint16, QString> entry;

	int response = 0;

	if (!logEntries.isEmpty())
	{
		entry = logEntries.first();
		switch(pos)
		{
		    case 0:
		        response = std::get<0>(entry);
		        break;
		    case 1:
                response = std::get<1>(entry);
		        break;
		    default:
		        break;

		}
	}

    return response;
}

void ChatDialog::timeoutHandler()
{
	qDebug() << "TIMEOUT OCCURED!!!";

	// when trasitioning to candidate state, follower
	nodeState.currentTerm ++;

	nodeStatus = CANDIDATE;

	numberOfVotes = 0;

    sendRequestVoteRPC();

	heartbeatTimer->stop();
}

void ChatDialog::gotReturnPressed()
{
	QString text = textline->text();

//	textview->append(local_origin + ": " + textline->text());

	checkCommand(text);
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


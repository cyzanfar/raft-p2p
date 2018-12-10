#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QtNetwork/QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include<tuple>

// enum for state of a node
enum node_status { WAITING, FOLLOWER, CANDIDATE, LEADER };

struct node_state {
	int currentTerm; // init to zero
	QString votedFor = NULL;
	QString id;
	QMap<quint32, QMap<QString, QVariant>> logEntries;
	volatile int commitIndex;
	volatile int lastApplied;
	bool isLeader;
	bool voteGranted; // TODO should this be a list for majority

};

struct leader_state {

	QMap<QString, QVariant> nextIndex; // only present on leader
	QMap<QString, QVariant> matchIndex; // only present on leader
};


class NetSocket : public QUdpSocket
{
	Q_OBJECT

	public:
		NetSocket();
		QList<quint16> PeerList();
		void sendPingMessage(QHostAddress sendto, quint16 port);

		void sendStatusMessage(QHostAddress sendto, quint16 port, QMap<QString, quint32> localStatusMap);
		bool bind();

	private:
		int myPortMin, myPortMax;
};


class ChatDialog : public QDialog
{
	Q_OBJECT

public:
	ChatDialog();
	NetSocket *socket;
	QString local_origin;
	QList<quint16> neighborList;
	QTimer *heartbeatTimer;
	void checkCommand(QString command);
	void sendRequestVoteRPC();
	void processRequestVote(QMap<QString, QVariant> voteRequest, quint16 senderPort);
	void processAppendEntries(QMap<QString, QVariant> voteRequest);
	void sendVote(quint8 vote, quint16 senderPort);

public slots:
	void gotReturnPressed();
	void readPendingMessages();
	void timeoutHandler();
	void antiEntropyHandler();

private:
	QTextEdit *textview;
	QLineEdit *textline;
	void Ping(NetSocket *sock);
	void processReceivedMessage(QMap<QString, QVariant> messageReceived,QHostAddress sender, quint16 senderPort);
	void sendMessage(QByteArray buffer, quint16 senderPort);
	void sendRandomMessage(QByteArray buffer);
	void processIncomingData(QByteArray datagramReceived, NetSocket *socket, quint16 senderPort);
	QByteArray serializeLocalMessage(QString messageText);
	QByteArray serializeMessage(QMap<QString, QVariant> messageToSend);
	void processStatusMessage(QMap<QString, QMap<QString, quint32> > peerWantMap, QHostAddress sender, quint16 senderPort);
	void cacheLastSentMessage(quint16 peerPost, QByteArray buffer);
	void getRandomNeighbor();
};

#endif // P2PAPP_MAIN_HH

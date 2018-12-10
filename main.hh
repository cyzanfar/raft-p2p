#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QtNetwork/QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include<tuple>

// enum for status of a node
enum node_status { WAITING, FOLLOWER, CANDIDATE, LEADER };


struct node_state {
	quint32 currentTerm; // init to 0
	QString votedFor;
	QString id; // init to node id
	QList<std::tuple<quint16, quint16, QString>> logEntries; // empty on start
	volatile quint32 commitIndex; // init to 0
	volatile quint32 lastApplied; // init to 0
	bool isLeader; // TODO init to false?

};

struct leader_state {

	QMap<QString, QVariant> nextIndex; // init to leader last log+1
	QMap<QString, QVariant> matchIndex; // init to 0
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
	void sendHeartbeat();
	QTimer *requestVoteTimer;
	void processRequestVote(QMap<QString, QVariant> voteRequest, quint16 senderPort);
	void processAppendEntries(QMap<QString, QVariant> AppendEntries);
	void sendVote(quint8 vote, quint16 senderPort);
	int generateRandomTimeRange();

public slots:
	void gotReturnPressed();
	void readPendingMessages();
	void handleHeartbeatTimeout();
	void handleRequestVoteTimeout();

private:
	QTextEdit *textview;
	QLineEdit *textline;
	quint16 numberOfVotes;
	void checkCommand(QString command);
	void sendRequestVoteRPC();
	void addVoteCount(quint8 vote);
	void sendMessage(QByteArray buffer, quint16 senderPort);
	void processIncomingData(QByteArray datagramReceived, NetSocket *socket, quint16 senderPort);
	int getLastEntryFor(QList<std::tuple<quint16, quint16, QString>> logEntries, int pos);
};

#endif // P2PAPP_MAIN_HH

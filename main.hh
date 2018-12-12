#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QtNetwork/QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <tuple>

#define REQUEST_VOTE "RequestVote"
#define APPEND_ENTRIES "AppendEntries"
#define VOTE_REPLY "VoteReply"
#define ACK "ACK"
#define MSG_ACK "MSGACK"
#define MSG "MSG"


// enum for status of a node
enum node_status { WAITING, FOLLOWER, CANDIDATE, LEADER };


struct node_state {
	quint32 currentTerm; // init to 0
	QString votedFor;
	QString id; // init to node id
	// origin, message
	QList<QMap<QString, QVariant>> messageList;
	// index, term, command
	QMap<quint32, QMap<QString, QVariant>> logEntries; // empty on start
	// index of highest log entry known commited
	quint32 commitIndex; // init to 0
	// index of highest log entry applied
	quint32 lastApplied; // init to 0
	quint32 nextPending;
	quint16 leaderPort;
};

struct leader_state {
	QMap<QString, QVariant> nextIndex; // init to leader last log+1
	QMap<QString, QVariant> matchIndex; // init to 0
};

class AppendEntryRPC
{
	public:
		AppendEntryRPC();
		quint32 term;
		QString leaderId;
		quint32 prevLogIndex;
		quint32 prevLogTerm;
		QMap<quint32, QMap<QString, QVariant>> entries;
		quint32 leaderCommit;
		QByteArray serializeObject();
		void deserializeStream(QByteArray receivedData);
};

class NetSocket : public QUdpSocket
{
	Q_OBJECT

	public:
		NetSocket();
		QList<quint16> PeerList();
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
	QList<QString> droppedNodes;
	QTimer *heartbeatTimer;
	QTimer *leaderTimeout;
	void sendHeartbeat(quint16 port);
	QTimer *electionTimeout;
	void processRequestVote(QMap<QString, QVariant> voteRequest, quint16 senderPort);
	void processAppendEntries(AppendEntryRPC appendEntry, quint16 port);
	void sendVote(quint8 vote, quint16 senderPort);
	int generateRandomTimeRange(int min, int max);

public slots:
	void gotReturnPressed();
	void readPendingMessages();
	void handleHeartbeatTimeout();
	void handleElectionTimeout();
	void handleLeaderTimeout();

private:
	QTextEdit *textview;
	QLineEdit *textline;
	quint16 numberOfVotes;
	quint16 numberOfMsgVotes;
	quint32 getLastTerm();
	void checkCommand(QString command);
	void sendRequestVoteRPC();
	void addVoteCount(quint8 vote);
	void sendMsgACK(quint16 senderPort, QString origin);
	void addMsgVoteCount(quint8 msgSuccess, QString origin);
	void sendMessage(QByteArray buffer, quint16 senderPort);
	void processIncomingData(QByteArray datagramReceived, NetSocket *socket, quint16 senderPort);
	void processACK(QMap<QString, QVariant> ack, quint16 senderPort);
    void getNodeCommand();
	void processDropNode(QString dropNodeMessage);
	void restoreDroppedNode(QString restoreNodeMessage);
	void processMessageReceived(QString messageReceived, QString origin);
    void sendAckToLeader(quint8 success, quint16 senderPort);
};


#endif // P2PAPP_MAIN_HH

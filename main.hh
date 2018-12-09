#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>

// state struct for waiting ACK (status) or not
struct state {
    int waitingForStatus;
};

class NetSocket : public QUdpSocket
{
	Q_OBJECT

	public:
		QList<quint16> pingList;
		QMap<quint16, int> pingTimes;
		QElapsedTimer pingTimer;
		NetSocket();
		QList<quint16> PeerList();
		void sendPingMessage(QHostAddress sendto, quint16 port);
		void sendStatusMessage(QHostAddress sendto, quint16 port, QMap<QString, quint32> localStatusMap);
		void processPingMessage(QHostAddress sender, quint16 senderPort);
		void processPingReply(quint16 senderPort, QList<quint16> neighborList);

		// Bind this socket to a P2Papp-specific default port.
		bool bind();

	private:
		int myPortMin, myPortMax;
		void sendPingReply(QHostAddress sendto, quint16 port);
};


class ChatDialog : public QDialog
{
	Q_OBJECT

public:
	ChatDialog();
	NetSocket *socket;
	quint32 currentSeqNum;
	QString local_origin;
	QMap<QString, quint32> localStatusMap;
	QMap<quint16, QMap<QString, QVariant>> last_message_sent;
	QList<quint16> neighborList;
	QTimer *timer;
	QTimer *antiEntropyTimer;
	QMap<QString, QMap<quint32, QMap<QString, QVariant>>> messageList;


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
	void sendMessage(QByteArray buffer, quint16 port);
	void sendRandomMessage(QByteArray buffer);
	void processIncomingData(QByteArray datagramReceived, QHostAddress sender, quint16 senderPort, NetSocket *socket);
	QByteArray serializeLocalMessage(QString messageText);
	QByteArray serializeMessage(QMap<QString, QVariant> messageToSend);
	void processStatusMessage(QMap<QString, QMap<QString, quint32>> peerWantMap, QHostAddress sender, quint16 senderPort);
	void cacheLastSentMessage(quint16 peerPost, QByteArray buffer);
	void getRandomNeighbor();
};

#endif // P2PAPP_MAIN_HH

#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>

class QTcpSocket;

namespace mixxx {

class Os2lConnection : public QObject {
    Q_OBJECT
  public:
    Os2lConnection(const QString& name,
            const QHostAddress& host,
            quint16 port,
            QObject* pParent = nullptr);
    ~Os2lConnection() override;

    void connectToHost();
    void disconnect();
    bool isConnected() const;
    QString name() const;

    void sendBeat(int pos, double bpm, bool change, double strength);
    void sendButton(const QString& name, const QString& state,
            const QString& page = QString());
    void sendCommand(int id, double param);

  signals:
    void connected();
    void disconnected();

  private slots:
    void slotConnected();
    void slotDisconnected();
    void slotError();

  private:
    void sendJson(const QByteArray& data);

    QString m_name;
    QHostAddress m_host;
    quint16 m_port;
    QTcpSocket* m_pSocket;
};

} // namespace mixxx

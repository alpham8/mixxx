#include "os2l/os2lconnection.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include "moc_os2lconnection.cpp"

namespace mixxx {

Os2lConnection::Os2lConnection(const QString& name,
        const QHostAddress& host,
        quint16 port,
        QObject* pParent)
        : QObject(pParent),
          m_name(name),
          m_host(host),
          m_port(port),
          m_pSocket(new QTcpSocket(this)) {
    connect(m_pSocket, &QTcpSocket::connected,
            this, &Os2lConnection::slotConnected);
    connect(m_pSocket, &QTcpSocket::disconnected,
            this, &Os2lConnection::slotDisconnected);
    connect(m_pSocket, &QTcpSocket::errorOccurred,
            this, &Os2lConnection::slotError);
}

Os2lConnection::~Os2lConnection() {
    disconnect();
}

void Os2lConnection::connectToHost() {
    if (m_pSocket->state() == QAbstractSocket::UnconnectedState) {
        m_pSocket->connectToHost(m_host, m_port);
    }
}

void Os2lConnection::disconnect() {
    if (m_pSocket->state() != QAbstractSocket::UnconnectedState) {
        m_pSocket->disconnectFromHost();
    }
}

bool Os2lConnection::isConnected() const {
    return m_pSocket->state() == QAbstractSocket::ConnectedState;
}

QString Os2lConnection::name() const {
    return m_name;
}

void Os2lConnection::sendBeat(int pos, double bpm, bool change, double strength) {
    QJsonObject obj;
    obj[QStringLiteral("evt")] = QStringLiteral("beat");
    obj[QStringLiteral("pos")] = pos;
    obj[QStringLiteral("bpm")] = bpm;
    obj[QStringLiteral("change")] = change;
    obj[QStringLiteral("strength")] = strength;
    sendJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void Os2lConnection::sendButton(const QString& name,
        const QString& state,
        const QString& page) {
    QJsonObject obj;
    obj[QStringLiteral("evt")] = QStringLiteral("btn");
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("state")] = state;
    if (!page.isEmpty()) {
        obj[QStringLiteral("page")] = page;
    }
    sendJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void Os2lConnection::sendCommand(int id, double param) {
    QJsonObject obj;
    obj[QStringLiteral("evt")] = QStringLiteral("cmd");
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("param")] = param;
    sendJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void Os2lConnection::sendJson(const QByteArray& data) {
    if (!isConnected()) {
        return;
    }
    m_pSocket->write(data);
    m_pSocket->write("\n");
}

void Os2lConnection::slotConnected() {
    qDebug() << "[OS2L] Connected to" << m_name;
    emit connected();
}

void Os2lConnection::slotDisconnected() {
    qDebug() << "[OS2L] Disconnected from" << m_name;
    emit disconnected();
}

void Os2lConnection::slotError() {
    qWarning() << "[OS2L] Connection error for" << m_name
               << m_pSocket->errorString();
}

} // namespace mixxx

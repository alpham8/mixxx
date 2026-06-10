#include "os2l/seratoemulation.h"

#include <QHostInfo>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QtEndian>

#include <csignal>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "track/track.h"

#include "moc_seratoemulation.cpp"

namespace mixxx {

namespace {
constexpr const char* kServiceType = "_SeratoIOSRemote._tcp";
constexpr int kPlayheadIntervalMs = 3;
constexpr quint16 kDefaultPort = 0;
} // namespace

SeratoEmulation::SeratoEmulation(UserSettingsPointer pConfig, QObject* pParent)
        : QObject(pParent),
          m_pConfig(pConfig),
          m_pServer(new QTcpServer(this)),
          m_pClient(nullptr),
          m_pPlayheadTimer(new QTimer(this)),
          m_paired(false),
          m_avahiPid(0) {
    connect(m_pServer, &QTcpServer::newConnection,
            this, &SeratoEmulation::slotNewConnection);

    m_pPlayheadTimer->setInterval(kPlayheadIntervalMs);
    m_pPlayheadTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pPlayheadTimer, &QTimer::timeout,
            this, &SeratoEmulation::slotSendPlayhead);
}

SeratoEmulation::~SeratoEmulation() {
    stop();
}

void SeratoEmulation::start() {
    if (m_pServer->isListening()) {
        return;
    }

    if (!m_pServer->listen(QHostAddress::Any, kDefaultPort)) {
        qWarning() << "[SeratoEmu] Failed to start TCP server:"
                   << m_pServer->errorString();
        return;
    }

    quint16 port = m_pServer->serverPort();
    qDebug() << "[SeratoEmu] Listening on port" << port;

    // Register mDNS service via avahi-publish-service
    QString hostname = QHostInfo::localHostName();
    QString serviceName = QStringLiteral("SDJ @ %1").arg(hostname.toUpper());

    QStringList args;
    args << QStringLiteral("avahi-publish-service")
         << serviceName
         << QString::fromLatin1(kServiceType)
         << QString::number(port);

    m_avahiPid = fork();
    if (m_avahiPid == 0) {
        QByteArray name = serviceName.toUtf8();
        QByteArray type = QByteArray(kServiceType);
        QByteArray portStr = QString::number(port).toUtf8();
        execlp("avahi-publish-service",
                "avahi-publish-service",
                name.constData(),
                type.constData(),
                portStr.constData(),
                nullptr);
        _exit(1);
    } else if (m_avahiPid > 0) {
        qDebug() << "[SeratoEmu] mDNS service registered:"
                 << serviceName << kServiceType << port;
    } else {
        qWarning() << "[SeratoEmu] Failed to fork avahi-publish-service";
    }

    QTimer::singleShot(2000, this, [this]() {
        connectDeckControls();
    });
}

void SeratoEmulation::stop() {
    m_pPlayheadTimer->stop();

    if (m_pClient) {
        m_pClient->disconnectFromHost();
        m_pClient = nullptr;
    }

    m_pServer->close();
    m_paired = false;

    if (m_avahiPid > 0) {
        kill(m_avahiPid, SIGTERM);
        m_avahiPid = 0;
    }

    m_pBeatActive.reset();
    m_pPlayposition1.reset();
    m_pPlayposition2.reset();
    m_pBpm1.reset();
    m_pBpm2.reset();
    m_pPlay1.reset();
    m_pPlay2.reset();
    m_pCrossfader.reset();
    m_pVolume1.reset();
    m_pVolume2.reset();

    qDebug() << "[SeratoEmu] Stopped";
}

bool SeratoEmulation::isRunning() const {
    return m_pServer->isListening();
}

bool SeratoEmulation::isConnected() const {
    return m_pClient && m_pClient->state() == QTcpSocket::ConnectedState && m_paired;
}

void SeratoEmulation::slotNewConnection() {
    QTcpSocket* pSocket = m_pServer->nextPendingConnection();
    if (!pSocket) {
        return;
    }

    if (m_pClient) {
        qDebug() << "[SeratoEmu] Rejecting second connection";
        pSocket->disconnectFromHost();
        pSocket->deleteLater();
        return;
    }

    m_pClient = pSocket;
    m_paired = false;
    connect(m_pClient, &QTcpSocket::disconnected,
            this, &SeratoEmulation::slotClientDisconnected);
    connect(m_pClient, &QTcpSocket::readyRead,
            this, &SeratoEmulation::slotClientReadyRead);

    qDebug() << "[SeratoEmu] Client connected from"
             << m_pClient->peerAddress().toString();
}

void SeratoEmulation::slotClientDisconnected() {
    qDebug() << "[SeratoEmu] Client disconnected:" << m_clientName;
    m_pPlayheadTimer->stop();
    m_pClient->deleteLater();
    m_pClient = nullptr;
    m_paired = false;
    m_clientName.clear();
    emit clientDisconnected();
}

void SeratoEmulation::slotClientReadyRead() {
    if (!m_pClient) {
        return;
    }
    QByteArray data = m_pClient->readAll();
    handleOscMessage(data);
}

void SeratoEmulation::handleOscMessage(const QByteArray& data) {
    if (data.size() < 4) {
        return;
    }

    // Check for /StreamMgmt/Authorize/Request
    if (data.contains("/StreamMgmt/Authorize/Request")) {
        // Extract the auth token blob (16 bytes after type tag ,bii)
        int blobStart = data.indexOf(",bii");
        if (blobStart > 0) {
            int blobOffset = ((blobStart + 5 + 3) & ~3);
            if (blobOffset + 4 <= data.size()) {
                qint32 blobLen = qFromBigEndian<qint32>(
                        data.constData() + blobOffset);
                if (blobLen == 16 && blobOffset + 4 + 16 <= data.size()) {
                    m_authToken = data.mid(blobOffset + 4, 16);
                }
            }
        }
        qDebug() << "[SeratoEmu] Authorize request received";
        sendAuthorizeResponse(m_authToken);
        return;
    }

    // Check for /StreamMgmt/Pairing/Pair
    if (data.contains("/StreamMgmt/Pairing/Pair")) {
        // Extract client name from the OSC string args
        int typeStart = data.indexOf(",ssi");
        if (typeStart > 0) {
            int strOffset = ((typeStart + 5 + 3) & ~3);
            int end = data.indexOf('\0', strOffset);
            if (end > strOffset) {
                m_clientName = QString::fromUtf8(
                        data.mid(strOffset, end - strOffset));
            }
        }
        qDebug() << "[SeratoEmu] Pairing request from:" << m_clientName;
        sendPairingStatus();
        sendRegistrations();
        sendInitialState();

        m_paired = true;
        m_pPlayheadTimer->start();

        emit clientConnected(m_clientName);
        return;
    }

    // /Ping messages - respond with auth token
    if (data.contains("/Ping")) {
        if (!m_authToken.isEmpty()) {
            sendOscMessage(m_authToken);
        }
        return;
    }
}

void SeratoEmulation::sendOscMessage(const QByteArray& data) {
    if (m_pClient && m_pClient->state() == QTcpSocket::ConnectedState) {
        m_pClient->write(data);
    }
}

// === OSC Message Building ===

QByteArray SeratoEmulation::oscString(const QString& s) {
    QByteArray bytes = s.toUtf8();
    bytes.append('\0');
    while (bytes.size() % 4 != 0) {
        bytes.append('\0');
    }
    return bytes;
}

QByteArray SeratoEmulation::oscInt(int value) {
    QByteArray bytes(4, '\0');
    qToBigEndian<qint32>(value, bytes.data());
    return bytes;
}

QByteArray SeratoEmulation::oscFloat(float value) {
    QByteArray bytes(4, '\0');
    qToBigEndian<quint32>(*reinterpret_cast<quint32*>(&value), bytes.data());
    return bytes;
}

QByteArray SeratoEmulation::oscBlob(const QByteArray& data) {
    QByteArray result;
    qint32 len = data.size();
    result.append(4, '\0');
    qToBigEndian<qint32>(len, result.data());
    result.append(data);
    while (result.size() % 4 != 0) {
        result.append('\0');
    }
    return result;
}

QByteArray SeratoEmulation::buildOscMessage(const QString& path,
        const QByteArray& typeTag,
        const QByteArray& args) {
    QByteArray msg;
    msg.append(oscString(path));
    msg.append(oscString(QStringLiteral(",") + QString::fromLatin1(typeTag)));
    msg.append(args);
    return msg;
}

QByteArray SeratoEmulation::buildOscBundle(
        const std::vector<QByteArray>& messages) {
    QByteArray bundle;
    bundle.append("#bundle\0", 8);
    bundle.append(8, '\0');
    for (const auto& msg : messages) {
        qint32 len = msg.size();
        QByteArray lenBytes(4, '\0');
        qToBigEndian<qint32>(len, lenBytes.data());
        bundle.append(lenBytes);
        bundle.append(msg);
    }
    return bundle;
}

// === Protocol Messages ===

void SeratoEmulation::sendAuthorizeResponse(const QByteArray& authToken) {
    QString uuid = QUuid::createUuid().toString();
    QString hostname = QHostInfo::localHostName();
    QString name = QStringLiteral("Mixxx @ %1").arg(hostname);

    QByteArray args;
    args.append(oscString(uuid));
    args.append(oscString(name));
    args.append(oscBlob(authToken));

    QByteArray msg = buildOscMessage(
            QStringLiteral("/StreamMgmt/Authorize/Response"),
            QByteArrayLiteral("ssb"),
            args);

    std::vector<QByteArray> msgs = {msg};
    sendOscMessage(buildOscBundle(msgs));
}

void SeratoEmulation::sendPairingStatus() {
    QByteArray args;
    args.append(oscString(QStringLiteral("Paired")));

    QByteArray msg = buildOscMessage(
            QStringLiteral("/StreamMgmt/Pairing/StatusChanged"),
            QByteArrayLiteral("s"),
            args);

    // Also send auth token after bundle
    std::vector<QByteArray> msgs = {msg};
    QByteArray bundle = buildOscBundle(msgs);
    sendOscMessage(bundle);
    if (!m_authToken.isEmpty()) {
        sendOscMessage(m_authToken);
    }
}

void SeratoEmulation::sendRegistrations() {
    // SoundSwitch expects registration messages indicating what data we'll send
    std::vector<QString> paths = {
            QStringLiteral("/Register/Status/Deck/Song/Title"),
            QStringLiteral("/Register/Status/Deck/Playhead"),
            QStringLiteral("/Register/Status/Deck/Song/Artist"),
            QStringLiteral("/Register/Status/Deck/Song/Valid"),
            QStringLiteral("/Register/Status/Deck/Song/Filepath"),
    };

    std::vector<QByteArray> msgs;
    for (const auto& path : paths) {
        msgs.push_back(buildOscMessage(path, QByteArrayLiteral(""), QByteArray()));
    }
    sendOscMessage(buildOscBundle(msgs));
    if (!m_authToken.isEmpty()) {
        sendOscMessage(m_authToken);
    }
}

void SeratoEmulation::sendInitialState() {
    // Send empty state for 4 decks (indices 0-3)
    for (int i = 0; i < 4; ++i) {
        sendSongValid(i, 0.0f);
        sendSongInfo(i);
        sendLoopState(i);
        sendPlayhead(i);
    }
    sendCrossfader(0.5f);
    for (int i = 0; i < 4; ++i) {
        sendDeckUpfader(i, 1.0f);
    }
}

void SeratoEmulation::sendPlayhead(int deckIndex) {
    if (!m_pClient || !m_paired) {
        return;
    }

    float position = 0.0f;
    float bpm = 0.0f;
    float beatPhase = 0.0f;

    if (deckIndex == 0 && m_pPlayposition1) {
        position = static_cast<float>(m_pPlayposition1->get());
        if (m_pBpm1) {
            bpm = static_cast<float>(m_pBpm1->get());
        }
    } else if (deckIndex == 1 && m_pPlayposition2) {
        position = static_cast<float>(m_pPlayposition2->get());
        if (m_pBpm2) {
            bpm = static_cast<float>(m_pBpm2->get());
        }
    }

    QByteArray args;
    args.append(oscInt(deckIndex));
    args.append(oscFloat(position));
    args.append(oscFloat(bpm));
    args.append(oscFloat(beatPhase));

    QByteArray msg = buildOscMessage(
            QStringLiteral("/Status/Deck/Playhead"),
            QByteArrayLiteral("ifff"),
            args);

    std::vector<QByteArray> msgs = {msg};
    sendOscMessage(buildOscBundle(msgs));
}

void SeratoEmulation::sendDeckUpfader(int deckIndex, float value) {
    QByteArray args;
    args.append(oscInt(deckIndex));
    args.append(oscFloat(value));

    QByteArray msg = buildOscMessage(
            QStringLiteral("/Status/Video/Deck/Mixer/Upfader"),
            QByteArrayLiteral("if"),
            args);

    std::vector<QByteArray> msgs = {msg};
    sendOscMessage(buildOscBundle(msgs));
}

void SeratoEmulation::sendCrossfader(float value) {
    QByteArray args;
    args.append(oscFloat(value));

    QByteArray msg = buildOscMessage(
            QStringLiteral("/Status/Video/Mixer/Crossfader"),
            QByteArrayLiteral("f"),
            args);

    std::vector<QByteArray> msgs = {msg};
    sendOscMessage(buildOscBundle(msgs));
}

void SeratoEmulation::sendSongInfo(int deckIndex) {
    // Title
    {
        QByteArray args;
        args.append(oscInt(deckIndex));
        args.append(oscString(QString()));
        QByteArray msg = buildOscMessage(
                QStringLiteral("/Status/Deck/Song/Title"),
                QByteArrayLiteral("is"),
                args);
        std::vector<QByteArray> msgs = {msg};
        sendOscMessage(buildOscBundle(msgs));
    }
    // Artist
    {
        QByteArray args;
        args.append(oscInt(deckIndex));
        args.append(oscString(QString()));
        QByteArray msg = buildOscMessage(
                QStringLiteral("/Status/Deck/Song/Artist"),
                QByteArrayLiteral("is"),
                args);
        std::vector<QByteArray> msgs = {msg};
        sendOscMessage(buildOscBundle(msgs));
    }
    // Filepath
    {
        QByteArray args;
        args.append(oscInt(deckIndex));
        args.append(oscString(QString()));
        QByteArray msg = buildOscMessage(
                QStringLiteral("/Status/Deck/Song/Filepath"),
                QByteArrayLiteral("is"),
                args);
        std::vector<QByteArray> msgs = {msg};
        sendOscMessage(buildOscBundle(msgs));
    }
}

void SeratoEmulation::sendSongValid(int deckIndex, float valid) {
    QByteArray args;
    args.append(oscInt(deckIndex));
    args.append(oscFloat(valid));

    QByteArray msg = buildOscMessage(
            QStringLiteral("/Status/Deck/Song/Valid"),
            QByteArrayLiteral("if"),
            args);

    std::vector<QByteArray> msgs = {msg};
    sendOscMessage(buildOscBundle(msgs));
}

void SeratoEmulation::sendLoopState(int deckIndex) {
    // LoopRollOn
    {
        QByteArray args;
        args.append(oscInt(deckIndex));
        args.append(oscFloat(0.0f));
        QByteArray msg = buildOscMessage(
                QStringLiteral("/Status/Deck/Loop/LoopRollOn"),
                QByteArrayLiteral("if"),
                args);
        std::vector<QByteArray> msgs = {msg};
        sendOscMessage(buildOscBundle(msgs));
    }
    // BeatLength
    {
        QByteArray args;
        args.append(oscInt(deckIndex));
        args.append(oscFloat(4.0f));
        QByteArray msg = buildOscMessage(
                QStringLiteral("/Status/Deck/Loop/BeatLength"),
                QByteArrayLiteral("if"),
                args);
        std::vector<QByteArray> msgs = {msg};
        sendOscMessage(buildOscBundle(msgs));
    }
    // AutoLoopOn
    {
        QByteArray args;
        args.append(oscInt(deckIndex));
        args.append(oscFloat(0.0f));
        QByteArray msg = buildOscMessage(
                QStringLiteral("/Status/Deck/Loop/AutoLoopOn"),
                QByteArrayLiteral("if"),
                args);
        std::vector<QByteArray> msgs = {msg};
        sendOscMessage(buildOscBundle(msgs));
    }
}

void SeratoEmulation::slotSendPlayhead() {
    sendPlayhead(0);
    sendPlayhead(1);
}

void SeratoEmulation::slotBeatActive(double value) {
    Q_UNUSED(value);
}

void SeratoEmulation::connectDeckControls() {
    m_pPlayposition1 = std::make_unique<ControlProxy>(
            QStringLiteral("[Channel1]"), QStringLiteral("playposition"), this);
    m_pPlayposition2 = std::make_unique<ControlProxy>(
            QStringLiteral("[Channel2]"), QStringLiteral("playposition"), this);
    m_pBpm1 = std::make_unique<ControlProxy>(
            QStringLiteral("[Channel1]"), QStringLiteral("bpm"), this);
    m_pBpm2 = std::make_unique<ControlProxy>(
            QStringLiteral("[Channel2]"), QStringLiteral("bpm"), this);
    m_pPlay1 = std::make_unique<ControlProxy>(
            QStringLiteral("[Channel1]"), QStringLiteral("play"), this);
    m_pPlay2 = std::make_unique<ControlProxy>(
            QStringLiteral("[Channel2]"), QStringLiteral("play"), this);
    m_pCrossfader = std::make_unique<ControlProxy>(
            QStringLiteral("[Master]"), QStringLiteral("crossfader"), this);
    m_pVolume1 = std::make_unique<ControlProxy>(
            QStringLiteral("[Channel1]"), QStringLiteral("volume"), this);
    m_pVolume2 = std::make_unique<ControlProxy>(
            QStringLiteral("[Channel2]"), QStringLiteral("volume"), this);

    qDebug() << "[SeratoEmu] Deck controls connected";
}

} // namespace mixxx

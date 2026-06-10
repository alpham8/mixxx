#include "os2l/os2lmanager.h"

#include <QHostAddress>
#include <QTimer>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "os2l/os2lconnection.h"
#include "os2l/os2ldiscovery.h"

#include "moc_os2lmanager.cpp"

namespace mixxx {

namespace {
const ConfigKey kEnabledConfigKey(QStringLiteral("[OS2L]"),
        QStringLiteral("Enabled"));
} // namespace

Os2lManager::Os2lManager(UserSettingsPointer pConfig, QObject* pParent)
        : QObject(pParent),
          m_pConfig(pConfig),
          m_enabled(false),
          m_pOs2lEnabled(nullptr),
          m_beatCounter(0),
          m_lastBpm(0.0) {
    m_pOs2lEnabled = new ControlObject(
            ConfigKey(QStringLiteral("[OS2L]"), QStringLiteral("enabled")),
            true, false, false, 1.0);
    connect(m_pOs2lEnabled, &ControlObject::valueChanged,
            this, [this](double value) {
                setEnabled(value > 0.0);
            });

    bool enabled = m_pConfig->getValue(kEnabledConfigKey, true);
    setEnabled(enabled);

    // Defer ControlProxy creation — beat_active doesn't exist yet
    // during CoreServices::initialize(). Decks are created later.
    QTimer::singleShot(2000, this, [this]() {
        connectDeckControls();
    });
}

Os2lManager::~Os2lManager() {
    m_hotcueProxies.clear();
    m_pCueGotoAndPlay.reset();
    m_pPlay.reset();
    m_pBeatActive.reset();
    stopDiscovery();
    disconnectAll();
    delete m_pOs2lEnabled;
}

bool Os2lManager::isEnabled() const {
    return m_enabled;
}

bool Os2lManager::isConnected() const {
    for (const auto& [key, pConn] : m_connections) {
        if (pConn->isConnected()) {
            return true;
        }
    }
    return false;
}

int Os2lManager::connectionCount() const {
    int count = 0;
    for (const auto& [key, pConn] : m_connections) {
        if (pConn->isConnected()) {
            count++;
        }
    }
    return count;
}

void Os2lManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    m_pConfig->setValue(kEnabledConfigKey, enabled);
    m_pOs2lEnabled->setAndConfirm(enabled ? 1.0 : 0.0);

    if (m_enabled) {
        startDiscovery();
    } else {
        stopDiscovery();
        disconnectAll();
    }

    emit enabledChanged(m_enabled);
    emit connectionStatusChanged(isConnected());
}

void Os2lManager::startDiscovery() {
    if (m_pDiscovery) {
        return;
    }
    m_pDiscovery = std::make_unique<Os2lDiscovery>(this);
    connect(m_pDiscovery.get(), &Os2lDiscovery::serviceFound,
            this, &Os2lManager::slotServiceFound);
    connect(m_pDiscovery.get(), &Os2lDiscovery::serviceLost,
            this, &Os2lManager::slotServiceLost);
    m_pDiscovery->start();
}

void Os2lManager::stopDiscovery() {
    if (m_pDiscovery) {
        m_pDiscovery->stop();
        m_pDiscovery.reset();
    }
}

void Os2lManager::disconnectAll() {
    m_connections.clear();
    emit connectionStatusChanged(false);
}

void Os2lManager::slotServiceFound(const QString& name,
        const QHostAddress& host,
        quint16 port) {
    std::string key = name.toStdString();
    if (m_connections.count(key) > 0) {
        return;
    }
    qDebug() << "[OS2L] Service found:" << name
             << host.toString() << port;
    auto pConnection = std::make_unique<Os2lConnection>(
            name, host, port, this);
    connect(pConnection.get(), &Os2lConnection::connected,
            this, [this]() {
                emit connectionStatusChanged(true);
            });
    connect(pConnection.get(), &Os2lConnection::disconnected,
            this, [this]() {
                emit connectionStatusChanged(isConnected());
            });
    pConnection->connectToHost();
    m_connections.emplace(std::move(key), std::move(pConnection));
}

void Os2lManager::slotServiceLost(const QString& name) {
    qDebug() << "[OS2L] Service lost:" << name;
    m_connections.erase(name.toStdString());
    emit connectionStatusChanged(isConnected());
}

void Os2lManager::slotBeatActive(double value) {
    if (value <= 0.0 || !m_enabled) {
        return;
    }
    broadcastBeat();
}

void Os2lManager::broadcastBeat() {
    // Read BPM from Channel1 via ControlProxy
    // For now we read it directly — a PollingControlProxy for BPM
    // can be added later for multi-deck support
    double bpm = ControlObject::get(
            ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("bpm")));
    double strength = ControlObject::get(
            ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("VuMeter")));

    bool change = (bpm != m_lastBpm);
    m_lastBpm = bpm;

    for (const auto& [key, pConn] : m_connections) {
        pConn->sendBeat(m_beatCounter, bpm, change, strength);
    }
    m_beatCounter++;
}

void Os2lManager::broadcastButton(const QString& name, const QString& state) {
    for (const auto& [key, pConn] : m_connections) {
        pConn->sendButton(name, state);
    }
}

void Os2lManager::connectDeckControls() {
    const QString group = QStringLiteral("[Channel1]");

    m_pBeatActive = std::make_unique<ControlProxy>(
            group, QStringLiteral("beat_active"), this);
    m_pBeatActive->connectValueChanged(this, &Os2lManager::slotBeatActive);

    m_pPlay = std::make_unique<ControlProxy>(
            group, QStringLiteral("play"), this);
    m_pPlay->connectValueChanged(this, &Os2lManager::slotPlayChanged);

    m_pCueGotoAndPlay = std::make_unique<ControlProxy>(
            group, QStringLiteral("cue_gotoandplay"), this);
    m_pCueGotoAndPlay->connectValueChanged(this, &Os2lManager::slotCueGotoAndPlay);

    constexpr int kNumHotcues = 8;
    for (int i = 1; i <= kNumHotcues; ++i) {
        auto pProxy = std::make_unique<ControlProxy>(
                group,
                QStringLiteral("hotcue_%1_activate").arg(i),
                this);
        pProxy->connectValueChanged(this, &Os2lManager::slotHotcueActivated);
        m_hotcueProxies.push_back(std::move(pProxy));
    }

    qDebug() << "[OS2L] Deck controls connected (beat, play, cue, 8 hotcues)";
}

void Os2lManager::slotHotcueActivated(double value) {
    if (value <= 0.0 || !m_enabled) {
        return;
    }
    auto* pProxy = qobject_cast<ControlProxy*>(sender());
    if (!pProxy) {
        return;
    }
    QString coName = pProxy->getKey().item;
    broadcastButton(coName, QStringLiteral("on"));
    m_beatCounter = 0;
}

void Os2lManager::slotPlayChanged(double value) {
    if (!m_enabled) {
        return;
    }
    broadcastButton(QStringLiteral("play"),
            value > 0.0 ? QStringLiteral("on") : QStringLiteral("off"));
}

void Os2lManager::slotCueGotoAndPlay(double value) {
    if (value <= 0.0 || !m_enabled) {
        return;
    }
    broadcastButton(QStringLiteral("cue_gotoandplay"), QStringLiteral("on"));
    m_beatCounter = 0;
}

} // namespace mixxx

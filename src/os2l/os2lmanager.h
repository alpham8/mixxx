#pragma once

#include <QHostAddress>
#include <QObject>
#include <memory>
#include <unordered_map>

#include "preferences/usersettings.h"

class ControlObject;
class ControlProxy;

namespace mixxx {

class Os2lConnection;
class Os2lDiscovery;

class Os2lManager : public QObject {
    Q_OBJECT
  public:
    explicit Os2lManager(UserSettingsPointer pConfig,
            QObject* pParent = nullptr);
    ~Os2lManager() override;

    bool isEnabled() const;
    bool isConnected() const;
    int connectionCount() const;

  public slots:
    void setEnabled(bool enabled);

  signals:
    void enabledChanged(bool enabled);
    void connectionStatusChanged(bool connected);

  private slots:
    void slotServiceFound(const QString& name,
            const QHostAddress& host,
            quint16 port);
    void slotServiceLost(const QString& name);
    void slotBeatActive(double value);

  private:
    void startDiscovery();
    void stopDiscovery();
    void disconnectAll();
    void broadcastBeat();

    UserSettingsPointer m_pConfig;
    bool m_enabled;

    std::unique_ptr<Os2lDiscovery> m_pDiscovery;
    std::unordered_map<std::string, std::unique_ptr<Os2lConnection>> m_connections;

    std::unique_ptr<ControlProxy> m_pBeatActive;
    ControlObject* m_pOs2lEnabled;

    int m_beatCounter;
    double m_lastBpm;
};

} // namespace mixxx

#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

#include "preferences/usersettings.h"

class QProcess;
class QTcpServer;
class QTcpSocket;
class QTimer;
class ControlProxy;

namespace mixxx {

class SeratoEmulation : public QObject {
    Q_OBJECT
  public:
    explicit SeratoEmulation(UserSettingsPointer pConfig,
            QObject* pParent = nullptr);
    ~SeratoEmulation() override;

    void start();
    void stop();
    bool isRunning() const;
    bool isConnected() const;

  signals:
    void clientConnected(const QString& name);
    void clientDisconnected();

  private slots:
    void slotNewConnection();
    void slotClientDisconnected();
    void slotClientReadyRead();
    void slotSendPlayhead();
    void slotBeatActive(double value);

  private:
    void handleOscMessage(const QByteArray& data);
    void sendOscMessage(const QByteArray& data);
    void sendAuthorizeResponse(const QByteArray& authToken);
    void sendPairingStatus();
    void sendRegistrations();
    void sendInitialState();
    void sendPlayhead(int deckIndex);
    void sendDeckUpfader(int deckIndex, float value);
    void sendCrossfader(float value);
    void sendSongInfo(int deckIndex);
    void sendSongValid(int deckIndex, float valid);
    void sendLoopState(int deckIndex);
    void connectDeckControls();

    static QByteArray buildOscMessage(const QString& path,
            const QByteArray& typeTag,
            const QByteArray& args);
    static QByteArray buildOscBundle(
            const std::vector<QByteArray>& messages);
    static QByteArray oscString(const QString& s);
    static QByteArray oscInt(int value);
    static QByteArray oscFloat(float value);
    static QByteArray oscBlob(const QByteArray& data);

    UserSettingsPointer m_pConfig;
    QTcpServer* m_pServer;
    QTcpSocket* m_pClient;
    QTimer* m_pPlayheadTimer;
    bool m_paired;
    QByteArray m_authToken;
    QString m_clientName;

    // mDNS registration
    QProcess* m_pAvahiProcess;

    // Deck control proxies (deferred creation)
    std::unique_ptr<ControlProxy> m_pBeatActive;
    std::unique_ptr<ControlProxy> m_pPlayposition1;
    std::unique_ptr<ControlProxy> m_pPlayposition2;
    std::unique_ptr<ControlProxy> m_pBpm1;
    std::unique_ptr<ControlProxy> m_pBpm2;
    std::unique_ptr<ControlProxy> m_pPlay1;
    std::unique_ptr<ControlProxy> m_pPlay2;
    std::unique_ptr<ControlProxy> m_pCrossfader;
    std::unique_ptr<ControlProxy> m_pVolume1;
    std::unique_ptr<ControlProxy> m_pVolume2;
};

} // namespace mixxx

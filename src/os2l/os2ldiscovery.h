#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>

struct AvahiClient;
struct AvahiServiceBrowser;
struct AvahiSimplePoll;

namespace mixxx {

class Os2lDiscovery : public QObject {
    Q_OBJECT
  public:
    explicit Os2lDiscovery(QObject* pParent = nullptr);
    ~Os2lDiscovery() override;

    void start();
    void stop();
    bool isRunning() const;

  signals:
    void serviceFound(const QString& name, const QHostAddress& host, quint16 port);
    void serviceLost(const QString& name);

  private:
    void pollLoop();

    AvahiSimplePoll* m_pPoll;
    AvahiClient* m_pClient;
    AvahiServiceBrowser* m_pBrowser;
    void* m_pContext; ///< Opaque pointer to callback context (AvahiContext)
    bool m_running;

    class PollThread;
    PollThread* m_pThread;
};

} // namespace mixxx

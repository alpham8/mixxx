#include "os2l/os2ldiscovery.h"

#include <QThread>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>

namespace mixxx {

namespace {
constexpr const char* kOs2lServiceType = "_os2l._tcp";

/// Context passed as userdata to all Avahi callbacks so they can
/// reach the poll loop, client, and the owning QObject without
/// befriending internal Avahi types in the header.
struct AvahiContext {
    Os2lDiscovery* pDiscovery;
    AvahiSimplePoll* pPoll;
    AvahiClient* pClient;
};

void resolveCallback(
        AvahiServiceResolver* pResolver,
        AvahiIfIndex /*interface*/,
        AvahiProtocol /*protocol*/,
        AvahiResolverEvent event,
        const char* name,
        const char* /*type*/,
        const char* /*domain*/,
        const char* /*hostName*/,
        const AvahiAddress* pAddress,
        uint16_t port,
        AvahiStringList* /*pTxt*/,
        AvahiLookupResultFlags /*flags*/,
        void* pUserdata) {
    auto* pCtx = static_cast<AvahiContext*>(pUserdata);
    if (event == AVAHI_RESOLVER_FOUND && pAddress && name) {
        char addrBuf[AVAHI_ADDRESS_STR_MAX];
        avahi_address_snprint(addrBuf, sizeof(addrBuf), pAddress);
        QHostAddress host(QString::fromUtf8(addrBuf));
        QString serviceName = QString::fromUtf8(name);
        quint16 servicePort = port;
        Os2lDiscovery* pDisc = pCtx->pDiscovery;
        QMetaObject::invokeMethod(pDisc,
                [pDisc, serviceName, host, servicePort]() {
                    emit pDisc->serviceFound(serviceName, host, servicePort);
                },
                Qt::QueuedConnection);
    }
    avahi_service_resolver_free(pResolver);
}

void browseCallback(
        AvahiServiceBrowser* /*pBrowser*/,
        AvahiIfIndex interface,
        AvahiProtocol protocol,
        AvahiBrowserEvent event,
        const char* name,
        const char* type,
        const char* domain,
        AvahiLookupResultFlags /*flags*/,
        void* pUserdata) {
    auto* pCtx = static_cast<AvahiContext*>(pUserdata);
    switch (event) {
    case AVAHI_BROWSER_NEW:
        avahi_service_resolver_new(
                pCtx->pClient,
                interface,
                protocol,
                name,
                type,
                domain,
                AVAHI_PROTO_UNSPEC,
                static_cast<AvahiLookupFlags>(0),
                &resolveCallback,
                pUserdata);
        break;
    case AVAHI_BROWSER_REMOVE:
        if (name) {
            QString serviceName = QString::fromUtf8(name);
            Os2lDiscovery* pDisc = pCtx->pDiscovery;
            QMetaObject::invokeMethod(pDisc,
                    [pDisc, serviceName]() {
                        emit pDisc->serviceLost(serviceName);
                    },
                    Qt::QueuedConnection);
        }
        break;
    default:
        break;
    }
}

void clientCallback(
        AvahiClient* /*pClient*/,
        AvahiClientState state,
        void* pUserdata) {
    auto* pCtx = static_cast<AvahiContext*>(pUserdata);
    if (state == AVAHI_CLIENT_FAILURE) {
        qWarning() << "[OS2L] Avahi client failure";
        avahi_simple_poll_quit(pCtx->pPoll);
    }
}

} // namespace

class Os2lDiscovery::PollThread : public QThread {
  public:
    explicit PollThread(Os2lDiscovery* pDiscovery)
            : m_pDiscovery(pDiscovery) {
    }
    void run() override {
        m_pDiscovery->pollLoop();
    }

  private:
    Os2lDiscovery* m_pDiscovery;
};

Os2lDiscovery::Os2lDiscovery(QObject* pParent)
        : QObject(pParent),
          m_pPoll(nullptr),
          m_pClient(nullptr),
          m_pBrowser(nullptr),
          m_pContext(nullptr),
          m_running(false),
          m_pThread(nullptr) {
}

Os2lDiscovery::~Os2lDiscovery() {
    stop();
}

void Os2lDiscovery::start() {
    if (m_running) {
        return;
    }

    m_pPoll = avahi_simple_poll_new();
    if (!m_pPoll) {
        qWarning() << "[OS2L] Failed to create Avahi simple poll";
        return;
    }

    // The context struct is owned by this instance and freed in stop().
    auto* pCtx = new AvahiContext{this, m_pPoll, nullptr};
    m_pContext = pCtx;

    int error = 0;
    m_pClient = avahi_client_new(
            avahi_simple_poll_get(m_pPoll),
            AVAHI_CLIENT_NO_FAIL,
            &clientCallback,
            pCtx,
            &error);
    if (!m_pClient) {
        qWarning() << "[OS2L] Failed to create Avahi client:"
                   << avahi_strerror(error);
        delete pCtx;
        m_pContext = nullptr;
        avahi_simple_poll_free(m_pPoll);
        m_pPoll = nullptr;
        return;
    }
    pCtx->pClient = m_pClient;

    m_pBrowser = avahi_service_browser_new(
            m_pClient,
            AVAHI_IF_UNSPEC,
            AVAHI_PROTO_UNSPEC,
            kOs2lServiceType,
            nullptr,
            static_cast<AvahiLookupFlags>(0),
            &browseCallback,
            pCtx);
    if (!m_pBrowser) {
        qWarning() << "[OS2L] Failed to create service browser:"
                   << avahi_strerror(avahi_client_errno(m_pClient));
        delete pCtx;
        m_pContext = nullptr;
        avahi_client_free(m_pClient);
        m_pClient = nullptr;
        avahi_simple_poll_free(m_pPoll);
        m_pPoll = nullptr;
        return;
    }

    m_running = true;
    m_pThread = new PollThread(this);
    m_pThread->start();
    qDebug() << "[OS2L] Discovery started, browsing for" << kOs2lServiceType;
}

void Os2lDiscovery::stop() {
    if (!m_running) {
        return;
    }
    m_running = false;
    if (m_pPoll) {
        avahi_simple_poll_quit(m_pPoll);
    }
    if (m_pThread) {
        m_pThread->wait();
        delete m_pThread;
        m_pThread = nullptr;
    }
    if (m_pBrowser) {
        avahi_service_browser_free(m_pBrowser);
        m_pBrowser = nullptr;
    }
    if (m_pClient) {
        avahi_client_free(m_pClient);
        m_pClient = nullptr;
    }
    if (m_pPoll) {
        avahi_simple_poll_free(m_pPoll);
        m_pPoll = nullptr;
    }
    delete static_cast<AvahiContext*>(m_pContext);
    m_pContext = nullptr;
    qDebug() << "[OS2L] Discovery stopped";
}

bool Os2lDiscovery::isRunning() const {
    return m_running;
}

void Os2lDiscovery::pollLoop() {
    while (m_running && m_pPoll) {
        avahi_simple_poll_iterate(m_pPoll, 200);
    }
}

} // namespace mixxx

#include "moc_os2ldiscovery.cpp"

#ifndef AMNEZIA_APPLICATION_H
#define AMNEZIA_APPLICATION_H

#include <QCommandLineParser>
#include <QNetworkAccessManager>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QThread>
#include <QMap>
#include <QJsonObject>
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
  #include <QLocalServer>
  #include <QLocalSocket>
#else
  class QLocalSocket;
#endif
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
  #include <QGuiApplication>
#else
  #include <QApplication>
#endif
#include <QClipboard>

#include "core/controllers/coreController.h"
#include "settings.h"
#include "vpnconnection.h"

#define amnApp (static_cast<AmneziaApplication *>(QCoreApplication::instance()))

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
  #define AMNEZIA_BASE_CLASS QGuiApplication
#else
  #define AMNEZIA_BASE_CLASS QApplication
#endif

class AmneziaApplication : public AMNEZIA_BASE_CLASS
{
    Q_OBJECT
public:
    struct CliCommand {
        bool requested = false;
        QString command; // "status" | "connect" | "disconnect" | "servers" | "watch"
        int index = -1;  // server index for connect, -1 = default server
        bool json = false;
        bool redact = false; // mask IP octets (1.2.3.4 -> 1.2.*.*)
    };

    AmneziaApplication(int &argc, char *argv[]);
    virtual ~AmneziaApplication();

    void init();
    void registerTypes();
    void loadFonts();
    bool parseCommands();

    // Parses argv (idempotent, shared with parseCommands). Returns true when
    // a CLI command was requested.
    bool parseCli();
    CliCommand cliCommand() const { return m_cliCommand; }

    // Executes a CLI command. Returns the process exit code, or -1 when the
    // command requires becoming the primary instance (connect with no GUI
    // running): the caller must continue normal startup afterwards.
    int execCliCommand(const CliCommand &cmd);

    bool isForceMinimized() const { return m_forceMinimized; }

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    void startLocalServer();
#endif

    QQmlApplicationEngine *qmlEngine() const;
    QNetworkAccessManager *networkManager();
    QClipboard *getClipboard();

public slots:
    void forceQuit();

private:
    static bool m_forceQuit;
    QQmlApplicationEngine *m_engine {};
    std::shared_ptr<Settings> m_settings;

    QScopedPointer<CoreController> m_coreController;

    QSharedPointer<ContainerProps> m_containerProps;
    QSharedPointer<ProtocolProps> m_protocolProps;

    QCommandLineParser m_parser;

    QCommandLineOption m_optAutostart;
    QCommandLineOption m_optCleanup;
    QCommandLineOption m_optConnect;
    QCommandLineOption m_optImport;
    QCommandLineOption m_optDisconnect;
    QCommandLineOption m_optStatus;
    QCommandLineOption m_optJson;
    QCommandLineOption m_optRedact;
    QCommandLineOption m_optServers;
    QCommandLineOption m_optWatch;

    QSharedPointer<VpnConnection> m_vpnConnection;
    QThread m_vpnConnectionThread;

    QNetworkAccessManager *m_nam;

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    QLocalServer *m_localServer = nullptr;
    QMap<QLocalSocket *, QByteArray> m_ipcBuffers;
    QList<QLocalSocket *> m_subscribers;
    QMetaObject::Connection m_subscribeHook;
#endif

    CliCommand m_cliCommand;
    bool m_cliParsed = false;
    bool m_forceMinimized = false;
    static constexpr int kNoPendingCli = -2;
    int m_pendingCliIndex = kNoPendingCli;

    void ensureArgvParsed();
    int sendIpcCommand(const CliCommand &cmd);
    int runWatch(const CliCommand &cmd);
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    void onLocalServerNewConnection();
    void handleLocalRequest(QLocalSocket *sock);
    // Returns true when the socket must stay open (subscribe stream).
    bool dispatchLocalRequest(QLocalSocket *sock, const QJsonObject &req);
    void pushStatusToSubscribers();
#endif
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // AMNEZIA_APPLICATION_H

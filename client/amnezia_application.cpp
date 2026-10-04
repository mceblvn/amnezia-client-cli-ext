#include "amnezia_application.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QQuickItem>
#include <QQuickStyle>
#include <QResource>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>
#include <QEvent>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "cli/ipcprotocol.h"
#include "cli/statuscollector.h"
#include "core/errorstrings.h"
#include "ui/controllers/connectionController.h"

#include "logger.h"
#include "ui/controllers/pageController.h"
#include "ui/models/installedAppsModel.h"
#include "version.h"

#include "platforms/ios/QRCodeReaderBase.h"

#include "protocols/qml_register_protocols.h"
#include <QtQuick/QQuickWindow>  // for QQuickWindow
#include <QWindow>              // for qobject_cast<QWindow*>

bool AmneziaApplication::m_forceQuit = false;

AmneziaApplication::AmneziaApplication(int &argc, char *argv[]) : AMNEZIA_BASE_CLASS(argc, argv),
      m_optAutostart({QStringLiteral("a"), QStringLiteral("autostart")}, QStringLiteral("System autostart")),
      m_optCleanup  ({QStringLiteral("c"), QStringLiteral("cleanup")}, QStringLiteral("Cleanup logs")),
      m_optConnect  ({QStringLiteral("connect")}, QStringLiteral("Connect to server by index on startup"), QStringLiteral("index")),
      m_optImport   ({QStringLiteral("import")}, QStringLiteral("Import configuration from data string"), QStringLiteral("data")),
      m_optDisconnect({QStringLiteral("disconnect")}, QStringLiteral("Disconnect from VPN and exit")),
      m_optStatus   ({QStringLiteral("status")}, QStringLiteral("Print connection status and exit")),
      m_optJson     ({QStringLiteral("json")}, QStringLiteral("Machine-readable JSON output")),
      m_optRedact   ({QStringLiteral("redact")}, QStringLiteral("Mask IP addresses in output (1.2.3.4 -> 1.2.*.*)")),
      m_optServers  ({QStringLiteral("servers")}, QStringLiteral("List configured servers and exit")),
      m_optWatch    ({QStringLiteral("watch")}, QStringLiteral("Stream connection status lines until interrupted"))
{
    setDesktopFileName(QStringLiteral(APPLICATION_NAME));
    setQuitOnLastWindowClosed(false);

    // Fix config file permissions
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    {
        QSettings s(ORGANIZATION_NAME, APPLICATION_NAME);
        s.setValue("permFixed", true);
    }

    QString configLoc1 = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation).first() + "/" + ORGANIZATION_NAME + "/"
            + APPLICATION_NAME + ".conf";
    QFile::setPermissions(configLoc1, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QString configLoc2 = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation).first() + "/" + ORGANIZATION_NAME + "/"
            + APPLICATION_NAME + "/" + APPLICATION_NAME + ".conf";
    QFile::setPermissions(configLoc2, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif

    m_settings = std::shared_ptr<Settings>(new Settings);
    m_nam = new QNetworkAccessManager(this);
}

AmneziaApplication::~AmneziaApplication()
{
#ifdef AMNEZIA_DESKTOP
    if (m_vpnConnection && m_vpnConnectionThread.isRunning()) {
        QMetaObject::invokeMethod(m_vpnConnection.get(), "disconnectSlots", Qt::BlockingQueuedConnection);
        
        QMetaObject::invokeMethod(m_vpnConnection.get(), "disconnectFromVpn", Qt::BlockingQueuedConnection);
    }
#endif

    m_vpnConnectionThread.requestInterruption();
    m_vpnConnectionThread.quit();

    if (!m_vpnConnectionThread.wait(3000)) {
        m_vpnConnectionThread.terminate();
        m_vpnConnectionThread.wait(500);
    }

    if (m_engine) {
        delete m_engine;
    }
}

#ifdef Q_OS_ANDROID
namespace {
    static void clearQtCaches()
    {
        const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!cacheRoot.isEmpty()) {
            QDir(cacheRoot + "/QtShaderCache").removeRecursively();
            QDir(cacheRoot + "/qmlcache").removeRecursively();
        }
    }
}
#endif

void AmneziaApplication::init()
{
    m_engine = new QQmlApplicationEngine;

    const QUrl url(QStringLiteral("qrc:/ui/qml/main2.qml"));
    QObject::connect(
        m_engine, &QQmlApplicationEngine::objectCreated, this,
        [this, url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
                return;
            }
            // install filter on main window
            if (auto win = qobject_cast<QQuickWindow*>(obj)) {
                win->installEventFilter(this);
#ifdef Q_OS_ANDROID
                QObject::connect(win, &QQuickWindow::sceneGraphError,
                    [](QQuickWindow::SceneGraphError, const QString &msg) {
                        qWarning() << "Scene graph error (suppressed):" << msg;
                    });
                // Keep graphics context alive across hide/show cycles to avoid
                // eglSwapBuffers/makeCurrent being called on a context Android has reclaimed.
                win->setPersistentSceneGraph(true);
                win->setPersistentGraphics(true);
#endif
                if (!m_forceMinimized) {
                    win->show();
                }
            }
        },
        Qt::QueuedConnection);

    m_engine->rootContext()->setContextProperty("Debug", &Logger::Instance());

#ifdef MACOS_NE
    m_engine->rootContext()->setContextProperty("IsMacOsNeBuild", true);
#else
    m_engine->rootContext()->setContextProperty("IsMacOsNeBuild", false);
#endif

    m_vpnConnection.reset(new VpnConnection(m_settings));
    m_vpnConnection->moveToThread(&m_vpnConnectionThread);
    m_vpnConnectionThread.start();

    m_coreController.reset(new CoreController(m_vpnConnection, m_settings, m_engine));

    m_engine->addImportPath("qrc:/ui/qml/Modules/");

    if (m_parser.isSet(m_optImport)) {
        const QString data = m_parser.value(m_optImport);
        if (!data.isEmpty()) {
            if (m_coreController) {
                m_coreController->importConfigFromData(data);
            }
        }
    }

    m_engine->load(url);

    m_coreController->setQmlRoot();

    bool enabled = m_settings->isSaveLogs();
#ifndef Q_OS_ANDROID
    if (enabled) {
        if (!Logger::init(false)) {
            qWarning() << "Initialization of debug subsystem failed";
        }
    }
#endif
    Logger::setServiceLogsEnabled(enabled);

#ifdef Q_OS_WIN //TODO
    if (m_parser.isSet(m_optAutostart))
        m_coreController->pageController()->showOnStartup();
    else
        emit m_coreController->pageController()->raiseMainWindow();
#else
    if (m_forceMinimized)
        emit m_coreController->pageController()->hideMainWindow();
    else
        m_coreController->pageController()->showOnStartup();
#endif

// Android TextArea clipboard workaround
// Text from TextArea always has "text/html" mime-type:
// /qt/6.6.1/Src/qtdeclarative/src/quick/items/qquicktextcontrol.cpp:1865
// Next, html is created for this mime-type:
// /qt/6.6.1/Src/qtdeclarative/src/quick/items/qquicktextcontrol.cpp:1885
// And this html goes to the Androids clipboard, i.e. text from TextArea is always copied as richText:
// /qt/6.6.1/Src/qtbase/src/plugins/platforms/android/androidjniclipboard.cpp:46
// So we catch all the copies to the clipboard and clear them from "text/html"
#ifdef Q_OS_ANDROID
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, []() {
        auto clipboard = QGuiApplication::clipboard();
        if (clipboard->mimeData()->hasHtml()) {
            clipboard->setText(clipboard->text());
        }
    });
#endif

    if (m_pendingCliIndex != kNoPendingCli) {
        const int idx = m_pendingCliIndex;
        QTimer::singleShot(0, this, [this, idx]() {
            if (m_coreController) {
                m_coreController->cliConnect(idx);
            }
        });
    } else if (m_parser.isSet(m_optConnect)) {
        bool ok = false;
        int idx = m_parser.value(m_optConnect).toInt(&ok);
        if (ok) {
            QTimer::singleShot(0, this, [this, idx]() {
                if (m_coreController) {
                    m_coreController->openConnectionByIndex(idx);
                }
            });
        }
    }
}

void AmneziaApplication::registerTypes()
{
    qRegisterMetaType<ServerCredentials>("ServerCredentials");

    qRegisterMetaType<DockerContainer>("DockerContainer");
    qRegisterMetaType<TransportProto>("TransportProto");
    qRegisterMetaType<Proto>("Proto");
    qRegisterMetaType<ServiceType>("ServiceType");

    declareQmlProtocolEnum();
    declareQmlContainerEnum();

    qmlRegisterType<QRCodeReader>("QRCodeReader", 1, 0, "QRCodeReader");

    m_containerProps.reset(new ContainerProps());
    qmlRegisterSingletonInstance("ContainerProps", 1, 0, "ContainerProps", m_containerProps.get());

    m_protocolProps.reset(new ProtocolProps());
    qmlRegisterSingletonInstance("ProtocolProps", 1, 0, "ProtocolProps", m_protocolProps.get());

    qmlRegisterSingletonType(QUrl("qrc:/ui/qml/Filters/ContainersModelFilters.qml"), "ContainersModelFilters", 1, 0,
                             "ContainersModelFilters");

    qmlRegisterType<InstalledAppsModel>("InstalledAppsModel", 1, 0, "InstalledAppsModel");

    Vpn::declareQmlVpnConnectionStateEnum();
    PageLoader::declareQmlPageEnum();
}

void AmneziaApplication::loadFonts()
{
    QQuickStyle::setStyle("Basic");

    QFontDatabase::addApplicationFont(":/fonts/pt-root-ui_vf.ttf");
}

void AmneziaApplication::ensureArgvParsed()
{
    if (m_cliParsed) {
        return;
    }
    m_cliParsed = true;

    m_parser.setApplicationDescription(APPLICATION_NAME);
    m_parser.addHelpOption();
    m_parser.addVersionOption();

    m_parser.addOption(m_optAutostart);
    m_parser.addOption(m_optCleanup);
    m_parser.addOption(m_optConnect);
    m_parser.addOption(m_optImport);
    m_parser.addOption(m_optDisconnect);
    m_parser.addOption(m_optStatus);
    m_parser.addOption(m_optJson);
    m_parser.addOption(m_optRedact);
    m_parser.addOption(m_optServers);
    m_parser.addOption(m_optWatch);
    m_parser.addPositionalArgument(QStringLiteral("command"),
        QStringLiteral("CLI command: status, connect [index], disconnect, servers, watch"));
    m_parser.addPositionalArgument(QStringLiteral("index"),
        QStringLiteral("Server index for connect (default server if omitted)"));

    m_parser.process(*this);

    CliCommand cmd;
    cmd.json = m_parser.isSet(m_optJson);
    cmd.redact = m_parser.isSet(m_optRedact);
    if (m_parser.isSet(m_optStatus)) {
        cmd.requested = true;
        cmd.command = QString::fromLatin1(amnezia::cli::kCmdStatus);
    } else if (m_parser.isSet(m_optDisconnect)) {
        cmd.requested = true;
        cmd.command = QString::fromLatin1(amnezia::cli::kCmdDisconnect);
    } else if (m_parser.isSet(m_optConnect)) {
        bool ok = false;
        const int idx = m_parser.value(m_optConnect).toInt(&ok);
        cmd.requested = true;
        cmd.command = QString::fromLatin1(amnezia::cli::kCmdConnect);
        cmd.index = ok ? idx : -1;
    } else if (m_parser.isSet(m_optServers)) {
        cmd.requested = true;
        cmd.command = QString::fromLatin1(amnezia::cli::kCmdServers);
    } else if (m_parser.isSet(m_optWatch)) {
        cmd.requested = true;
        cmd.command = QStringLiteral("watch");
    }
    const QStringList pos = m_parser.positionalArguments();
    if (!pos.isEmpty()) {
        const QString verb = pos.first().toLower();
        if (verb == QString::fromLatin1(amnezia::cli::kCmdStatus)) {
            cmd.requested = true;
            cmd.command = QString::fromLatin1(amnezia::cli::kCmdStatus);
        } else if (verb == QString::fromLatin1(amnezia::cli::kCmdDisconnect)) {
            cmd.requested = true;
            cmd.command = QString::fromLatin1(amnezia::cli::kCmdDisconnect);
        } else if (verb == QString::fromLatin1(amnezia::cli::kCmdConnect)) {
            cmd.requested = true;
            cmd.command = QString::fromLatin1(amnezia::cli::kCmdConnect);
            cmd.index = -1;
            if (pos.size() > 1) {
                bool ok = false;
                const int idx = pos.at(1).toInt(&ok);
                if (ok) {
                    cmd.index = idx;
                }
            }
        } else if (verb == QString::fromLatin1(amnezia::cli::kCmdServers)) {
            cmd.requested = true;
            cmd.command = QString::fromLatin1(amnezia::cli::kCmdServers);
        } else if (verb == QStringLiteral("watch")) {
            cmd.requested = true;
            cmd.command = QStringLiteral("watch");
        }
    }
    m_cliCommand = cmd;
}

bool AmneziaApplication::parseCli()
{
    ensureArgvParsed();
    return m_cliCommand.requested;
}

bool AmneziaApplication::parseCommands()
{
    ensureArgvParsed();

    if (m_parser.isSet(m_optCleanup)) {
        Logger::cleanUp();
        QTimer::singleShot(100, this, [this] { quit(); });
        exec();
        return false;
    }
    return true;
}

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
namespace {
const char *kAppInstanceName = "AmneziaVPNInstance";
}

void AmneziaApplication::startLocalServer() {
    const QString serverName(kAppInstanceName);
    QLocalServer::removeServer(serverName);

    m_localServer = new QLocalServer(this);
    m_localServer->listen(serverName);

    QObject::connect(m_localServer, &QLocalServer::newConnection, this,
                     &AmneziaApplication::onLocalServerNewConnection);
}

void AmneziaApplication::onLocalServerNewConnection()
{
    if (!m_localServer) {
        return;
    }
    while (m_localServer->hasPendingConnections()) {
        QLocalSocket *sock = m_localServer->nextPendingConnection();
        QObject::connect(sock, &QLocalSocket::readyRead, this, [this, sock]() { handleLocalRequest(sock); });
        QObject::connect(sock, &QLocalSocket::disconnected, this, [this, sock]() {
            m_ipcBuffers.remove(sock);
            sock->deleteLater();
        });
    }
}

void AmneziaApplication::handleLocalRequest(QLocalSocket *sock)
{
    if (!sock) {
        return;
    }
    m_ipcBuffers[sock].append(sock->readAll());
    QByteArray &buf = m_ipcBuffers[sock];
    int pos = -1;
    while ((pos = buf.indexOf('\n')) != -1) {
        const QByteArray line = buf.left(pos);
        buf.remove(0, pos + 1);
        if (line.trimmed().isEmpty()) {
            continue;
        }
        bool ok = false;
        const QJsonObject req = amnezia::cli::decodeLine(line, &ok);
        if (!ok) {
            sock->disconnectFromServer();
            return;
        }
        // One-shot commands answer and drop the client so the CLI process
        // can exit; subscribe keeps the socket open as an event stream.
        if (!dispatchLocalRequest(sock, req)) {
            sock->disconnectFromServer();
        }
        return;
    }
}

bool AmneziaApplication::dispatchLocalRequest(QLocalSocket *sock, const QJsonObject &req)
{
    using namespace amnezia::cli;
    const QString cmd = req.value(QLatin1String(kCmdKey)).toString();

    auto replyError = [sock](const QString &error, int code) {
        sock->write(encodeResponse(false, QJsonObject(), error, code));
    };

    if (!m_coreController) {
        replyError(QStringLiteral("Client is still starting, try again"), static_cast<int>(ErrorCode::InternalError));
        return false;
    }

    if (cmd == QLatin1String(kCmdRaise)) {
        emit m_coreController->pageController()->raiseMainWindow();
        sock->write(encodeResponse(true));
    } else if (cmd == QLatin1String(kCmdStatus)) {
        sock->write(encodeResponse(true, m_coreController->cliStatusJson()));
    } else if (cmd == QLatin1String(kCmdServers)) {
        sock->write(encodeResponse(true, m_coreController->cliServersJson()));
    } else if (cmd == QLatin1String(kCmdSubscribe)) {
        if (!m_subscribers.contains(sock)) {
            m_subscribers.append(sock);
            QObject::connect(sock, &QLocalSocket::disconnected, this, [this, sock]() {
                m_subscribers.removeAll(sock);
                if (m_subscribers.isEmpty() && m_subscribeHook) {
                    QObject::disconnect(m_subscribeHook);
                }
            });
            if (!m_subscribeHook && m_coreController->connectionController()) {
                m_subscribeHook = QObject::connect(m_coreController->connectionController(),
                    &ConnectionController::connectionStateChanged, this,
                    [this]() { pushStatusToSubscribers(); });
            }
        }
        sock->write(encodeResponse(true, m_coreController->cliStatusJson()));
        return true;
    } else if (cmd == QLatin1String(kCmdDisconnect)) {
        const ErrorCode err = m_coreController->cliDisconnect();
        if (err == ErrorCode::NoError) {
            sock->write(encodeResponse(true));
        } else {
            replyError(errorString(err), static_cast<int>(err));
        }
    } else if (cmd == QLatin1String(kCmdConnect)) {
        int idx = -1;
        if (req.contains(QLatin1String(kIndexKey))) {
            idx = req.value(QLatin1String(kIndexKey)).toInt(-1);
        }
        const ErrorCode err = m_coreController->cliConnect(idx);
        if (err == ErrorCode::NoError) {
            QJsonObject data;
            data.insert(QStringLiteral("accepted"), true);
            sock->write(encodeResponse(true, data));
        } else {
            replyError(errorString(err), static_cast<int>(err));
        }
    } else {
        replyError(QStringLiteral("Unknown command"), static_cast<int>(ErrorCode::NotImplementedError));
    }
    return false;
}

void AmneziaApplication::pushStatusToSubscribers()
{
    if (m_subscribers.isEmpty() || !m_coreController) {
        return;
    }
    const QByteArray line = amnezia::cli::encodeResponse(true, m_coreController->cliStatusJson());
    for (QLocalSocket *s : std::as_const(m_subscribers)) {
        if (s->state() == QLocalSocket::ConnectedState) {
            s->write(line);
        }
    }
    if (!m_subscribers.isEmpty()) {
        m_subscribers.first()->flush();
    }
}

namespace {

QString daemonSocketPath()
{
    const QString varPath = QStringLiteral("/var/run/amneziavpn/daemon.socket");
    if (QFileInfo::exists(varPath)) {
        return varPath;
    }
    return QStringLiteral("/tmp/amneziavpn.socket");
}

// One-shot blocking query to the WireGuard daemon (no client init needed).
// Returns an empty object when the daemon is unreachable.
QJsonObject queryDaemon(const QByteArray &request)
{
    QLocalSocket sock;
    sock.connectToServer(daemonSocketPath());
    if (!sock.waitForConnected(2000)) {
        return QJsonObject();
    }
    sock.write(request + '\n');
    if (!sock.waitForBytesWritten(2000)) {
        return QJsonObject();
    }
    QByteArray buf;
    while (!buf.contains('\n')) {
        if (!sock.waitForReadyRead(2000)) {
            break;
        }
        buf += sock.readAll();
    }
    bool ok = false;
    const QJsonObject obj = amnezia::cli::decodeLine(buf, &ok);
    return ok ? obj : QJsonObject();
}

void printCliStatus(const AmneziaApplication::CliCommand &cmd, const QJsonObject &data)
{
    QJsonObject out = data;
    if (cmd.redact) {
        amnezia::cli::redactStatusIps(out);
    }
    QTextStream s(stdout);
    if (cmd.json) {
        s << amnezia::cli::formatStatusJson(out);
    } else {
        s << amnezia::cli::formatStatusHuman(out);
    }
    s.flush();
}

void printCliServers(const AmneziaApplication::CliCommand &cmd, const QJsonObject &data)
{
    QTextStream s(stdout);
    if (cmd.json) {
        s << QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented));
    } else {
        s << amnezia::cli::formatServersHuman(data);
    }
    s.flush();
}

void printWatchLine(const AmneziaApplication::CliCommand &cmd, const QJsonObject &resp)
{
    if (!resp.value(QLatin1String(amnezia::cli::kOkKey)).toBool(false)) {
        QTextStream(stderr) << resp.value(QLatin1String(amnezia::cli::kErrorKey)).toString() << "\n";
        return;
    }
    QJsonObject data = resp.value(QLatin1String(amnezia::cli::kDataKey)).toObject();
    if (cmd.redact) {
        amnezia::cli::redactStatusIps(data);
    }
    QTextStream s(stdout);
    if (cmd.json) {
        s << QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
    } else {
        s << amnezia::cli::formatStatusHuman(data) << "\n";
    }
    s.flush();
}

} // namespace

int AmneziaApplication::sendIpcCommand(const CliCommand &cmd)
{
    using namespace amnezia::cli;
    QLocalSocket sock;
    sock.connectToServer(QString(kAppInstanceName));
    if (!sock.waitForConnected(2000)) {
        QTextStream(stderr) << "AmneziaVPN is not running\n";
        return ExitFailure;
    }
    QJsonObject req;
    req[QLatin1String(kCmdKey)] = cmd.command;
    if (cmd.command == QLatin1String(kCmdConnect) && cmd.index >= 0) {
        req[QLatin1String(kIndexKey)] = cmd.index;
    }
    sock.write(encodeRequest(req));
    if (!sock.waitForBytesWritten(2000)) {
        QTextStream(stderr) << "Failed to send command\n";
        return ExitFailure;
    }
    QByteArray buf;
    while (!buf.contains('\n')) {
        if (!sock.waitForReadyRead(5000)) {
            break;
        }
        buf += sock.readAll();
    }
    bool ok = false;
    const QJsonObject resp = decodeLine(buf, &ok);
    if (!ok || !resp.value(QLatin1String(kOkKey)).toBool(false)) {
        const QString error = resp.value(QLatin1String(kErrorKey)).toString(QStringLiteral("No response from client"));
        QTextStream(stderr) << error << "\n";
        return ExitFailure;
    }
    if (cmd.command == QLatin1String(kCmdStatus)) {
        printCliStatus(cmd, resp.value(QLatin1String(kDataKey)).toObject());
    } else if (cmd.command == QLatin1String(kCmdServers)) {
        printCliServers(cmd, resp.value(QLatin1String(kDataKey)).toObject());
    } else if (cmd.json) {
        QTextStream(stdout) << QString::fromUtf8(
            QJsonDocument(resp.value(QLatin1String(kDataKey)).toObject()).toJson(QJsonDocument::Indented));
    } else {
        QTextStream(stdout) << "[OK] " << cmd.command << " accepted\n";
    }
    return ExitOk;
}

int AmneziaApplication::runWatch(const CliCommand &cmd)
{
    using namespace amnezia::cli;
    QLocalSocket sock;
    sock.connectToServer(QString(kAppInstanceName));
    if (!sock.waitForConnected(2000)) {
        QTextStream(stderr) << "AmneziaVPN is not running\n";
        return ExitFailure;
    }
    QJsonObject req;
    req[QLatin1String(kCmdKey)] = QLatin1String(kCmdSubscribe);
    sock.write(encodeRequest(req));
    if (!sock.waitForBytesWritten(2000)) {
        QTextStream(stderr) << "Failed to subscribe\n";
        return ExitFailure;
    }
    QByteArray buf;
    for (;;) {
        if (!sock.waitForReadyRead(60000)) {
            // Idle timeout is normal (pushes only happen on state changes):
            // stay subscribed unless the peer is actually gone.
            if (sock.state() != QLocalSocket::ConnectedState) {
                break;
            }
            continue;
        }
        buf += sock.readAll();
        int pos = -1;
        while ((pos = buf.indexOf('\n')) != -1) {
            const QByteArray line = buf.left(pos);
            buf.remove(0, pos + 1);
            if (line.trimmed().isEmpty()) {
                continue;
            }
            bool ok = false;
            const QJsonObject resp = decodeLine(line, &ok);
            if (ok) {
                printWatchLine(cmd, resp);
            }
        }
    }
    return ExitOk;
}

int AmneziaApplication::execCliCommand(const CliCommand &cmd)
{
    using namespace amnezia::cli;
    // Is the primary instance up?
    {
        QLocalSocket probe;
        probe.connectToServer(QString(kAppInstanceName));
        if (probe.waitForConnected(500)) {
            probe.disconnectFromServer();
            if (cmd.command == QStringLiteral("watch")) {
                return runWatch(cmd);
            }
            return sendIpcCommand(cmd);
        }
    }

    // No primary instance running.
    if (cmd.command == QLatin1String(kCmdStatus)) {
        // Best effort: ask the WireGuard daemon directly (covers WG/AWG
        // tunnels outliving the client). XRay state is unknown here.
        QJsonObject data = queryDaemon(QByteArrayLiteral("{\"type\":\"status\"}"));
        if (data.isEmpty() || !data.value(QStringLiteral("connected")).toBool(false)) {
            data = QJsonObject();
            data.insert(QStringLiteral("connected"), false);
            data.insert(QStringLiteral("connectionState"), QStringLiteral("Disconnected"));
        } else {
            data.remove(QStringLiteral("type"));
        }
        printCliStatus(cmd, data);
        return ExitOk;
    }
    if (cmd.command == QLatin1String(kCmdServers) || cmd.command == QStringLiteral("watch")) {
        QTextStream(stderr) << "AmneziaVPN is not running\n";
        return ExitFailure;
    }
    if (cmd.command == QLatin1String(kCmdDisconnect)) {
        if (!queryDaemon(QByteArrayLiteral("{\"type\":\"deactivate\"}")).isEmpty()
            || QFileInfo::exists(daemonSocketPath())) {
            QTextStream(stdout) << "[OK] deactivate sent to daemon\n";
        } else {
            QTextStream(stdout) << "[OK] already disconnected\n";
        }
        return ExitOk;
    }

    // connect with no primary: become the primary (minimized) and connect.
    m_forceMinimized = true;
    m_pendingCliIndex = cmd.index;
    return -1;
}
#endif

bool AmneziaApplication::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Close) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        quit();
#else
        if (m_forceQuit) {
            quit();
        } else {
            if (m_coreController && m_coreController->pageController()) {
                m_coreController->pageController()->hideMainWindow();
            }
        }
#endif
        return true; // eat the close
    }
    // call base QObject::eventFilter
    return QObject::eventFilter(watched, event);
}

void AmneziaApplication::forceQuit()
{
    m_forceQuit = true;
    quit();
}

QQmlApplicationEngine *AmneziaApplication::qmlEngine() const
{
    return m_engine;
}

QNetworkAccessManager *AmneziaApplication::networkManager()
{
    return m_nam;
}

QClipboard *AmneziaApplication::getClipboard()
{
    return this->clipboard();
}

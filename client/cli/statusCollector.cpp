#include "statusCollector.h"

#include <QJsonDocument>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTextStream>

#include "core/controllers/serversController.h"
#include "core/models/containerConfig.h"
#include "core/models/protocols/awgProtocolConfig.h"
#include "core/protocols/vpnProtocol.h"
#include "core/utils/constants/protocolConstants.h"
#include "core/utils/containers/containerUtils.h"
#include "ui/controllers/connectionUiController.h"
#include "vpnConnection.h"

namespace amnezia {
namespace cli {
namespace {

QString awgVersionOf(const ContainerConfig &containerConfig)
{
    if (const AwgProtocolConfig *awg = containerConfig.getAwgProtocolConfig()) {
        QString version = awg->clientProtocolVersion();
        if (version.isEmpty()) {
            version = awg->serverProtocolVersion();
        }
        return version;
    }
    return QString();
}

} // namespace

ServerDisplay serverDisplay(DockerContainer container, const ContainerConfig &containerConfig)
{
    ServerDisplay d;
    if (ContainerUtils::isAwgContainer(container)) {
        QString name = ContainerUtils::containerHumanNames().value(container, QStringLiteral("AmneziaWG"));
        const QString version = awgVersionOf(containerConfig);
        bool thirdParty = false;
        if (const AwgProtocolConfig *awg = containerConfig.getAwgProtocolConfig()) {
            thirdParty = awg->serverConfig.isThirdPartyConfig;
        }
        if (container == DockerContainer::Awg && name == QStringLiteral("AmneziaWG") && !thirdParty) {
            // Mirror ServerDescription: legacy container string keeps the Legacy tag.
            name = QStringLiteral("AmneziaWG Legacy");
        }
        d.protocol = name + AwgProtocolConfig::protocolVersionString(version);
        d.protoShort = QStringLiteral("AWG") + (version.isEmpty() ? QString() : QStringLiteral(" ") + version);
        d.outdated = !version.isEmpty() && version != QLatin1String(protocols::awg::awgV3);
        return d;
    }
    if (container == DockerContainer::Xray) {
        d.protocol = QStringLiteral("XRay (VLESS)");
        d.protoShort = QStringLiteral("VLESS");
        return d;
    }
    if (container == DockerContainer::SSXray) {
        d.protocol = QStringLiteral("XRay (Shadowsocks)");
        d.protoShort = QStringLiteral("SS");
        return d;
    }
    if (container == DockerContainer::WireGuard) {
        d.protocol = QStringLiteral("WireGuard");
        d.protoShort = QStringLiteral("WG");
        return d;
    }
    if (container == DockerContainer::OpenVpn) {
        d.protocol = QStringLiteral("OpenVPN");
        d.protoShort = QStringLiteral("OVPN");
        return d;
    }
    if (container == DockerContainer::Cloak) {
        d.protocol = ContainerUtils::containerHumanNames().value(container, QStringLiteral("Cloak"));
        d.protoShort = QStringLiteral("Cloak");
        return d;
    }
    if (container == DockerContainer::ShadowSocks) {
        d.protocol = ContainerUtils::containerHumanNames().value(container, QStringLiteral("ShadowSocks"));
        d.protoShort = QStringLiteral("OVPN-SS");
        return d;
    }
    d.protocol = ContainerUtils::containerHumanNames().value(container, ContainerUtils::containerToString(container));
    d.protoShort = d.protocol;
    return d;
}

QJsonObject collectStatus(ServersController *serversController,
                          ConnectionUiController *connectionUiController,
                          VpnConnection *vpnConnection)
{
    Q_UNUSED(connectionUiController)

    QJsonObject data;

    // Protocol-level snapshot from the VPN thread (state, addresses, bytes).
    QJsonObject snap;
    if (vpnConnection) {
        QMetaObject::invokeMethod(vpnConnection, "snapshotStatus", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QJsonObject, snap));
    }
    const bool connected = snap.value(QStringLiteral("connected")).toBool(false);
    const auto state = static_cast<Vpn::ConnectionState>(snap.value(QStringLiteral("connectionState")).toInt(0));

    data.insert(QStringLiteral("connected"), connected);
    data.insert(QStringLiteral("connectionState"), VpnProtocol::textConnectionState(state));

    // Server-level info from the repositories (GUI thread).
    if (serversController) {
        const QString defaultId = serversController->getDefaultServerId();
        if (!defaultId.isEmpty()) {
            data.insert(QStringLiteral("serverName"), serversController->notificationDisplayName(defaultId));
            const DockerContainer container = serversController->getDefaultContainer(defaultId);
            const ContainerConfig containerConfig =
                serversController->getServerContainersMap(defaultId).value(container);

            const ServerDisplay display = serverDisplay(container, containerConfig);
            data.insert(QStringLiteral("protocol"), display.protocol);
            data.insert(QStringLiteral("protoShort"), display.protoShort);
            if (display.outdated) {
                data.insert(QStringLiteral("outdated"), true);
            }
        }
    }

    if (connected) {
        const QString device = snap.value(QStringLiteral("deviceIpv4Address")).toString();
        const QString gateway = snap.value(QStringLiteral("serverIpv4Gateway")).toString();
        // XRay reports loopback constants from the protocol object; the real
        // TUN address is the well-known defaultLocalAddr.
        if (device.isEmpty() || device == QLatin1String(protocols::xray::defaultLocalListenAddr)) {
            data.insert(QStringLiteral("deviceIpv4Address"), QLatin1String(protocols::xray::defaultLocalAddr));
        } else {
            data.insert(QStringLiteral("deviceIpv4Address"), device);
        }
        QString gw = gateway;
        if (gw.isEmpty() || gw == QLatin1String(protocols::xray::defaultLocalAddr)
            || gw == QLatin1String(protocols::xray::defaultLocalListenAddr)) {
            gw = snap.value(QStringLiteral("remoteAddress")).toString();
        }
        data.insert(QStringLiteral("serverIpv4Gateway"), gw);
        data.insert(QStringLiteral("rxBytes"), snap.value(QStringLiteral("rxBytes")).toVariant().toLongLong());
        data.insert(QStringLiteral("txBytes"), snap.value(QStringLiteral("txBytes")).toVariant().toLongLong());
        const QString since = snap.value(QStringLiteral("since")).toString();
        if (!since.isEmpty()) {
            data.insert(QStringLiteral("date"), since);
        }
    }

    return data;
}

QString formatStatusHuman(const QJsonObject &data)
{
    QString out;
    QTextStream s(&out);
    const bool connected = data.value(QStringLiteral("connected")).toBool(false);
    const QString state = data.value(QStringLiteral("connectionState")).toString();
    s << "connected: " << (connected ? "yes" : "no");
    if (!state.isEmpty()) {
        s << " (" << state << ")";
    }
    s << "\n";
    if (data.contains(QStringLiteral("serverName"))) {
        s << "  server:  " << data.value(QStringLiteral("serverName")).toString() << "\n";
    }
    if (data.contains(QStringLiteral("protocol"))) {
        s << "  proto:   " << data.value(QStringLiteral("protocol")).toString();
        if (data.value(QStringLiteral("outdated")).toBool(false)) {
            s << " [outdated]";
        }
        s << "\n";
    }
    if (connected) {
        s << "  device:  " << data.value(QStringLiteral("deviceIpv4Address")).toString() << "\n";
        s << "  gateway: " << data.value(QStringLiteral("serverIpv4Gateway")).toString() << "\n";
        s << "  rx/tx:   " << data.value(QStringLiteral("rxBytes")).toVariant().toLongLong() << " / "
          << data.value(QStringLiteral("txBytes")).toVariant().toLongLong() << " bytes\n";
        if (data.contains(QStringLiteral("date"))) {
            s << "  since:   " << data.value(QStringLiteral("date")).toString() << "\n";
        }
    }
    return out;
}

QString formatStatusJson(const QJsonObject &data)
{
    // Pretty when piped (machine-readable default), compact on a tty.
    const bool pretty = true;
    return QString::fromUtf8(QJsonDocument(data).toJson(pretty ? QJsonDocument::Indented : QJsonDocument::Compact));
}

QString formatServersHuman(const QJsonObject &data)
{
    QString out;
    QTextStream s(&out);
    const QJsonArray list = data.value(QStringLiteral("servers")).toArray();
    if (list.isEmpty()) {
        s << "no servers\n";
        return out;
    }
    for (const QJsonValue &v : list) {
        const QJsonObject e = v.toObject();
        s << e.value(QStringLiteral("index")).toInt(-1) << ": "
          << e.value(QStringLiteral("name")).toString();
        QString proto = e.value(QStringLiteral("protoShort")).toString();
        if (proto.isEmpty()) {
            proto = e.value(QStringLiteral("protocol")).toString();
        }
        const QString country = e.value(QStringLiteral("country")).toString();
        s << " (" << proto;
        if (!country.isEmpty()) {
            s << QStringLiteral(" · ") << country;
            const int n = e.value(QStringLiteral("countriesAvailable")).toInt(0);
            if (n > 0) {
                s << QStringLiteral(" (%1 available)").arg(n);
            }
        }
        s << ")";
        if (e.value(QStringLiteral("outdated")).toBool(false)) {
            s << " [outdated]";
        }
        if (e.value(QStringLiteral("default")).toBool(false)) {
            s << " [default]";
        }
        if (e.value(QStringLiteral("current")).toBool(false)) {
            s << " [current]";
        }
        s << "\n";
    }
    return out;
}

void redactStatusIps(QJsonObject &data)
{
    static const QRegularExpression ipv4(QStringLiteral("\\b(\\d{1,3}\\.\\d{1,3})\\.\\d{1,3}\\.\\d{1,3}\\b"));
    for (const QString &key : { QStringLiteral("deviceIpv4Address"), QStringLiteral("serverIpv4Gateway") }) {
        const QString value = data.value(key).toString();
        if (value.isEmpty()) {
            continue;
        }
        data.insert(key, QString(value).replace(ipv4, QStringLiteral("\\1.*.*")));
    }
}

} // namespace cli
} // namespace amnezia

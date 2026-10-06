#include "statuscollector.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTextStream>

#include "protocols/protocols_defs.h"
#include "protocols/vpnprotocol.h"
#include "settings.h"
#include "ui/controllers/connectionController.h"
#include "ui/models/servers_model.h"
#include "vpnconnection.h"

namespace amnezia {
namespace cli {

ServerDisplay serverDisplay(DockerContainer container, const QString &protocolVersion,
                            bool isThirdPartyConfig)
{
    ServerDisplay d;
    if (ContainerProps::isAwgContainer(container)) {
        QString name = ContainerProps::containerHumanNames().value(container, QStringLiteral("AmneziaWG"));
        if (container == DockerContainer::Awg && name == QStringLiteral("AmneziaWG") && !isThirdPartyConfig) {
            name = QStringLiteral("AmneziaWG Legacy");
        }
        QString ver;
        if (protocolVersion == QLatin1String(protocols::awg::awgV2)) {
            ver = QStringLiteral(" (version 2)");
        } else if (protocolVersion == QLatin1String(protocols::awg::awgV1_5)) {
            ver = QStringLiteral(" (version 1.5)");
        }
        d.protocol = name + ver;
        d.protoShort = QStringLiteral("AWG")
            + (protocolVersion.isEmpty() ? QString() : QStringLiteral(" ") + protocolVersion);
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
    d.protocol = ContainerProps::containerHumanNames().value(container, ContainerProps::containerToString(container));
    d.protoShort = d.protocol;
    return d;
}

namespace {

ServerDisplay displayForServer(ServersModel *serversModel, const std::shared_ptr<Settings> &settings, int index)
{
    const QJsonObject serverConfig = serversModel->getServerConfig(index);
    const DockerContainer container =
        ContainerProps::containerFromString(serverConfig.value(config_key::defaultContainer).toString());
    QString protocolVersion;
    bool thirdParty = false;
    if (ContainerProps::isAwgContainer(container) && settings) {
        const QJsonObject containerConfig = settings->containerConfig(index, container);
        const QJsonObject protoConfig =
            containerConfig.value(ContainerProps::containerTypeToProtocolString(container)).toObject();
        protocolVersion = ProtocolProps::getProtocolVersion(protoConfig);
        thirdParty = protoConfig.value(config_key::isThirdPartyConfig).toBool();
    }
    return serverDisplay(container, protocolVersion, thirdParty);
}

QJsonObject takeSnapshot(VpnConnection *vpnConnection)
{
    QJsonObject snap;
    if (vpnConnection) {
        QMetaObject::invokeMethod(vpnConnection, "snapshotStatus", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QJsonObject, snap));
    }
    return snap;
}

} // namespace

QJsonObject collectStatus(ServersModel *serversModel,
                          ConnectionController *connectionController,
                          VpnConnection *vpnConnection,
                          const std::shared_ptr<Settings> &settings)
{
    Q_UNUSED(connectionController)

    QJsonObject data;

    const QJsonObject snap = takeSnapshot(vpnConnection);
    const bool connected = snap.value(QStringLiteral("connected")).toBool(false);
    const auto state = static_cast<Vpn::ConnectionState>(snap.value(QStringLiteral("connectionState")).toInt(0));

    data.insert(QStringLiteral("connected"), connected);
    data.insert(QStringLiteral("connectionState"), VpnProtocol::textConnectionState(state));

    if (serversModel && serversModel->getServersCount() > 0) {
        const int defaultIndex = serversModel->getDefaultServerIndex();
        data.insert(QStringLiteral("serverName"), serversModel->getDefaultServerName());
        const ServerDisplay display = displayForServer(serversModel, settings, defaultIndex);
        data.insert(QStringLiteral("protocol"), display.protocol);
        data.insert(QStringLiteral("protoShort"), display.protoShort);
    }

    if (connected) {
        const QString device = snap.value(QStringLiteral("deviceIpv4Address")).toString();
        const QString gateway = snap.value(QStringLiteral("serverIpv4Gateway")).toString();
        if (!device.isEmpty()) {
            data.insert(QStringLiteral("deviceIpv4Address"), device);
        }
        QString gw = gateway;
        // A gateway equal to the device address carries no routing info
        // (XRay reports loopback constants for both) — use the remote
        // server address instead.
        if (gw.isEmpty() || (!device.isEmpty() && gw == device)) {
            gw = snap.value(QStringLiteral("remoteAddress")).toString();
        }
        if (!gw.isEmpty()) {
            data.insert(QStringLiteral("serverIpv4Gateway"), gw);
        }
        data.insert(QStringLiteral("rxBytes"), snap.value(QStringLiteral("rxBytes")).toVariant().toLongLong());
        data.insert(QStringLiteral("txBytes"), snap.value(QStringLiteral("txBytes")).toVariant().toLongLong());
        const QString since = snap.value(QStringLiteral("since")).toString();
        if (!since.isEmpty()) {
            data.insert(QStringLiteral("date"), since);
        }
    }

    return data;
}

QJsonObject collectServers(ServersModel *serversModel,
                           VpnConnection *vpnConnection,
                           const std::shared_ptr<Settings> &settings)
{
    QJsonArray list;
    if (serversModel) {
        const int defaultIndex = serversModel->getDefaultServerIndex();
        int currentIndex = -1;
        const QJsonObject snap = takeSnapshot(vpnConnection);
        if (snap.value(QStringLiteral("connected")).toBool(false)) {
            currentIndex = snap.value(QStringLiteral("serverIndex")).toInt(-1);
        }
        const int count = serversModel->getServersCount();
        for (int i = 0; i < count; ++i) {
            const QJsonObject serverConfig = serversModel->getServerConfig(i);
            QJsonObject e;
            e.insert(QStringLiteral("index"), i);
            e.insert(QStringLiteral("name"), serverConfig.value(config_key::description).toString());
            const ServerDisplay display = displayForServer(serversModel, settings, i);
            e.insert(QStringLiteral("protocol"), display.protocol);
            e.insert(QStringLiteral("protoShort"), display.protoShort);
            if (i == defaultIndex) {
                e.insert(QStringLiteral("default"), true);
            }
            if (i == currentIndex) {
                e.insert(QStringLiteral("current"), true);
            }
            list.append(e);
        }
    }
    QJsonObject data;
    data.insert(QStringLiteral("servers"), list);
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
        s << "  proto:   " << data.value(QStringLiteral("protocol")).toString() << "\n";
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
    return QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented));
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
        s << " (" << proto << ")";
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

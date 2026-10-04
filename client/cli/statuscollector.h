#ifndef AMNEZIA_CLI_STATUSCOLLECTOR_H
#define AMNEZIA_CLI_STATUSCOLLECTOR_H

#include <QJsonObject>
#include <QString>
#include <memory>

#include "containers/containers_defs.h"

class ServersModel;
class ConnectionController;
class VpnConnection;
class Settings;

namespace amnezia {
namespace cli {

// Human + compact protocol display for one server container.
// protocol: "AmneziaWG (version 2)", "XRay (VLESS)", ...
// protoShort: "AWG 2", "VLESS", "WG", "OVPN", ...
struct ServerDisplay {
    QString protocol;
    QString protoShort;
};

ServerDisplay serverDisplay(DockerContainer container, const QString &protocolVersion,
                            bool isThirdPartyConfig);

// Builds the status object from the live client state (no config file
// parsing, no daemon socket, no shelling out).
QJsonObject collectStatus(ServersModel *serversModel,
                          ConnectionController *connectionController,
                          VpnConnection *vpnConnection,
                          const std::shared_ptr<Settings> &settings);

QJsonObject collectServers(ServersModel *serversModel,
                           VpnConnection *vpnConnection,
                           const std::shared_ptr<Settings> &settings);

QString formatStatusHuman(const QJsonObject &data);
QString formatStatusJson(const QJsonObject &data);
QString formatServersHuman(const QJsonObject &data);

// Replaces IPv4 addresses in deviceIpv4Address/serverIpv4Gateway with
// masked form (1.2.3.4 -> 1.2.*.*).
void redactStatusIps(QJsonObject &data);

} // namespace cli
} // namespace amnezia

#endif // AMNEZIA_CLI_STATUSCOLLECTOR_H

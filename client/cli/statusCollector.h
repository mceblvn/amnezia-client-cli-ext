#ifndef AMNEZIA_CLI_STATUS_COLLECTOR_H
#define AMNEZIA_CLI_STATUS_COLLECTOR_H

#include <QJsonObject>
#include <QString>

#include "core/utils/containerEnum.h"
#include "core/models/containerConfig.h"

class ServersController;
class ConnectionUiController;
class VpnConnection;

namespace amnezia {
namespace cli {

// Human + compact protocol display for one server container.
// protocol: "AmneziaWG (version 3.1)", "XRay (VLESS)", ...
// protoShort: "AWG 3.1", "VLESS", "WG", "OVPN", ...
struct ServerDisplay {
    QString protocol;
    QString protoShort;
    bool outdated = false;
};

ServerDisplay serverDisplay(DockerContainer container, const ContainerConfig &containerConfig);

// Builds the status object from the live client state (no config file
// parsing, no daemon socket, no shelling out).
//   - server part is read from ServersController in the calling (GUI) thread;
//   - protocol part (addresses, byte counters, timestamps) comes from
//     VpnConnection::snapshotStatus() via BlockingQueuedConnection.
//
// JSON keys are a superset of the legacy amnezia-helper status:
// connected, connectionState, serverName, protocol, outdated,
// deviceIpv4Address, serverIpv4Gateway, rxBytes, txBytes, date.
QJsonObject collectStatus(ServersController *serversController,
                          ConnectionUiController *connectionUiController,
                          VpnConnection *vpnConnection);

QString formatStatusHuman(const QJsonObject &data);
QString formatStatusJson(const QJsonObject &data);
QString formatServersHuman(const QJsonObject &data);

// Replaces IPv4 addresses in deviceIpv4Address/serverIpv4Gateway with
// masked form (1.2.3.4 -> 1.2.*.*). Applied at presentation time so both
// human and JSON output stay consistent.
void redactStatusIps(QJsonObject &data);

} // namespace cli
} // namespace amnezia

#endif // AMNEZIA_CLI_STATUS_COLLECTOR_H

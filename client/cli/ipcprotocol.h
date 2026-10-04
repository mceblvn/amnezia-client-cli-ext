#ifndef AMNEZIA_CLI_IPCPROTOCOL_H
#define AMNEZIA_CLI_IPCPROTOCOL_H

// Line-delimited JSON protocol between a secondary (CLI) AmneziaVPN process
// and the primary (GUI) instance over QLocalSocket.
//
// Request:  {"cmd":"status"} / {"cmd":"connect","index":0} /
//            {"cmd":"disconnect"} / {"cmd":"servers"} / {"cmd":"subscribe"} /
//            {"cmd":"raise"}
//   - "index" is optional for connect; when absent the default server is used.
//   - every message is terminated by "\n".
//
// Response: {"ok":true,"data":{...}} / {"ok":false,"error":"...","errorCode":N}
//   - "data" carries the status object / {"servers":[...]}.
//   - "connect"/"disconnect" replies mean "accepted", NOT "connected".
//   - "subscribe" keeps the socket open: one {"ok":true,"data":{...}} line
//     per connection-state change (the `watch` stream).

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace amnezia {
namespace cli {

inline const char *kCmdKey = "cmd";
inline const char *kIndexKey = "index";
inline const char *kOkKey = "ok";
inline const char *kDataKey = "data";
inline const char *kErrorKey = "error";
inline const char *kErrorCodeKey = "errorCode";

inline const char *kCmdRaise = "raise";
inline const char *kCmdStatus = "status";
inline const char *kCmdConnect = "connect";
inline const char *kCmdDisconnect = "disconnect";
inline const char *kCmdServers = "servers";
inline const char *kCmdSubscribe = "subscribe";

enum ExitCode {
    ExitOk = 0,
    ExitFailure = 1,
    ExitUsage = 2,
};

inline QByteArray encodeRequest(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
}

inline QByteArray encodeResponse(bool ok, const QJsonObject &data = QJsonObject(),
                                 const QString &error = QString(), int errorCode = 0)
{
    QJsonObject obj;
    obj[kOkKey] = ok;
    if (ok) {
        if (!data.isEmpty()) {
            obj[kDataKey] = data;
        }
    } else {
        obj[kErrorKey] = error;
        obj[kErrorCodeKey] = errorCode;
    }
    return encodeRequest(obj);
}

inline QJsonObject decodeLine(const QByteArray &line, bool *ok = nullptr)
{
    QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
    const bool valid = doc.isObject();
    if (ok) {
        *ok = valid;
    }
    return valid ? doc.object() : QJsonObject();
}

} // namespace cli
} // namespace amnezia

#endif // AMNEZIA_CLI_IPCPROTOCOL_H

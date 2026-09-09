#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QString>

class Database;

struct ApiRequest {
    QString method;
    QString path;
    QMap<QString, QString> query;
    QMap<QString, QString> headers;
    QJsonObject body;
};

struct ApiResponse {
    int status = 200;
    QByteArray contentType = "application/json; charset=utf-8";
    QByteArray body;
};

class Router final {
  public:
    Router(Database &database, QString webRoot);
    ApiResponse dispatch(const ApiRequest &request);

  private:
    ApiResponse json(const QJsonObject &payload, int status = 200) const;
    ApiResponse fail(const QString &message, int status = 400,
                     const QString &code = QStringLiteral("bad_request")) const;
    ApiResponse staticFile(const QString &path) const;
    int userId(const ApiRequest &request) const;
    int adminId(const ApiRequest &request) const;
    QString token(const ApiRequest &request) const;
    static int pathId(const QString &path, const QString &prefix,
                      const QString &suffix = QString());
    static double distanceKm(double lat1, double lon1, double lat2, double lon2);
    void audit(const QString &actorType, int actorId, const QString &action, const QString &detail);

    Database &m_database;
    QString m_webRoot;
    QHash<QString, int> m_userTokens;
    QHash<QString, int> m_adminTokens;
};

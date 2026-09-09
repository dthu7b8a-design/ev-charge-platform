#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

class Database final {
  public:
    explicit Database(QString path);
    ~Database();

    bool open(QString *error = nullptr);
    bool initialize(QString *error = nullptr);
    QSqlDatabase connection() const;
    QString path() const;

    static QString hashPassword(const QString &password, const QString &salt);
    static QString randomSalt();
    static QJsonObject rowToJson(class QSqlQuery &query);
    static QJsonArray queryRows(class QSqlQuery &query, QString *error = nullptr);
    static bool exec(class QSqlQuery &query, QString *error = nullptr);

  private:
    bool createSchema(QString *error);
    bool seed(QString *error);
    bool tableEmpty(const QString &table) const;
    bool executeSql(const QString &sql, QString *error = nullptr);
    void audit(const QString &actorType, int actorId, const QString &action, const QString &detail);

    QString m_path;
    QString m_connectionName;
    QSqlDatabase m_db;
};

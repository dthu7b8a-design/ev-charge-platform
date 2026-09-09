#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>

class ApiClient final : public QObject {
    Q_OBJECT
  public:
    explicit ApiClient(QString baseUrl, QObject *parent = nullptr);
    void setToken(const QString &token);
    QString token() const;
    void get(const QString &path, std::function<void(QJsonObject)> done);
    void post(const QString &path, const QJsonObject &body, std::function<void(QJsonObject)> done);
    void patch(const QString &path, const QJsonObject &body, std::function<void(QJsonObject)> done);
  signals:
    void networkError(const QString &message);

  private:
    void send(const QByteArray &method, const QString &path, const QJsonObject &body,
              std::function<void(QJsonObject)> done);
    QString m_baseUrl;
    QString m_token;
    QNetworkAccessManager m_manager;
};

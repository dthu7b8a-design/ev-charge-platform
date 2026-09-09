#include "apiclient.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ApiClient::ApiClient(QString baseUrl, QObject *parent)
    : QObject(parent), m_baseUrl(std::move(baseUrl)) {}
void ApiClient::setToken(const QString &token) {
    m_token = token;
}
QString ApiClient::token() const {
    return m_token;
}
void ApiClient::get(const QString &path, std::function<void(QJsonObject)> done) {
    send("GET", path, {}, std::move(done));
}
void ApiClient::post(const QString &path, const QJsonObject &body,
                     std::function<void(QJsonObject)> done) {
    send("POST", path, body, std::move(done));
}
void ApiClient::patch(const QString &path, const QJsonObject &body,
                      std::function<void(QJsonObject)> done) {
    send("PATCH", path, body, std::move(done));
}

void ApiClient::send(const QByteArray &method, const QString &path, const QJsonObject &body,
                     std::function<void(QJsonObject)> done) {
    QNetworkRequest request(QUrl(m_baseUrl + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    QNetworkReply *reply = m_manager.sendCustomRequest(
        request, method,
        body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)]() {
        const QByteArray bytes = reply->readAll();
        const QJsonDocument document = QJsonDocument::fromJson(bytes);
        QJsonObject result = document.isObject()
                                 ? document.object()
                                 : QJsonObject{{QStringLiteral("ok"), false},
                                               {QStringLiteral("error"), QString::fromUtf8(bytes)}};
        if (reply->error() != QNetworkReply::NoError && !result.contains(QStringLiteral("error")))
            result.insert(QStringLiteral("error"), reply->errorString());
        if (!result.value(QStringLiteral("ok")).toBool())
            emit networkError(
                result.value(QStringLiteral("error")).toString(QStringLiteral("请求失败")));
        done(result);
        reply->deleteLater();
    });
}

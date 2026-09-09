#include "httpserver.h"

#include "router.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
QByteArray reason(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 409:
        return "Conflict";
    case 413:
        return "Payload Too Large";
    case 422:
        return "Unprocessable Entity";
    default:
        return "Internal Server Error";
    }
}
}

HttpServer::HttpServer(Router &router, QObject *parent) : QTcpServer(parent), m_router(router) {}

void HttpServer::incomingConnection(qintptr descriptor) {
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(descriptor)) {
        socket->deleteLater();
        return;
    }
    m_buffers.insert(socket, QByteArray());
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { consume(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        m_buffers.remove(socket);
        socket->deleteLater();
    });
}

void HttpServer::consume(QTcpSocket *socket) {
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();
    const qsizetype boundary = buffer.indexOf("\r\n\r\n");
    if (boundary < 0)
        return;
    const QList<QByteArray> lines = buffer.left(boundary).split('\n');
    if (lines.isEmpty())
        return;
    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() < 2)
        return;
    QMap<QString, QString> headers;
    int contentLength = 0;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = QString::fromLatin1(line.left(colon)).toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        headers.insert(key, value);
        if (key == QStringLiteral("content-length"))
            contentLength = value.toInt();
    }
    if (contentLength < 0 || contentLength > 1024 * 1024) {
        const QByteArray body =
            "{\"ok\":false,\"code\":\"payload_too_large\",\"error\":\"请求正文超过 1 MiB 限制\"}";
        QByteArray output =
            "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " +
            QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        socket->write(output);
        socket->disconnectFromHost();
        return;
    }
    const qsizetype total = boundary + 4 + contentLength;
    if (buffer.size() < total)
        return;
    const QByteArray bodyBytes = buffer.mid(boundary + 4, contentLength);
    buffer.remove(0, total);
    const QUrl url = QUrl::fromEncoded(first.at(1));
    const QUrlQuery urlQuery(url);
    ApiRequest request;
    request.method = QString::fromLatin1(first.at(0));
    request.path = url.path();
    request.headers = headers;
    for (const auto &item : urlQuery.queryItems())
        request.query.insert(item.first, item.second);
    if (!bodyBytes.isEmpty()) {
        QJsonParseError parse;
        const QJsonDocument document = QJsonDocument::fromJson(bodyBytes, &parse);
        if (parse.error == QJsonParseError::NoError && document.isObject())
            request.body = document.object();
    }
    ApiResponse response;
    if (request.method == QStringLiteral("OPTIONS"))
        response = {204, "text/plain", QByteArray()};
    else
        response = m_router.dispatch(request);
    QByteArray output =
        "HTTP/1.1 " + QByteArray::number(response.status) + " " + reason(response.status) + "\r\n";
    output += "Content-Type: " + response.contentType + "\r\n";
    output += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    output +=
        "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Authorization, Content-Type\r\nAccess-Control-Allow-Methods: GET, POST, PATCH, OPTIONS\r\n";
    output += "Cache-Control: no-store\r\nConnection: close\r\n\r\n" + response.body;
    socket->write(output);
    socket->disconnectFromHost();
}

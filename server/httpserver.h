#pragma once

#include <QHash>
#include <QTcpServer>

class Router;
class QTcpSocket;

class HttpServer final : public QTcpServer {
    Q_OBJECT
  public:
    explicit HttpServer(Router &router, QObject *parent = nullptr);

  protected:
    void incomingConnection(qintptr socketDescriptor) override;

  private:
    void consume(QTcpSocket *socket);
    Router &m_router;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

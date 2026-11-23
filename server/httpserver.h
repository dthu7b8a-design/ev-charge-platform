#pragma once                                  // 头文件守卫：防止同一编译单元内被重复包含

#include <QHash>                              // 提供 QHash 容器，用于按套接字索引缓冲区
#include <QTcpServer>                         // 提供 QTcpServer 基类，本类在其之上实现 HTTP 服务器

class Router;                                 // 前向声明路由类：本文件只需类型名，无需完整定义
class QTcpSocket;                             // 前向声明套接字类：降低编译耦合

class HttpServer final : public QTcpServer {  // 自定义 HTTP 服务器，final 表示禁止再被继承
    Q_OBJECT                                  // 启用 Qt 元对象系统（信号与槽、动态属性）
  public:
    explicit HttpServer(Router &router, QObject *parent = nullptr);  // 构造函数：router 为业务分发入口，parent 用于对象树回收

  protected:
    void incomingConnection(qintptr socketDescriptor) override;  // 重写基类回调：每来一个新连接都会调用它

  private:
    void consume(QTcpSocket *socket);         // 私有方法：读取并解析该连接上的 HTTP 报文并回写响应
    Router &m_router;                         // 持有路由引用（引用意味着必须由外部保证其生命周期长于本对象）
    QHash<QTcpSocket *, QByteArray> m_buffers;// 每个连接独立的接收缓冲区，解决 TCP 粘包/半包问题
};

#pragma once                                  // 头文件守卫：避免在同一编译单元内被重复包含

#include <QJsonObject>                        // 提供 QJsonObject，作为请求体与响应结果的数据载体
#include <QNetworkAccessManager>              // 提供 Qt 网络访问管理器，负责实际发起 HTTP 请求

class ApiClient final : public QObject {      // 客户端 API 封装类，final 表示不可再被继承
    Q_OBJECT                                  // 启用 Qt 元对象系统，本类用到了 signals
  public:
    explicit ApiClient(QString baseUrl, QObject *parent = nullptr);  // 构造函数：baseUrl 为服务根地址（如 http://127.0.0.1:8080）
    void setToken(const QString &token);      // 设置登录令牌，后续请求会在 Authorization 头中携带
    QString token() const;                    // 读取当前令牌，界面可据此判断登录态
    void get(const QString &path, std::function<void(QJsonObject)> done);  // 发起 GET 请求，回调返回解析后的 JSON
    void post(const QString &path, const QJsonObject &body, std::function<void(QJsonObject)> done);      // 发起 POST 请求，body 为 JSON 请求体
    void patch(const QString &path, const QJsonObject &body, std::function<void(QJsonObject)> done);     // 发起 PATCH 请求，用于局部更新资源
  signals:
    void networkError(const QString &message);  // 网络错误或业务失败信号，供界面弹提示

  private:
    void send(const QByteArray &method, const QString &path, const QJsonObject &body,
              std::function<void(QJsonObject)> done);      // 私有统一发送入口，GET/POST/PATCH 都走这里
    QString m_baseUrl;                       // 服务根地址，与 path 拼接成完整 URL
    QString m_token;                         // 登录令牌，空串表示未登录
    QNetworkAccessManager m_manager;         // Qt 网络管理器，内部维护连接池与请求队列
};

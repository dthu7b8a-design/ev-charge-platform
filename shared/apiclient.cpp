#include "apiclient.h"                        // 引入本类的声明，确保定义与声明保持一致

#include <QJsonDocument>                      // 提供 JSON 序列化与反序列化能力
#include <QNetworkReply>                      // 提供网络应答对象，用于读取响应数据与错误状态
#include <QNetworkRequest>                    // 提供请求对象，用于设置 URL、头字段
#include <QUrl>                               // 提供 URL 类型，拼接基础地址与路径

ApiClient::ApiClient(QString baseUrl, QObject *parent)
    : QObject(parent), m_baseUrl(std::move(baseUrl)) {}  // 构造：初始化基类，并把 baseUrl 移动进成员（避免拷贝）
void ApiClient::setToken(const QString &token) {  // 设置令牌
    m_token = token;                          // 直接覆盖保存，供后续请求附加到 Authorization 头
}
QString ApiClient::token() const {            // 读取令牌
    return m_token;                           // 返回当前令牌副本，空串代表未登录
}
void ApiClient::get(const QString &path, std::function<void(QJsonObject)> done) {  // 对外暴露的 GET 接口
    send("GET", path, {}, std::move(done));   // 复用统一发送逻辑，GET 无请求体故传空 JSON 对象
}
void ApiClient::post(const QString &path, const QJsonObject &body,
                     std::function<void(QJsonObject)> done) {  // 对外暴露的 POST 接口
    send("POST", path, body, std::move(done));  // 复用统一发送逻辑，携带 JSON 请求体
}
void ApiClient::patch(const QString &path, const QJsonObject &body,
                      std::function<void(QJsonObject)> done) {  // 对外暴露的 PATCH 接口
    send("PATCH", path, body, std::move(done));  // 复用统一发送逻辑，用于局部更新
}

void ApiClient::send(const QByteArray &method, const QString &path, const QJsonObject &body,
                     std::function<void(QJsonObject)> done) {  // 私有实现：真正发起一次 HTTP 请求
    QNetworkRequest request(QUrl(m_baseUrl + path));  // 用“根地址 + 路径”构造请求 URL
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));  // 声明请求体为 JSON
    if (!m_token.isEmpty())                   // 已登录（令牌非空）时才附带认证信息
        request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());  // 按 Bearer 方案写入 Authorization 头
    QNetworkReply *reply = m_manager.sendCustomRequest(  // 用自定义方法发送请求，GET/POST/PATCH 统一处理
        request, method,
        body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact));  // 空体发送空字节，否则序列化为紧凑 JSON
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)]() {  // 请求完成时回调（异步）
        const QByteArray bytes = reply->readAll();  // 一次性读取服务端返回的原始字节
        const QJsonDocument document = QJsonDocument::fromJson(bytes);  // 尝试把字节解析成 JSON 文档
        QJsonObject result = document.isObject()  // 解析结果是对象则直接采用
                                 ? document.object()  // 使用服务端返回的标准 JSON 结果
                                 : QJsonObject{{QStringLiteral("ok"), false},  // 否则手工构造失败结果
                                               {QStringLiteral("error"), QString::fromUtf8(bytes)}};  // 把原始响应文本作为错误信息
        if (reply->error() != QNetworkReply::NoError && !result.contains(QStringLiteral("error")))  // 网络层出错且结果里还没有错误描述
            result.insert(QStringLiteral("error"), reply->errorString());  // 补上 Qt 提供的网络错误说明
        if (!result.value(QStringLiteral("ok")).toBool())  // 业务未成功（ok 不为 true）
            emit networkError(                // 发出错误信号，界面可连接到状态栏或弹窗
                result.value(QStringLiteral("error")).toString(QStringLiteral("请求失败")));  // 取错误信息，缺失时用默认文案
        done(result);                         // 无论成败都执行调用方回调，交由上层处理
        reply->deleteLater();                 // 延迟释放应答对象，避免在信号回调中直接析构
    });
}

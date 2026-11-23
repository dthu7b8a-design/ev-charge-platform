#include "httpserver.h"                       // 引入本类的声明，确保定义与声明一致

#include "router.h"                           // 引入路由层：解析后的请求交给 Router::dispatch 处理

#include <QJsonDocument>                      // 提供 JSON 文档的解析与序列化能力
#include <QJsonParseError>                    // 提供 JSON 解析错误的详细信息
#include <QTcpSocket>                         // TCP 套接字：真正收发 HTTP 报文的通道
#include <QUrl>                               // URL 处理：用于拆分路径与查询串
#include <QUrlQuery>                          // 查询串解析：把 ?k=v&k2=v2 拆成键值对

namespace {                                   // 匿名命名空间：其中的符号仅本 .cpp 可见，不污染全局符号表
QByteArray reason(int status) {               // 将数字状态码映射为标准的 HTTP 原因短语
    switch (status) {                         // 按状态码分支返回对应文本
    case 200:                                 // 200：请求处理成功
        return "OK";                          // 返回 200 的原因短语
    case 201:                                 // 201：资源创建成功
        return "Created";                     // 返回 201 的原因短语
    case 204:                                 // 204：处理成功但无响应正文
        return "No Content";                  // 返回 204 的原因短语
    case 400:                                 // 400：请求报文格式有误
        return "Bad Request";                 // 返回 400 的原因短语
    case 401:                                 // 401：未认证（缺少或无效的令牌）
        return "Unauthorized";                // 返回 401 的原因短语
    case 403:                                 // 403：已认证但权限不足
        return "Forbidden";                   // 返回 403 的原因短语
    case 404:                                 // 404：请求的资源不存在
        return "Not Found";                   // 返回 404 的原因短语
    case 409:                                 // 409：与现有资源状态冲突
        return "Conflict";                    // 返回 409 的原因短语
    case 413:                                 // 413：请求正文超出体积限制
        return "Payload Too Large";           // 返回 413 的原因短语
    case 422:                                 // 422：格式正确但业务语义校验未通过
        return "Unprocessable Entity";        // 返回 422 的原因短语
    default:                                  // 其余未逐一列举的状态码
        return "Internal Server Error";       // 兜底按 500 服务器内部错误返回
    }                                         // switch 分支结束
}                                             // reason 函数定义结束
}                                             // 匿名命名空间结束

HttpServer::HttpServer(Router &router, QObject *parent) : QTcpServer(parent), m_router(router) {}  // 构造：初始化基类并保存路由引用，函数体为空

void HttpServer::incomingConnection(qintptr descriptor) {  // 基类回调：有客户端接入时被调用，descriptor 是系统套接字句柄
    auto *socket = new QTcpSocket(this);      // 创建套接字对象，父对象设为 this 以便随服务器一起析构
    if (!socket->setSocketDescriptor(descriptor)) {  // 尝试接管系统分配的套接字描述符
        socket->deleteLater();                // 接管失败：延迟销毁，避免内存泄漏
        return;                               // 放弃该连接，不再继续处理
    }
    m_buffers.insert(socket, QByteArray());   // 为该连接准备一份空的接收缓冲区
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { consume(socket); });  // 有数据可读时触发解析流程
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {  // 连接断开时做清理
        m_buffers.remove(socket);             // 移除该连接对应的缓冲区，释放内存
        socket->deleteLater();                // 延迟销毁套接字（事件循环中安全删除）
    });
}

void HttpServer::consume(QTcpSocket *socket) {  // 处理某个连接上已到达的数据
    QByteArray &buffer = m_buffers[socket]; // 取得该连接累积的缓冲区（引用，原地追加）
    buffer += socket->readAll();            // 把本次可读的数据追加到缓冲区末尾
    const qsizetype boundary = buffer.indexOf("\r\n\r\n");  // 查找请求头结束标记（空行 CRLF CRLF）
    if (boundary < 0)                       // 尚未收到完整的请求头
        return;                             // 直接返回，等待后续数据到达再解析
    const QList<QByteArray> lines = buffer.left(boundary).split('\n');  // 把头部按行拆分，得到请求行与各头字段
    if (lines.isEmpty())                    // 报文异常：一行都没有
        return;                             // 放弃本次解析
    const QList<QByteArray> first = lines.first().trimmed().split(' ');  // 拆分请求行：方法、目标、协议版本
    if (first.size() < 2)                   // 请求行不合法（少于两段）
        return;                             // 放弃本次解析
    QMap<QString, QString> headers;         // 存放解析后的请求头，键统一为小写
    int contentLength = 0;                  // 请求体长度，默认 0 表示没有请求体
    for (int i = 1; i < lines.size(); ++i) {  // 从第 1 行开始逐行解析头字段（第 0 行是请求行）
        const QByteArray line = lines.at(i).trimmed();  // 去掉行首尾空白和残留的 \r
        const int colon = line.indexOf(':'); // 定位键值之间的冒号
        if (colon <= 0)                     // 没有冒号或键为空：跳过该行
            continue;                       // 继续解析下一行
        const QString key = QString::fromLatin1(line.left(colon)).toLower();  // 头字段名按 Latin1 解析并转小写
        const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();  // 头字段值按 UTF-8 解码并去空白
        headers.insert(key, value);         // 将头字段写入映射表
        if (key == QStringLiteral("content-length"))  // 命中 Content-Length 字段
            contentLength = value.toInt();  // 记录请求体字节数，转换失败则为 0
    }
    if (contentLength < 0 || contentLength > 1024 * 1024) {  // 长度为负或超过 1 MiB 上限
        const QByteArray body =             // 构造统一的 413 错误响应正文（JSON 格式）
            "{\"ok\":false,\"code\":\"payload_too_large\",\"error\":\"请求正文超过 1 MiB 限制\"}";
        QByteArray output =                 // 手工拼接完整的 HTTP/1.1 响应报文
            "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " +
            QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        socket->write(output);              // 立即把 413 响应写回客户端
        socket->disconnectFromHost();       // 关闭连接，不再接收剩余数据
        return;                             // 结束本次处理
    }
    const qsizetype total = boundary + 4 + contentLength;  // 一个完整请求占用的字节数（4 是头部结束的空行）
    if (buffer.size() < total)              // 请求体尚未收全
        return;                             // 返回等待，避免解析半包数据
    const QByteArray bodyBytes = buffer.mid(boundary + 4, contentLength);  // 从缓冲区中取出请求体字节
    buffer.remove(0, total);                // 移除已处理的这一个请求，留下后续可能存在的报文
    const QUrl url = QUrl::fromEncoded(first.at(1));  // 按百分号编码解析请求目标（路径 + 查询串）
    const QUrlQuery urlQuery(url);          // 用查询串构造解析器
    ApiRequest request;                     // 组装交给路由层的请求对象
    request.method = QString::fromLatin1(first.at(0));  // 请求方法：GET / POST / PATCH / OPTIONS 等
    request.path = url.path();              // 请求路径，不含查询串
    request.headers = headers;              // 保存解析得到的请求头
    for (const auto &item : urlQuery.queryItems())  // 遍历查询参数键值对
        request.query.insert(item.first, item.second);  // 写入 query 映射供业务层读取
    if (!bodyBytes.isEmpty()) {             // 仅当存在请求体时才尝试解析 JSON
        QJsonParseError parse;              // 用于接收解析过程中的错误详情
        const QJsonDocument document = QJsonDocument::fromJson(bodyBytes, &parse);  // 解析请求体为 JSON 文档
        if (parse.error == QJsonParseError::NoError && document.isObject())  // 解析无错误且顶层是对象
            request.body = document.object();  // 保存 JSON 对象作为请求体
    }
    ApiResponse response;                   // 声明响应对象，稍后填充状态、类型与正文
    if (request.method == QStringLiteral("OPTIONS"))  // 浏览器发出的 CORS 预检请求直接放行
        response = {204, "text/plain", QByteArray()};  // 返回 204 空正文，配合下面的 CORS 头
    else                                    // 其余方法交给业务路由处理
        response = m_router.dispatch(request);  // 由 Router 分发请求并返回处理结果
    QByteArray output =                     // 开始拼装响应：状态行（协议、状态码、原因短语）
        "HTTP/1.1 " + QByteArray::number(response.status) + " " + reason(response.status) + "\r\n";
    output += "Content-Type: " + response.contentType + "\r\n";  // 追加响应内容类型与字符集
    output += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";  // 追加响应正文长度
    output +=                               // 追加 CORS 头：允许任意来源、指定可用请求头与方法
        "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Authorization, Content-Type\r\nAccess-Control-Allow-Methods: GET, POST, PATCH, OPTIONS\r\n";
    output += "Cache-Control: no-store\r\nConnection: close\r\n\r\n" + response.body;  // 禁用缓存、短连接、空行分隔后接正文
    socket->write(output);                  // 将完整响应报文写给客户端
    socket->disconnectFromHost();           // 一请求一连接模式，发送完毕后断开
}

#include "database.h"
#include "httpserver.h"
#include "router.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QHostAddress>
#include <QTextStream>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("charge_server"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("东软电动汽车充电桩应用管理平台服务端"));
    parser.addHelpOption();
    QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("监听端口"), QStringLiteral("port"),
                                  QStringLiteral("8080"));
    QCommandLineOption dataOption({QStringLiteral("d"), QStringLiteral("data")},
                                  QStringLiteral("数据目录"), QStringLiteral("path"),
                                  QDir::currentPath() + QStringLiteral("/data"));
    QCommandLineOption webOption({QStringLiteral("w"), QStringLiteral("web")},
                                 QStringLiteral("Web 静态目录"), QStringLiteral("path"),
                                 QDir::currentPath() + QStringLiteral("/web"));
    parser.addOptions({portOption, dataOption, webOption});
    parser.process(app);
    Database database(QDir(parser.value(dataOption)).filePath(QStringLiteral("evcharge.db")));
    QString error;
    if (!database.open(&error) || !database.initialize(&error)) {
        QTextStream(stderr) << QStringLiteral("数据库启动失败：") << error << Qt::endl;
        return 2;
    }
    Router router(database, parser.value(webOption));
    HttpServer server(router);
    if (!server.listen(QHostAddress::Any, parser.value(portOption).toUShort())) {
        QTextStream(stderr) << QStringLiteral("服务监听失败：") << server.errorString() << Qt::endl;
        return 3;
    }
    QTextStream(stdout) << QStringLiteral(
                               "charge_server 已启动：http://0.0.0.0:%1\n数据库：%2\nWeb：%3")
                               .arg(server.serverPort())
                               .arg(database.path(), parser.value(webOption))
                        << Qt::endl;
    return app.exec();
}

#include "database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QtMath>

namespace {

QString sqlError(const QSqlQuery &query) {
    return query.lastError().text();
}

QString nowIso() {
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

}

Database::Database(QString path)
    : m_path(std::move(path)), m_connectionName(QStringLiteral("main-%1").arg(
                                   QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

Database::~Database() {
    if (m_db.isValid()) {
        m_db.close();
    }
    const QString name = m_connectionName;
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool Database::open(QString *error) {
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建数据库目录：%1").arg(info.absolutePath());
        return false;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
    return true;
}

bool Database::initialize(QString *error) {
    if (!createSchema(error))
        return false;
    return seed(error);
}

QSqlDatabase Database::connection() const {
    return m_db;
}

QString Database::path() const {
    return m_path;
}

QString Database::randomSalt() {
    QByteArray bytes(16, Qt::Uninitialized);
    for (char &value : bytes) {
        value = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return QString::fromLatin1(bytes.toHex());
}

QString Database::hashPassword(const QString &password, const QString &salt) {
    QByteArray digest = (salt + password).toUtf8();
    for (int i = 0; i < 60000; ++i) {
        digest = QCryptographicHash::hash(digest + salt.toUtf8(), QCryptographicHash::Sha256);
    }
    return QString::fromLatin1(digest.toHex());
}

bool Database::exec(QSqlQuery &query, QString *error) {
    if (query.exec())
        return true;
    if (error)
        *error = sqlError(query);
    return false;
}

QJsonObject Database::rowToJson(QSqlQuery &query) {
    QJsonObject object;
    const QSqlRecord record = query.record();
    for (int i = 0; i < record.count(); ++i) {
        const QVariant value = query.value(i);
        object.insert(record.fieldName(i), QJsonValue::fromVariant(value));
    }
    return object;
}

QJsonArray Database::queryRows(QSqlQuery &query, QString *error) {
    QJsonArray rows;
    if (!query.exec()) {
        if (error)
            *error = sqlError(query);
        return rows;
    }
    while (query.next())
        rows.append(rowToJson(query));
    return rows;
}

bool Database::executeSql(const QString &sql, QString *error) {
    QSqlQuery query(m_db);
    if (query.exec(sql))
        return true;
    if (error)
        *error = query.lastError().text() + QStringLiteral("\nSQL: ") + sql;
    return false;
}

bool Database::createSchema(QString *error) {
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "phone TEXT NOT NULL UNIQUE CHECK(length(phone)=11),"
            "nickname TEXT NOT NULL, avatar TEXT NOT NULL DEFAULT '',"
            "balance REAL NOT NULL DEFAULT 0 CHECK(balance>=0),"
            "registered_at TEXT NOT NULL,"
            "status TEXT NOT NULL DEFAULT 'normal' CHECK(status IN ('normal','frozen')))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS admins ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE,"
                       "password_salt TEXT NOT NULL, password_hash TEXT NOT NULL,"
                       "created_at TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS stations ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE,"
                       "address TEXT NOT NULL, latitude REAL NOT NULL, longitude REAL NOT NULL,"
                       "price REAL NOT NULL CHECK(price>0), created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS piles ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, station_id INTEGER NOT NULL,"
            "code TEXT NOT NULL UNIQUE, type TEXT NOT NULL CHECK(type IN ('fast','slow')),"
            "power REAL NOT NULL CHECK(power>0),"
            "status TEXT NOT NULL DEFAULT 'idle' CHECK(status IN ('idle','reserved','charging','fault','offline')),"
            "total_sessions INTEGER NOT NULL DEFAULT 0, total_minutes INTEGER NOT NULL DEFAULT 0,"
            "last_restart_at TEXT, FOREIGN KEY(station_id) REFERENCES stations(id) ON DELETE CASCADE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS orders ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, order_no TEXT NOT NULL UNIQUE,"
            "user_id INTEGER NOT NULL, pile_id INTEGER NOT NULL,"
            "status TEXT NOT NULL CHECK(status IN ('reserved','charging','pending_payment','settled','cancelled')),"
            "reserved_at TEXT NOT NULL, started_at TEXT, ended_at TEXT, settled_at TEXT,"
            "duration_minutes INTEGER NOT NULL DEFAULT 0, energy_kwh REAL NOT NULL DEFAULT 0,"
            "amount REAL NOT NULL DEFAULT 0,"
            "FOREIGN KEY(user_id) REFERENCES users(id), FOREIGN KEY(pile_id) REFERENCES piles(id))"),
        QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_one_active_order_per_user "
            "ON orders(user_id) WHERE status IN ('reserved','charging','pending_payment')"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS audit_logs ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, actor_type TEXT NOT NULL, actor_id INTEGER NOT NULL,"
            "action TEXT NOT NULL, detail TEXT NOT NULL, created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS hourly_load ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, station_id INTEGER NOT NULL, ts TEXT NOT NULL,"
            "sessions INTEGER NOT NULL, energy_kwh REAL NOT NULL, weather REAL NOT NULL DEFAULT 0.5,"
            "is_holiday INTEGER NOT NULL DEFAULT 0, UNIQUE(station_id,ts),"
            "FOREIGN KEY(station_id) REFERENCES stations(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_orders_user ON orders(user_id,status)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_orders_time ON orders(settled_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_piles_station ON piles(station_id,status)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_load_ts ON hourly_load(ts)")};
    for (const QString &statement : statements) {
        if (!executeSql(statement, error))
            return false;
    }
    return true;
}

bool Database::tableEmpty(const QString &table) const {
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table));
    return !query.next() || query.value(0).toInt() == 0;
}

void Database::audit(const QString &actorType, int actorId, const QString &action,
                     const QString &detail) {
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("INSERT INTO audit_logs(actor_type,actor_id,action,detail,created_at) "
                       "VALUES(?,?,?,?,?)"));
    query.addBindValue(actorType);
    query.addBindValue(actorId);
    query.addBindValue(action);
    query.addBindValue(detail);
    query.addBindValue(nowIso());
    query.exec();
}

bool Database::seed(QString *error) {
    if (!m_db.transaction()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    auto rollback = [this, error](const QString &message) {
        m_db.rollback();
        if (error)
            *error = message;
        return false;
    };

    if (tableEmpty(QStringLiteral("admins"))) {
        const QString salt = randomSalt();
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "INSERT INTO admins(username,password_salt,password_hash,created_at) VALUES(?,?,?,?)"));
        query.addBindValue(QStringLiteral("admin"));
        query.addBindValue(salt);
        query.addBindValue(hashPassword(QStringLiteral("123456"), salt));
        query.addBindValue(nowIso());
        if (!query.exec())
            return rollback(sqlError(query));
    }

    if (tableEmpty(QStringLiteral("stations"))) {
        const QList<QVariantList> stations = {
            {QStringLiteral("深圳市民中心充电站"),
             QStringLiteral("深圳市福田区福中三路市民中心停车场"), 22.54310, 114.05787, 1.20},
            {QStringLiteral("福田CBD充电站"), QStringLiteral("深圳市福田区益田路卓越世纪中心"),
             22.53462, 114.05452, 1.60},
            {QStringLiteral("深圳湾公园充电站"), QStringLiteral("深圳市南山区滨海大道深圳湾公园"),
             22.50671, 113.99445, 1.50},
            {QStringLiteral("南山科技园充电站"), QStringLiteral("深圳市南山区科技园南区高新南一道"),
             22.54030, 113.95410, 1.30},
            {QStringLiteral("宝安中心充电站"), QStringLiteral("深圳市宝安区新湖路宝安中心广场"),
             22.55454, 113.88310, 1.10},
            {QStringLiteral("龙岗大运中心充电站"), QStringLiteral("深圳市龙岗区龙翔大道大运中心"),
             22.69510, 114.22950, 1.00}};
        QSqlQuery stationQuery(m_db);
        stationQuery.prepare(QStringLiteral(
            "INSERT INTO stations(name,address,latitude,longitude,price,created_at) VALUES(?,?,?,?,?,?)"));
        for (const QVariantList &station : stations) {
            for (int i = 0; i < station.size(); ++i)
                stationQuery.bindValue(i, station.at(i));
            stationQuery.bindValue(5, nowIso());
            if (!stationQuery.exec())
                return rollback(sqlError(stationQuery));
            const int stationId = stationQuery.lastInsertId().toInt();
            QSqlQuery pileQuery(m_db);
            pileQuery.prepare(QStringLiteral(
                "INSERT INTO piles(station_id,code,type,power,status,total_sessions,total_minutes) "
                "VALUES(?,?,?,?,?,?,?)"));
            for (int index = 1; index <= 6; ++index) {
                const QString code = QStringLiteral("SZ%1-%2")
                                         .arg(stationId, 3, 10, QLatin1Char('0'))
                                         .arg(index, 2, 10, QLatin1Char('0'));
                pileQuery.bindValue(0, stationId);
                pileQuery.bindValue(1, code);
                pileQuery.bindValue(2,
                                    index <= 4 ? QStringLiteral("fast") : QStringLiteral("slow"));
                pileQuery.bindValue(3, index <= 4 ? 120.0 : 7.0);
                pileQuery.bindValue(4, index == 3 && (stationId % 3 == 0) ? QStringLiteral("fault")
                                                                          : QStringLiteral("idle"));
                pileQuery.bindValue(5, 80 + stationId * 25 + index * 7);
                pileQuery.bindValue(6, 4200 + stationId * 900 + index * 180);
                if (!pileQuery.exec())
                    return rollback(sqlError(pileQuery));
            }
        }
    }

    if (tableEmpty(QStringLiteral("users"))) {
        const QList<QVariantList> users = {
            {QStringLiteral("13800138001"), QStringLiteral("用户8001"), 92.50,
             QStringLiteral("normal")},
            {QStringLiteral("13800138002"), QStringLiteral("用户8002"), 25.00,
             QStringLiteral("normal")},
            {QStringLiteral("13800138003"), QStringLiteral("用户8003"), 180.00,
             QStringLiteral("normal")},
            {QStringLiteral("13800138004"), QStringLiteral("低余额用户"), 1.00,
             QStringLiteral("normal")},
            {QStringLiteral("13800138006"), QStringLiteral("冻结用户"), 50.00,
             QStringLiteral("frozen")}};
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "INSERT INTO users(phone,nickname,balance,registered_at,status) VALUES(?,?,?,?,?)"));
        for (const QVariantList &user : users) {
            query.bindValue(0, user.at(0));
            query.bindValue(1, user.at(1));
            query.bindValue(2, user.at(2));
            query.bindValue(3, QDateTime::currentDateTime().addDays(-20).toString(Qt::ISODate));
            query.bindValue(4, user.at(3));
            if (!query.exec())
                return rollback(sqlError(query));
        }
    }

    if (tableEmpty(QStringLiteral("orders"))) {
        QRandomGenerator generator(20260831);
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "INSERT INTO orders(order_no,user_id,pile_id,status,reserved_at,started_at,ended_at,settled_at,duration_minutes,energy_kwh,amount) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
        for (int day = 29; day >= 0; --day) {
            const int count = 2 + generator.bounded(5);
            for (int index = 0; index < count; ++index) {
                const int userId = 1 + generator.bounded(4);
                const int pileId = 1 + generator.bounded(36);
                const int minutes = 20 + generator.bounded(100);
                const double energy =
                    qRound((minutes * (0.25 + generator.generateDouble() * 0.35)) * 10.0) / 10.0;
                const QDateTime start =
                    QDateTime::currentDateTime().addDays(-day).addSecs(-(index + 1) * 3600);
                const QDateTime end = start.addSecs(minutes * 60);
                const double amount = qRound(energy * (1.0 + (pileId % 6) * 0.1) * 100.0) / 100.0;
                query.bindValue(0, QStringLiteral("H%1%2")
                                       .arg(start.toString(QStringLiteral("MMddhhmm")))
                                       .arg(index));
                query.bindValue(1, userId);
                query.bindValue(2, pileId);
                query.bindValue(3, QStringLiteral("settled"));
                query.bindValue(4, start.addSecs(-300).toString(Qt::ISODate));
                query.bindValue(5, start.toString(Qt::ISODate));
                query.bindValue(6, end.toString(Qt::ISODate));
                query.bindValue(7, end.addSecs(60).toString(Qt::ISODate));
                query.bindValue(8, minutes);
                query.bindValue(9, energy);
                query.bindValue(10, amount);
                if (!query.exec())
                    return rollback(sqlError(query));
            }
        }
    }

    if (tableEmpty(QStringLiteral("hourly_load"))) {
        QRandomGenerator generator(24680);
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "INSERT INTO hourly_load(station_id,ts,sessions,energy_kwh,weather,is_holiday) VALUES(?,?,?,?,?,?)"));
        const QDateTime origin = QDateTime::currentDateTime().addDays(-30);
        for (int stationId = 1; stationId <= 6; ++stationId) {
            for (int hour = 0; hour < 30 * 24; ++hour) {
                const QDateTime at = origin.addSecs(hour * 3600);
                const int h = at.time().hour();
                const double peak = (h >= 7 && h <= 9) || (h >= 17 && h <= 21)
                                        ? 1.7
                                        : (h >= 0 && h <= 5 ? 0.35 : 0.85);
                const int sessions = qMax(
                    0, qRound((stationId * 0.45 + 1.2) * peak + generator.generateDouble() * 2.2));
                const double energy =
                    qRound((sessions * (12.0 + generator.generateDouble() * 22.0)) * 10.0) / 10.0;
                query.bindValue(0, stationId);
                query.bindValue(1, at.toString(Qt::ISODate));
                query.bindValue(2, sessions);
                query.bindValue(3, energy);
                query.bindValue(4, 0.35 + generator.generateDouble() * 0.6);
                query.bindValue(5, at.date().dayOfWeek() >= 6 ? 1 : 0);
                if (!query.exec())
                    return rollback(sqlError(query));
            }
        }
    }

    if (!m_db.commit())
        return rollback(m_db.lastError().text());
    audit(QStringLiteral("system"), 0, QStringLiteral("database.seed"),
          QStringLiteral("初始化演示数据"));
    return true;
}

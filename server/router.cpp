#include "router.h"

#include "database.h"
#include "forecastservice.h"

#include <QtConcurrent>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QUuid>
#include <QtMath>

namespace {

QString isoNow() {
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QJsonObject okObject(const QJsonValue &data = QJsonObject()) {
    QJsonObject result{{QStringLiteral("ok"), true}};
    result.insert(QStringLiteral("data"), data);
    return result;
}

QString orderNo() {
    return QStringLiteral("EV%1%2").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss")),
        QString::number(QRandomGenerator::global()->bounded(1000), 10)
            .rightJustified(3, QLatin1Char('0')));
}

}

Router::Router(Database &database, QString webRoot)
    : m_database(database), m_webRoot(std::move(webRoot)) {}

ApiResponse Router::json(const QJsonObject &payload, int status) const {
    return {status, "application/json; charset=utf-8",
            QJsonDocument(payload).toJson(QJsonDocument::Compact)};
}

ApiResponse Router::fail(const QString &message, int status, const QString &code) const {
    return json(QJsonObject{{QStringLiteral("ok"), false},
                            {QStringLiteral("error"), message},
                            {QStringLiteral("code"), code}},
                status);
}

QString Router::token(const ApiRequest &request) const {
    QString value = request.headers.value(QStringLiteral("authorization"));
    if (value.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive))
        value = value.mid(7).trimmed();
    return value;
}

int Router::userId(const ApiRequest &request) const {
    const int id = m_userTokens.value(token(request), 0);
    if (!id)
        return 0;
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("SELECT 1 FROM users WHERE id=? AND status='normal'"));
    query.addBindValue(id);
    query.exec();
    return query.next() ? id : 0;
}
int Router::adminId(const ApiRequest &request) const {
    return m_adminTokens.value(token(request), 0);
}

int Router::pathId(const QString &path, const QString &prefix, const QString &suffix) {
    if (!path.startsWith(prefix) || (!suffix.isEmpty() && !path.endsWith(suffix)))
        return 0;
    const int end = suffix.isEmpty() ? path.size() : path.size() - suffix.size();
    bool valid = false;
    const int id = path.mid(prefix.size(), end - prefix.size()).toInt(&valid);
    return valid ? id : 0;
}

double Router::distanceKm(double lat1, double lon1, double lat2, double lon2) {
    const double p = M_PI / 180.0;
    const double a = 0.5 - qCos((lat2 - lat1) * p) / 2 +
                     qCos(lat1 * p) * qCos(lat2 * p) * (1 - qCos((lon2 - lon1) * p)) / 2;
    return 12742.0 * qAsin(qSqrt(a));
}

void Router::audit(const QString &actorType, int actorId, const QString &action,
                   const QString &detail) {
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO audit_logs(actor_type,actor_id,action,detail,created_at) VALUES(?,?,?,?,?)"));
    query.addBindValue(actorType);
    query.addBindValue(actorId);
    query.addBindValue(action);
    query.addBindValue(detail);
    query.addBindValue(isoNow());
    query.exec();
}

ApiResponse Router::staticFile(const QString &requested) const {
    QString path = requested;
    if (path == QStringLiteral("/") || path == QStringLiteral("/mobile") ||
        path == QStringLiteral("/mobile/"))
        path = QStringLiteral("/mobile/index.html");
    if (path == QStringLiteral("/dashboard") || path == QStringLiteral("/dashboard/"))
        path = QStringLiteral("/dashboard/index.html");
    path = QUrl::fromPercentEncoding(path.toUtf8());
    if (path.contains(QStringLiteral("..")))
        return fail(QStringLiteral("非法路径"), 403);
    const QString absolute = QFileInfo(m_webRoot + path).absoluteFilePath();
    if (!absolute.startsWith(QFileInfo(m_webRoot).absoluteFilePath()))
        return fail(QStringLiteral("非法路径"), 403);
    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("页面不存在"), 404, QStringLiteral("not_found"));
    const QByteArray mime = QMimeDatabase().mimeTypeForFile(absolute).name().toUtf8();
    return {200, mime, file.readAll()};
}

ApiResponse Router::dispatch(const ApiRequest &r) {
    QSqlDatabase db = m_database.connection();

    if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/health")) {
        return json(
            okObject(QJsonObject{{QStringLiteral("service"), QStringLiteral("charge_server")},
                                 {QStringLiteral("time"), isoNow()},
                                 {QStringLiteral("database"), true}}));
    }

    if (r.method == QStringLiteral("POST") && r.path == QStringLiteral("/api/auth/phone")) {
        const QString phone = r.body.value(QStringLiteral("phone")).toString().trimmed();
        if (!QRegularExpression(QStringLiteral("^1[3-9][0-9]{9}$")).match(phone).hasMatch())
            return fail(QStringLiteral("请输入有效的 11 位手机号"), 422,
                        QStringLiteral("invalid_phone"));
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT * FROM users WHERE phone=?"));
        query.addBindValue(phone);
        query.exec();
        bool created = false;
        if (!query.next()) {
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral(
                "INSERT INTO users(phone,nickname,balance,registered_at,status) VALUES(?,?,?,?, 'normal')"));
            insert.addBindValue(phone);
            insert.addBindValue(QStringLiteral("用户%1").arg(phone.right(4)));
            insert.addBindValue(0.0);
            insert.addBindValue(isoNow());
            if (!insert.exec())
                return fail(insert.lastError().text(), 500, QStringLiteral("database_error"));
            query.prepare(QStringLiteral("SELECT * FROM users WHERE id=?"));
            query.addBindValue(insert.lastInsertId());
            query.exec();
            query.next();
            created = true;
        }
        const QJsonObject user = Database::rowToJson(query);
        if (user.value(QStringLiteral("status")).toString() == QStringLiteral("frozen"))
            return fail(QStringLiteral("账号已被冻结，请联系管理员"), 403,
                        QStringLiteral("account_frozen"));
        const QString session = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_userTokens.insert(session, user.value(QStringLiteral("id")).toInt());
        audit(QStringLiteral("user"), user.value(QStringLiteral("id")).toInt(),
              QStringLiteral("login"), phone);
        return json(okObject(QJsonObject{{QStringLiteral("token"), session},
                                         {QStringLiteral("created"), created},
                                         {QStringLiteral("user"), user}}));
    }

    if (r.method == QStringLiteral("POST") && r.path == QStringLiteral("/api/admin/login")) {
        const QString username = r.body.value(QStringLiteral("username")).toString();
        const QString password = r.body.value(QStringLiteral("password")).toString();
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT * FROM admins WHERE username=?"));
        query.addBindValue(username);
        query.exec();
        if (!query.next() ||
            Database::hashPassword(password,
                                   query.value(QStringLiteral("password_salt")).toString()) !=
                query.value(QStringLiteral("password_hash")).toString())
            return fail(QStringLiteral("管理员账号或密码错误"), 401,
                        QStringLiteral("invalid_credentials"));
        const int id = query.value(QStringLiteral("id")).toInt();
        const QString session = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_adminTokens.insert(session, id);
        audit(QStringLiteral("admin"), id, QStringLiteral("login"), username);
        return json(okObject(QJsonObject{{QStringLiteral("token"), session},
                                         {QStringLiteral("username"), username}}));
    }

    if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/stations")) {
        const double lat =
            r.query.value(QStringLiteral("lat"), QStringLiteral("22.5431")).toDouble();
        const double lon =
            r.query.value(QStringLiteral("lon"), QStringLiteral("114.0579")).toDouble();
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT s.*, COUNT(p.id) pile_count, SUM(CASE WHEN p.status='idle' THEN 1 ELSE 0 END) free_count "
            "FROM stations s LEFT JOIN piles p ON p.station_id=s.id GROUP BY s.id"));
        QString error;
        QJsonArray rows = Database::queryRows(query, &error);
        if (!error.isEmpty())
            return fail(error, 500);
        QVector<QJsonObject> sorted;
        int recommendedId = 0;
        double bestRecommendationScore = -1e9;
        for (int i = 0; i < rows.size(); ++i) {
            QJsonObject station = rows.at(i).toObject();
            const double distance =
                qRound(distanceKm(lat, lon, station.value(QStringLiteral("latitude")).toDouble(),
                                  station.value(QStringLiteral("longitude")).toDouble()) *
                       10.0) /
                10.0;
            QSqlQuery load(db);
            load.prepare(QStringLiteral(
                "SELECT AVG(energy_kwh) FROM hourly_load WHERE station_id=? AND strftime('%H',ts)=?"));
            load.addBindValue(station.value(QStringLiteral("id")).toInt());
            load.addBindValue(
                QDateTime::currentDateTime().addSecs(3600).toString(QStringLiteral("hh")));
            load.exec();
            load.next();
            const double predictedLoad = qRound(load.value(0).toDouble() * 10.0) / 10.0;
            const int pileCount = station.value(QStringLiteral("pile_count")).toInt();
            const int predictedFree = qMax(0, pileCount - qCeil(predictedLoad / 30.0));
            const double score = predictedFree * 10.0 - distance;
            if (score > bestRecommendationScore) {
                bestRecommendationScore = score;
                recommendedId = station.value(QStringLiteral("id")).toInt();
            }
            station.insert(QStringLiteral("distance_km"), distance);
            station.insert(QStringLiteral("predicted_load_kwh"), predictedLoad);
            station.insert(QStringLiteral("predicted_free_piles"), predictedFree);
            sorted.append(station);
        }
        std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value(QStringLiteral("distance_km")).toDouble() <
                   b.value(QStringLiteral("distance_km")).toDouble();
        });
        rows = QJsonArray();
        for (QJsonObject station : sorted) {
            station.insert(QStringLiteral("recommended"),
                           station.value(QStringLiteral("id")).toInt() == recommendedId);
            rows.append(station);
        }
        return json(okObject(rows));
    }

    const int stationId =
        pathId(r.path, QStringLiteral("/api/stations/"), QStringLiteral("/piles"));
    if (r.method == QStringLiteral("GET") && stationId > 0) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT p.*,s.name station_name,s.price FROM piles p JOIN stations s ON s.id=p.station_id WHERE p.station_id=? ORDER BY p.code"));
        query.addBindValue(stationId);
        QString error;
        const QJsonArray rows = Database::queryRows(query, &error);
        return error.isEmpty() ? json(okObject(rows)) : fail(error, 500);
    }

    if (r.path.startsWith(QStringLiteral("/api/me")) ||
        r.path.startsWith(QStringLiteral("/api/orders"))) {
        const int uid = userId(r);
        if (!uid)
            return fail(QStringLiteral("请先登录"), 401, QStringLiteral("unauthorized"));

        if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/me")) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral(
                "SELECT id,phone,nickname,avatar,balance,registered_at,status FROM users WHERE id=?"));
            query.addBindValue(uid);
            query.exec();
            return query.next() ? json(okObject(Database::rowToJson(query)))
                                : fail(QStringLiteral("用户不存在"), 404);
        }
        if (r.method == QStringLiteral("PATCH") && r.path == QStringLiteral("/api/me")) {
            const QString nickname =
                r.body.value(QStringLiteral("nickname")).toString().trimmed().left(24);
            const QString avatar = r.body.value(QStringLiteral("avatar")).toString().left(512);
            if (nickname.isEmpty())
                return fail(QStringLiteral("昵称不能为空"), 422);
            QSqlQuery q(db);
            q.prepare(QStringLiteral("UPDATE users SET nickname=?,avatar=? WHERE id=?"));
            q.addBindValue(nickname);
            q.addBindValue(avatar);
            q.addBindValue(uid);
            if (!q.exec())
                return fail(q.lastError().text(), 500);
            audit(QStringLiteral("user"), uid, QStringLiteral("profile.update"), nickname);
            return json(okObject());
        }
        if (r.method == QStringLiteral("POST") && r.path == QStringLiteral("/api/me/recharge")) {
            const double amount = r.body.value(QStringLiteral("amount")).toDouble();
            if (amount < 1 || amount > 10000)
                return fail(QStringLiteral("充值金额需在 1～10000 元之间"), 422);
            QSqlQuery q(db);
            q.prepare(QStringLiteral("UPDATE users SET balance=round(balance+?,2) WHERE id=?"));
            q.addBindValue(amount);
            q.addBindValue(uid);
            if (!q.exec())
                return fail(q.lastError().text(), 500);
            audit(QStringLiteral("user"), uid, QStringLiteral("wallet.recharge"),
                  QString::number(amount));
            return json(okObject());
        }
        if (r.method == QStringLiteral("GET") && (r.path == QStringLiteral("/api/orders") ||
                                                  r.path == QStringLiteral("/api/orders/active"))) {
            QSqlQuery q(db);
            const bool active = r.path.endsWith(QStringLiteral("/active"));
            q.prepare(QStringLiteral("SELECT o.*,p.code pile_code,p.power pile_power,"
                                     "s.name station_name,s.price FROM orders o "
                                     "JOIN piles p ON p.id=o.pile_id "
                                     "JOIN stations s ON s.id=p.station_id WHERE o.user_id=? ") +
                      (active ? QStringLiteral(
                                    "AND o.status IN ('reserved','charging','pending_payment') ")
                              : QString()) +
                      QStringLiteral("ORDER BY o.id DESC"));
            q.addBindValue(uid);
            QString error;
            QJsonArray rows = Database::queryRows(q, &error);
            return error.isEmpty() ? json(okObject(rows)) : fail(error, 500);
        }
        if (r.method == QStringLiteral("POST") && r.path == QStringLiteral("/api/orders/reserve")) {
            const int pileId = r.body.value(QStringLiteral("pile_id")).toInt();
            if (!db.transaction())
                return fail(db.lastError().text(), 500);
            QSqlQuery user(db);
            user.prepare(QStringLiteral("SELECT status FROM users WHERE id=?"));
            user.addBindValue(uid);
            user.exec();
            user.next();
            QSqlQuery pile(db);
            pile.prepare(QStringLiteral("SELECT status FROM piles WHERE id=?"));
            pile.addBindValue(pileId);
            pile.exec();
            if (!pile.next() || pile.value(0).toString() != QStringLiteral("idle")) {
                db.rollback();
                return fail(QStringLiteral("该充电桩当前不可预约"), 409,
                            QStringLiteral("pile_unavailable"));
            }
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral(
                "INSERT INTO orders(order_no,user_id,pile_id,status,reserved_at) VALUES(?,?,?,'reserved',?)"));
            insert.addBindValue(orderNo());
            insert.addBindValue(uid);
            insert.addBindValue(pileId);
            insert.addBindValue(isoNow());
            if (!insert.exec()) {
                db.rollback();
                return fail(QStringLiteral("存在未完成订单，请先完成结算"), 409,
                            QStringLiteral("active_order_exists"));
            }
            const int newOrderId = insert.lastInsertId().toInt();
            QSqlQuery update(db);
            update.prepare(QStringLiteral("UPDATE piles SET status='reserved' WHERE id=?"));
            update.addBindValue(pileId);
            update.exec();
            db.commit();
            audit(QStringLiteral("user"), uid, QStringLiteral("order.reserve"),
                  QString::number(newOrderId));
            return json(okObject(QJsonObject{{QStringLiteral("order_id"), newOrderId}}), 201);
        }

        const int startId =
            pathId(r.path, QStringLiteral("/api/orders/"), QStringLiteral("/start"));
        const int stopId = pathId(r.path, QStringLiteral("/api/orders/"), QStringLiteral("/stop"));
        const int settleId =
            pathId(r.path, QStringLiteral("/api/orders/"), QStringLiteral("/settle"));
        const int cancelId =
            pathId(r.path, QStringLiteral("/api/orders/"), QStringLiteral("/cancel"));
        if (r.method == QStringLiteral("POST") && startId > 0) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "UPDATE orders SET status='charging',started_at=? WHERE id=? AND user_id=? AND status='reserved'"));
            q.addBindValue(isoNow());
            q.addBindValue(startId);
            q.addBindValue(uid);
            q.exec();
            if (q.numRowsAffected() != 1)
                return fail(QStringLiteral("订单状态不允许启动"), 409);
            QSqlQuery p(db);
            p.prepare(QStringLiteral(
                "UPDATE piles SET status='charging' WHERE id=(SELECT pile_id FROM orders WHERE id=?)"));
            p.addBindValue(startId);
            p.exec();
            return json(okObject());
        }
        if (r.method == QStringLiteral("POST") && stopId > 0) {
            QSqlQuery select(db);
            select.prepare(QStringLiteral(
                "SELECT o.started_at,p.power,s.price FROM orders o JOIN piles p ON p.id=o.pile_id JOIN stations s ON s.id=p.station_id WHERE o.id=? AND o.user_id=? AND o.status='charging'"));
            select.addBindValue(stopId);
            select.addBindValue(uid);
            select.exec();
            if (!select.next())
                return fail(QStringLiteral("订单状态不允许结束"), 409);
            const int seconds =
                qMax(60, select.value(0).toDateTime().secsTo(QDateTime::currentDateTime()));
            const int minutes = qMax(1, qCeil(seconds / 60.0));
            const double energy =
                qRound((seconds / 3600.0) * select.value(1).toDouble() * 10.0) / 10.0;
            const double amount = qRound(energy * select.value(2).toDouble() * 100.0) / 100.0;
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "UPDATE orders SET status='pending_payment',ended_at=?,duration_minutes=?,energy_kwh=?,amount=? WHERE id=?"));
            q.addBindValue(isoNow());
            q.addBindValue(minutes);
            q.addBindValue(energy);
            q.addBindValue(amount);
            q.addBindValue(stopId);
            q.exec();
            return json(okObject(QJsonObject{{QStringLiteral("amount"), amount},
                                             {QStringLiteral("energy_kwh"), energy}}));
        }
        if (r.method == QStringLiteral("POST") && settleId > 0) {
            if (!db.transaction())
                return fail(db.lastError().text(), 500);
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT amount,pile_id FROM orders WHERE id=? AND user_id=? AND status='pending_payment'"));
            q.addBindValue(settleId);
            q.addBindValue(uid);
            q.exec();
            if (!q.next()) {
                db.rollback();
                return fail(QStringLiteral("订单状态不允许结算"), 409);
            }
            const double amount = q.value(0).toDouble();
            const int pile = q.value(1).toInt();
            QSqlQuery pay(db);
            pay.prepare(
                QStringLiteral("UPDATE users SET balance=balance-? WHERE id=? AND balance>=?"));
            pay.addBindValue(amount);
            pay.addBindValue(uid);
            pay.addBindValue(amount);
            pay.exec();
            if (pay.numRowsAffected() != 1) {
                db.rollback();
                return fail(QStringLiteral("余额不足，请先充值"), 409,
                            QStringLiteral("insufficient_balance"));
            }
            QSqlQuery done(db);
            done.prepare(
                QStringLiteral("UPDATE orders SET status='settled',settled_at=? WHERE id=?"));
            done.addBindValue(isoNow());
            done.addBindValue(settleId);
            done.exec();
            QSqlQuery free(db);
            free.prepare(QStringLiteral(
                "UPDATE piles SET status='idle',total_sessions=total_sessions+1,total_minutes=total_minutes+(SELECT duration_minutes FROM orders WHERE id=?) WHERE id=?"));
            free.addBindValue(settleId);
            free.addBindValue(pile);
            free.exec();
            db.commit();
            return json(okObject());
        }
        if (r.method == QStringLiteral("POST") && cancelId > 0) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "UPDATE orders SET status='cancelled',ended_at=? WHERE id=? AND user_id=? AND status='reserved'"));
            q.addBindValue(isoNow());
            q.addBindValue(cancelId);
            q.addBindValue(uid);
            q.exec();
            if (q.numRowsAffected() != 1)
                return fail(QStringLiteral("仅已预约订单可取消"), 409);
            QSqlQuery p(db);
            p.prepare(QStringLiteral(
                "UPDATE piles SET status='idle' WHERE id=(SELECT pile_id FROM orders WHERE id=?)"));
            p.addBindValue(cancelId);
            p.exec();
            return json(okObject());
        }
    }

    if (r.path.startsWith(QStringLiteral("/api/admin/"))) {
        const int aid = adminId(r);
        if (!aid)
            return fail(QStringLiteral("管理员未登录"), 401, QStringLiteral("unauthorized"));
        if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/admin/summary")) {
            QSqlQuery q(db);
            q.exec(QStringLiteral(
                "SELECT round(COALESCE(SUM(CASE WHEN date(settled_at)=date('now','localtime') THEN amount END),0),2),round(COALESCE(SUM(CASE WHEN strftime('%Y-%m',settled_at)=strftime('%Y-%m','now','localtime') THEN amount END),0),2),round(COALESCE(SUM(amount),0),2),COUNT(*) FROM orders WHERE status='settled'"));
            q.next();
            QSqlQuery u(db);
            u.exec(QStringLiteral("SELECT COUNT(*) FROM users"));
            u.next();
            QSqlQuery p(db);
            p.exec(QStringLiteral("SELECT COUNT(*),SUM(status='idle') FROM piles"));
            p.next();
            return json(
                okObject(QJsonObject{{QStringLiteral("today_revenue"), q.value(0).toDouble()},
                                     {QStringLiteral("month_revenue"), q.value(1).toDouble()},
                                     {QStringLiteral("total_revenue"), q.value(2).toDouble()},
                                     {QStringLiteral("settled_orders"), q.value(3).toInt()},
                                     {QStringLiteral("users"), u.value(0).toInt()},
                                     {QStringLiteral("piles"), p.value(0).toInt()},
                                     {QStringLiteral("idle_piles"), p.value(1).toInt()}}));
        }
        if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/admin/sales")) {
            const int days =
                qBound(7, r.query.value(QStringLiteral("days"), QStringLiteral("7")).toInt(), 30);
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "WITH RECURSIVE dates(d) AS (SELECT date('now','localtime',?) UNION ALL SELECT date(d,'+1 day') FROM dates WHERE d<date('now','localtime')) SELECT d date,round(COALESCE(SUM(o.amount),0),2) revenue,COUNT(o.id) orders FROM dates LEFT JOIN orders o ON date(o.settled_at)=d AND o.status='settled' GROUP BY d ORDER BY d"));
            q.addBindValue(QStringLiteral("-%1 day").arg(days - 1));
            QString e;
            const QJsonArray rows = Database::queryRows(q, &e);
            return e.isEmpty() ? json(okObject(rows)) : fail(e, 500);
        }
        if (r.method == QStringLiteral("GET") &&
            r.path == QStringLiteral("/api/admin/pile-distribution")) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT status,COUNT(*) count FROM piles GROUP BY status"));
            QString e;
            const QJsonArray rows = Database::queryRows(q, &e);
            return e.isEmpty() ? json(okObject(rows)) : fail(e, 500);
        }
        if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/admin/stations")) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT s.*,COUNT(p.id) pile_count,SUM(p.status='idle') idle_count,CASE WHEN COUNT(p.id)=0 THEN 0 ELSE round(100.0*SUM(p.status NOT IN ('fault','offline'))/COUNT(p.id),1) END online_rate FROM stations s LEFT JOIN piles p ON p.station_id=s.id GROUP BY s.id ORDER BY s.id"));
            QString e;
            auto rows = Database::queryRows(q, &e);
            return e.isEmpty() ? json(okObject(rows)) : fail(e, 500);
        }
        if (r.method == QStringLiteral("POST") && r.path == QStringLiteral("/api/admin/stations")) {
            const QString name = r.body.value(QStringLiteral("name")).toString().trimmed(),
                          address = r.body.value(QStringLiteral("address")).toString().trimmed();
            const double latitude = r.body.value(QStringLiteral("latitude")).toDouble(),
                         longitude = r.body.value(QStringLiteral("longitude")).toDouble(),
                         price = r.body.value(QStringLiteral("price")).toDouble();
            const int requestedPiles = r.body.value(QStringLiteral("pile_count")).toInt();
            if (name.isEmpty() || address.isEmpty() || latitude < -90 || latitude > 90 ||
                longitude < -180 || longitude > 180 || price <= 0 || requestedPiles < 1 ||
                requestedPiles > 50)
                return fail(QStringLiteral("请完整填写名称、地址、合法经纬度、电价和 1～50 个电桩"),
                            422);
            if (!db.transaction())
                return fail(db.lastError().text(), 500);
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT INTO stations(name,address,latitude,longitude,price,created_at) VALUES(?,?,?,?,?,?)"));
            q.addBindValue(name);
            q.addBindValue(address);
            q.addBindValue(latitude);
            q.addBindValue(longitude);
            q.addBindValue(price);
            q.addBindValue(isoNow());
            if (!q.exec()) {
                db.rollback();
                return fail(q.lastError().text(), 409);
            }
            const int newStationId = q.lastInsertId().toInt();
            QSqlQuery pile(db);
            pile.prepare(QStringLiteral(
                "INSERT INTO piles(station_id,code,type,power,status) VALUES(?,?,?,?, 'idle')"));
            for (int i = 1; i <= requestedPiles; ++i) {
                pile.bindValue(0, newStationId);
                pile.bindValue(1, QStringLiteral("ST%1-%2")
                                      .arg(newStationId, 3, 10, QLatin1Char('0'))
                                      .arg(i, 2, 10, QLatin1Char('0')));
                pile.bindValue(2, i <= qCeil(requestedPiles * 0.7) ? QStringLiteral("fast")
                                                                   : QStringLiteral("slow"));
                pile.bindValue(3, i <= qCeil(requestedPiles * 0.7) ? 120.0 : 7.0);
                if (!pile.exec()) {
                    db.rollback();
                    return fail(pile.lastError().text(), 500);
                }
            }
            if (!db.commit())
                return fail(db.lastError().text(), 500);
            audit(QStringLiteral("admin"), aid, QStringLiteral("station.add"),
                  QStringLiteral("%1:%2 piles").arg(name).arg(requestedPiles));
            return json(okObject(QJsonObject{{QStringLiteral("id"), newStationId},
                                             {QStringLiteral("pile_count"), requestedPiles}}),
                        201);
        }
        if (r.method == QStringLiteral("POST") && r.path == QStringLiteral("/api/admin/piles")) {
            const int sid = r.body.value(QStringLiteral("station_id")).toInt();
            const QString code =
                r.body.value(QStringLiteral("code")).toString().trimmed().toUpper();
            const QString type = r.body.value(QStringLiteral("type")).toString();
            const double power = r.body.value(QStringLiteral("power")).toDouble();
            if (sid <= 0 ||
                !QRegularExpression(QStringLiteral("^[A-Z0-9-]{3,24}$")).match(code).hasMatch() ||
                !QStringList{QStringLiteral("fast"), QStringLiteral("slow")}.contains(type) ||
                power <= 0 || power > 500)
                return fail(QStringLiteral("充电桩参数不合法"), 422);
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT INTO piles(station_id,code,type,power,status) SELECT ?,?,?,?,'idle' WHERE EXISTS(SELECT 1 FROM stations WHERE id=?)"));
            q.addBindValue(sid);
            q.addBindValue(code);
            q.addBindValue(type);
            q.addBindValue(power);
            q.addBindValue(sid);
            if (!q.exec() || q.numRowsAffected() != 1)
                return fail(q.lastError().text().isEmpty()
                                ? QStringLiteral("充电站不存在或设备编号重复")
                                : q.lastError().text(),
                            409);
            const int newPileId = q.lastInsertId().toInt();
            audit(QStringLiteral("admin"), aid, QStringLiteral("pile.add"), code);
            return json(okObject(QJsonObject{{QStringLiteral("id"), newPileId}}), 201);
        }
        if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/admin/piles")) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT p.*,s.name station_name FROM piles p JOIN stations s ON s.id=p.station_id ORDER BY p.id"));
            QString e;
            auto rows = Database::queryRows(q, &e);
            return e.isEmpty() ? json(okObject(rows)) : fail(e, 500);
        }
        const int restartId =
            pathId(r.path, QStringLiteral("/api/admin/piles/"), QStringLiteral("/restart"));
        if (r.method == QStringLiteral("POST") && restartId > 0) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "UPDATE piles SET status='idle',last_restart_at=? WHERE id=? AND status!='charging'"));
            q.addBindValue(isoNow());
            q.addBindValue(restartId);
            q.exec();
            if (q.numRowsAffected() != 1)
                return fail(QStringLiteral("充电中的设备不可重启"), 409);
            audit(QStringLiteral("admin"), aid, QStringLiteral("pile.restart"),
                  QString::number(restartId));
            return json(okObject());
        }
        const int pileStatusId =
            pathId(r.path, QStringLiteral("/api/admin/piles/"), QStringLiteral("/status"));
        if (r.method == QStringLiteral("PATCH") && pileStatusId > 0) {
            const QString status = r.body.value(QStringLiteral("status")).toString();
            if (!QStringList{QStringLiteral("idle"), QStringLiteral("fault"),
                             QStringLiteral("offline")}
                     .contains(status))
                return fail(QStringLiteral("非法设备状态"), 422);
            QSqlQuery q(db);
            q.prepare(
                QStringLiteral("UPDATE piles SET status=? WHERE id=? AND status!='charging'"));
            q.addBindValue(status);
            q.addBindValue(pileStatusId);
            q.exec();
            return q.numRowsAffected() == 1 ? json(okObject())
                                            : fail(QStringLiteral("状态更新失败"), 409);
        }
        if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/admin/users")) {
            const QString search = r.query.value(QStringLiteral("search"));
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT id,phone,nickname,balance,registered_at,status FROM users WHERE phone LIKE ? OR nickname LIKE ? ORDER BY id DESC"));
            q.addBindValue(QStringLiteral("%%1%").arg(search));
            q.addBindValue(QStringLiteral("%%1%").arg(search));
            QString e;
            auto rows = Database::queryRows(q, &e);
            return e.isEmpty() ? json(okObject(rows)) : fail(e, 500);
        }
        const int userStatusId =
            pathId(r.path, QStringLiteral("/api/admin/users/"), QStringLiteral("/status"));
        if (r.method == QStringLiteral("PATCH") && userStatusId > 0) {
            const QString status = r.body.value(QStringLiteral("status")).toString();
            if (!QStringList{QStringLiteral("normal"), QStringLiteral("frozen")}.contains(status))
                return fail(QStringLiteral("非法用户状态"), 422);
            QSqlQuery q(db);
            q.prepare(QStringLiteral("UPDATE users SET status=? WHERE id=?"));
            q.addBindValue(status);
            q.addBindValue(userStatusId);
            q.exec();
            audit(QStringLiteral("admin"), aid, QStringLiteral("user.status"),
                  QStringLiteral("%1:%2").arg(userStatusId).arg(status));
            return json(okObject());
        }
        if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/admin/orders")) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT o.*,u.phone,p.code pile_code,s.name station_name FROM orders o JOIN users u ON u.id=o.user_id JOIN piles p ON p.id=o.pile_id JOIN stations s ON s.id=p.station_id ORDER BY o.id DESC LIMIT 300"));
            QString e;
            auto rows = Database::queryRows(q, &e);
            return e.isEmpty() ? json(okObject(rows)) : fail(e, 500);
        }
        if (r.method == QStringLiteral("GET") && r.path == QStringLiteral("/api/admin/forecast")) {
            const int hours =
                qBound(1, r.query.value(QStringLiteral("hours"), QStringLiteral("24")).toInt(), 24);
            const int sid =
                r.query.value(QStringLiteral("station_id"), QStringLiteral("0")).toInt();
            QSqlQuery q(db);
            if (sid > 0) {
                q.prepare(QStringLiteral(
                    "SELECT ts,energy_kwh,weather,is_holiday FROM hourly_load WHERE station_id=? ORDER BY ts DESC LIMIT 720"));
                q.addBindValue(sid);
                q.exec();
            } else
                q.exec(QStringLiteral(
                    "SELECT ts,SUM(energy_kwh),AVG(weather),MAX(is_holiday) FROM hourly_load GROUP BY ts ORDER BY ts DESC LIMIT 720"));
            QVector<ForecastService::Sample> samples;
            while (q.next()) {
                const QDateTime at = QDateTime::fromString(q.value(0).toString(), Qt::ISODate);
                samples.append({at.time().hour(), at.date().dayOfWeek() - 1, q.value(3).toInt(),
                                q.value(2).toDouble(), q.value(1).toDouble()});
            }
            auto future = QtConcurrent::run(
                [samples, hours]() { return ForecastService::predict(samples, hours); });
            QJsonArray forecast = future.result();
            QSqlQuery count(db);
            if (sid > 0) {
                count.prepare(QStringLiteral("SELECT COUNT(*) FROM piles WHERE station_id=?"));
                count.addBindValue(sid);
                count.exec();
            } else
                count.exec(QStringLiteral("SELECT COUNT(*) FROM piles"));
            count.next();
            const int pileCount = count.value(0).toInt();
            double mean = 0;
            for (const auto &value : forecast)
                mean += value.toObject().value(QStringLiteral("predicted_kwh")).toDouble();
            mean /= qMax(1, forecast.size());
            for (int i = 0; i < forecast.size(); ++i) {
                QJsonObject item = forecast.at(i).toObject();
                const double load = item.value(QStringLiteral("predicted_kwh")).toDouble();
                const int freePiles = qMax(0, pileCount - qCeil(load / 30.0));
                item.insert(QStringLiteral("predicted_free_piles"), freePiles);
                item.insert(QStringLiteral("peak"), load > mean * 1.08);
                item.insert(QStringLiteral("congestion"),
                            freePiles < pileCount * 0.2
                                ? QStringLiteral("high")
                                : (freePiles < pileCount * 0.5 ? QStringLiteral("medium")
                                                               : QStringLiteral("low")));
                item.insert(QStringLiteral("station_id"), sid);
                forecast.replace(i, item);
            }
            return json(okObject(forecast));
        }
    }

    if (!r.path.startsWith(QStringLiteral("/api/")) && r.method == QStringLiteral("GET"))
        return staticFile(r.path);
    return fail(QStringLiteral("接口不存在"), 404, QStringLiteral("not_found"));
}

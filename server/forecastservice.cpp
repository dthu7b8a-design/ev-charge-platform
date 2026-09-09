#include "forecastservice.h"

#include <QDateTime>
#include <QJsonObject>
#include <QtMath>

QJsonArray ForecastService::predict(const QVector<Sample> &history, int horizonHours) {
    // Lightweight ridge-style linear regression trained by batch gradient descent.
    // Features: intercept, hour sin/cos, weekend, holiday, weather.
    QVector<double> w(6, 0.0);
    if (!history.isEmpty()) {
        const double rate = 0.04;
        for (int epoch = 0; epoch < 500; ++epoch) {
            QVector<double> gradient(6, 0.0);
            for (const Sample &s : history) {
                const double angle = 2.0 * M_PI * s.hour / 24.0;
                const QVector<double> x{1.0,
                                        qSin(angle),
                                        qCos(angle),
                                        s.weekday >= 5 ? 1.0 : 0.0,
                                        static_cast<double>(s.holiday),
                                        s.weather};
                double y = 0.0;
                for (int i = 0; i < w.size(); ++i)
                    y += w.at(i) * x.at(i);
                for (int i = 0; i < w.size(); ++i)
                    gradient[i] += (y - s.load) * x.at(i);
            }
            for (int i = 0; i < w.size(); ++i) {
                const double regularization = i == 0 ? 0.0 : 0.002 * w.at(i);
                w[i] -= rate * (gradient.at(i) / history.size() + regularization);
            }
        }
    }

    QJsonArray result;
    const QDateTime start = QDateTime::currentDateTime().addSecs(3600);
    const int count = qBound(1, horizonHours, 24);
    for (int i = 0; i < count; ++i) {
        const QDateTime at = start.addSecs(i * 3600);
        const double angle = 2.0 * M_PI * at.time().hour() / 24.0;
        const QVector<double> x{
            1.0, qSin(angle), qCos(angle), at.date().dayOfWeek() >= 6 ? 1.0 : 0.0, 0.0, 0.65};
        double load = history.isEmpty() ? 8.0 : 0.0;
        for (int k = 0; k < w.size(); ++k)
            load += w.at(k) * x.at(k);
        QJsonObject row;
        row.insert(QStringLiteral("time"), at.toString(Qt::ISODate));
        row.insert(QStringLiteral("predicted_kwh"), qRound(qMax(0.0, load) * 10.0) / 10.0);
        row.insert(QStringLiteral("confidence"), qMax(0.68, 0.93 - i * 0.008));
        result.append(row);
    }
    return result;
}

#pragma once

#include <QJsonArray>
#include <QVector>

class ForecastService final {
  public:
    struct Sample {
        int hour = 0;
        int weekday = 0;
        int holiday = 0;
        double weather = 0.5;
        double load = 0;
    };
    static QJsonArray predict(const QVector<Sample> &history, int horizonHours);
};

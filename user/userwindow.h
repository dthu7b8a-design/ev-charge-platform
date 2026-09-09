#pragma once

#include "apiclient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTimer;
class QWebEngineView;

class UserWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit UserWindow(QWidget *parent = nullptr);

  private:
    QWidget *buildLogin();
    QWidget *buildHome();
    QWidget *buildStations();
    QWidget *buildOrder();
    QWidget *buildProfile();

    void login();
    void logout();
    void locate();
    void loadStations();
    void loadPiles(int stationId, const QString &name, double lat, double lon);
    void navigate(const QString &mode);
    void loadMe();
    void loadOrder(bool notifyExisting = true);
    void orderAction(const QString &action);
    void showError(const QString &message);
    void updateOrderButtons(const QString &status);
    void updateLiveCharge();
    void stopLiveCharge();

    ApiClient m_api;
    QNetworkAccessManager *m_geocoder = nullptr;
    QStackedWidget *m_pages = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_phone = nullptr;
    QLabel *m_welcome = nullptr;
    QLabel *m_balance = nullptr;

    QComboBox *m_region = nullptr;
    QLineEdit *m_address = nullptr;
    QLabel *m_locationStatus = nullptr;
    QStackedWidget *m_stationFlow = nullptr;
    QTableWidget *m_stations = nullptr;
    QTableWidget *m_piles = nullptr;
    QLabel *m_stationTitle = nullptr;
    QLabel *m_mapTitle = nullptr;
    QWebEngineView *m_map = nullptr;

    QLabel *m_orderState = nullptr;
    QLabel *m_orderInfo = nullptr;
    QWidget *m_liveChargeCard = nullptr;
    QLabel *m_liveDuration = nullptr;
    QLabel *m_liveEnergy = nullptr;
    QLabel *m_liveAmount = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_settle = nullptr;
    QPushButton *m_cancel = nullptr;
    QTimer *m_chargeTimer = nullptr;

    QLineEdit *m_nickname = nullptr;
    QLineEdit *m_avatar = nullptr;
    QLabel *m_avatarPreview = nullptr;

    int m_orderId = 0;
    QDateTime m_chargeStartedAt;
    double m_chargePowerKw = 0.0;
    double m_chargePrice = 0.0;
    QJsonArray m_stationRows;
    double m_lat = 22.54310;
    double m_lon = 114.05787;
    double m_targetLat = 22.54310;
    double m_targetLon = 114.05787;
    QString m_targetName;
    bool m_orderPrompted = false;
    bool m_startMapConsumed = false;
};

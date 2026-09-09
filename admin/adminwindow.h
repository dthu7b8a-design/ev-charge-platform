#pragma once
#include "apiclient.h"
#include <QMainWindow>
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QChartView;

class AdminWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit AdminWindow(QWidget *parent = nullptr);

  private:
    QWidget *buildLogin();
    QWidget *buildConsole();
    QWidget *buildOverview();
    QWidget *buildPiles();
    QWidget *buildStations();
    QWidget *buildUsers();
    QWidget *buildOrders();
    QWidget *buildForecast();
    void login();
    void refreshAll();
    void loadOverview(int days = 7);
    void loadPiles();
    void loadStations();
    void loadUsers();
    void loadOrders();
    void loadForecast(int hours = 24);
    void fillTable(QTableWidget *table, const QJsonArray &rows, const QStringList &keys);
    ApiClient m_api;
    QStackedWidget *m_pages = nullptr;
    QLineEdit *m_username = nullptr;
    QLineEdit *m_password = nullptr;
    QLabel *m_today = nullptr;
    QLabel *m_month = nullptr;
    QLabel *m_total = nullptr;
    QLabel *m_assets = nullptr;
    QLabel *m_forecastWarning = nullptr;
    QChartView *m_salesChart = nullptr;
    QChartView *m_stateChart = nullptr;
    QChartView *m_forecastChart = nullptr;
    QTableWidget *m_distribution = nullptr;
    QTableWidget *m_piles = nullptr;
    QTableWidget *m_stations = nullptr;
    QTableWidget *m_users = nullptr;
    QTableWidget *m_orders = nullptr;
    QLineEdit *m_search = nullptr;
};

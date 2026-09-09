#include "adminwindow.h"
#include "theme.h"

#include <QtCharts>
#include <QBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>

namespace {
QLabel *heading(const QString &t) {
    auto *l = new QLabel(t);
    l->setObjectName(QStringLiteral("Title"));
    return l;
}
QFrame *metricCard(const QString &name, QLabel **value) {
    auto *layout = new QVBoxLayout;
    auto *caption = new QLabel(name);
    caption->setObjectName(QStringLiteral("Subtitle"));
    *value = new QLabel(QStringLiteral("—"));
    (*value)->setObjectName(QStringLiteral("Metric"));
    layout->addWidget(caption);
    layout->addWidget(*value);
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("Card"));
    frame->setLayout(layout);
    return frame;
}
void tableStyle(QTableWidget *t) {
    t->setAlternatingRowColors(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}
}

AdminWindow::AdminWindow(QWidget *parent)
    : QMainWindow(parent), m_api(QStringLiteral("http://127.0.0.1:8080"), this) {
    setWindowTitle(QStringLiteral("充电无忧 · 运营管理平台"));
    resize(1280, 820);
    m_pages = new QStackedWidget;
    m_pages->addWidget(buildLogin());
    m_pages->addWidget(buildConsole());
    setCentralWidget(m_pages);
    connect(&m_api, &ApiClient::networkError, this,
            [this](const QString &m) { statusBar()->showMessage(m, 5000); });
    auto *t = new QTimer(this);
    t->setInterval(30000);
    connect(t, &QTimer::timeout, this, [this]() {
        if (!m_api.token().isEmpty())
            refreshAll();
    });
    t->start();
    if (qEnvironmentVariableIntValue("EVCHARGE_AUTOLOGIN") == 1)
        QTimer::singleShot(350, this, &AdminWindow::login);
}

QWidget *AdminWindow::buildLogin() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(420, 150, 420, 150);
    l->addWidget(heading(QStringLiteral("运营管理平台")));
    auto *s = new QLabel(QStringLiteral("设备、营收和用户，一处看清。"));
    s->setObjectName(QStringLiteral("Subtitle"));
    l->addWidget(s);
    m_username = new QLineEdit(QStringLiteral("admin"));
    m_password = new QLineEdit(QStringLiteral("123456"));
    m_password->setEchoMode(QLineEdit::Password);
    l->addSpacing(20);
    l->addWidget(m_username);
    l->addWidget(m_password);
    auto *b = new QPushButton(QStringLiteral("管理员登录"));
    l->addWidget(b);
    l->addStretch();
    connect(b, &QPushButton::clicked, this, &AdminWindow::login);
    connect(m_password, &QLineEdit::returnPressed, this, &AdminWindow::login);
    return w;
}
QWidget *AdminWindow::buildConsole() {
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->addTab(buildOverview(), QStringLiteral("运营概览"));
    tabs->addTab(buildPiles(), QStringLiteral("充电桩"));
    tabs->addTab(buildStations(), QStringLiteral("充电站"));
    tabs->addTab(buildUsers(), QStringLiteral("用户"));
    tabs->addTab(buildOrders(), QStringLiteral("订单"));
    tabs->addTab(buildForecast(), QStringLiteral("负荷预测"));
    bool ok = false;
    const int startTab = qEnvironmentVariable("EVCHARGE_START_TAB").toInt(&ok);
    if (ok && startTab >= 0 && startTab < tabs->count())
        tabs->setCurrentIndex(startTab);
    return tabs;
}
QWidget *AdminWindow::buildOverview() {
    auto *w = new QWidget;
    auto *root = new QVBoxLayout(w);
    root->setContentsMargins(24, 20, 24, 20);
    auto *head = new QHBoxLayout;
    head->addWidget(heading(QStringLiteral("实时运营概览")));
    head->addStretch();
    auto *b7 = new QPushButton(QStringLiteral("近 7 天"));
    auto *b30 = new QPushButton(QStringLiteral("近 30 天"));
    head->addWidget(b7);
    head->addWidget(b30);
    root->addLayout(head);
    auto *metrics = new QHBoxLayout;
    metrics->addWidget(metricCard(QStringLiteral("今日营收"), &m_today));
    metrics->addWidget(metricCard(QStringLiteral("本月营收"), &m_month));
    metrics->addWidget(metricCard(QStringLiteral("累计营收"), &m_total));
    metrics->addWidget(metricCard(QStringLiteral("设备与用户"), &m_assets));
    root->addLayout(metrics);
    auto *charts = new QHBoxLayout;
    m_salesChart = new QChartView;
    m_stateChart = new QChartView;
    auto *statePanel = new QWidget;
    auto *stateLayout = new QVBoxLayout(statePanel);
    stateLayout->setContentsMargins(0, 0, 0, 0);
    stateLayout->addWidget(m_stateChart, 1);
    m_distribution = new QTableWidget(0, 3);
    m_distribution->setHorizontalHeaderLabels(
        {QStringLiteral("状态"), QStringLiteral("数量"), QStringLiteral("占比")});
    tableStyle(m_distribution);
    m_distribution->setMaximumHeight(135);
    stateLayout->addWidget(m_distribution);
    charts->addWidget(m_salesChart, 2);
    charts->addWidget(statePanel, 1);
    root->addLayout(charts, 1);
    connect(b7, &QPushButton::clicked, this, [this]() { loadOverview(7); });
    connect(b30, &QPushButton::clicked, this, [this]() { loadOverview(30); });
    return w;
}
QWidget *AdminWindow::buildPiles() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(20, 18, 20, 18);
    auto *h = new QHBoxLayout;
    h->addWidget(heading(QStringLiteral("充电桩状态与远程控制")));
    h->addStretch();
    auto *add = new QPushButton(QStringLiteral("新增充电桩"));
    auto *r = new QPushButton(QStringLiteral("刷新"));
    h->addWidget(add);
    h->addWidget(r);
    l->addLayout(h);
    m_piles = new QTableWidget(0, 9);
    m_piles->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("所属站"), QStringLiteral("编号"),
         QStringLiteral("类型"), QStringLiteral("功率"), QStringLiteral("状态"),
         QStringLiteral("累计次数"), QStringLiteral("累计时长"), QStringLiteral("操作")});
    tableStyle(m_piles);
    m_piles->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Fixed);
    m_piles->setColumnWidth(8, 190);
    m_piles->verticalHeader()->setDefaultSectionSize(40);
    l->addWidget(m_piles);
    connect(r, &QPushButton::clicked, this, &AdminWindow::loadPiles);
    connect(add, &QPushButton::clicked, this, [this]() {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("新增充电桩"));
        QFormLayout form(&d);
        QLineEdit station(QStringLiteral("1")), code(QStringLiteral("SZ001-07")),
            type(QStringLiteral("fast")), power(QStringLiteral("120"));
        form.addRow(QStringLiteral("充电站 ID"), &station);
        form.addRow(QStringLiteral("设备编号"), &code);
        form.addRow(QStringLiteral("类型 fast/slow"), &type);
        form.addRow(QStringLiteral("功率 kW"), &power);
        QDialogButtonBox box(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&box);
        connect(&box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(&box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted)
            m_api.post(QStringLiteral("/api/admin/piles"),
                       {{QStringLiteral("station_id"), station.text().toInt()},
                        {QStringLiteral("code"), code.text()},
                        {QStringLiteral("type"), type.text()},
                        {QStringLiteral("power"), power.text().toDouble()}},
                       [this](auto result) {
                           if (result.value(QStringLiteral("ok")).toBool()) {
                               loadPiles();
                               loadOverview();
                           }
                       });
    });
    return w;
}
QWidget *AdminWindow::buildStations() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(20, 18, 20, 18);
    auto *h = new QHBoxLayout;
    h->addWidget(heading(QStringLiteral("充电站管理（双击行查看站内详情）")));
    h->addStretch();
    auto *add = new QPushButton(QStringLiteral("新增充电站"));
    h->addWidget(add);
    l->addLayout(h);
    m_stations = new QTableWidget(0, 9);
    m_stations->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("名称"), QStringLiteral("地址"),
         QStringLiteral("纬度"), QStringLiteral("经度"), QStringLiteral("电价"),
         QStringLiteral("桩数"), QStringLiteral("空闲"), QStringLiteral("在线率")});
    tableStyle(m_stations);
    l->addWidget(m_stations);
    connect(add, &QPushButton::clicked, this, [this]() {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("新增充电站"));
        QFormLayout form(&d);
        QLineEdit name, address, lat(QStringLiteral("22.5431")), lon(QStringLiteral("114.0579")),
            price(QStringLiteral("1.20")), pileCount(QStringLiteral("4"));
        form.addRow(QStringLiteral("名称"), &name);
        form.addRow(QStringLiteral("地址"), &address);
        form.addRow(QStringLiteral("纬度"), &lat);
        form.addRow(QStringLiteral("经度"), &lon);
        form.addRow(QStringLiteral("电价"), &price);
        form.addRow(QStringLiteral("电桩数量"), &pileCount);
        QDialogButtonBox box(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&box);
        connect(&box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(&box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted)
            m_api.post(QStringLiteral("/api/admin/stations"),
                       {{QStringLiteral("name"), name.text()},
                        {QStringLiteral("address"), address.text()},
                        {QStringLiteral("latitude"), lat.text().toDouble()},
                        {QStringLiteral("longitude"), lon.text().toDouble()},
                        {QStringLiteral("price"), price.text().toDouble()},
                        {QStringLiteral("pile_count"), pileCount.text().toInt()}},
                       [this](auto r) {
                           if (r.value(QStringLiteral("ok")).toBool()) {
                               loadStations();
                               loadPiles();
                           }
                       });
    });
    connect(m_stations, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const int id = m_stations->item(row, 0)->text().toInt();
        const QString name = m_stations->item(row, 1)->text();
        m_api.get(QStringLiteral("/api/stations/%1/piles").arg(id), [this, name](auto r) {
            QDialog d(this);
            d.setWindowTitle(name + QStringLiteral(" · 站内电桩"));
            auto *layout = new QVBoxLayout(&d);
            auto *table = new QTableWidget(0, 5);
            table->setHorizontalHeaderLabels({QStringLiteral("编号"), QStringLiteral("类型"),
                                              QStringLiteral("功率"), QStringLiteral("状态"),
                                              QStringLiteral("累计时长")});
            tableStyle(table);
            const auto rows = r.value(QStringLiteral("data")).toArray();
            table->setRowCount(rows.size());
            for (int i = 0; i < rows.size(); ++i) {
                const auto p = rows.at(i).toObject();
                const QStringList values{
                    p.value(QStringLiteral("code")).toString(),
                    p.value(QStringLiteral("type")).toString(),
                    QString::number(p.value(QStringLiteral("power")).toDouble()),
                    Theme::statusText(p.value(QStringLiteral("status")).toString()),
                    QString::number(p.value(QStringLiteral("total_minutes")).toInt())};
                for (int c = 0; c < values.size(); ++c)
                    table->setItem(i, c, new QTableWidgetItem(values.at(c)));
            }
            layout->addWidget(table);
            d.resize(760, 430);
            d.exec();
        });
    });
    return w;
}
QWidget *AdminWindow::buildUsers() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(20, 18, 20, 18);
    auto *h = new QHBoxLayout;
    h->addWidget(heading(QStringLiteral("用户管理")));
    h->addStretch();
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("手机号或昵称"));
    auto *b = new QPushButton(QStringLiteral("搜索"));
    h->addWidget(m_search);
    h->addWidget(b);
    l->addLayout(h);
    m_users = new QTableWidget(0, 7);
    m_users->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("手机号"),
                                        QStringLiteral("昵称"), QStringLiteral("余额"),
                                        QStringLiteral("注册时间"), QStringLiteral("状态"),
                                        QStringLiteral("操作")});
    tableStyle(m_users);
    m_users->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_users->setColumnWidth(6, 120);
    m_users->verticalHeader()->setDefaultSectionSize(40);
    l->addWidget(m_users);
    connect(b, &QPushButton::clicked, this, &AdminWindow::loadUsers);
    connect(m_search, &QLineEdit::returnPressed, this, &AdminWindow::loadUsers);
    return w;
}
QWidget *AdminWindow::buildOrders() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(20, 18, 20, 18);
    l->addWidget(heading(QStringLiteral("全量订单")));
    m_orders = new QTableWidget(0, 10);
    m_orders->setHorizontalHeaderLabels({QStringLiteral("订单号"), QStringLiteral("用户"),
                                         QStringLiteral("充电站"), QStringLiteral("设备"),
                                         QStringLiteral("状态"), QStringLiteral("时长"),
                                         QStringLiteral("电量"), QStringLiteral("金额"),
                                         QStringLiteral("预约"), QStringLiteral("结算")});
    tableStyle(m_orders);
    l->addWidget(m_orders);
    return w;
}
QWidget *AdminWindow::buildForecast() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(24, 20, 24, 20);
    auto *h = new QHBoxLayout;
    h->addWidget(heading(QStringLiteral("机器学习负荷预测")));
    h->addStretch();
    for (const int hours : {1, 6, 24}) {
        auto *b = new QPushButton(QStringLiteral("未来 %1 小时").arg(hours));
        h->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, hours]() { loadForecast(hours); });
    }
    l->addLayout(h);
    auto *s = new QLabel(QStringLiteral(
        "特征：历史能耗、小时周期、星期/节假日、天气系数；模型在服务端线程池训练与推理。"));
    s->setObjectName(QStringLiteral("Subtitle"));
    l->addWidget(s);
    m_forecastWarning = new QLabel(QStringLiteral("正在计算负荷预警…"));
    m_forecastWarning->setObjectName(QStringLiteral("Metric"));
    l->addWidget(m_forecastWarning);
    m_forecastChart = new QChartView;
    l->addWidget(m_forecastChart, 1);
    return w;
}
void AdminWindow::login() {
    m_api.post(QStringLiteral("/api/admin/login"),
               {{QStringLiteral("username"), m_username->text()},
                {QStringLiteral("password"), m_password->text()}},
               [this](auto r) {
                   if (!r.value(QStringLiteral("ok")).toBool())
                       return;
                   m_api.setToken(r.value(QStringLiteral("data"))
                                      .toObject()
                                      .value(QStringLiteral("token"))
                                      .toString());
                   m_pages->setCurrentIndex(1);
                   refreshAll();
               });
}
void AdminWindow::refreshAll() {
    loadOverview();
    loadPiles();
    loadStations();
    loadUsers();
    loadOrders();
    loadForecast();
}
void AdminWindow::loadOverview(int days) {
    m_api.get(QStringLiteral("/api/admin/summary"), [this](auto r) {
        const auto d = r.value(QStringLiteral("data")).toObject();
        m_today->setText(QStringLiteral("¥%1").arg(
            d.value(QStringLiteral("today_revenue")).toDouble(), 0, 'f', 2));
        m_month->setText(QStringLiteral("¥%1").arg(
            d.value(QStringLiteral("month_revenue")).toDouble(), 0, 'f', 2));
        m_total->setText(QStringLiteral("¥%1").arg(
            d.value(QStringLiteral("total_revenue")).toDouble(), 0, 'f', 2));
        m_assets->setText(QStringLiteral("%1 桩 / %2 用户")
                              .arg(d.value(QStringLiteral("piles")).toInt())
                              .arg(d.value(QStringLiteral("users")).toInt()));
    });
    m_api.get(QStringLiteral("/api/admin/sales?days=%1").arg(days), [this](auto r) {
        auto *chart = new QChart;
        chart->setTitle(QStringLiteral("营收趋势折线图"));
        auto *series = new QLineSeries;
        series->setName(QStringLiteral("营收（元）"));
        auto *axisX = new QCategoryAxis;
        const auto rows = r.value(QStringLiteral("data")).toArray();
        double maximum = 1.0;
        for (int i = 0; i < rows.size(); ++i) {
            const auto row = rows.at(i).toObject();
            const double revenue = row.value(QStringLiteral("revenue")).toDouble();
            series->append(i + 0.5, revenue);
            axisX->append(row.value(QStringLiteral("date")).toString().mid(5), i + 1);
            maximum = qMax(maximum, revenue);
        }
        chart->addSeries(series);
        axisX->setRange(0, rows.size());
        axisX->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
        chart->addAxis(axisX, Qt::AlignBottom);
        series->attachAxis(axisX);
        auto *axisY = new QValueAxis;
        axisY->setLabelFormat(QStringLiteral("%.0f"));
        axisY->setRange(0, maximum * 1.2);
        chart->addAxis(axisY, Qt::AlignLeft);
        series->attachAxis(axisY);
        chart->legend()->hide();
        chart->setTheme(QChart::ChartThemeDark);
        m_salesChart->setChart(chart);
    });
    m_api.get(QStringLiteral("/api/admin/pile-distribution"), [this](auto r) {
        const auto rows = r.value(QStringLiteral("data")).toArray();
        int total = 0;
        for (const auto &v : rows)
            total += v.toObject().value(QStringLiteral("count")).toInt();
        m_distribution->setRowCount(rows.size());
        auto *chart = new QChart;
        chart->setTitle(QStringLiteral("设备状态"));
        auto *series = new QPieSeries;
        for (int i = 0; i < rows.size(); ++i) {
            const auto o = rows.at(i).toObject();
            const QString status = o.value(QStringLiteral("status")).toString();
            const int count = o.value(QStringLiteral("count")).toInt();
            m_distribution->setItem(i, 0, new QTableWidgetItem(Theme::statusText(status)));
            m_distribution->setItem(i, 1, new QTableWidgetItem(QString::number(count)));
            m_distribution->setItem(i, 2,
                                    new QTableWidgetItem(QStringLiteral("%1%").arg(
                                        total ? count * 100.0 / total : 0, 0, 'f', 1)));
            auto *slice = series->append(Theme::statusText(status), count);
            if (status == QStringLiteral("idle"))
                slice->setColor(QColor(QStringLiteral("#48c774")));
            else if (status == QStringLiteral("fault"))
                slice->setColor(QColor(QStringLiteral("#e66772")));
            else if (status == QStringLiteral("charging"))
                slice->setColor(QColor(QStringLiteral("#4f8df7")));
        }
        chart->addSeries(series);
        chart->setTheme(QChart::ChartThemeDark);
        m_stateChart->setChart(chart);
    });
}
void AdminWindow::loadPiles() {
    m_api.get(QStringLiteral("/api/admin/piles"), [this](auto r) {
        const auto rows = r.value(QStringLiteral("data")).toArray();
        m_piles->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const auto o = rows.at(i).toObject();
            const QStringList values{
                QString::number(o.value(QStringLiteral("id")).toInt()),
                o.value(QStringLiteral("station_name")).toString(),
                o.value(QStringLiteral("code")).toString(),
                o.value(QStringLiteral("type")).toString(),
                QString::number(o.value(QStringLiteral("power")).toDouble()),
                Theme::statusText(o.value(QStringLiteral("status")).toString()),
                QString::number(o.value(QStringLiteral("total_sessions")).toInt()),
                QStringLiteral("%1 分钟").arg(o.value(QStringLiteral("total_minutes")).toInt())};
            for (int c = 0; c < values.size(); ++c)
                m_piles->setItem(i, c, new QTableWidgetItem(values.at(c)));
            auto *actions = new QWidget;
            actions->setMinimumHeight(36);
            auto *layout = new QHBoxLayout(actions);
            layout->setContentsMargins(3, 3, 3, 3);
            layout->setSpacing(6);
            auto *restart = new QPushButton(QStringLiteral("重启"));
            auto *toggle = new QPushButton(o.value(QStringLiteral("status")).toString() ==
                                                   QStringLiteral("fault")
                                               ? QStringLiteral("恢复")
                                               : QStringLiteral("报障"));
            restart->setMinimumSize(72, 30);
            toggle->setMinimumSize(72, 30);
            const bool charging =
                o.value(QStringLiteral("status")).toString() == QStringLiteral("charging");
            restart->setEnabled(!charging);
            toggle->setEnabled(!charging);
            layout->addWidget(restart);
            layout->addWidget(toggle);
            m_piles->setCellWidget(i, 8, actions);
            connect(restart, &QPushButton::clicked, this, [this, o]() {
                m_api.post(QStringLiteral("/api/admin/piles/%1/restart")
                               .arg(o.value(QStringLiteral("id")).toInt()),
                           {}, [this](auto) {
                               loadPiles();
                               loadOverview();
                           });
            });
            connect(toggle, &QPushButton::clicked, this, [this, o]() {
                const QString next =
                    o.value(QStringLiteral("status")).toString() == QStringLiteral("fault")
                        ? QStringLiteral("idle")
                        : QStringLiteral("fault");
                m_api.patch(QStringLiteral("/api/admin/piles/%1/status")
                                .arg(o.value(QStringLiteral("id")).toInt()),
                            {{QStringLiteral("status"), next}}, [this](auto) {
                                loadPiles();
                                loadOverview();
                            });
            });
        }
    });
}
void AdminWindow::loadStations() {
    m_api.get(QStringLiteral("/api/admin/stations"), [this](auto r) {
        fillTable(m_stations, r.value(QStringLiteral("data")).toArray(),
                  {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("address"),
                   QStringLiteral("latitude"), QStringLiteral("longitude"), QStringLiteral("price"),
                   QStringLiteral("pile_count"), QStringLiteral("idle_count"),
                   QStringLiteral("online_rate")});
        for (int row = 0; row < m_stations->rowCount(); ++row)
            m_stations->item(row, 8)->setText(m_stations->item(row, 8)->text() +
                                              QStringLiteral("%"));
    });
}
void AdminWindow::loadUsers() {
    m_api.get(
        QStringLiteral("/api/admin/users?search=%1").arg(QUrl::toPercentEncoding(m_search->text())),
        [this](auto r) {
            const auto rows = r.value(QStringLiteral("data")).toArray();
            m_users->setRowCount(rows.size());
            for (int i = 0; i < rows.size(); ++i) {
                const auto o = rows.at(i).toObject();
                const QStringList values{
                    QString::number(o.value(QStringLiteral("id")).toInt()),
                    o.value(QStringLiteral("phone")).toString(),
                    o.value(QStringLiteral("nickname")).toString(),
                    QString::number(o.value(QStringLiteral("balance")).toDouble(), 'f', 2),
                    o.value(QStringLiteral("registered_at")).toString(),
                    Theme::statusText(o.value(QStringLiteral("status")).toString())};
                for (int c = 0; c < values.size(); ++c)
                    m_users->setItem(i, c, new QTableWidgetItem(values.at(c)));
                const QString next =
                    o.value(QStringLiteral("status")).toString() == QStringLiteral("normal")
                        ? QStringLiteral("frozen")
                        : QStringLiteral("normal");
                auto *b =
                    new QPushButton(next == QStringLiteral("frozen") ? QStringLiteral("冻结")
                                                                     : QStringLiteral("解冻"));
                b->setMinimumSize(92, 30);
                m_users->setCellWidget(i, 6, b);
                connect(b, &QPushButton::clicked, this, [this, o, next]() {
                    m_api.patch(QStringLiteral("/api/admin/users/%1/status")
                                    .arg(o.value(QStringLiteral("id")).toInt()),
                                {{QStringLiteral("status"), next}}, [this](auto) { loadUsers(); });
                });
            }
        });
}
void AdminWindow::loadOrders() {
    m_api.get(QStringLiteral("/api/admin/orders"), [this](auto r) {
        fillTable(m_orders, r.value(QStringLiteral("data")).toArray(),
                  {QStringLiteral("order_no"), QStringLiteral("phone"),
                   QStringLiteral("station_name"), QStringLiteral("pile_code"),
                   QStringLiteral("status"), QStringLiteral("duration_minutes"),
                   QStringLiteral("energy_kwh"), QStringLiteral("amount"),
                   QStringLiteral("reserved_at"), QStringLiteral("settled_at")});
    });
}
void AdminWindow::loadForecast(int hours) {
    m_api.get(QStringLiteral("/api/admin/forecast?hours=%1").arg(hours), [this](auto r) {
        auto *chart = new QChart;
        chart->setTitle(QStringLiteral("预测总负荷（kWh）"));
        auto *s = new QLineSeries;
        const auto rows = r.value(QStringLiteral("data")).toArray();
        QString peakTime;
        int minimumFree = 9999;
        for (int i = 0; i < rows.size(); ++i) {
            const auto item = rows.at(i).toObject();
            s->append(i, item.value(QStringLiteral("predicted_kwh")).toDouble());
            minimumFree =
                qMin(minimumFree, item.value(QStringLiteral("predicted_free_piles")).toInt());
            if (peakTime.isEmpty() && item.value(QStringLiteral("peak")).toBool())
                peakTime = item.value(QStringLiteral("time")).toString().mid(11, 5);
        }
        m_forecastWarning->setText(
            peakTime.isEmpty() ? QStringLiteral("负荷平稳 · 预计最少 %1 个空闲桩").arg(minimumFree)
                               : QStringLiteral("负荷预警 · %1 左右预计达峰 · 最少 %2 个空闲桩")
                                     .arg(peakTime)
                                     .arg(minimumFree));
        chart->addSeries(s);
        chart->createDefaultAxes();
        chart->setTheme(QChart::ChartThemeDark);
        chart->legend()->hide();
        m_forecastChart->setChart(chart);
    });
}
void AdminWindow::fillTable(QTableWidget *t, const QJsonArray &rows, const QStringList &keys) {
    t->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto o = rows.at(i).toObject();
        for (int c = 0; c < keys.size(); ++c) {
            QString text;
            if (o.value(keys.at(c)).isDouble())
                text = QString::number(o.value(keys.at(c)).toDouble());
            else
                text = o.value(keys.at(c)).toString();
            if (keys.at(c) == QStringLiteral("status"))
                text = Theme::statusText(text);
            t->setItem(i, c, new QTableWidgetItem(text));
        }
    }
}

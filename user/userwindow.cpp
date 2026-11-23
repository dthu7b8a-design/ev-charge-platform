#include "userwindow.h"
#include "theme.h"

#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpressionValidator>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebEngineView>
#include <QWebEngineSettings>
#include <QtMath>

namespace {
QLabel *title(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("Title"));
    return l;
}
QPushButton *secondary(const QString &text) {
    auto *b = new QPushButton(text);
    b->setObjectName(QStringLiteral("Secondary"));
    return b;
}
QString distanceText(double distanceKm) {
    return distanceKm < 0.05 ? QStringLiteral("< 0.1 km")
                             : QStringLiteral("%1 km").arg(distanceKm, 0, 'f', 1);
}
}

UserWindow::UserWindow(QWidget *parent)
    : QMainWindow(parent), m_api(QStringLiteral("http://127.0.0.1:8080"), this) {
    setWindowTitle(QStringLiteral("充电无忧 · 用户端"));
    resize(430, 820);
    setMinimumSize(390, 700);

    m_geocoder = new QNetworkAccessManager(this);
    m_pages = new QStackedWidget;
    m_pages->addWidget(buildLogin());
    m_pages->addWidget(buildHome());
    setCentralWidget(m_pages);

    connect(&m_api, &ApiClient::networkError, this, &UserWindow::showError);

    auto *orderRefreshTimer = new QTimer(this);
    orderRefreshTimer->setInterval(5000);
    connect(orderRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_api.token().isEmpty())
            loadOrder();
    });
    orderRefreshTimer->start();

    m_chargeTimer = new QTimer(this);
    m_chargeTimer->setInterval(1000);
    connect(m_chargeTimer, &QTimer::timeout, this, &UserWindow::updateLiveCharge);

    if (qEnvironmentVariableIntValue("EVCHARGE_AUTOLOGIN") == 1)
        QTimer::singleShot(350, this, &UserWindow::login);
}

QWidget *UserWindow::buildLogin() {
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(34, 90, 34, 50);
    root->addStretch();
    root->addWidget(title(QStringLiteral("充电，从容一点。")));
    auto *sub = new QLabel(QStringLiteral("输入手机号即可登录；首次使用将自动注册。"));
    sub->setObjectName(QStringLiteral("Subtitle"));
    sub->setWordWrap(true);
    root->addWidget(sub);
    root->addSpacing(24);
    m_phone = new QLineEdit(QStringLiteral("13800138001"));
    m_phone->setPlaceholderText(QStringLiteral("11 位手机号"));
    m_phone->setMaxLength(11);
    m_phone->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,11}")), m_phone));
    root->addWidget(m_phone);
    auto *button = new QPushButton(QStringLiteral("登录 / 自动注册"));
    root->addWidget(button);
    connect(button, &QPushButton::clicked, this, &UserWindow::login);
    connect(m_phone, &QLineEdit::returnPressed, this, &UserWindow::login);
    root->addStretch(2);
    return page;
}

QWidget *UserWindow::buildHome() {
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildStations(), QStringLiteral("找桩"));
    m_tabs->addTab(buildOrder(), QStringLiteral("订单"));
    m_tabs->addTab(buildProfile(), QStringLiteral("我的"));
    return m_tabs;
}

QWidget *UserWindow::buildStations() {
    m_stationFlow = new QStackedWidget;
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(16, 18, 16, 16);
    m_welcome = title(QStringLiteral("附近充电站"));
    root->addWidget(m_welcome);
    auto *locator = new QHBoxLayout;
    m_region = new QComboBox;
    m_region->addItems({QStringLiteral("福田区"), QStringLiteral("南山区"),
                        QStringLiteral("宝安区"), QStringLiteral("龙岗区")});
    m_region->setMinimumSize(84, 40);
    m_address = new QLineEdit(QStringLiteral("深圳市民中心"));
    m_address->setMinimumHeight(40);
    m_address->setPlaceholderText(QStringLiteral("输入地址或使用区域示例位置"));
    auto *locateButton = new QPushButton(QStringLiteral("定位"));
    locateButton->setMinimumSize(72, 40);
    locator->addWidget(m_region);
    locator->addWidget(m_address, 1);
    locator->addWidget(locateButton);
    root->addLayout(locator);
    m_locationStatus = new QLabel(QStringLiteral("当前位置：福田区 · 深圳市民中心（模拟 GPS）"));
    m_locationStatus->setObjectName(QStringLiteral("Subtitle"));
    m_locationStatus->setWordWrap(true);
    root->addWidget(m_locationStatus);
    connect(locateButton, &QPushButton::clicked, this, &UserWindow::locate);
    connect(m_region, &QComboBox::currentTextChanged, this, [this](const QString &region) {
        if (region == QStringLiteral("南山区"))
            m_address->setText(QStringLiteral("南山科技园"));
        else if (region == QStringLiteral("宝安区"))
            m_address->setText(QStringLiteral("宝安中心"));
        else if (region == QStringLiteral("龙岗区"))
            m_address->setText(QStringLiteral("龙岗大运中心"));
        else
            m_address->setText(QStringLiteral("深圳市民中心"));
    });
    m_stations = new QTableWidget(0, 4);
    m_stations->setHorizontalHeaderLabels({QStringLiteral("充电站"), QStringLiteral("价格"),
                                           QStringLiteral("空闲"), QStringLiteral("距离")});
    m_stations->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stations->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_stations->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_stations->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_stations->setColumnWidth(3, 76);
    m_stations->verticalHeader()->setDefaultSectionSize(34);
    m_stations->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stations->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_stations, 2);
    m_stationTitle = new QLabel(QStringLiteral("选择充电站查看设备"));
    m_stationTitle->setObjectName(QStringLiteral("Subtitle"));
    root->addWidget(m_stationTitle);
    m_piles = new QTableWidget(0, 4);
    m_piles->setHorizontalHeaderLabels({QStringLiteral("编号"), QStringLiteral("类型"),
                                        QStringLiteral("功率"), QStringLiteral("状态/操作")});
    m_piles->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_piles->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_piles->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_piles->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_piles->setColumnWidth(3, 108);
    m_piles->verticalHeader()->setDefaultSectionSize(42);
    m_piles->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_piles, 2);
    auto *navigation = new QHBoxLayout;
    auto *drive = new QPushButton(QStringLiteral("驾车导航"));
    auto *walk = secondary(QStringLiteral("步行导航"));
    navigation->addWidget(drive);
    navigation->addWidget(walk);
    root->addLayout(navigation);
    connect(drive, &QPushButton::clicked, this, [this]() { navigate(QStringLiteral("drive")); });
    connect(walk, &QPushButton::clicked, this, [this]() { navigate(QStringLiteral("walk")); });
    connect(m_stations, &QTableWidget::cellClicked, this, [this](int row) {
        if (row < 0 || row >= m_stationRows.size())
            return;
        const auto s = m_stationRows.at(row).toObject();
        loadPiles(s.value(QStringLiteral("id")).toInt(), s.value(QStringLiteral("name")).toString(),
                  s.value(QStringLiteral("latitude")).toDouble(),
                  s.value(QStringLiteral("longitude")).toDouble());
    });

    auto *mapPage = new QWidget;
    auto *mapRoot = new QVBoxLayout(mapPage);
    mapRoot->setContentsMargins(16, 14, 16, 16);
    auto *mapHeader = new QHBoxLayout;
    auto *back = secondary(QStringLiteral("← 返回找桩"));
    back->setMinimumSize(112, 38);
    mapHeader->addWidget(back);
    mapHeader->addStretch();
    mapRoot->addLayout(mapHeader);
    m_mapTitle = title(QStringLiteral("腾讯地图导航"));
    m_mapTitle->setWordWrap(true);
    mapRoot->addWidget(m_mapTitle);
    auto *mapHint =
        new QLabel(QStringLiteral("路线由腾讯地图展示；返回后仍保留当前站点和定位结果。"));
    mapHint->setObjectName(QStringLiteral("Subtitle"));
    mapHint->setWordWrap(true);
    mapRoot->addWidget(mapHint);
    m_map = new QWebEngineView;
    m_map->setZoomFactor(0.80);
    m_map->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
    m_map->setHtml(QStringLiteral(
        "<style>body{background:#1b1e21;color:#9aa4ae;font:14px sans-serif;display:grid;place-items:center;height:100vh;margin:0;overflow:hidden}</style><div>选择站点后进入驾车或步行导航</div>"));
    mapRoot->addWidget(m_map, 1);
    connect(back, &QPushButton::clicked, this, [this]() { m_stationFlow->setCurrentIndex(0); });

    m_stationFlow->addWidget(page);
    m_stationFlow->addWidget(mapPage);
    m_stationFlow->setCurrentIndex(0);
    return m_stationFlow;
}

QWidget *UserWindow::buildOrder() {
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(20, 24, 20, 20);
    root->addWidget(title(QStringLiteral("当前订单")));

    m_orderState = new QLabel(QStringLiteral("暂无进行中订单"));
    m_orderState->setObjectName(QStringLiteral("Metric"));
    root->addWidget(m_orderState);

    m_orderInfo = new QLabel(QStringLiteral("预约充电桩后，可在这里完成启动、结束和结算。"));
    m_orderInfo->setWordWrap(true);
    m_orderInfo->setObjectName(QStringLiteral("Subtitle"));
    root->addWidget(m_orderInfo);

    auto *liveCard = new QFrame;
    liveCard->setObjectName(QStringLiteral("Card"));
    auto *liveLayout = new QGridLayout(liveCard);
    liveLayout->setContentsMargins(16, 14, 16, 14);
    liveLayout->setHorizontalSpacing(18);
    liveLayout->setVerticalSpacing(7);

    auto *liveTitle = new QLabel(QStringLiteral("实时充电数据 · 每秒更新"));
    liveTitle->setStyleSheet(QStringLiteral("font-weight: 700;"));
    liveLayout->addWidget(liveTitle, 0, 0, 1, 2);

    auto addLiveRow = [liveLayout](int row, const QString &caption, QLabel **valueLabel) {
        auto *captionLabel = new QLabel(caption);
        captionLabel->setObjectName(QStringLiteral("Subtitle"));
        *valueLabel = new QLabel;
        (*valueLabel)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        (*valueLabel)->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
        liveLayout->addWidget(captionLabel, row, 0);
        liveLayout->addWidget(*valueLabel, row, 1);
    };
    addLiveRow(1, QStringLiteral("充电时长"), &m_liveDuration);
    addLiveRow(2, QStringLiteral("实时电量"), &m_liveEnergy);
    addLiveRow(3, QStringLiteral("当前消费"), &m_liveAmount);
    m_liveAmount->setStyleSheet(
        QStringLiteral("font-size: 20px; font-weight: 700; color: #7ce38b;"));

    auto *billingHint =
        new QLabel(QStringLiteral("金额按站点单价实时估算，结束充电时由服务端按相同规则确认。"));
    billingHint->setObjectName(QStringLiteral("Subtitle"));
    billingHint->setWordWrap(true);
    liveLayout->addWidget(billingHint, 4, 0, 1, 2);

    m_liveChargeCard = liveCard;
    m_liveChargeCard->hide();
    root->addWidget(m_liveChargeCard);
    root->addSpacing(12);

    m_start = new QPushButton(QStringLiteral("开始充电"));
    m_stop = new QPushButton(QStringLiteral("结束充电"));
    m_settle = new QPushButton(QStringLiteral("钱包结算"));
    m_cancel = secondary(QStringLiteral("取消预约"));
    for (auto *button : {m_start, m_stop, m_settle, m_cancel})
        root->addWidget(button);

    connect(m_start, &QPushButton::clicked, this,
            [this]() { orderAction(QStringLiteral("start")); });
    connect(m_stop, &QPushButton::clicked, this, [this]() { orderAction(QStringLiteral("stop")); });
    connect(m_settle, &QPushButton::clicked, this,
            [this]() { orderAction(QStringLiteral("settle")); });
    connect(m_cancel, &QPushButton::clicked, this,
            [this]() { orderAction(QStringLiteral("cancel")); });

    root->addStretch();
    updateOrderButtons(QString());
    return page;
}

QWidget *UserWindow::buildProfile() {
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(20, 24, 20, 20);
    root->addWidget(title(QStringLiteral("个人中心")));

    m_avatarPreview = new QLabel(QStringLiteral("默认头像"));
    m_avatarPreview->setAlignment(Qt::AlignCenter);
    m_avatarPreview->setFixedSize(72, 72);
    m_avatarPreview->setStyleSheet(
        QStringLiteral("background: #30363d; border-radius: 36px; color: #aab2bb;"));
    root->addWidget(m_avatarPreview, 0, Qt::AlignHCenter);

    m_balance = new QLabel(QStringLiteral("余额 ¥ 0.00"));
    m_balance->setObjectName(QStringLiteral("Metric"));
    root->addWidget(m_balance);

    auto *recharge = new QPushButton(QStringLiteral("充值余额"));
    root->addWidget(recharge);

    auto *form = new QFormLayout;
    m_nickname = new QLineEdit;
    m_avatar = new QLineEdit;
    m_avatar->setPlaceholderText(QStringLiteral("本地头像文件或 URL"));
    form->addRow(QStringLiteral("昵称"), m_nickname);
    form->addRow(QStringLiteral("头像"), m_avatar);
    root->addLayout(form);

    auto *choose = secondary(QStringLiteral("选择本地头像"));
    auto *save = secondary(QStringLiteral("保存资料"));
    auto *logoutButton = new QPushButton(QStringLiteral("退出登录"));
    logoutButton->setObjectName(QStringLiteral("Danger"));
    logoutButton->setMinimumHeight(42);
    root->addWidget(choose);
    root->addWidget(save);
    root->addStretch();
    root->addWidget(logoutButton);

    connect(recharge, &QPushButton::clicked, this, [this]() {
        bool accepted = false;
        const double amount =
            QInputDialog::getDouble(this, QStringLiteral("钱包充值"), QStringLiteral("充值金额"),
                                    50, 1, 10000, 2, &accepted);
        if (!accepted)
            return;

        m_api.post(QStringLiteral("/api/me/recharge"), {{QStringLiteral("amount"), amount}},
                   [this](const QJsonObject &result) {
                       if (result.value(QStringLiteral("ok")).toBool())
                           loadMe();
                   });
    });

    connect(choose, &QPushButton::clicked, this, [this]() {
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("选择头像"), QString(),
                                         QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp)"));
        if (path.isEmpty())
            return;

        m_avatar->setText(path);
        const QPixmap image(path);
        if (!image.isNull()) {
            m_avatarPreview->setText(QString());
            m_avatarPreview->setPixmap(image.scaled(
                m_avatarPreview->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        }
    });

    connect(save, &QPushButton::clicked, this, [this]() {
        m_api.patch(QStringLiteral("/api/me"),
                    {{QStringLiteral("nickname"), m_nickname->text()},
                     {QStringLiteral("avatar"), m_avatar->text()}},
                    [this](const QJsonObject &result) {
                        if (result.value(QStringLiteral("ok")).toBool()) {
                            statusBar()->showMessage(QStringLiteral("资料已保存"), 2500);
                            loadMe();
                        }
                    });
    });

    connect(logoutButton, &QPushButton::clicked, this, &UserWindow::logout);
    return page;
}

void UserWindow::login() {
    m_api.post(QStringLiteral("/api/auth/phone"), {{QStringLiteral("phone"), m_phone->text()}},
               [this](const QJsonObject &result) {
                   if (!result.value(QStringLiteral("ok")).toBool())
                       return;

                   const auto data = result.value(QStringLiteral("data")).toObject();
                   m_api.setToken(data.value(QStringLiteral("token")).toString());
                   m_pages->setCurrentIndex(1);
                   loadMe();
                   loadStations();

                   if (qEnvironmentVariableIntValue("EVCHARGE_START_PROFILE") == 1) {
                       m_tabs->setCurrentIndex(2);
                       loadOrder(false);
                   } else if (qEnvironmentVariableIntValue("EVCHARGE_START_ORDER") == 1) {
                       m_orderPrompted = true;
                       m_tabs->setCurrentIndex(1);
                       loadOrder(false);
                   } else {
                       loadOrder();
                   }
               });
}

void UserWindow::logout() {
    const QString message =
        m_chargeTimer->isActive()
            ? QStringLiteral("当前充电订单仍会继续计费，重新登录后可继续查看。\n\n确定退出登录吗？")
            : QStringLiteral("确定退出当前账号吗？");
    const auto choice = QMessageBox::question(this, QStringLiteral("退出登录"), message,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    stopLiveCharge();
    m_api.setToken(QString());
    m_orderId = 0;
    m_orderPrompted = false;
    m_stationRows = QJsonArray();

    m_orderState->setText(QStringLiteral("暂无进行中订单"));
    m_orderInfo->setText(QStringLiteral("预约充电桩后，可在这里完成启动、结束和结算。"));
    updateOrderButtons(QString());
    m_stations->setRowCount(0);
    m_piles->setRowCount(0);
    m_nickname->clear();
    m_avatar->clear();
    m_balance->setText(QStringLiteral("余额 ¥ 0.00"));
    m_avatarPreview->setPixmap(QPixmap());
    m_avatarPreview->setText(QStringLiteral("默认头像"));

    m_tabs->setCurrentIndex(0);
    m_stationFlow->setCurrentIndex(0);
    m_pages->setCurrentIndex(0);
    m_phone->setFocus();
    m_phone->selectAll();
    statusBar()->showMessage(QStringLiteral("已安全退出登录"), 3000);
}
void UserWindow::locate() {
    auto fallback = [this]() {
        const QString region = m_region->currentText();
        if (region == QStringLiteral("南山区")) {
            m_lat = 22.54030;
            m_lon = 113.95410;
        } else if (region == QStringLiteral("宝安区")) {
            m_lat = 22.55454;
            m_lon = 113.88310;
        } else if (region == QStringLiteral("龙岗区")) {
            m_lat = 22.69510;
            m_lon = 114.22950;
        } else {
            m_lat = 22.54310;
            m_lon = 114.05787;
        }
        m_locationStatus->setText(QStringLiteral("当前位置：%1 · %2（模拟 GPS，%3, %4）")
                                      .arg(region, m_address->text())
                                      .arg(m_lat, 0, 'f', 5)
                                      .arg(m_lon, 0, 'f', 5));
        statusBar()->showMessage(
            QStringLiteral("已按 %1 模拟 GPS 定位；距离已重新计算").arg(region), 3000);
        loadStations();
    };
    const QString key = qEnvironmentVariable("TENCENT_MAP_KEY");
    if (key.isEmpty()) {
        fallback();
        return;
    }
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("address"), m_address->text());
    query.addQueryItem(QStringLiteral("key"), key);
    url.setQuery(query);
    auto *reply = m_geocoder->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, fallback]() {
        const auto document = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        const auto root = document.object();
        const auto location = root.value(QStringLiteral("result"))
                                  .toObject()
                                  .value(QStringLiteral("location"))
                                  .toObject();
        if (root.value(QStringLiteral("status")).toInt() == 0 && !location.isEmpty()) {
            m_lat = location.value(QStringLiteral("lat")).toDouble();
            m_lon = location.value(QStringLiteral("lng")).toDouble();
            m_locationStatus->setText(QStringLiteral("当前位置：%1（腾讯地址解析，%2, %3）")
                                          .arg(m_address->text())
                                          .arg(m_lat, 0, 'f', 5)
                                          .arg(m_lon, 0, 'f', 5));
            statusBar()->showMessage(QStringLiteral("腾讯地图地址解析成功；距离已重新计算"), 3000);
            loadStations();
        } else {
            statusBar()->showMessage(QStringLiteral("地址解析失败，已使用区域模拟定位"), 3000);
            fallback();
        }
    });
}
void UserWindow::loadStations() {
    m_api.get(
        QStringLiteral("/api/stations?lat=%1&lon=%2").arg(m_lat, 0, 'f', 6).arg(m_lon, 0, 'f', 6),
        [this](auto r) {
            if (!r.value(QStringLiteral("ok")).toBool())
                return;
            m_stationRows = r.value(QStringLiteral("data")).toArray();
            m_stations->setRowCount(m_stationRows.size());
            for (int i = 0; i < m_stationRows.size(); ++i) {
                const auto s = m_stationRows.at(i).toObject();
                const QStringList values{
                    s.value(QStringLiteral("name")).toString(),
                    QStringLiteral("¥%1").arg(s.value(QStringLiteral("price")).toDouble(), 0, 'f',
                                              2),
                    QStringLiteral("%1/%2")
                        .arg(s.value(QStringLiteral("free_count")).toInt())
                        .arg(s.value(QStringLiteral("pile_count")).toInt()),
                    distanceText(s.value(QStringLiteral("distance_km")).toDouble())};
                for (int c = 0; c < 4; ++c)
                    m_stations->setItem(i, c, new QTableWidgetItem(values.at(c)));
            }
            if (!m_stationRows.isEmpty()) {
                m_stations->selectRow(0);
                const auto first = m_stationRows.first().toObject();
                loadPiles(first.value(QStringLiteral("id")).toInt(),
                          first.value(QStringLiteral("name")).toString(),
                          first.value(QStringLiteral("latitude")).toDouble(),
                          first.value(QStringLiteral("longitude")).toDouble());
            }
        });
}
void UserWindow::loadPiles(int id, const QString &name, double lat, double lon) {
    m_targetLat = lat;
    m_targetLon = lon;
    m_targetName = name;
    m_stationTitle->setText(name);
    m_api.get(QStringLiteral("/api/stations/%1/piles").arg(id), [this](auto r) {
        const auto rows = r.value(QStringLiteral("data")).toArray();
        m_piles->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const auto p = rows.at(i).toObject();
            m_piles->setItem(i, 0,
                             new QTableWidgetItem(p.value(QStringLiteral("code")).toString()));
            m_piles->setItem(i, 1,
                             new QTableWidgetItem(p.value(QStringLiteral("type")).toString() ==
                                                          QStringLiteral("fast")
                                                      ? QStringLiteral("快充")
                                                      : QStringLiteral("慢充")));
            m_piles->setItem(i, 2,
                             new QTableWidgetItem(QStringLiteral("%1 kW").arg(
                                 p.value(QStringLiteral("power")).toDouble())));
            const QString state = p.value(QStringLiteral("status")).toString();
            if (state == QStringLiteral("idle")) {
                auto *b = new QPushButton(QStringLiteral("预约"));
                b->setMinimumSize(88, 32);
                m_piles->setCellWidget(i, 3, b);
                connect(b, &QPushButton::clicked, this, [this, p]() {
                    m_api.post(QStringLiteral("/api/orders/reserve"),
                               {{QStringLiteral("pile_id"), p.value(QStringLiteral("id")).toInt()}},
                               [this](auto result) {
                                   if (result.value(QStringLiteral("ok")).toBool()) {
                                       m_orderPrompted = true;
                                       m_tabs->setCurrentIndex(1);
                                       loadOrder(false);
                                       loadStations();
                                       statusBar()->showMessage(
                                           QStringLiteral("预约成功，请开始充电"), 3000);
                                   }
                               });
                });
            } else
                m_piles->setItem(i, 3, new QTableWidgetItem(Theme::statusText(state)));
        }
    });
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(name));
    m_map->load(QUrl(
        QStringLiteral(
            "https://apis.map.qq.com/uri/v1/marker?marker=coord:%1,%2;title:%3&referer=evcharge_demo")
            .arg(lat, 0, 'f', 6)
            .arg(lon, 0, 'f', 6)
            .arg(encoded)));
    if (qEnvironmentVariableIntValue("EVCHARGE_START_MAP") == 1 && !m_startMapConsumed) {
        m_startMapConsumed = true;
        QTimer::singleShot(350, this, [this]() { navigate(QStringLiteral("drive")); });
    }
}
void UserWindow::navigate(const QString &mode) {
    if (m_targetName.isEmpty()) {
        showError(QStringLiteral("请先选择充电站"));
        return;
    }
    const QString modeText =
        mode == QStringLiteral("walk") ? QStringLiteral("步行导航") : QStringLiteral("驾车导航");
    m_mapTitle->setText(QStringLiteral("%1 · %2").arg(m_targetName, modeText));
    const QString url =
        QStringLiteral(
            "https://apis.map.qq.com/uri/v1/routeplan?type=%1&from=当前位置&fromcoord=%2,%3&to=%4&tocoord=%5,%6&policy=0&referer=evcharge_demo")
            .arg(mode)
            .arg(m_lat, 0, 'f', 6)
            .arg(m_lon, 0, 'f', 6)
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(m_targetName)))
            .arg(m_targetLat, 0, 'f', 6)
            .arg(m_targetLon, 0, 'f', 6);
    m_map->load(QUrl(url));
    m_stationFlow->setCurrentIndex(1);
}
void UserWindow::loadMe() {
    m_api.get(QStringLiteral("/api/me"), [this](auto r) {
        const auto u = r.value(QStringLiteral("data")).toObject();
        m_nickname->setText(u.value(QStringLiteral("nickname")).toString());
        m_avatar->setText(u.value(QStringLiteral("avatar")).toString());
        const QPixmap image(m_avatar->text());
        if (!image.isNull()) {
            m_avatarPreview->setText(QString());
            m_avatarPreview->setPixmap(image.scaled(
                m_avatarPreview->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        } else {
            m_avatarPreview->setPixmap(QPixmap());
            m_avatarPreview->setText(QStringLiteral("默认头像"));
        }
        m_balance->setText(QStringLiteral("余额 ¥ %1")
                               .arg(u.value(QStringLiteral("balance")).toDouble(), 0, 'f', 2));
        m_welcome->setText(
            QStringLiteral("%1，附近充电站").arg(u.value(QStringLiteral("nickname")).toString()));
    });
}
void UserWindow::loadOrder(bool notifyExisting) {
    m_api.get(QStringLiteral("/api/orders/active"), [this,
                                                     notifyExisting](const QJsonObject &result) {
        if (!result.value(QStringLiteral("ok")).toBool() || m_api.token().isEmpty())
            return;

        const auto rows = result.value(QStringLiteral("data")).toArray();
        if (rows.isEmpty()) {
            m_orderId = 0;
            m_orderPrompted = false;
            m_orderState->setText(QStringLiteral("暂无进行中订单"));
            m_orderInfo->setText(QStringLiteral("预约充电桩后，可在这里完成启动、结束和结算。"));
            stopLiveCharge();
            updateOrderButtons(QString());
            return;
        }

        const auto order = rows.first().toObject();
        m_orderId = order.value(QStringLiteral("id")).toInt();
        const QString state = order.value(QStringLiteral("status")).toString();
        const QString stationName = order.value(QStringLiteral("station_name")).toString();
        const QString pileCode = order.value(QStringLiteral("pile_code")).toString();
        m_orderState->setText(Theme::statusText(state));

        if (state == QStringLiteral("charging")) {
            m_chargeStartedAt = QDateTime::fromString(
                order.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
            m_chargePowerKw = order.value(QStringLiteral("pile_power")).toDouble();
            m_chargePrice = order.value(QStringLiteral("price")).toDouble();
            m_orderInfo->setText(QStringLiteral("%1\n设备 %2 · %3 kW · ¥%4/kWh")
                                     .arg(stationName, pileCode)
                                     .arg(m_chargePowerKw, 0, 'f', 0)
                                     .arg(m_chargePrice, 0, 'f', 2));

            if (m_chargeStartedAt.isValid() && m_chargePowerKw > 0.0 && m_chargePrice > 0.0) {
                m_liveChargeCard->show();
                updateLiveCharge();
                if (!m_chargeTimer->isActive())
                    m_chargeTimer->start();
            } else {
                stopLiveCharge();
            }
        } else {
            stopLiveCharge();
            m_orderInfo->setText(
                QStringLiteral("%1\n设备 %2 · %3 分钟 · %4 kWh\n应付 ¥%5")
                    .arg(stationName, pileCode)
                    .arg(order.value(QStringLiteral("duration_minutes")).toInt())
                    .arg(order.value(QStringLiteral("energy_kwh")).toDouble(), 0, 'f', 1)
                    .arg(order.value(QStringLiteral("amount")).toDouble(), 0, 'f', 2));
        }

        updateOrderButtons(state);
        if (notifyExisting && !m_orderPrompted) {
            m_orderPrompted = true;
            m_tabs->setCurrentIndex(1);
            QMessageBox::information(
                this, QStringLiteral("未完成订单"),
                QStringLiteral("检测到此前未完成的充电订单，请继续当前流程或完成结算。"));
        }
    });
}

void UserWindow::updateOrderButtons(const QString &status) {
    m_start->setEnabled(status == QStringLiteral("reserved"));
    m_cancel->setEnabled(status == QStringLiteral("reserved"));
    m_stop->setEnabled(status == QStringLiteral("charging"));
    m_settle->setEnabled(status == QStringLiteral("pending_payment"));
}

void UserWindow::updateLiveCharge() {
    if (!m_chargeStartedAt.isValid())
        return;

    const qint64 elapsedSeconds =
        qMax<qint64>(0, m_chargeStartedAt.secsTo(QDateTime::currentDateTime()));
    const qint64 billingSeconds = qMax<qint64>(60, elapsedSeconds);
    const double energyKwh = qRound((billingSeconds / 3600.0) * m_chargePowerKw * 10.0) / 10.0;
    const double amount = qRound(energyKwh * m_chargePrice * 100.0) / 100.0;

    const qint64 hours = elapsedSeconds / 3600;
    const qint64 minutes = (elapsedSeconds % 3600) / 60;
    const qint64 seconds = elapsedSeconds % 60;
    const QString duration = QStringLiteral("%1:%2:%3")
                                 .arg(hours, 2, 10, QLatin1Char('0'))
                                 .arg(minutes, 2, 10, QLatin1Char('0'))
                                 .arg(seconds, 2, 10, QLatin1Char('0'));

    m_liveDuration->setText(duration);
    m_liveEnergy->setText(QStringLiteral("%1 kWh").arg(energyKwh, 0, 'f', 1));
    m_liveAmount->setText(QStringLiteral("¥ %1").arg(amount, 0, 'f', 2));
}

void UserWindow::stopLiveCharge() {
    if (m_chargeTimer)
        m_chargeTimer->stop();
    if (m_liveChargeCard)
        m_liveChargeCard->hide();

    m_chargeStartedAt = QDateTime();
    m_chargePowerKw = 0.0;
    m_chargePrice = 0.0;
}

void UserWindow::orderAction(const QString &action) {
    if (!m_orderId)
        return;

    m_api.post(QStringLiteral("/api/orders/%1/%2").arg(m_orderId).arg(action), {},
               [this](const QJsonObject &result) {
                   if (!result.value(QStringLiteral("ok")).toBool())
                       return;

                   loadOrder();
                   loadStations();
                   loadMe();
               });
}
void UserWindow::showError(const QString &message) {
    statusBar()->showMessage(message, 5000);
}

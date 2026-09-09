#include "theme.h"

QString Theme::styleSheet() {
    return QStringLiteral(R"(
*{font-family:"Noto Sans CJK SC","Microsoft YaHei",sans-serif;font-size:14px;color:#e8edf2}
QWidget{background:#111315} QMainWindow,QDialog{background:#111315}
QFrame#Card{background:#1b1e21;border:1px solid #2b3035;border-radius:14px}
QLabel#Title{font-size:25px;font-weight:700} QLabel#Subtitle{color:#98a2ad} QLabel#Metric{font-size:28px;font-weight:700;color:#7ce38b}
QPushButton{background:#2f6fed;border:0;border-radius:9px;padding:9px 16px;font-weight:600}
QPushButton:hover{background:#4780ee} QPushButton:disabled{background:#33383d;color:#777f87}
QPushButton#Secondary{background:#292d31} QPushButton#Danger{background:#9f3a45}
QLineEdit,QComboBox,QDoubleSpinBox,QSpinBox{background:#202428;border:1px solid #363c42;border-radius:8px;padding:8px;selection-background-color:#2f6fed}
QTabWidget::pane{border:0} QTabBar::tab{background:#1b1e21;padding:10px 18px;margin:3px;border-radius:8px;color:#aeb7c0} QTabBar::tab:selected{background:#2f6fed;color:white}
QTableWidget{background:#171a1d;alternate-background-color:#1c2024;border:1px solid #2c3137;border-radius:10px;gridline-color:#2b3035}
QHeaderView::section{background:#22262a;border:0;padding:8px;font-weight:600}
QScrollBar:vertical{background:transparent;width:9px} QScrollBar::handle:vertical{background:#3a4148;border-radius:4px;min-height:30px}
QMessageBox{background:#191c20}
)");
}

QString Theme::statusText(const QString &s) {
    if (s == QStringLiteral("idle"))
        return QStringLiteral("空闲");
    if (s == QStringLiteral("reserved"))
        return QStringLiteral("已预约");
    if (s == QStringLiteral("charging"))
        return QStringLiteral("充电中");
    if (s == QStringLiteral("pending_payment"))
        return QStringLiteral("待结算");
    if (s == QStringLiteral("settled"))
        return QStringLiteral("已结算");
    if (s == QStringLiteral("cancelled"))
        return QStringLiteral("已取消");
    if (s == QStringLiteral("fault"))
        return QStringLiteral("故障");
    if (s == QStringLiteral("offline"))
        return QStringLiteral("离线");
    if (s == QStringLiteral("normal"))
        return QStringLiteral("正常");
    if (s == QStringLiteral("frozen"))
        return QStringLiteral("冻结");
    return s;
}

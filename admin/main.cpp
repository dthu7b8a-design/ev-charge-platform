#include "adminwindow.h"
#include "theme.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EVChargeAdmin"));
    QApplication::setApplicationDisplayName(QStringLiteral("EV Charge Admin"));
    QApplication::setDesktopFileName(QStringLiteral("evcharge-admin"));
    app.setStyleSheet(Theme::styleSheet());

    AdminWindow window;
    window.show();
    return app.exec();
}

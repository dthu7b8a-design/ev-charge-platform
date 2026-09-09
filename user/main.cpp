#include "theme.h"
#include "userwindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EVChargeUser"));
    QApplication::setApplicationDisplayName(QStringLiteral("EV Charge User"));
    QApplication::setDesktopFileName(QStringLiteral("evcharge-user"));
    app.setStyleSheet(Theme::styleSheet());

    UserWindow window;
    window.show();
    return app.exec();
}

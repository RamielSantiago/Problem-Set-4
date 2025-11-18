#include <QApplication>
#include "clientwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ClientWindow w;
    w.resize(1366, 800);
    w.show();
    return app.exec();
}
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("edge-hmi"));
    QApplication::setOrganizationName(QStringLiteral("JiangChngY"));

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption configOption({QStringLiteral("c"), QStringLiteral("config")},
                                    QStringLiteral("配置文件路径"), QStringLiteral("path"),
                                    QStringLiteral("/opt/edge-gateway/config/mp157.ini"));
    QCommandLineOption windowedOption(QStringLiteral("windowed"), QStringLiteral("窗口模式运行"));
    parser.addOption(configOption);
    parser.addOption(windowedOption);
    parser.process(app);

    MainWindow window(parser.value(configOption));
    if (parser.isSet(windowedOption)) window.show(); else window.showFullScreen();
    return app.exec();
}


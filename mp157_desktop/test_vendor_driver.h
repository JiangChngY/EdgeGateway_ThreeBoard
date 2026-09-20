// Injected only into a disposable host-test copy, never a deployed desktop.
#include <QtTest>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QTimer>
static QQuickItem *edgeFind(QQuickItem *item, const char *property, const QString &value) {
    if (item->property(property).toString() == value) return item;
    for (auto child : item->childItems())
        if (auto match = edgeFind(child, property, value)) return match;
    return nullptr;
}
static void edgeVendorTest(QQmlApplicationEngine *engine) {
    QTimer::singleShot(800, engine, [engine] {
        auto window = engine->rootObjects().isEmpty() ? nullptr :
            qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        if (!window) qFatal("VENDOR TEST: no window");
        const QString output = qEnvironmentVariable("EDGE_VENDOR_CAPTURE");
        auto capture = [&](const QString &name) {
            if (!window->grabWindow().save(output + "/" + name + ".png")) qFatal("VENDOR TEST: capture failed");
        };
        auto click = [&](QQuickItem *item) {
            if (!item) qFatal("VENDOR TEST: missing control");
            const auto point = item->mapToScene(QPointF(item->width()/2, item->height()/2));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
            QTest::qWait(700);
        };
        auto icon = edgeFind(window->contentItem(), "text", QStringLiteral("边缘采集"));
        if (!icon) qFatal("VENDOR TEST: gateway icon absent");
        capture("vendor-desktop");
        click(icon);
        auto dashboard = edgeFind(window->contentItem(), "objectName", "edge-dashboard");
        if (!dashboard || !dashboard->isVisible()) qFatal("VENDOR TEST: activity did not open");
        capture("vendor-gateway");
        click(edgeFind(dashboard, "text", QStringLiteral("‹ 桌面")));
        if (dashboard->isVisible()) qFatal("VENDOR TEST: home did not close activity");
        capture("vendor-home-return");
        click(icon);
        if (!dashboard->isVisible()) qFatal("VENDOR TEST: activity did not reopen");
        qInfo("PASS: vendor desktop icon, activity open, home return, reopen");
        QCoreApplication::exit(0);
    });
}

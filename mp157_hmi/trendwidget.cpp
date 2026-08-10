#include "trendwidget.h"

#include <QPainter>
#include <QPainterPath>

TrendWidget::TrendWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(190);
}

void TrendWidget::append(double temperature, double humidity)
{
    m_temperature.append(temperature);
    m_humidity.append(humidity);
    while (m_temperature.size() > m_capacity) m_temperature.remove(0);
    while (m_humidity.size() > m_capacity) m_humidity.remove(0);
    update();
}

QSize TrendWidget::minimumSizeHint() const
{
    return QSize(500, 190);
}

void TrendWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#0f172a"));
    const QRectF area = rect().adjusted(48, 16, -16, -34);

    painter.setPen(QPen(QColor("#334155"), 1));
    for (int i = 0; i <= 4; ++i) {
        const qreal y = area.top() + area.height() * i / 4.0;
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    auto drawSeries = [&](const QVector<double> &values, const QColor &color,
                          double minValue, double maxValue) {
        if (values.size() < 2) return;
        QPainterPath path;
        for (int i = 0; i < values.size(); ++i) {
            const qreal x = area.left() + area.width() * i / qMax(1, values.size() - 1);
            const double ratio = qBound(0.0, (values[i] - minValue) / (maxValue - minValue), 1.0);
            const qreal y = area.bottom() - area.height() * ratio;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        painter.setPen(QPen(color, 3));
        painter.drawPath(path);
    };

    drawSeries(m_temperature, QColor("#f97316"), 0.0, 50.0);
    drawSeries(m_humidity, QColor("#22d3ee"), 0.0, 100.0);

    painter.setPen(Qt::white);
    painter.drawText(12, 28, QStringLiteral("100"));
    painter.drawText(20, int(area.bottom()), QStringLiteral("0"));
    painter.setPen(QColor("#f97316"));
    painter.drawText(int(area.left()), height() - 10, QStringLiteral("温度"));
    painter.setPen(QColor("#22d3ee"));
    painter.drawText(int(area.left()) + 60, height() - 10, QStringLiteral("湿度"));
}

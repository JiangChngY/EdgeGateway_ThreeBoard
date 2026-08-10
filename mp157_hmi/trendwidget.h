#ifndef TREND_WIDGET_H
#define TREND_WIDGET_H

#include <QVector>
#include <QWidget>

class TrendWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TrendWidget(QWidget *parent = nullptr);
    void append(double temperature, double humidity);
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_temperature;
    QVector<double> m_humidity;
    int m_capacity = 120;
};

#endif


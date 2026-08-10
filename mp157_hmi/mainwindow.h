#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "datastore.h"
#include "edgeuploader.h"
#include "serialdevice.h"
#include "trendwidget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &configPath, QWidget *parent = nullptr);

private slots:
    void scanPorts();
    void toggleSerial();
    void onSample(const SensorSample &sample);
    void onSerialStatus(bool connected, const QString &detail);
    void onUplinkStatus(bool connected, const QString &detail, int pending);
    void onCommandAck(quint8 commandId, quint8 status);
    void applyThreshold();
    void resetAlarm();
    void refreshHistory();
    void checkDeviceTimeout();

private:
    QWidget *makeValueCard(const QString &title, QLabel **valueLabel, const QString &unit);
    void buildUi();
    void setDeviceOnline(bool online, const QString &detail);

    QString m_configPath;
    DataStore m_store;
    SerialDevice m_serial;
    EdgeUploader m_uploader;
    QDateTime m_lastSampleAt;
    QTimer m_deviceTimer;
    QTimer m_thresholdAckTimer;
    double m_threshold = 30.0;
    double m_pendingThreshold = 30.0;
    bool m_thresholdPending = false;
    bool m_alarmActive = false;
    bool m_demoMode = false;

    QLabel *m_clockLabel = nullptr;
    QLabel *m_deviceStatus = nullptr;
    QLabel *m_uplinkStatus = nullptr;
    QLabel *m_temperatureValue = nullptr;
    QLabel *m_humidityValue = nullptr;
    QLabel *m_lightValue = nullptr;
    QLabel *m_analogValue = nullptr;
    QLabel *m_alarmLabel = nullptr;
    TrendWidget *m_trend = nullptr;
    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QPushButton *m_serialButton = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
    QTableWidget *m_historyTable = nullptr;
};

#endif

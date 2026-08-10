#include "mainwindow.h"

#include <QDateTime>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtMath>

MainWindow::MainWindow(const QString &configPath, QWidget *parent)
    : QMainWindow(parent), m_configPath(configPath), m_uploader(&m_store, this)
{
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    const QString database = settings.value(QStringLiteral("storage/database"),
                                             QStringLiteral("/opt/edge-gateway/data/mp157.db")).toString();
    QString error;
    if (!m_store.open(database, &error)) {
        QMessageBox::critical(this, QStringLiteral("数据库错误"), error);
    }
    m_threshold = settings.value(QStringLiteral("alarm/temperature_threshold"), 30.0).toDouble();
    m_uploader.configure(settings.value(QStringLiteral("uplink/host"), QStringLiteral("192.168.10.2")).toString(),
                         quint16(settings.value(QStringLiteral("uplink/port"), 9000).toUInt()),
                         settings.value(QStringLiteral("device/gateway_id"), QStringLiteral("mp157-01")).toString());

    buildUi();
    scanPorts();
    m_thresholdSpin->setValue(m_threshold);

    const QString preferredPort = settings.value(QStringLiteral("serial/port"), QStringLiteral("/dev/ttyUSB0")).toString();
    const int index = m_portCombo->findText(preferredPort);
    if (index >= 0) {
        m_portCombo->setCurrentIndex(index);
    } else {
        m_portCombo->addItem(preferredPort);
        m_portCombo->setCurrentText(preferredPort);
    }
    m_baudCombo->setCurrentText(settings.value(QStringLiteral("serial/baud"), 115200).toString());

    connect(&m_serial, &SerialDevice::sampleReceived, this, &MainWindow::onSample);
    connect(&m_serial, &SerialDevice::connectionChanged, this, &MainWindow::onSerialStatus);
    connect(&m_serial, &SerialDevice::protocolError, this, [this](const QString &text) {
        statusBar()->showMessage(text, 3000);
    });
    connect(&m_serial, &SerialDevice::commandAcknowledged,
            this, &MainWindow::onCommandAck);
    connect(&m_uploader, &EdgeUploader::statusChanged, this, &MainWindow::onUplinkStatus);
    connect(&m_deviceTimer, &QTimer::timeout, this, &MainWindow::checkDeviceTimeout);
    m_deviceTimer.start(1000);
    m_thresholdAckTimer.setSingleShot(true);
    m_thresholdAckTimer.setInterval(2500);
    connect(&m_thresholdAckTimer, &QTimer::timeout, this, [this] {
        if (!m_thresholdPending) return;
        m_thresholdPending = false;
        m_thresholdSpin->setEnabled(true);
        m_thresholdSpin->setValue(m_threshold);
        statusBar()->showMessage(QStringLiteral("阈值ACK超时：未保存新值"), 5000);
    });
    m_uploader.start();

    m_demoMode = settings.value(QStringLiteral("demo/enabled"), false).toBool();
    if (m_demoMode) {
        QTimer *demoTimer = new QTimer(this);
        connect(demoTimer, &QTimer::timeout, this, [this] {
            static quint16 sequence = 0;
            SensorSample sample;
            sample.sourceSequence = ++sequence;
            sample.receivedAt = QDateTime::currentDateTime();
            sample.temperature = 25.0 + qSin(sequence / 8.0) * 4.0;
            sample.humidity = 58.0 + qCos(sequence / 10.0) * 8.0;
            sample.light = quint16(500 + (sequence * 23u) % 2500u);
            sample.analog = quint16(900 + (sequence * 37u) % 2800u);
            sample.validFlags = 0x07;
            sample.alarm = sample.temperature >= m_threshold;
            onSample(sample);
        });
        demoTimer->start(1000);
        statusBar()->showMessage(QStringLiteral("演示模式：使用本机模拟数据"));
    } else if (settings.value(QStringLiteral("serial/auto_open"), true).toBool()) {
        m_serial.open(m_portCombo->currentText(), m_baudCombo->currentText().toInt());
    }

    QTimer *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this] {
        m_clockLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    });
    clockTimer->start(1000);
}

QWidget *MainWindow::makeValueCard(const QString &title, QLabel **valueLabel, const QString &unit)
{
    QFrame *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    QVBoxLayout *layout = new QVBoxLayout(card);
    QLabel *titleLabel = new QLabel(title);
    *valueLabel = new QLabel(QStringLiteral("--"));
    (*valueLabel)->setObjectName(QStringLiteral("value"));
    QLabel *unitLabel = new QLabel(unit);
    unitLabel->setObjectName(QStringLiteral("unit"));
    layout->addWidget(titleLabel);
    layout->addWidget(*valueLabel, 1, Qt::AlignCenter);
    layout->addWidget(unitLabel, 0, Qt::AlignRight);
    return card;
}

void MainWindow::buildUi()
{
    resize(1024, 600);
    setWindowTitle(QStringLiteral("三板分布式边缘数据采集网关"));

    QWidget *central = new QWidget;
    QVBoxLayout *root = new QVBoxLayout(central);
    QHBoxLayout *header = new QHBoxLayout;
    QLabel *title = new QLabel(QStringLiteral("EDGE GATEWAY  边缘数据采集网关"));
    title->setObjectName(QStringLiteral("appTitle"));
    m_clockLabel = new QLabel;
    m_deviceStatus = new QLabel(QStringLiteral("F103：离线"));
    m_uplinkStatus = new QLabel(QStringLiteral("i.MX6ULL：连接中"));
    header->addWidget(title);
    header->addStretch();
    header->addWidget(m_deviceStatus);
    header->addWidget(m_uplinkStatus);
    header->addWidget(m_clockLabel);
    root->addLayout(header);

    QTabWidget *tabs = new QTabWidget;
    QWidget *monitor = new QWidget;
    QVBoxLayout *monitorLayout = new QVBoxLayout(monitor);
    QGridLayout *cards = new QGridLayout;
    cards->addWidget(makeValueCard(QStringLiteral("温度"), &m_temperatureValue, QStringLiteral("℃")), 0, 0);
    cards->addWidget(makeValueCard(QStringLiteral("湿度"), &m_humidityValue, QStringLiteral("%RH")), 0, 1);
    cards->addWidget(makeValueCard(QStringLiteral("光照ADC"), &m_lightValue, QStringLiteral("0~4095")), 0, 2);
    cards->addWidget(makeValueCard(QStringLiteral("模拟量ADC"), &m_analogValue, QStringLiteral("0~4095")), 0, 3);
    monitorLayout->addLayout(cards);
    m_alarmLabel = new QLabel(QStringLiteral("系统正常"));
    m_alarmLabel->setObjectName(QStringLiteral("alarmNormal"));
    monitorLayout->addWidget(m_alarmLabel);
    m_trend = new TrendWidget;
    monitorLayout->addWidget(m_trend, 1);

    QWidget *history = new QWidget;
    QVBoxLayout *historyLayout = new QVBoxLayout(history);
    QPushButton *refreshButton = new QPushButton(QStringLiteral("刷新最近100条"));
    m_historyTable = new QTableWidget(0, 6);
    m_historyTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("温度"),
                                               QStringLiteral("湿度"), QStringLiteral("光照"),
                                               QStringLiteral("模拟量"), QStringLiteral("报警")});
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    historyLayout->addWidget(refreshButton);
    historyLayout->addWidget(m_historyTable);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshHistory);

    QWidget *settingsPage = new QWidget;
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsPage);
    QGroupBox *serialGroup = new QGroupBox(QStringLiteral("F103串口"));
    QHBoxLayout *serialLayout = new QHBoxLayout(serialGroup);
    m_portCombo = new QComboBox;
    m_portCombo->setEditable(true);
    m_baudCombo = new QComboBox;
    m_baudCombo->addItems({QStringLiteral("9600"), QStringLiteral("115200"), QStringLiteral("230400")});
    m_serialButton = new QPushButton(QStringLiteral("打开串口"));
    QPushButton *scanButton = new QPushButton(QStringLiteral("扫描"));
    serialLayout->addWidget(m_portCombo, 2);
    serialLayout->addWidget(m_baudCombo);
    serialLayout->addWidget(scanButton);
    serialLayout->addWidget(m_serialButton);
    connect(scanButton, &QPushButton::clicked, this, &MainWindow::scanPorts);
    connect(m_serialButton, &QPushButton::clicked, this, &MainWindow::toggleSerial);

    QGroupBox *controlGroup = new QGroupBox(QStringLiteral("阈值与控制"));
    QGridLayout *controlLayout = new QGridLayout(controlGroup);
    m_thresholdSpin = new QDoubleSpinBox;
    m_thresholdSpin->setRange(-20.0, 80.0);
    m_thresholdSpin->setSuffix(QStringLiteral(" ℃"));
    QPushButton *applyButton = new QPushButton(QStringLiteral("下发温度阈值"));
    QPushButton *resetButton = new QPushButton(QStringLiteral("复位报警"));
    QPushButton *ledOn = new QPushButton(QStringLiteral("LED开"));
    QPushButton *ledOff = new QPushButton(QStringLiteral("LED关"));
    controlLayout->addWidget(m_thresholdSpin, 0, 0);
    controlLayout->addWidget(applyButton, 0, 1);
    controlLayout->addWidget(resetButton, 0, 2);
    controlLayout->addWidget(ledOn, 1, 0);
    controlLayout->addWidget(ledOff, 1, 1);
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyThreshold);
    connect(resetButton, &QPushButton::clicked, this, &MainWindow::resetAlarm);
    connect(ledOn, &QPushButton::clicked, this, [this] { m_serial.setLed(true); });
    connect(ledOff, &QPushButton::clicked, this, [this] { m_serial.setLed(false); });
    settingsLayout->addWidget(serialGroup);
    settingsLayout->addWidget(controlGroup);
    settingsLayout->addStretch();

    tabs->addTab(monitor, QStringLiteral("实时监控"));
    tabs->addTab(history, QStringLiteral("历史数据"));
    tabs->addTab(settingsPage, QStringLiteral("设备设置"));
    root->addWidget(tabs, 1);
    setCentralWidget(central);

    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget{background:#e2e8f0;color:#0f172a;font-size:16px;}"
        "#appTitle{font-size:24px;font-weight:700;color:#0b4f6c;}"
        "#card{background:white;border:1px solid #cbd5e1;border-radius:10px;}"
        "#value{font-size:34px;font-weight:700;color:#0f766e;}"
        "#unit{color:#64748b;}"
        "#alarmNormal{background:#dcfce7;color:#166534;padding:10px;border-radius:6px;font-weight:700;}"
        "#alarmActive{background:#fee2e2;color:#991b1b;padding:10px;border-radius:6px;font-weight:700;}"
        "#alarmInvalid{background:#fef3c7;color:#92400e;padding:10px;border-radius:6px;font-weight:700;}"
        "QPushButton{background:#0b4f6c;color:white;border:0;border-radius:6px;padding:10px;}"
        "QPushButton:pressed{background:#083b50;}"
        "QTabBar::tab{padding:10px 24px;background:#cbd5e1;}"
        "QTabBar::tab:selected{background:#0b4f6c;color:white;}"));
}

void MainWindow::scanPorts()
{
    const QString current = m_portCombo ? m_portCombo->currentText() : QString();
    if (!m_portCombo) return;
    m_portCombo->clear();
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
        m_portCombo->addItem(port.systemLocation());
    }
    if (!current.isEmpty() && m_portCombo->findText(current) < 0) m_portCombo->addItem(current);
    const int index = m_portCombo->findText(current);
    if (index >= 0) m_portCombo->setCurrentIndex(index);
}

void MainWindow::toggleSerial()
{
    if (m_serial.isOpen()) {
        m_serial.close();
    } else {
        m_serial.open(m_portCombo->currentText(), m_baudCombo->currentText().toInt());
    }
}

void MainWindow::onSample(const SensorSample &sample)
{
    const bool environmentValid =
        (sample.validFlags & EG_SENSOR_VALID_TEMPERATURE_HUMIDITY) != 0;
    const bool lightValid = (sample.validFlags & EG_SENSOR_VALID_LIGHT) != 0;
    const bool analogValid = (sample.validFlags & EG_SENSOR_VALID_ANALOG) != 0;
    const bool synthetic = (sample.validFlags & EG_SENSOR_DATA_SYNTHETIC) != 0;

    m_lastSampleAt = sample.receivedAt;
    setDeviceOnline(true, synthetic ? QStringLiteral("合成演示数据")
                                    : QStringLiteral("数据正常"));
    m_temperatureValue->setText(environmentValid
                                    ? QString::number(sample.temperature, 'f', 1)
                                    : QStringLiteral("--"));
    m_humidityValue->setText(environmentValid
                                 ? QString::number(sample.humidity, 'f', 1)
                                 : QStringLiteral("--"));
    m_lightValue->setText(lightValid ? QString::number(sample.light)
                                     : QStringLiteral("--"));
    m_analogValue->setText(analogValid ? QString::number(sample.analog)
                                       : QStringLiteral("--"));
    if (environmentValid) m_trend->append(sample.temperature, sample.humidity);

    QString error;
    if (!m_store.insertSample(sample, &error)) statusBar()->showMessage(error, 5000);
    m_uploader.submitSample(sample);

    if (!environmentValid) {
        if (m_alarmLabel->objectName() != QStringLiteral("alarmInvalid")) {
            m_alarmActive = false;
            m_alarmLabel->setObjectName(QStringLiteral("alarmInvalid"));
            m_alarmLabel->setText(QStringLiteral("温湿度数据无效：已禁止阈值判断"));
            m_alarmLabel->style()->unpolish(m_alarmLabel);
            m_alarmLabel->style()->polish(m_alarmLabel);
            m_store.insertAlarm(QStringLiteral("warning"), m_alarmLabel->text());
        }
        return;
    }

    /* F103 is the authoritative alarm state. The local threshold is only
     * committed after the corresponding command ACK arrives. */
    const bool alarm = sample.alarm;
    if (alarm != m_alarmActive ||
        m_alarmLabel->objectName() == QStringLiteral("alarmInvalid")) {
        m_alarmActive = alarm;
        m_alarmLabel->setObjectName(alarm ? QStringLiteral("alarmActive") : QStringLiteral("alarmNormal"));
        m_alarmLabel->setText(alarm ? QStringLiteral("温度报警：请检查现场") : QStringLiteral("系统正常"));
        m_alarmLabel->style()->unpolish(m_alarmLabel);
        m_alarmLabel->style()->polish(m_alarmLabel);
        m_store.insertAlarm(alarm ? QStringLiteral("warning") : QStringLiteral("info"), m_alarmLabel->text());
    }
}

void MainWindow::onSerialStatus(bool connected, const QString &detail)
{
    m_serialButton->setText(connected ? QStringLiteral("关闭串口") : QStringLiteral("打开串口"));
    m_portCombo->setEnabled(!connected);
    m_baudCombo->setEnabled(!connected);
    if (!connected && m_thresholdPending) {
        m_thresholdPending = false;
        m_thresholdAckTimer.stop();
        m_thresholdSpin->setEnabled(true);
        m_thresholdSpin->setValue(m_threshold);
        statusBar()->showMessage(QStringLiteral("连接中断：阈值未获ACK，已恢复原值"), 5000);
    }
    setDeviceOnline(connected && !m_lastSampleAt.isNull(), detail);
}

void MainWindow::onUplinkStatus(bool connected, const QString &detail, int pending)
{
    m_uplinkStatus->setText(QStringLiteral("i.MX6ULL：%1｜缓存%2").arg(detail).arg(pending));
    m_uplinkStatus->setStyleSheet(connected ? QStringLiteral("color:#166534") : QStringLiteral("color:#b45309"));
}

void MainWindow::applyThreshold()
{
    if (m_demoMode) {
        m_threshold = m_thresholdSpin->value();
        QSettings settings(m_configPath, QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        settings.setValue(QStringLiteral("alarm/temperature_threshold"), m_threshold);
        settings.sync();
        statusBar()->showMessage(QStringLiteral("演示模式阈值已更新"), 3000);
        return;
    }
    if (m_thresholdPending) {
        statusBar()->showMessage(QStringLiteral("上一条阈值命令仍在等待ACK"), 3000);
        return;
    }
    m_pendingThreshold = m_thresholdSpin->value();
    if (m_serial.setTemperatureThreshold(m_pendingThreshold)) {
        m_thresholdPending = true;
        m_thresholdSpin->setEnabled(false);
        m_thresholdAckTimer.start();
        statusBar()->showMessage(QStringLiteral("阈值已发送，等待F103确认"), 3000);
    }
}

void MainWindow::resetAlarm()
{
    if (m_demoMode) {
        m_alarmActive = false;
        m_alarmLabel->setObjectName(QStringLiteral("alarmNormal"));
        m_alarmLabel->setText(QStringLiteral("演示报警已复位"));
        m_alarmLabel->style()->unpolish(m_alarmLabel);
        m_alarmLabel->style()->polish(m_alarmLabel);
        return;
    }
    if (m_serial.resetAlarm()) {
        statusBar()->showMessage(QStringLiteral("复位命令已发送，等待F103确认"), 3000);
    }
}

void MainWindow::onCommandAck(quint8 commandId, quint8 status)
{
    if (commandId == EG_CMD_SET_TEMP_THRESHOLD) {
        m_thresholdAckTimer.stop();
        m_thresholdSpin->setEnabled(true);
        if (!m_thresholdPending) {
            statusBar()->showMessage(QStringLiteral("收到已过期的阈值ACK"), 3000);
            return;
        }
        m_thresholdPending = false;
        if (status == 0) {
            m_threshold = m_pendingThreshold;
            QSettings settings(m_configPath, QSettings::IniFormat);
            settings.setIniCodec("UTF-8");
            settings.setValue(QStringLiteral("alarm/temperature_threshold"), m_threshold);
            settings.sync();
            statusBar()->showMessage(QStringLiteral("F103已确认新阈值：%1 ℃")
                                         .arg(m_threshold, 0, 'f', 1), 5000);
        } else {
            m_thresholdSpin->setValue(m_threshold);
            statusBar()->showMessage(QStringLiteral("F103拒绝阈值命令，状态码：%1")
                                         .arg(int(status)), 5000);
        }
        return;
    }

    if (commandId == EG_CMD_ALARM_RESET) {
        if (status == 0) {
            m_alarmActive = false;
            m_alarmLabel->setObjectName(QStringLiteral("alarmNormal"));
            m_alarmLabel->setText(QStringLiteral("报警已消音，温度回落后自动重新布防"));
            m_alarmLabel->style()->unpolish(m_alarmLabel);
            m_alarmLabel->style()->polish(m_alarmLabel);
            statusBar()->showMessage(QStringLiteral("F103已确认报警复位"), 5000);
        } else {
            statusBar()->showMessage(QStringLiteral("F103拒绝报警复位，状态码：%1")
                                         .arg(int(status)), 5000);
        }
        return;
    }

    statusBar()->showMessage(QStringLiteral("F103命令ACK：命令%1，状态%2")
                                 .arg(int(commandId)).arg(int(status)), 3000);
}

void MainWindow::refreshHistory()
{
    const QList<SensorSample> samples = m_store.recentSamples(100);
    m_historyTable->setRowCount(samples.size());
    for (int row = 0; row < samples.size(); ++row) {
        const SensorSample &sample = samples[row];
        const bool environmentValid =
            (sample.validFlags & EG_SENSOR_VALID_TEMPERATURE_HUMIDITY) != 0;
        const bool lightValid = (sample.validFlags & EG_SENSOR_VALID_LIGHT) != 0;
        const bool analogValid = (sample.validFlags & EG_SENSOR_VALID_ANALOG) != 0;
        const QStringList cells = {
            sample.receivedAt.toString(QStringLiteral("MM-dd hh:mm:ss")),
            environmentValid ? QString::number(sample.temperature, 'f', 1) : QStringLiteral("--"),
            environmentValid ? QString::number(sample.humidity, 'f', 1) : QStringLiteral("--"),
            lightValid ? QString::number(sample.light) : QStringLiteral("--"),
            analogValid ? QString::number(sample.analog) : QStringLiteral("--"),
            sample.alarm ? QStringLiteral("是") : QStringLiteral("否")
        };
        for (int column = 0; column < cells.size(); ++column) {
            m_historyTable->setItem(row, column, new QTableWidgetItem(cells[column]));
        }
    }
}

void MainWindow::checkDeviceTimeout()
{
    if (!m_lastSampleAt.isNull() && m_lastSampleAt.msecsTo(QDateTime::currentDateTime()) > 3500) {
        setDeviceOnline(false, QStringLiteral("超过3.5秒未收到数据"));
    }
}

void MainWindow::setDeviceOnline(bool online, const QString &detail)
{
    m_deviceStatus->setText(QStringLiteral("F103：%1｜%2").arg(online ? QStringLiteral("在线") : QStringLiteral("离线"), detail));
    m_deviceStatus->setStyleSheet(online ? QStringLiteral("color:#166534") : QStringLiteral("color:#b91c1c"));
}

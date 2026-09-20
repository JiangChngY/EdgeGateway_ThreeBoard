import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Rectangle {
    id: panel
    objectName: "edge-dashboard"
    color: "#edf2f8"
    property int page: 0
    property string selectedPort: gateway.port
    function updatePortSelection() {
        if (gateway.ports.indexOf(selectedPort) < 0) selectedPort = gateway.port
        serialChoice.currentIndex = Math.max(0, gateway.ports.indexOf(selectedPort))
    }
    Connections { target: gateway; onPortsChanged: panel.updatePortSelection() }
    signal returnHome()
    function val(key, decimals) {
        var v = gateway.sample[key]
        return gateway.online && v !== undefined && v !== null ? Number(v).toFixed(decimals || 0) : "--"
    }
    Component.onCompleted: { updatePortSelection(); gateway.start() }
    Popup {
        id: keypad
        objectName: "uplink-keypad-popup"
        property var editor: uplinkHost
        x: (panel.width - width) / 2; y: (panel.height - height) / 2
        width: 400; height: 360; modal: true; focus: true
        ColumnLayout {
            anchors.fill: parent
            RowLayout {
                Button { text: "主机 IP"; onClicked: keypad.editor = uplinkHost }
                Button { text: "端口"; onClicked: keypad.editor = uplinkPort }
                Button { objectName: "keypad-clear"; text: "清空"; onClicked: keypad.editor.text = "" }
            }
            Text { text: (keypad.editor === uplinkHost ? "IP：" : "端口：") + keypad.editor.text; font.pixelSize: 22; color: "#12283f" }
            GridLayout {
                columns: 3; Layout.fillWidth: true; Layout.fillHeight: true
                Repeater {
                    model: ["1","2","3","4","5","6","7","8","9",".","0","←"]
                    Button {
                        objectName: "keypad-key-" + modelData
                        text: modelData; Layout.fillWidth: true; Layout.fillHeight: true
                        onClicked: {
                            if (modelData === "←") keypad.editor.text = keypad.editor.text.slice(0, -1)
                            else if (modelData !== "." || keypad.editor === uplinkHost) keypad.editor.text += modelData
                        }
                    }
                }
            }
            Button { text: "完成"; Layout.fillWidth: true; onClicked: keypad.close() }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        anchors.bottomMargin: 42
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Button { text: "‹ 桌面"; implicitHeight: 48; onClicked: panel.returnHome() }
            ColumnLayout {
                Text { text: "边缘采集"; font.pixelSize: 27; font.bold: true; color: "#12283f" }
                Text { text: "DHT11 · NTC · 超声波"; color: "#62748b"; font.pixelSize: 14 }
            }
            Item { Layout.fillWidth: true }
            Rectangle { width: 10; height: 10; radius: 5; color: gateway.online ? "#12a885" : "#94a3b8" }
            Text { text: gateway.online ? (gateway.synthetic ? "合成演示数据" : "设备在线") : "等待设备"; color: "#314760"; font.pixelSize: 17 }
        }
        RowLayout {
            Repeater {
                model: ["实时采集", "历史记录", "连接设置", "汇聚上传"]
                Button {
                    objectName: "page-button-" + index
                    text: modelData; checkable: true; checked: panel.page === index
                    implicitHeight: 48; Layout.fillWidth: true
                    onClicked: { panel.page = index; if (index === 1) gateway.refreshHistory(); if (index === 2) { gateway.scanPorts(); gateway.refreshNetwork() } }
                }
            }
        }
        StackLayout {
            currentIndex: panel.page; Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                GridLayout {
                    columns: 3; rowSpacing: 14; columnSpacing: 14
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Repeater {
                        model: [
                            {title:"空气温度", key:"temperature", unit:"℃", decimals:1},
                            {title:"相对湿度", key:"humidity", unit:"%RH", decimals:1},
                            {title:"超声波距离", key:"distance", unit:"mm", decimals:0},
                            {title:"NTC 模拟量", key:"ntc", unit:"ADC / 4095", decimals:0},
                            {title:"NTC 电压", key:"millivolts", unit:"mV", decimals:0},
                            {title:"NTC 阈值输出", key:"digital", unit:"DO 电平", decimals:0}
                        ]
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Layout.minimumHeight: 110; radius: 14; color: "white"
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 16
                                Text { text: modelData.title; color: "#5b7087"; font.pixelSize: 17 }
                                Text { objectName: "value-" + modelData.key; text: panel.val(modelData.key, modelData.decimals); font.pixelSize: 35; font.bold: true; color: "#126d88" }
                                Text { text: modelData.unit; color: "#7f91a4"; font.pixelSize: 14 }
                            }
                        }
                    }
                }
                Text { text: "NTC 显示原始值；DO 为模块电平，温度换算需先标定。"; color: "#62748b"; font.pixelSize: 14 }
            }
            ColumnLayout {
                RowLayout {
                    Text { text: "最近 100 条 · MP157 采集记录"; font.pixelSize: 18; color: "#12283f" }
                    Item { Layout.fillWidth: true }
                    Button { text: "刷新"; onClicked: gateway.refreshHistory() }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
                    model: gateway.history
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        width: ListView.view.width; height: 64; radius: 8; color: "white"
                        Column {
                            anchors.centerIn: parent; width: parent.width - 24; spacing: 5
                            Text { text: modelData.time + "  /  " + modelData.board + "  #" + modelData.sequence + (modelData.synthetic ? "  合成演示" : ""); color: "#526981"; font.pixelSize: 14 }
                            Text {
                                function field(k) { return modelData[k] === undefined || modelData[k] === null ? "--" : modelData[k] }
                                text: field("temperature") + " ℃     " + field("humidity") + " %RH     NTC " + field("ntc") + "     距离 " + field("distance") + " mm"
                                color: "#12283f"; font.pixelSize: 17
                            }
                        }
                    }
                    Text { anchors.centerIn: parent; visible: gateway.history.length === 0; text: "暂无记录，连接 F103 后自动保存"; color: "#62748b" }
                }
            }
            ColumnLayout {
                Text { text: "F103 USART2 → USB 转串口 → MP157 USB HOST"; font.pixelSize: 20; color: "#12283f" }
                RowLayout {
                    ComboBox {
                        id: serialChoice; objectName: "serial-choice"; model: gateway.ports; Layout.fillWidth: true; implicitHeight: 48
                        onActivated: panel.selectedPort = currentText
                    }
                    Button { text: "扫描"; implicitHeight: 48; onClicked: gateway.scanPorts() }
                    Button { text: gateway.connected ? "暂停采集" : "连接 115200"; implicitHeight: 48; onClicked: gateway.connected ? gateway.disconnectPort() : gateway.connectPort(serialChoice.currentText) }
                }
                Text { text: "串口：" + gateway.port + "    协议错误累计：" + gateway.errors; font.pixelSize: 16; color: "#526981" }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#d7e0ea" }
                Text { text: "本机网络"; font.pixelSize: 21; color: "#12283f" }
                Text { text: gateway.network; font.pixelSize: 18; color: "#126d88" }
                Button { text: "刷新网络状态"; onClicked: gateway.refreshNetwork() }
                Item { Layout.fillHeight: true }
                Text { text: "回到桌面后继续采集；点击“暂停采集”才释放串口。"; font.pixelSize: 16; color: "#62748b" }
            }
            ColumnLayout {
                spacing: 12
                Text { text: "i.MX6ULL 汇聚服务"; font.pixelSize: 23; color: "#12283f" }
                Text { text: "采集数据先保存；网络恢复后自动补传，收到确认后移出队列。"; font.pixelSize: 16; color: "#526981" }
                RowLayout {
                    TextField { id: uplinkHost; objectName: "uplink-host"; Component.onCompleted: text = gateway.uplinkHost; placeholderText: "汇聚主机 IP"; Layout.fillWidth: true; implicitHeight: 48 }
                    TextField { id: uplinkPort; objectName: "uplink-port"; Component.onCompleted: text = gateway.uplinkPort; placeholderText: "9000"; Layout.preferredWidth: 110; implicitHeight: 48; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 1; top: 65535 } }
                    Button { objectName: "uplink-keypad"; text: "数字键盘"; implicitHeight: 48; onClicked: keypad.open() }
                }
                RowLayout {
                    Button { objectName: "uplink-enable"; text: "保存并连接"; implicitHeight: 48; onClicked: gateway.configureUplink(uplinkHost.text, Number(uplinkPort.text), true) }
                    Button { text: "暂停上传"; enabled: gateway.uplinkEnabled; implicitHeight: 48; onClicked: gateway.configureUplink(gateway.uplinkHost, gateway.uplinkPort, false) }
                    Text { text: gateway.uplinkEnabled ? "自动上传已启用" : "上传已暂停"; color: "#526981"; font.pixelSize: 17 }
                }
                Text { text: "待确认：" + (gateway.queuedUploads < 0 ? "未知" : gateway.queuedUploads) + " 条"; font.pixelSize: 25; color: "#126d88" }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: gateway.uplinkStatus; font.pixelSize: 18; color: "#526981" }
                Item { Layout.fillHeight: true }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "暂停上传仍会保存新数据。新旧板卡字段分别传输，NTC 和距离不会作为光照上报。"; font.pixelSize: 16; color: "#62748b" }
            }
        }
        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: gateway.status; font.pixelSize: 15; color: "#526981" }
    }
}

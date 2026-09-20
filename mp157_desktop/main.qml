import QtQuick 2.12
import QtQuick.Window 2.12
Window {
    visible: true; width: 1024; height: 600; title: "EdgeGateway MP157"
    Dashboard { anchors.fill: parent; onReturnHome: Qt.quit() }
}

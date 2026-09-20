import QtQuick 2.12
import QtQuick.Window 2.12
import "qrc:/common"
Item {
    id: client
    objectName: "edgegateway"
    anchors.fill: parent
    property real scaleFactor: Screen.desktopAvailableWidth / 1024
    property string programmerName: objectName
    Dashboard {
        id: appMainBody
        anchors.fill: parent
        visible: !common.appMainBodyOpacityVisable
        onReturnHome: {
            common.navigateToHome()
            common.appActiveChanged(false)
            showAppBackAnimation()
            app_swipeView.visible = false
            main_swipeView.enabled = true
            client.visible = false
        }
    }
    Common { id: common; visible: true }
}

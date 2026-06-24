import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid

Item {
    id: root

    required property PlasmoidItem plasmoidItem

    implicitWidth: Kirigami.Units.gridUnit * 2
    implicitHeight: Kirigami.Units.gridUnit * 2

    HoverHandler {
        id: hoverHandler
    }

    TapHandler {
        onTapped: {
            root.plasmoidItem.expanded = !root.plasmoidItem.expanded;
        }
    }

    Item {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing

        scale: hoverHandler.hovered ? 1.1 : 1.0
        
        Behavior on scale {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutBack
            }
        }

        Image {
            id: iconImage
            anchors.fill: parent
            source: "icon.svg"
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "transparent"
            border.width: 1.5
            border.color: "#00ffff"
            opacity: hoverHandler.hovered ? 0.6 : 0.0
            
            Behavior on opacity {
                NumberAnimation { duration: Kirigami.Units.shortDuration }
            }
        }
    }
}

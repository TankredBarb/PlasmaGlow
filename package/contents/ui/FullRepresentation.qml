import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras

PlasmaExtras.Representation {
    id: root

    required property var plasmoidItem

    implicitWidth: Kirigami.Units.gridUnit * 20
    implicitHeight: root.plasmoidItem.controller.gammaAvailable ? Kirigami.Units.gridUnit * 18 : Kirigami.Units.gridUnit * 11

    collapseMarginsHint: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.mediumSpacing
        spacing: Kirigami.Units.largeSpacing

        // --- HEADER ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Image {
                source: "icon.svg"
                Layout.preferredWidth: Kirigami.Units.gridUnit * 1.5
                Layout.preferredHeight: Kirigami.Units.gridUnit * 1.5
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Item {
                implicitWidth: titleLabel.implicitWidth
                implicitHeight: titleLabel.implicitHeight
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                
                // Shadow offset
                PlasmaComponents3.Label {
                    text: "PlasmaGlow"
                    font.bold: true
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.2
                    color: "#000000"
                    opacity: 0.4
                    x: 1
                    y: 1
                }
                
                // Main neon text
                PlasmaComponents3.Label {
                    id: titleLabel
                    text: "PlasmaGlow"
                    font.bold: true
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.2
                    color: "#ff007f"
                }
            }

            Item { Layout.fillWidth: true }

            // Refresh button
            PlasmaComponents3.Button {
                icon.name: "view-refresh"
                flat: true
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: "Refresh color correction backend"
                onClicked: root.plasmoidItem.controller.refresh()
            }

            // Reset button
            PlasmaComponents3.Button {
                icon.name: "edit-undo"
                flat: true
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: "Reset to Default"
                onClicked: root.plasmoidItem.controller.reset()
            }
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: !root.plasmoidItem.controller.isX11 && root.plasmoidItem.controller.backendReady
            text: "Global adjustment across all windows and outputs"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            opacity: 0.7
        }

        // --- COLOR ADJUSTMENT CONTROLS ---
        ColumnLayout {
            id: adjustmentControls
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Kirigami.Units.largeSpacing

            // --- OUTPUT SELECTOR ---
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                visible: root.plasmoidItem.controller.isX11

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    text: "Monitor Output"
                    font.bold: true
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    color: Kirigami.Theme.textColor
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                }

                PlasmaComponents3.ComboBox {
                    id: outputCombo
                    Layout.fillWidth: true
                    model: root.plasmoidItem.controller.outputs
                    
                    Connections {
                        target: root.plasmoidItem.controller
                        function onOutputChanged() {
                            outputCombo.currentIndex = outputCombo.model.indexOf(root.plasmoidItem.controller.output);
                        }
                        function onOutputsChanged() {
                            outputCombo.model = root.plasmoidItem.controller.outputs;
                            outputCombo.currentIndex = outputCombo.model.indexOf(root.plasmoidItem.controller.output);
                        }
                    }
                    
                    Component.onCompleted: {
                        currentIndex = model.indexOf(root.plasmoidItem.controller.output);
                    }

                    onActivated: index => {
                        root.plasmoidItem.controller.output = textAt(index);
                    }
                }
            }

            // --- SLIDER SECTION ---
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                enabled: root.plasmoidItem.controller.saturationAvailable

                RowLayout {
                    Layout.fillWidth: true
                    
                    PlasmaComponents3.Label {
                        text: "Color Saturation"
                        font.bold: true
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: Kirigami.Theme.textColor
                        opacity: 0.7
                    }

                    Item { Layout.fillWidth: true }

                    PlasmaComponents3.Label {
                        text: (root.plasmoidItem.controller.saturation).toFixed(2) + "x"
                        font.bold: true
                        font.family: "Monospace"
                        //color: "#1d9ffc" // Readable blue value indicator
                        color: "#0000ff"
                    }
                }

                // Custom Styled Neon Slider
                QQC2.Slider {
                    id: satSlider
                    Layout.fillWidth: true
                    from: 0.0
                    to: 4.0
                    stepSize: 0.05
                    live: true
                    value: root.plasmoidItem.controller.saturation
                    
                    onMoved: {
                        root.plasmoidItem.controller.saturation = value;
                    }

                    Connections {
                        target: root.plasmoidItem.controller
                        function onSaturationChanged() {
                            satSlider.value = root.plasmoidItem.controller.saturation;
                        }
                    }

                    background: Rectangle {
                        x: satSlider.leftPadding
                        y: satSlider.topPadding + satSlider.availableHeight / 2 - height / 2
                        implicitWidth: 200
                        implicitHeight: 6
                        width: satSlider.availableWidth
                        height: implicitHeight
                        radius: 3
                        color: "#2a2c3f"

                        // Glowing active portion
                        Rectangle {
                            width: satSlider.visualPosition * parent.width
                            height: parent.height
                            radius: parent.radius
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "#7f00ff" } // Neon Violet
                                GradientStop { position: 0.5; color: "#ff007f" } // Hot Pink
                                GradientStop { position: 1.0; color: "#00ffff" } // Neon Cyan
                            }
                        }
                    }

                    handle: Item {
                        implicitWidth: Kirigami.Units.gridUnit * 2
                        implicitHeight: Kirigami.Units.gridUnit * 2
                        x: satSlider.leftPadding + satSlider.visualPosition * (satSlider.availableWidth - 18)
                           - (width - 18) / 2
                        y: satSlider.topPadding + satSlider.availableHeight / 2 - height / 2

                        Rectangle {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            radius: 9
                            color: "#ffffff"
                            border.color: "#ff007f"
                            border.width: 2

                            // Glow aura ring
                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + 6
                                height: parent.height + 6
                                radius: width / 2
                                color: "transparent"
                                border.color: "#00ffff"
                                border.width: 1.5
                                opacity: satInputArea.containsMouse || satInputArea.pressed
                                    || satSlider.hovered || satSlider.pressed ? 0.9 : 0.4

                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: satInputArea
                        parent: satSlider
                        anchors.fill: parent
                        z: 100
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor

                        function updateValue(mouseX) {
                            const fraction = Math.max(0, Math.min(1,
                                (mouseX - satSlider.leftPadding - 9) / (satSlider.availableWidth - 18)))
                            const position = satSlider.mirrored ? 1 - fraction : fraction
                            root.plasmoidItem.controller.saturation = satSlider.valueAt(position)
                        }

                        onPressed: mouse => {
                            satSlider.forceActiveFocus()
                            updateValue(mouse.x)
                        }
                        onPositionChanged: mouse => {
                            if (pressed) {
                                updateValue(mouse.x)
                            }
                        }
                    }
                }
            }

            // --- PRESETS ROW ---
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                enabled: root.plasmoidItem.controller.saturationAvailable

                PlasmaComponents3.Label {
                    text: "Presets:"
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    opacity: 0.6
                }

                Item { Layout.fillWidth: true }

                PlasmaComponents3.Button {
                    text: "1.0x"
                    flat: true
                    onClicked: root.plasmoidItem.controller.saturation = 1.0
                }
                PlasmaComponents3.Button {
                    text: "1.5x"
                    flat: true
                    onClicked: root.plasmoidItem.controller.saturation = 1.5
                }
                PlasmaComponents3.Button {
                    text: "2.0x"
                    flat: true
                    onClicked: root.plasmoidItem.controller.saturation = 2.0
                }
                PlasmaComponents3.Button {
                    text: "3.0x"
                    flat: true
                    onClicked: root.plasmoidItem.controller.saturation = 3.0
                }
            }

            // --- GAMMA SLIDER SECTION ---
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                visible: root.plasmoidItem.controller.gammaAvailable
                enabled: root.plasmoidItem.controller.gammaAvailable

                RowLayout {
                    Layout.fillWidth: true
                    
                    PlasmaComponents3.Label {
                        text: "Display Gamma"
                        font.bold: true
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: Kirigami.Theme.textColor
                        opacity: 0.7
                    }

                    Item { Layout.fillWidth: true }

                    PlasmaComponents3.Label {
                        text: (root.plasmoidItem.controller.gamma).toFixed(2)
                        font.bold: true
                        font.family: "Monospace"
                        //color: "#2ecc71" // Readable green value indicator
                        color: "#00ff00"
                    }
                }

                // Custom Styled Neon Gamma Slider
                QQC2.Slider {
                    id: gammaSlider
                    Layout.fillWidth: true
                    from: 0.1
                    to: 5.0
                    stepSize: 0.05
                    live: true
                    value: root.plasmoidItem.controller.gamma
                    
                    onMoved: {
                        root.plasmoidItem.controller.gamma = value;
                    }

                    Connections {
                        target: root.plasmoidItem.controller
                        function onGammaChanged() {
                            gammaSlider.value = root.plasmoidItem.controller.gamma;
                        }
                    }

                    background: Rectangle {
                        x: gammaSlider.leftPadding
                        y: gammaSlider.topPadding + gammaSlider.availableHeight / 2 - height / 2
                        implicitWidth: 200
                        implicitHeight: 6
                        width: gammaSlider.availableWidth
                        height: implicitHeight
                        radius: 3
                        color: "#2a2c3f"

                        // Glowing active portion
                        Rectangle {
                            width: gammaSlider.visualPosition * parent.width
                            height: parent.height
                            radius: parent.radius
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "#00ff88" } // Neon Mint Green
                                GradientStop { position: 0.5; color: "#00ffc4" } // Neon Teal
                                GradientStop { position: 1.0; color: "#00ffff" } // Neon Cyan
                            }
                        }
                    }

                    handle: Item {
                        implicitWidth: Kirigami.Units.gridUnit * 2
                        implicitHeight: Kirigami.Units.gridUnit * 2
                        x: gammaSlider.leftPadding + gammaSlider.visualPosition * (gammaSlider.availableWidth - 18)
                           - (width - 18) / 2
                        y: gammaSlider.topPadding + gammaSlider.availableHeight / 2 - height / 2

                        Rectangle {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            radius: 9
                            color: "#ffffff"
                            border.color: "#00ff88"
                            border.width: 2

                            // Glow aura ring
                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + 6
                                height: parent.height + 6
                                radius: width / 2
                                color: "transparent"
                                border.color: "#00ffff"
                                border.width: 1.5
                                opacity: gammaInputArea.containsMouse || gammaInputArea.pressed
                                    || gammaSlider.hovered || gammaSlider.pressed ? 0.9 : 0.4

                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: gammaInputArea
                        parent: gammaSlider
                        anchors.fill: parent
                        z: 100
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor

                        function updateValue(mouseX) {
                            const fraction = Math.max(0, Math.min(1,
                                (mouseX - gammaSlider.leftPadding - 9) / (gammaSlider.availableWidth - 18)))
                            const position = gammaSlider.mirrored ? 1 - fraction : fraction
                            root.plasmoidItem.controller.gamma = gammaSlider.valueAt(position)
                        }

                        onPressed: mouse => {
                            gammaSlider.forceActiveFocus()
                            updateValue(mouse.x)
                        }
                        onPositionChanged: mouse => {
                            if (pressed) {
                                updateValue(mouse.x)
                            }
                        }
                    }
                }

                // --- GAMMA PRESETS ROW ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    PlasmaComponents3.Label {
                        text: "Gamma Presets:"
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        opacity: 0.6
                    }

                    Item { Layout.fillWidth: true }

                    PlasmaComponents3.Button {
                        text: "0.8"
                        flat: true
                        onClicked: root.plasmoidItem.controller.gamma = 0.8
                    }
                    PlasmaComponents3.Button {
                        text: "1.0"
                        flat: true
                        onClicked: root.plasmoidItem.controller.gamma = 1.0
                    }
                    PlasmaComponents3.Button {
                        text: "1.2"
                        flat: true
                        onClicked: root.plasmoidItem.controller.gamma = 1.2
                    }
                    PlasmaComponents3.Button {
                        text: "1.5"
                        flat: true
                        onClicked: root.plasmoidItem.controller.gamma = 1.5
                    }
                }
            }
        }

        // --- BACKEND STATUS ---
        PlasmaExtras.PlaceholderMessage {
            Layout.fillWidth: true
            visible: !root.plasmoidItem.controller.backendReady || root.plasmoidItem.controller.error.length > 0
            iconName: "dialog-warning"
            text: root.plasmoidItem.controller.backendReady ? "Color correction failed" : "Color correction backend unavailable"
            explanation: root.plasmoidItem.controller.error.length > 0
                ? root.plasmoidItem.controller.error
                : "Use Refresh to check the backend again."
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: root.plasmoidItem.controller.isX11 && !root.plasmoidItem.controller.saturationAvailable
            text: root.plasmoidItem.controller.hasSaturation
                ? "Saturation is unavailable: no connected display output was found."
                : "Saturation is unavailable: vibrant-cli is not installed."
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: root.plasmoidItem.controller.isX11 && !root.plasmoidItem.controller.hasXGamma
            text: "Gamma is unavailable: xgamma is not installed."
            wrapMode: Text.Wrap
            opacity: 0.8
        }
    }
}

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras

PlasmaExtras.Representation {
    id: root

    required property var plasmoidItem
    readonly property color themeTextColor: Kirigami.Theme.textColor
    readonly property bool lightTheme: themeTextColor.r * 0.2126 + themeTextColor.g * 0.7152 + themeTextColor.b * 0.0722 < 0.5

    implicitWidth: Kirigami.Units.gridUnit * 20
    implicitHeight: contentLayout.implicitHeight + Kirigami.Units.mediumSpacing * 2
    Layout.minimumHeight: implicitHeight
    Layout.preferredHeight: implicitHeight
    Layout.maximumHeight: implicitHeight
    collapseMarginsHint: false

    onVisibleChanged: {
        if (!visible) {
            optionsMenu.close()
        }
    }

    Connections {
        target: root.plasmoidItem
        function onExpandedChanged() {
            if (!root.plasmoidItem.expanded) {
                optionsMenu.close()
            }
        }
    }

    function valueColor(accent) {
        return lightTheme ? Qt.darker(accent, 1.6) : Qt.lighter(accent, 1.15)
    }

    component SettingLabel: PlasmaComponents3.Label {
        id: label
        required property string helpText

        HoverHandler { id: labelHover }
        PlasmaComponents3.ToolTip {
            visible: root.visible && label.visible && labelHover.hovered && !optionsMenu.visible
            text: label.helpText
        }
    }

    component NeonSlider: QQC2.Slider {
        id: slider
        required property color startColor
        required property color accentColor
        required property color endColor
        property bool lightTheme: false
        property bool directInput: false
        readonly property bool engaged: hovered || pressed || inputArea.containsMouse || inputArea.pressed || activeFocus

        Layout.fillWidth: true
        live: true
        implicitHeight: Kirigami.Units.gridUnit * 2
        opacity: enabled ? 1 : 0.4

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            implicitWidth: 200
            implicitHeight: 6
            height: implicitHeight
            radius: 3
            color: slider.lightTheme ? "#d5d8e2" : "#303246"

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: slider.visualPosition * parent.width
                height: parent.height + 6
                radius: height / 2
                color: slider.accentColor
                opacity: slider.engaged ? 0.22 : 0.12
            }
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: slider.startColor }
                    GradientStop { position: 0.5; color: slider.accentColor }
                    GradientStop { position: 1.0; color: slider.endColor }
                }
            }
        }

        handle: Item {
            implicitWidth: 18
            implicitHeight: Kirigami.Units.gridUnit * 2
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2

            Rectangle {
                anchors.centerIn: parent
                width: slider.engaged ? 30 : 26
                height: width
                radius: width / 2
                color: slider.accentColor
                opacity: slider.engaged ? 0.23 : 0.12
                Behavior on width { NumberAnimation { duration: 120 } }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 24
                height: 24
                radius: 12
                color: "transparent"
                border.color: slider.endColor
                border.width: 1
                opacity: slider.engaged ? 0.9 : 0.45
            }
            Rectangle {
                anchors.centerIn: parent
                width: 18
                height: 18
                radius: 9
                color: "#ffffff"
                border.color: slider.accentColor
                border.width: 2
            }
        }

        // Preserve the color sliders' existing direct click/drag input path.
        MouseArea {
            id: inputArea
            anchors.fill: parent
            z: 100
            enabled: slider.directInput
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.PointingHandCursor

            function updateValue(mouseX) {
                const fraction = Math.max(0, Math.min(1,
                    (mouseX - slider.leftPadding - 9) / (slider.availableWidth - 18)))
                slider.value = slider.valueAt(slider.mirrored ? 1 - fraction : fraction)
                slider.moved()
            }
            onPressed: mouse => {
                slider.forceActiveFocus()
                updateValue(mouse.x)
            }
            onPositionChanged: mouse => {
                if (pressed) {
                    updateValue(mouse.x)
                }
            }
        }
    }

    component PresetRow: RowLayout {
        id: presets
        required property var values
        required property real currentValue
        property string suffix: ""
        property real valueScale: 1
        property int decimals: 1
        signal selected(real value)

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: presets.values
            delegate: PlasmaComponents3.Button {
                id: presetButton
                required property real modelData
                readonly property bool current: Math.abs(presets.currentValue - modelData) < 0.001
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                text: (modelData * presets.valueScale).toFixed(presets.decimals) + presets.suffix
                checkable: true
                autoExclusive: true
                checked: current
                onClicked: presets.selected(modelData)

                hoverEnabled: false
                HoverHandler {
                    id: presetHover
                    enabled: presetButton.enabled
                }

                KSvg.FrameSvgItem {
                    parent: presetButton.background
                    anchors.fill: parent
                    imagePath: "widgets/button"
                    prefix: presetButton.checked ? "normal" : "pressed"
                    opacity: presetHover.hovered && !presetButton.down
                        ? (presetButton.checked ? 0.18 : 0.28) : 0
                    Behavior on opacity {
                        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                    }
                }

                KSvg.FrameSvgItem {
                    parent: presetButton.background
                    anchors.fill: parent
                    anchors.leftMargin: -margins.left
                    anchors.topMargin: -margins.top
                    anchors.rightMargin: -margins.right
                    anchors.bottomMargin: -margins.bottom
                    imagePath: "widgets/button"
                    prefix: "hover"
                    opacity: presetHover.hovered && !presetButton.down ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                    }
                }
            }
        }
    }

    QQC2.Action {
        id: resetAction
        text: "Reset to Default"
        icon.name: "edit-undo"
        onTriggered: root.plasmoidItem.controller.reset()
    }

    QQC2.Menu {
        id: optionsMenu
        objectName: "optionsMenu"

        component IconMenuItem: QQC2.MenuItem {
            id: menuItem
            required property string iconName

            contentItem: PlasmaComponents3.Label {
                text: menuItem.text
                font: menuItem.font
                leftPadding: loginColorsItem.indicator.width + menuItem.spacing
                verticalAlignment: Text.AlignVCenter
            }
            indicator: Item {
                x: menuItem.leftPadding
                y: menuItem.topPadding + (menuItem.availableHeight - height) / 2
                width: loginColorsItem.indicator.width
                height: loginColorsItem.indicator.height
                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: Kirigami.Units.iconSizes.small
                    height: width
                    source: menuItem.iconName
                }
            }
        }

        QQC2.MenuItem {
            id: enableAdjustmentsItem
            objectName: "enableAdjustmentsItem"
            text: "Enable adjustments"
            checkable: true
            checked: root.plasmoidItem.controller.adjustmentsEnabled
            contentItem: PlasmaComponents3.Label {
                text: enableAdjustmentsItem.text
                font: enableAdjustmentsItem.font
                leftPadding: loginColorsItem.indicator.width + enableAdjustmentsItem.spacing
                verticalAlignment: Text.AlignVCenter
            }
            onTriggered: root.plasmoidItem.controller.adjustmentsEnabled = checked
        }
        QQC2.MenuItem {
            id: loginColorsItem
            text: "Colors on login screen"
            visible: !root.plasmoidItem.controller.isX11
            checkable: true
            checked: root.plasmoidItem.controller.applyToLogin
            contentItem: PlasmaComponents3.Label {
                text: loginColorsItem.text
                font: loginColorsItem.font
                leftPadding: loginColorsItem.indicator.width + loginColorsItem.spacing
                verticalAlignment: Text.AlignVCenter
            }
            onTriggered: root.plasmoidItem.controller.applyToLogin = checked
        }
        IconMenuItem {
            text: "Refresh"
            iconName: "view-refresh"
            onTriggered: root.plasmoidItem.controller.refresh()
        }
        IconMenuItem {
            objectName: "resetMenuItem"
            action: resetAction
            iconName: resetAction.icon.name
        }
        QQC2.MenuSeparator {}
        IconMenuItem {
            objectName: "aboutMenuItem"
            text: "About PlasmaGlow"
            iconName: "help-about"
            onTriggered: root.plasmoidItem.controller.showAboutDialog()
        }
    }

    QQC2.ScrollView {
        id: scrollView
        anchors.fill: parent
        anchors.margins: Kirigami.Units.mediumSpacing
        clip: true
        contentWidth: availableWidth
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

        ColumnLayout {
            id: contentLayout
            width: scrollView.availableWidth
            spacing: Kirigami.Units.mediumSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Image {
                    source: root.lightTheme ? "icon-light.svg" : "icon.svg"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 1.5
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 1.5
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                PlasmaComponents3.Label {
                    text: "PlasmaGlow"
                    font.bold: true
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.2
                    HoverHandler { id: titleHover }
                    PlasmaComponents3.ToolTip {
                        visible: root.visible && titleHover.hovered && !optionsMenu.visible
                        text: root.plasmoidItem.controller.isX11
                            ? "Adjust the selected monitor" : "Global adjustment across all windows and outputs"
                    }
                }
                Item { Layout.fillWidth: true }
                PlasmaComponents3.Button {
                    objectName: "resetButton"
                    id: resetButton
                    action: resetAction
                    display: QQC2.AbstractButton.IconOnly
                    flat: true
                    PlasmaComponents3.ToolTip {
                        visible: root.visible && resetButton.hovered && !resetButton.down && !optionsMenu.visible
                        text: resetAction.text
                    }
                }
                PlasmaComponents3.Button {
                    id: optionsButton
                    objectName: "optionsButton"
                    icon.name: "application-menu"
                    flat: true
                    PlasmaComponents3.ToolTip {
                        visible: root.visible && optionsButton.hovered && !optionsButton.down && !optionsMenu.visible
                        text: "Options"
                    }
                    onClicked: optionsMenu.popup(optionsButton, 0, optionsButton.height)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                enabled: root.plasmoidItem.controller.adjustmentsEnabled
                implicitHeight: colorLayout.implicitHeight + Kirigami.Units.mediumSpacing * 2
                radius: Kirigami.Units.cornerRadius
                color: Qt.rgba(0.8, 0.1, 0.6, root.lightTheme ? 0.045 : 0.07)
                border.color: Qt.rgba(1, 0, 0.5, root.lightTheme ? 0.2 : 0.25)

                ColumnLayout {
                    id: colorLayout
                    x: Kirigami.Units.mediumSpacing
                    y: Kirigami.Units.mediumSpacing
                    width: parent.width - Kirigami.Units.mediumSpacing * 2
                    spacing: Kirigami.Units.smallSpacing

                    PlasmaComponents3.Label {
                        text: "Color"
                        font.bold: true
                        Layout.bottomMargin: Kirigami.Units.smallSpacing
                    }
                    PlasmaComponents3.ComboBox {
                        id: outputCombo
                        Layout.fillWidth: true
                        visible: root.plasmoidItem.controller.isX11
                        model: root.plasmoidItem.controller.outputs
                        Component.onCompleted: currentIndex = model.indexOf(root.plasmoidItem.controller.output)
                        onActivated: index => { root.plasmoidItem.controller.output = textAt(index) }
                        Connections {
                            target: root.plasmoidItem.controller
                            function onOutputChanged() { outputCombo.currentIndex = outputCombo.model.indexOf(root.plasmoidItem.controller.output) }
                            function onOutputsChanged() {
                                outputCombo.model = root.plasmoidItem.controller.outputs
                                outputCombo.currentIndex = outputCombo.model.indexOf(root.plasmoidItem.controller.output)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: root.plasmoidItem.controller.saturationAvailable
                        SettingLabel {
                            text: "Saturation"
                            helpText: "Adjust color intensity. 1.0x is neutral; 0.0x removes all color."
                        }
                        Item { Layout.fillWidth: true }
                        PlasmaComponents3.Label {
                            text: root.plasmoidItem.controller.saturation.toFixed(2) + "x"
                            font.bold: true
                            font.family: "Monospace"
                            color: root.valueColor("#ff007f")
                        }
                    }
                    NeonSlider {
                        id: satSlider
                        objectName: "saturationSlider"
                        enabled: root.plasmoidItem.controller.saturationAvailable
                        from: 0.0; to: 4.0; stepSize: 0.05
                        value: root.plasmoidItem.controller.saturation
                        startColor: "#7f00ff"; accentColor: "#ff007f"; endColor: "#00ffff"
                        lightTheme: root.lightTheme
                        directInput: true
                        onMoved: root.plasmoidItem.controller.saturation = value
                        Connections {
                            target: root.plasmoidItem.controller
                            function onSaturationChanged() { satSlider.value = root.plasmoidItem.controller.saturation }
                        }
                    }
                    PresetRow {
                        objectName: "saturationPresets"
                        enabled: root.plasmoidItem.controller.saturationAvailable
                        values: [1.0, 1.5, 2.0, 3.0]
                        currentValue: root.plasmoidItem.controller.saturation
                        suffix: "x"
                        onSelected: value => { root.plasmoidItem.controller.saturation = value }
                        Layout.bottomMargin: Kirigami.Units.smallSpacing
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.plasmoidItem.controller.gammaAvailable
                        SettingLabel {
                            text: "Gamma"
                            helpText: "Adjust midtone brightness. 1.0 is neutral; higher values brighten the image, lower values darken it."
                        }
                        Item { Layout.fillWidth: true }
                        PlasmaComponents3.Label {
                            text: root.plasmoidItem.controller.gamma.toFixed(2)
                            font.bold: true
                            font.family: "Monospace"
                            color: root.valueColor("#00e69c")
                        }
                    }
                    NeonSlider {
                        id: gammaSlider
                        objectName: "gammaSlider"
                        visible: root.plasmoidItem.controller.gammaAvailable
                        enabled: root.plasmoidItem.controller.gammaAvailable
                        from: 0.1; to: 5.0; stepSize: 0.05
                        value: root.plasmoidItem.controller.gamma
                        startColor: "#00ff88"; accentColor: "#00ffc4"; endColor: "#00ffff"
                        lightTheme: root.lightTheme
                        directInput: true
                        onMoved: root.plasmoidItem.controller.gamma = value
                        Connections {
                            target: root.plasmoidItem.controller
                            function onGammaChanged() { gammaSlider.value = root.plasmoidItem.controller.gamma }
                        }
                    }
                    PresetRow {
                        objectName: "gammaPresets"
                        visible: root.plasmoidItem.controller.gammaAvailable
                        enabled: root.plasmoidItem.controller.gammaAvailable
                        values: [0.8, 1.0, 1.2, 1.5]
                        currentValue: root.plasmoidItem.controller.gamma
                        onSelected: value => { root.plasmoidItem.controller.gamma = value }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: !root.plasmoidItem.controller.isX11
                enabled: root.plasmoidItem.controller.adjustmentsEnabled && root.plasmoidItem.controller.sharpeningAvailable
                implicitHeight: sharpeningLayout.implicitHeight + Kirigami.Units.mediumSpacing * 2
                radius: Kirigami.Units.cornerRadius
                color: Qt.rgba(1, 0.4, 0.15, root.lightTheme ? 0.045 : 0.07)
                border.color: Qt.rgba(1, 0.45, 0.15, root.lightTheme ? 0.2 : 0.25)

                ColumnLayout {
                    id: sharpeningLayout
                    x: Kirigami.Units.mediumSpacing
                    y: Kirigami.Units.mediumSpacing
                    width: parent.width - Kirigami.Units.mediumSpacing * 2
                    spacing: Kirigami.Units.smallSpacing

                    SettingLabel {
                        text: "Sharpening"
                        helpText: "Global SDR sharpening. 0% is the minimum active strength; select Off to disable."
                        font.bold: true
                        Layout.bottomMargin: Kirigami.Units.smallSpacing
                    }
                    RowLayout {
                        id: modeRow
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Repeater {
                            model: ["off", "cas", "luma"]
                            delegate: QQC2.Button {
                                id: modeButton
                                required property string modelData
                                readonly property color accent: modelData === "off" ? "#d8a0ff" : modelData === "cas" ? "#ff8040" : "#00e5ff"
                                readonly property color gradientStart: modelData === "off" ? "#8842c7" : modelData === "cas" ? "#d46b10" : "#008c86"
                                readonly property color gradientEnd: modelData === "off" ? "#5a2aa0" : modelData === "cas" ? "#c42159" : "#3060d5"
                                readonly property color foreground: "#ffffff"
                                objectName: "modeButton-" + modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                                padding: Kirigami.Units.smallSpacing
                                text: modelData === "off" ? "Off" : modelData === "cas" ? "CAS" : "Luma"
                                checkable: true
                                autoExclusive: true
                                checked: root.plasmoidItem.controller.sharpeningMode === modelData
                                onClicked: root.plasmoidItem.controller.sharpeningMode = modelData

                                PlasmaComponents3.ToolTip {
                                    visible: root.visible && modeButton.hovered && !modeButton.down && !optionsMenu.visible
                                    text: modeButton.modelData === "off" ? "Disable sharpening. Color adjustments remain active."
                                        : modeButton.modelData === "cas" ? "Contrast Adaptive Sharpening: enhance detail using local contrast."
                                        : "Luma sharpening: enhance brightness detail with adjustable noise suppression."
                                }

                                contentItem: Item {
                                    implicitWidth: modeContent.implicitWidth
                                    implicitHeight: modeContent.implicitHeight
                                    RowLayout {
                                        id: modeContent
                                        anchors.centerIn: parent
                                        spacing: Kirigami.Units.smallSpacing
                                        Kirigami.Icon {
                                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                            source: Qt.resolvedUrl("mode-" + modeButton.modelData + ".svg")
                                            isMask: true
                                            color: modeButton.foreground
                                        }
                                        PlasmaComponents3.Label {
                                            objectName: "modeLabel-" + modeButton.modelData
                                            text: modeButton.text
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                                            font.bold: true
                                            color: modeButton.foreground
                                        }
                                    }
                                }
                                background: Item {
                                    implicitHeight: Kirigami.Units.gridUnit * 2.1
                                    implicitWidth: Kirigami.Units.gridUnit * 3

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.topMargin: 2
                                        anchors.bottomMargin: -2
                                        radius: Kirigami.Units.cornerRadius
                                        color: "#000000"
                                        opacity: root.lightTheme ? 0.16 : 0.35
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -2
                                        radius: Kirigami.Units.cornerRadius + 2
                                        color: modeButton.accent
                                        opacity: modeButton.activeFocus ? 0.35 : modeButton.checked ? 0.2 : 0
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -2
                                        radius: Kirigami.Units.cornerRadius + 2
                                        color: "transparent"
                                        border.color: root.lightTheme ? "#232629" : "#ffffff"
                                        border.width: 2
                                        visible: modeButton.checked
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: Kirigami.Units.cornerRadius
                                        gradient: Gradient {
                                            GradientStop {
                                                position: 0
                                                color: modeButton.checked ? Qt.lighter(modeButton.gradientStart, modeButton.down ? 1 : 1.15)
                                                    : Qt.darker(modeButton.gradientStart, modeButton.hovered ? 1.1 : 1.4)
                                            }
                                            GradientStop {
                                                position: 1
                                                color: modeButton.checked ? modeButton.gradientEnd
                                                    : Qt.darker(modeButton.gradientEnd, modeButton.hovered ? 1.1 : 1.4)
                                            }
                                        }
                                        border.color: modeButton.checked || modeButton.hovered || modeButton.activeFocus
                                            ? modeButton.accent : Qt.rgba(modeButton.accent.r, modeButton.accent.g, modeButton.accent.b, 0.5)
                                        border.width: modeButton.checked || modeButton.activeFocus ? 2 : 1
                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.margins: 3
                                            height: 1
                                            radius: 1
                                            color: "#ffffff"
                                            opacity: modeButton.checked ? 0.35 : 0.18
                                        }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.plasmoidItem.controller.sharpeningMode !== "off"
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        SettingLabel {
                            text: root.plasmoidItem.controller.sharpeningStrength > 0.5 ? "Strength (Overdrive)" : "Strength"
                            helpText: "Adjust sharpening strength. Above 50%, overdrive boosts the effect and can produce halos. Select Off to disable sharpening."
                        }
                        Item { Layout.fillWidth: true }
                        PlasmaComponents3.Label {
                            text: Math.round(root.plasmoidItem.controller.sharpeningStrength * 100) + "%"
                            font.bold: true
                            font.family: "Monospace"
                            color: root.valueColor("#ff8040")
                        }
                    }
                    NeonSlider {
                        id: sharpeningSlider
                        objectName: "sharpeningSlider"
                        visible: root.plasmoidItem.controller.sharpeningMode !== "off"
                        from: 0.0; to: 1.0; stepSize: 0.01
                        value: root.plasmoidItem.controller.sharpeningStrength
                        startColor: "#ffca40"; accentColor: "#ff8040"; endColor: "#ff007f"
                        lightTheme: root.lightTheme
                        onMoved: root.plasmoidItem.controller.sharpeningStrength = value
                    }
                    PresetRow {
                        objectName: "sharpeningPresets"
                        visible: root.plasmoidItem.controller.sharpeningMode !== "off"
                        values: [0.25, 0.5, 0.75, 1.0]
                        currentValue: root.plasmoidItem.controller.sharpeningStrength
                        valueScale: 100
                        decimals: 0
                        suffix: "%"
                        onSelected: value => { root.plasmoidItem.controller.sharpeningStrength = value }
                        Layout.bottomMargin: Kirigami.Units.smallSpacing
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.plasmoidItem.controller.sharpeningMode === "luma"
                        SettingLabel {
                            text: "Noise suppression"
                            helpText: "Reduce sharpening of small variations and noise in Luma mode. Higher values suppress more noise and fine detail."
                        }
                        Item { Layout.fillWidth: true }
                        PlasmaComponents3.Label {
                            text: Math.round(root.plasmoidItem.controller.sharpeningDenoise * 100) + "%"
                            font.bold: true
                            font.family: "Monospace"
                            color: root.valueColor("#00c8ff")
                        }
                    }
                    NeonSlider {
                        id: denoiseSlider
                        objectName: "denoiseSlider"
                        visible: root.plasmoidItem.controller.sharpeningMode === "luma"
                        from: 0.0; to: 1.0; stepSize: 0.01
                        value: root.plasmoidItem.controller.sharpeningDenoise
                        startColor: "#00ffff"; accentColor: "#00c8ff"; endColor: "#7f00ff"
                        lightTheme: root.lightTheme
                        onMoved: root.plasmoidItem.controller.sharpeningDenoise = value
                    }
                    PresetRow {
                        objectName: "denoisePresets"
                        visible: root.plasmoidItem.controller.sharpeningMode === "luma"
                        values: [0.0, 0.17, 0.5, 1.0]
                        currentValue: root.plasmoidItem.controller.sharpeningDenoise
                        valueScale: 100
                        decimals: 0
                        suffix: "%"
                        onSelected: value => { root.plasmoidItem.controller.sharpeningDenoise = value }
                    }
                    Connections {
                        target: root.plasmoidItem.controller
                        function onSharpeningChanged() {
                            sharpeningSlider.value = root.plasmoidItem.controller.sharpeningStrength
                            denoiseSlider.value = root.plasmoidItem.controller.sharpeningDenoise
                        }
                    }
                }
            }

            PlasmaExtras.PlaceholderMessage {
                Layout.fillWidth: true
                visible: !root.plasmoidItem.controller.backendReady || root.plasmoidItem.controller.error.length > 0
                iconName: "dialog-warning"
                text: root.plasmoidItem.controller.backendReady ? "PlasmaGlow adjustment failed" : "PlasmaGlow backend unavailable"
                explanation: root.plasmoidItem.controller.error.length > 0
                    ? root.plasmoidItem.controller.error : "Use Refresh to check the backend again."
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
}

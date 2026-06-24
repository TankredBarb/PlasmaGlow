import QtQuick
import org.kde.plasma.plasmoid
import plasma.applet.org.kde.plasmaglow

PlasmoidItem {
    id: root

    GlowController {
        id: backend
    }

    readonly property alias controller: backend

    compactRepresentation: CompactRepresentation {
        plasmoidItem: root
    }

    fullRepresentation: FullRepresentation {
        plasmoidItem: root
    }
}

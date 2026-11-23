import QtQuick

// A flat touch target in the suggestion strip that darkens while pressed.
Item {
    id: button
    signal tapped()

    Rectangle {
        anchors.fill: parent
        color: "#2c2c31"
        opacity: tap.pressed ? 1 : 0
    }
    TapHandler {
        id: tap
        enabled: button.enabled
        onTapped: button.tapped()
    }
}

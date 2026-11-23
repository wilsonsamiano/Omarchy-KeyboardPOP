import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window
import PressPop

// The keyboard never takes keyboard focus, so it has no shortcuts of its
// own: every control is a touch target.
ApplicationWindow {
    id: win
    visible: false
    title: "PressPop"
    width: Screen.width
    height: stripHeight + Math.round(Math.min(260, Math.max(170, Screen.height * 0.25)))
    color: "#151517"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property int stripHeight: 44
    readonly property color stripColor: "#1d1d20"
    readonly property color textColor: "#f2f2f4"
    readonly property color mutedColor: "#8e8e93"
    property bool statusVisible: false

    // The status (how PressPop is typing, or what's wrong) shows for a
    // moment whenever it changes, and again from the info button.
    Connections {
        target: backend
        function onStatusChanged() { win.statusVisible = backend.status !== ""; statusTimer.restart() }
    }
    onVisibleChanged: if (visible && backend.status !== "") { statusVisible = true; statusTimer.restart() }
    Timer { id: statusTimer; interval: 7000; onTriggered: win.statusVisible = backend.statusIsError }

    Rectangle {
        id: strip
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: win.stripHeight
        color: win.stripColor

        // Mirrored in right-to-left languages; the keys below are not,
        // since QWERTY reads the same everywhere.
        Item {
            anchors.fill: parent
            LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
            LayoutMirroring.childrenInherit: true

            StripButton {
                id: languageButton
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: 64
                onTapped: backend.cycleLanguage()
                Text {
                    anchors.centerIn: parent
                    text: backend.language.toUpperCase()
                    color: win.textColor
                    font { pixelSize: 15; weight: Font.DemiBold }
                }
            }

            Row {
                id: suggestions
                anchors { left: languageButton.right; right: infoButton.left; top: parent.top; bottom: parent.bottom }
                visible: !win.statusVisible

                Repeater {
                    model: 3
                    delegate: StripButton {
                        required property int index
                        readonly property string word: backend.suggestions[index] ?? ""
                        width: suggestions.width / 3
                        height: suggestions.height
                        enabled: word !== ""
                        onTapped: backend.pickSuggestion(index)

                        Rectangle {
                            visible: index > 0
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            width: 1; height: parent.height * 0.5
                            color: "#38383d"
                        }
                        Text {
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            text: parent.word
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: index === 1 ? theme.accent : win.textColor
                            font { pixelSize: 18; weight: index === 1 ? Font.DemiBold : Font.Normal }
                        }
                    }
                }
            }

            Text {
                anchors { left: languageButton.right; right: infoButton.left; verticalCenter: parent.verticalCenter; leftMargin: 8; rightMargin: 8 }
                visible: win.statusVisible
                text: backend.status
                color: backend.statusIsError ? "#ff6961" : win.mutedColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            StripButton {
                id: infoButton
                anchors { right: hideButton.left; top: parent.top; bottom: parent.bottom }
                width: backend.status !== "" ? 48 : 0
                visible: width > 0
                onTapped: { win.statusVisible = !win.statusVisible; if (win.statusVisible) statusTimer.restart() }
                Rectangle {
                    anchors.centerIn: parent
                    width: 22; height: 22; radius: 11
                    color: "transparent"
                    border { color: backend.statusIsError ? "#ff6961" : win.mutedColor; width: 1.5 }
                    Text {
                        anchors.centerIn: parent
                        text: "i"
                        color: backend.statusIsError ? "#ff6961" : win.mutedColor
                        font { pixelSize: 14; weight: Font.DemiBold; italic: true }
                    }
                }
            }

            StripButton {
                id: hideButton
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: 64
                onTapped: win.visible = false
                // A chevron pointing down: put the keyboard away.
                Item {
                    anchors.centerIn: parent
                    width: 20; height: 12
                    Rectangle { x: 0; y: 4; width: 13; height: 2.5; radius: 1.25; rotation: 40; antialiasing: true; color: win.textColor }
                    Rectangle { x: 7; y: 4; width: 13; height: 2.5; radius: 1.25; rotation: -40; antialiasing: true; color: win.textColor }
                }
            }
        }
    }

    // Above the strip so a top-row key's popup can rise over it; it only
    // takes touches on the keys.
    KeyboardView {
        anchors.fill: parent
        z: 1
        keysTop: win.stripHeight
        maxKeysWidth: 960
        keyboardBackend: backend
        accent: theme.accent
        accentForeground: theme.accentForeground
        radius: theme.radius
    }
}

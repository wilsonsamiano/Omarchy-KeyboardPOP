import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window
import KeyboardPOP

// The keyboard never takes keyboard focus, so it has no shortcuts of its
// own: every control is a touch target.
ApplicationWindow {
    id: win
    visible: false
    title: "KeyboardPOP"
    // Docked it spans the screen; floating it is a smaller card.
    width: floating ? Math.min(Screen.width - 24, 700) : Screen.width
    // The typing rows, plus a slimmer control row (Esc, Tab, Ctrl, Alt,
    // Super, arrows) on top, and floating a grab handle above the strip.
    height: handleHeight + stripHeight
            + Math.round(Math.min(260, Math.max(170, Screen.height * 0.25)) * 1.18 * (floating ? 0.86 : 1))
    color: floating ? "transparent" : palette_.background

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property bool floating: placement.floating
    readonly property int stripHeight: 44
    readonly property int handleHeight: floating ? 36 : 0
    readonly property int cardRadius: Math.max(theme.radius, 12)
    readonly property var palette_: theme.palette
    readonly property color stripColor: palette_.strip
    readonly property color textColor: palette_.label
    readonly property color mutedColor: palette_.hint
    readonly property color errorColor: palette_.error
    property bool statusVisible: false

    // The status (how KeyboardPOP is typing, or what's wrong) shows for a
    // moment whenever it changes, and again from the info button.
    Connections {
        target: backend
        function onStatusChanged() { win.statusVisible = backend.status !== ""; statusTimer.restart() }
    }
    onVisibleChanged: if (visible && backend.status !== "") { statusVisible = true; statusTimer.restart() }
    Timer { id: statusTimer; interval: 7000; onTriggered: win.statusVisible = backend.statusIsError }

    // Floating: a rounded card behind everything, edged so it reads over
    // any window.
    Rectangle {
        anchors.fill: parent
        visible: win.floating
        radius: win.cardRadius
        color: win.palette_.background
        border { width: 1; color: win.palette_.divider }
    }

    // Floating: drag here to move the keyboard.
    Rectangle {
        id: handle
        visible: win.floating
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 1 }
        height: win.handleHeight
        topLeftRadius: win.cardRadius
        topRightRadius: win.cardRadius
        color: win.stripColor
        Rectangle {
            anchors.centerIn: parent
            width: 56; height: 5; radius: 2.5
            color: dragArea.pressed ? theme.accent : win.mutedColor
        }
        MouseArea {
            id: dragArea
            anchors.fill: parent
            preventStealing: true
            property point pressAt
            onPressed: function(mouse) { pressAt = Qt.point(mouse.x, mouse.y) }
            // The window moves under the finger, so each step is measured
            // from where the press landed in it.
            onPositionChanged: function(mouse) { placement.dragBy(mouse.x - pressAt.x, mouse.y - pressAt.y) }
            onReleased: placement.endDrag()
            onCanceled: placement.endDrag()
        }
    }

    Rectangle {
        id: strip
        anchors { top: handle.visible ? handle.bottom : parent.top; left: parent.left; right: parent.right
                  leftMargin: win.floating ? 1 : 0; rightMargin: win.floating ? 1 : 0 }
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
                            color: win.palette_.divider
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
                color: backend.statusIsError ? win.errorColor : win.mutedColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            StripButton {
                id: infoButton
                anchors { right: placeButton.left; top: parent.top; bottom: parent.bottom }
                width: backend.status !== "" ? 48 : 0
                visible: width > 0
                onTapped: { win.statusVisible = !win.statusVisible; if (win.statusVisible) statusTimer.restart() }
                Rectangle {
                    anchors.centerIn: parent
                    width: 22; height: 22; radius: 11
                    color: "transparent"
                    border { color: backend.statusIsError ? win.errorColor : win.mutedColor; width: 1.5 }
                    Text {
                        anchors.centerIn: parent
                        text: "i"
                        color: backend.statusIsError ? win.errorColor : win.mutedColor
                        font { pixelSize: 14; weight: Font.DemiBold; italic: true }
                    }
                }
            }

            // Float the keyboard, or dock it again. The icon shows where it
            // goes: a card lifted off the bottom, or a bar along it.
            StripButton {
                id: placeButton
                anchors { right: hideButton.left; top: parent.top; bottom: parent.bottom }
                width: 52
                onTapped: placement.floating = !placement.floating
                Item {
                    anchors.centerIn: parent
                    width: 22; height: 18
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        radius: 3
                        border { width: 1.5; color: win.textColor }
                    }
                    Rectangle {
                        visible: !win.floating
                        x: 5; y: 5; width: 12; height: 7; radius: 1.5
                        color: win.textColor
                    }
                    Rectangle {
                        visible: win.floating
                        x: 3; y: parent.height - 7; width: parent.width - 6; height: 4; radius: 1
                        color: win.textColor
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
        keysTop: win.handleHeight + win.stripHeight
        maxKeysWidth: 960
        colors: theme.palette
        transparentBackground: win.floating
        keyboardBackend: backend
        accent: theme.accent
        accentForeground: theme.accentForeground
        radius: theme.radius
    }
}

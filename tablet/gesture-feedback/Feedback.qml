import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland
import QtQuick
import qs.Commons
import qs.Commons as Commons

// Lightweight feedback for touch gestures. Nothing is on screen (and no
// window exists) until a gesture fires; then a short animation plays in a
// window that touches pass straight through, and it goes away again.
//
//   omarchy-shell gesture desktop <n> <count> <bumped>
//     page dots for desktop n of count; bumped=1 when the swipe ran into
//     the first or last desktop, so the dots nudge instead.
Item {
  id: root

  property var shell: null
  property var manifest: null

  property int desktop: 1
  property int count: 5
  property bool bumped: false
  property bool showing: false

  ShellIpc {
    target: "gesture"
    function desktop(n: int, count: int, bumped: int): void {
      root.desktop = n
      root.count = Math.max(1, count)
      root.bumped = bumped !== 0
      root.showing = true
      dots.opacity = 1
      if (root.bumped) bump.restart()
      hideTimer.restart()
    }
  }

  Timer {
    id: hideTimer
    interval: 900
    onTriggered: fadeOut.start()
  }

  PanelWindow {
    id: window
    visible: root.showing
    color: "transparent"
    anchors.bottom: true
    implicitWidth: dots.width + 40
    implicitHeight: 64
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "omarchy-gesture-feedback"
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    // Touches and clicks pass through to whatever is underneath.
    mask: Region {}

    Rectangle {
      id: dots
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.top: parent.top
      anchors.topMargin: 8
      width: row.width + 24
      height: 28
      radius: height / 2
      color: Commons.Color.menu.background
      opacity: 0

      Row {
        id: row
        anchors.centerIn: parent
        spacing: 10

        Repeater {
          model: root.count
          Rectangle {
            required property int index
            readonly property bool current: index + 1 === root.desktop
            width: current ? 22 : 8
            height: 8
            radius: 4
            color: current ? Commons.Color.menu.border : Commons.Color.menu.text
            opacity: current ? 1 : 0.35
            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
          }
        }
      }

      // A nudge toward the end that was hit.
      transform: Translate { id: nudge }
      SequentialAnimation {
        id: bump
        readonly property real dir: root.desktop <= 1 ? 1 : -1
        NumberAnimation { target: nudge; property: "x"; to: 10 * bump.dir; duration: 70; easing.type: Easing.OutQuad }
        NumberAnimation { target: nudge; property: "x"; to: 0; duration: 220; easing.type: Easing.OutBack }
      }
    }

    NumberAnimation {
      id: fadeOut
      target: dots
      property: "opacity"
      to: 0
      duration: 250
      onFinished: root.showing = false
    }
  }
}

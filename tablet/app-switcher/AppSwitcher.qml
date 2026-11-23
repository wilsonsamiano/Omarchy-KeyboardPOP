import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland
import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// An iPad-style app switcher: a card for every open window, newest first.
//   tap a card            switch to that window
//   swipe a card up       close the window
//   drag onto a desktop   move the window to desktop 1-5 (the row below)
//   tap outside / Esc     dismiss
// Summoned with `omarchy-shell shell summon touch.app-switcher`, which the
// touchscreen gesture (swipe up from the bottom edge and hold) runs.
Item {
  id: root

  property var shell: null
  property var manifest: null
  property bool opened: false
  property int selectedIndex: 0
  property var windows: []

  readonly property int desktopCount: 5
  readonly property color scrim: Commons.Color.menu.scrim
  readonly property color cardColor: Commons.Color.menu.background
  readonly property color textColor: Commons.Color.menu.text
  readonly property color accent: Commons.Color.menu.border
  readonly property color accentText: Commons.Color.menu.background
  readonly property int radius: Style.cornerRadius

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }

  function open(payloadJson) {
    Hyprland.refreshToplevels()
    root.rebuild()
    root.selectedIndex = 0
    root.opened = true
    Qt.callLater(function() { keyCatcher.forceActiveFocus() })
  }

  function close() {
    root.opened = false
  }

  function dismiss() {
    root.opened = false
    if (root.shell && typeof root.shell.hide === "function")
      root.shell.hide((root.manifest && root.manifest.id) || "touch.app-switcher")
  }

  function toggle() {
    if (root.opened) root.dismiss()
    else root.open("{}")
  }

  // The windows worth switching to, most recently used first.
  function rebuild() {
    var list = []
    var toplevels = Hyprland.toplevels.values
    for (var i = 0; i < toplevels.length; i++) {
      var t = toplevels[i]
      var wl = t.wayland
      if (!wl || wl.parent) continue
      var ipc = t.lastIpcObject || {}
      var ws = t.workspace
      var wsName = ws ? String(ws.name || "") : ""
      // The screensaver and the like live on their own special workspaces.
      if (wsName.indexOf("special:screensaver") === 0) continue
      list.push({
        address: String(t.address || ""),
        title: String(t.title || wl.title || ""),
        appId: String(wl.appId || ipc["class"] || ""),
        workspaceId: ws ? ws.id : -1,
        minimized: wsName.indexOf("special:") === 0,
        focusOrder: ipc.focusHistoryID !== undefined ? ipc.focusHistoryID : 999,
        toplevel: wl
      })
    }
    list.sort(function(a, b) { return a.focusOrder - b.focusOrder })
    root.windows = list
  }

  Connections {
    target: Hyprland.toplevels
    function onValuesChanged() { if (root.opened) root.rebuild() }
  }

  function target(address) {
    var a = String(address || "")
    if (a.indexOf("0x") !== 0) a = "0x" + a
    return "window = \"address:" + a + "\""
  }

  function dispatch(steps) {
    Util.execDetached(steps.map(function(s) { return "hyprctl dispatch " + Util.shellQuote(s) }).join(" && "))
  }

  function switchTo(win) {
    root.dismiss()
    var t = root.target(win.address)
    var steps = []
    if (win.minimized) {
      var monitor = Hyprland.focusedMonitor
      var current = monitor && monitor.activeWorkspace ? monitor.activeWorkspace.id : 1
      steps.push("hl.dsp.window.move({ workspace = \"" + current + "\", " + t + " })")
    }
    steps.push("hl.dsp.focus({ " + t + " })")
    steps.push("hl.dsp.window.alter_zorder({ mode = \"top\", " + t + " })")
    root.dispatch(steps)
  }

  function closeWindow(win) {
    root.dispatch(["hl.dsp.window.close({ " + root.target(win.address) + " })"])
    // Gone from the list right away; Hyprland's update follows.
    root.windows = root.windows.filter(function(w) { return w.address !== win.address })
    if (root.selectedIndex >= root.windows.length) root.selectedIndex = Math.max(0, root.windows.length - 1)
  }

  function moveWindow(win, desktop) {
    root.dispatch(["hl.dsp.window.move({ workspace = \"" + desktop + "\", " + root.target(win.address) + " })"])
    var copy = root.windows.slice()
    for (var i = 0; i < copy.length; i++)
      if (copy[i].address === win.address) copy[i] = Object.assign({}, copy[i], { workspaceId: desktop, minimized: false })
    root.windows = copy
  }

  // Which desktop chip, if any, is under a point in the overlay.
  function desktopAt(x, y) {
    for (var i = 0; i < desktopRepeater.count; i++) {
      var chip = desktopRepeater.itemAt(i)
      var p = chip.mapFromItem(panel.contentItem, x, y)
      if (p.x >= 0 && p.y >= 0 && p.x <= chip.width && p.y <= chip.height) return i + 1
    }
    return 0
  }

  property int hoveredDesktop: 0

  OverlayWindow {
    id: panel
    shown: root.opened
    WlrLayershell.namespace: "omarchy-app-switcher"

    Rectangle {
      anchors.fill: parent
      color: root.scrim
    }

    MouseArea {
      anchors.fill: parent
      onClicked: root.dismiss()
    }

    Item {
      id: keyCatcher
      anchors.fill: parent
      focus: true
      Keys.onPressed: function(event) {
        var n = root.windows.length
        if (event.key === Qt.Key_Escape) root.dismiss()
        else if (n === 0) return
        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Tab) root.selectedIndex = (root.selectedIndex + 1) % n
        else if (event.key === Qt.Key_Left || event.key === Qt.Key_Backtab) root.selectedIndex = (root.selectedIndex - 1 + n) % n
        else if (event.key === Qt.Key_Down) root.selectedIndex = Math.min(n - 1, root.selectedIndex + grid.columns)
        else if (event.key === Qt.Key_Up) root.selectedIndex = Math.max(0, root.selectedIndex - grid.columns)
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) root.switchTo(root.windows[root.selectedIndex])
        else if (event.key === Qt.Key_Delete || event.key === Qt.Key_BackSpace) root.closeWindow(root.windows[root.selectedIndex])
        else return
        event.accepted = true
      }
    }

    Text {
      anchors.centerIn: parent
      visible: root.windows.length === 0
      text: strings.t("empty", "No open windows")
      color: root.textColor
      font.family: Style.font.menuFamily
      font.pixelSize: Style.font.title
    }

    // The cards, in as many columns as fit, centered above the desktop row.
    Flickable {
      id: scroller
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.top: parent.top
      anchors.bottom: desktopRow.top
      anchors.margins: Style.space(24)
      contentHeight: grid.height
      clip: true
      interactive: contentHeight > height

      Grid {
        id: grid
        readonly property int cardWidth: Math.min(Style.space(360), Math.max(Style.space(200), (scroller.width - Style.space(20)) / 2.2))
        readonly property int cardHeight: Math.round(cardWidth * 0.72) + Style.space(34)
        columns: Math.max(1, Math.floor((scroller.width + spacing) / (cardWidth + spacing)))
        spacing: Style.space(20)
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(0, (scroller.height - height) / 2)

        Repeater {
          model: root.windows

          Item {
            id: slot
            required property var modelData
            required property int index
            width: grid.cardWidth
            height: grid.cardHeight

            Item {
              id: card
              width: slot.width
              height: slot.height
              // Follows the finger while dragged.
              property real dragX: 0
              property real dragY: 0
              x: dragX
              y: dragY
              opacity: dragY < 0 ? Math.max(0.2, 1 + dragY / (slot.height * 1.2)) : 1
              z: drag.pressed ? 10 : 0
              Behavior on x { enabled: !drag.pressed; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
              Behavior on y { enabled: !drag.pressed; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

              Rectangle {
                id: frame
                anchors.fill: parent
                radius: root.radius
                color: root.cardColor
                border.width: slot.index === root.selectedIndex ? 3 : 0
                border.color: root.accent
              }

              Text {
                id: title
                anchors.left: parent.left
                anchors.right: badge.left
                anchors.top: parent.top
                anchors.margins: Style.space(10)
                text: slot.modelData.title || slot.modelData.appId
                elide: Text.ElideRight
                color: root.textColor
                font.family: Style.font.menuFamily
                font.pixelSize: Style.font.title
              }

              Text {
                id: badge
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Style.space(10)
                text: slot.modelData.minimized ? strings.t("minimized", "Minimized")
                                               : strings.t("desktop", "Desktop %1").arg(slot.modelData.workspaceId)
                color: root.textColor
                opacity: 0.6
                font.family: Style.font.menuFamily
                font.pixelSize: Style.font.bodySmall
              }

              // A still of the window.
              Item {
                id: shotArea
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.top: title.bottom
                anchors.margins: Style.space(10)
                clip: true

                ScreencopyView {
                  id: shot
                  anchors.centerIn: parent
                  captureSource: root.opened ? slot.modelData.toplevel : null
                  live: false
                  paintCursor: false
                  constraintSize: Qt.size(shotArea.width, shotArea.height)
                }

                Text {
                  anchors.centerIn: parent
                  visible: !shot.hasContent
                  text: slot.modelData.appId
                  color: root.textColor
                  opacity: 0.5
                  font.family: Style.font.menuFamily
                  font.pixelSize: Style.font.title
                }
              }

              MouseArea {
                id: drag
                anchors.fill: parent
                property point start
                property bool moved: false
                onPressed: function(mouse) {
                  start = Qt.point(mouse.x, mouse.y)
                  moved = false
                  root.selectedIndex = slot.index
                }
                onPositionChanged: function(mouse) {
                  var dx = mouse.x - start.x, dy = mouse.y - start.y
                  if (!moved && Math.abs(dx) + Math.abs(dy) < Style.space(12)) return
                  moved = true
                  card.dragX = dx
                  card.dragY = dy
                  var p = card.mapToItem(panel.contentItem, mouse.x, mouse.y)
                  root.hoveredDesktop = root.desktopAt(p.x, p.y)
                }
                onReleased: function(mouse) {
                  var desktop = root.hoveredDesktop
                  root.hoveredDesktop = 0
                  if (!moved) {
                    root.switchTo(slot.modelData)
                  } else if (desktop > 0) {
                    root.moveWindow(slot.modelData, desktop)
                  } else if (card.dragY < -slot.height * 0.35 && Math.abs(card.dragX) < slot.width * 0.6) {
                    root.closeWindow(slot.modelData)
                    return
                  }
                  card.dragX = 0
                  card.dragY = 0
                }
                onCanceled: { card.dragX = 0; card.dragY = 0; root.hoveredDesktop = 0 }
              }
            }
          }
        }
      }
    }

    Text {
      id: hint
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.bottom: desktopRow.top
      anchors.bottomMargin: Style.space(8)
      visible: root.windows.length > 0
      text: strings.t("hint", "Tap to switch · swipe up to close · drag onto a desktop to move")
      color: root.textColor
      opacity: 0.6
      font.family: Style.font.menuFamily
      font.pixelSize: Style.font.bodySmall
    }

    // Drop targets for moving a window to another desktop.
    Row {
      id: desktopRow
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.bottom: parent.bottom
      anchors.bottomMargin: Style.space(24)
      spacing: Style.space(14)

      Repeater {
        id: desktopRepeater
        model: root.desktopCount

        Rectangle {
          required property int index
          readonly property int desktop: index + 1
          readonly property bool current: Hyprland.focusedMonitor && Hyprland.focusedMonitor.activeWorkspace
                                          && Hyprland.focusedMonitor.activeWorkspace.id === desktop
          width: Style.space(64)
          height: Style.space(48)
          radius: root.radius
          color: root.hoveredDesktop === desktop ? root.accent : root.cardColor
          border.width: current ? 2 : 0
          border.color: root.accent

          Text {
            anchors.centerIn: parent
            text: parent.desktop
            color: root.hoveredDesktop === parent.desktop ? root.accentText : root.textColor
            font.family: Style.font.menuFamily
            font.pixelSize: Style.font.title
          }

          // Tapping a desktop goes there.
          MouseArea {
            anchors.fill: parent
            onClicked: {
              root.dismiss()
              root.dispatch(["hl.dsp.focus({ workspace = \"" + parent.desktop + "\" })"])
            }
          }
        }
      }
    }
  }
}

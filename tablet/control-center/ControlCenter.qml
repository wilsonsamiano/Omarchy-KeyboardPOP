import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// An iPad-style Control Center, pulled down from the top-right corner.
//   tap a tile        toggle Wi-Fi, Bluetooth, rotation lock, or night light
//   drag a slider     set brightness or volume
//   tap the speaker   mute or unmute
//   tap outside / Esc dismiss
// Summoned with `omarchy-shell shell summon touch.control-center`, which the
// touchscreen gesture (swipe down from the top-right corner) runs.
Item {
  id: root

  property var shell: null
  property var manifest: null
  property bool opened: false

  // What the system reports; tiles flip right away when tapped and the next
  // read confirms or corrects them.
  property bool wifiOn: false
  property bool bluetoothOn: false
  property bool rotationLocked: false
  property bool nightLightOn: false
  property real brightness: 0.5
  property real volume: 0.5
  property bool muted: false

  readonly property color scrim: Commons.Color.menu.scrim
  readonly property color cardColor: Commons.Color.menu.background
  readonly property color textColor: Commons.Color.menu.text
  readonly property color accent: Commons.Color.accent
  readonly property color accentText: Commons.Color.menu.background
  readonly property color trackColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.22)
  readonly property int radius: Style.cornerRadius
  readonly property string iconFont: Style.font.family

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }

  function open(payloadJson) {
    root.opened = true
    root.refresh()
    Qt.callLater(function() { keyCatcher.forceActiveFocus() })
  }

  function close() {
    root.opened = false
  }

  function dismiss() {
    root.opened = false
    if (root.shell && typeof root.shell.hide === "function")
      root.shell.hide((root.manifest && root.manifest.id) || "touch.control-center")
  }

  function toggle() {
    if (root.opened) root.dismiss()
    else root.open("{}")
  }

  function run(command) {
    Util.execDetached(command)
  }

  function refresh() {
    if (!status.running) status.running = true
  }

  // One read of everything the panel shows, as key=value lines, so opening
  // the panel costs a single process instead of six.
  Process {
    id: status
    command: ["sh", "-c",
      "echo wifi=$(nmcli radio wifi 2>/dev/null);" +
      "echo bt=$(bluetoothctl show 2>/dev/null | sed -n 's/.*Powered: //p' | head -n1);" +
      "echo vol=$(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null);" +
      "echo bright=$(brightnessctl -m 2>/dev/null | cut -d, -f4 | tr -d %);" +
      "echo rot=$(cat \"${XDG_STATE_HOME:-$HOME/.local/state}/autorotate/status\" 2>/dev/null);" +
      "echo night=$(omarchy-toggle-nightlight --status 2>/dev/null | jq -r .enabled 2>/dev/null)"]
    stdout: StdioCollector {
      onStreamFinished: root.parseStatus(text)
    }
  }

  function parseStatus(text) {
    var lines = String(text || "").split("\n")
    for (var i = 0; i < lines.length; i++) {
      var eq = lines[i].indexOf("=")
      if (eq < 0) continue
      var key = lines[i].slice(0, eq)
      var value = lines[i].slice(eq + 1).trim()
      if (key === "wifi") root.wifiOn = value === "enabled"
      else if (key === "bt") root.bluetoothOn = value === "yes"
      else if (key === "rot") root.rotationLocked = value === "locked"
      else if (key === "night") root.nightLightOn = value === "true"
      else if (key === "bright" && value !== "" && !brightnessSlider.dragging)
        root.brightness = Math.max(0, Math.min(1, Number(value) / 100))
      else if (key === "vol" && value !== "") {
        var m = value.match(/([0-9.]+)/)
        if (m && !volumeSlider.dragging) root.volume = Math.max(0, Math.min(1, Number(m[1])))
        root.muted = value.indexOf("MUTED") !== -1
      }
    }
  }

  // Changes made elsewhere (the bar, a key) show up while the panel is open.
  Timer {
    interval: 2000
    repeat: true
    running: root.opened
    onTriggered: root.refresh()
  }

  function toggleWifi() {
    root.wifiOn = !root.wifiOn
    root.run("nmcli radio wifi " + (root.wifiOn ? "on" : "off"))
  }

  function toggleBluetooth() {
    root.bluetoothOn = !root.bluetoothOn
    root.run((root.bluetoothOn ? "rfkill unblock bluetooth; " : "") + "bluetoothctl power " + (root.bluetoothOn ? "on" : "off"))
  }

  function toggleRotationLock() {
    root.rotationLocked = !root.rotationLocked
    root.run("autorotate lock")
  }

  function toggleNightLight() {
    root.nightLightOn = !root.nightLightOn
    root.run("omarchy-toggle-nightlight")
  }

  function setBrightness(value) {
    root.brightness = value
    root.run("brightnessctl -q set " + Math.max(1, Math.round(value * 100)) + "%")
  }

  function setVolume(value) {
    root.volume = value
    root.run("wpctl set-volume @DEFAULT_AUDIO_SINK@ " + value.toFixed(2) + (root.muted ? " && wpctl set-mute @DEFAULT_AUDIO_SINK@ 0" : ""))
    root.muted = false
  }

  function toggleMute() {
    root.muted = !root.muted
    root.run("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle")
  }

  OverlayWindow {
    id: panel
    shown: root.opened
    WlrLayershell.namespace: "omarchy-control-center"

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
        if (event.key === Qt.Key_Escape) {
          root.dismiss()
          event.accepted = true
        }
      }
    }

    // The card hangs from the top-right corner, where the swipe began.
    Rectangle {
      id: card
      readonly property real gap: Style.space(12)
      readonly property real tileSize: Style.space(76)

      anchors.top: parent.top
      anchors.right: parent.right
      anchors.topMargin: Style.space(16)
      anchors.rightMargin: Style.space(16)
      width: tileSize * 4 + gap * 5
      height: content.implicitHeight + gap * 2
      radius: Math.max(root.radius, Style.space(6))
      color: root.cardColor
      border.color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.12)
      border.width: 1

      opacity: root.opened ? 1 : 0
      transform: Translate { y: root.opened ? 0 : -Style.space(24) }
      Behavior on opacity { NumberAnimation { duration: Style.duration(160) } }

      // Taps on the card itself don't fall through to the dismissing scrim.
      MouseArea { anchors.fill: parent }

      Column {
        id: content
        x: card.gap
        y: card.gap
        width: card.width - card.gap * 2
        spacing: card.gap

        Row {
          spacing: card.gap
          Tile {
            icon: root.wifiOn ? "󰖩" : "󰖪"
            label: strings.t("wifi", "Wi-Fi")
            active: root.wifiOn
            onTapped: root.toggleWifi()
          }
          Tile {
            icon: root.bluetoothOn ? "󰂯" : "󰂲"
            label: strings.t("bluetooth", "Bluetooth")
            active: root.bluetoothOn
            onTapped: root.toggleBluetooth()
          }
          Tile {
            icon: root.rotationLocked ? "󰑸" : "󰑵"
            label: strings.t("rotationLock", "Rotation lock")
            active: root.rotationLocked
            onTapped: root.toggleRotationLock()
          }
          Tile {
            icon: "󰖔"
            label: strings.t("nightLight", "Night light")
            active: root.nightLightOn
            onTapped: root.toggleNightLight()
          }
        }

        SliderRow {
          id: brightnessRow
          icon: "󰃠"
          label: strings.t("brightness", "Brightness")
          PanelSlider {
            id: brightnessSlider
            width: brightnessRow.sliderWidth
            anchors.verticalCenter: parent.verticalCenter
            minimum: 0.01
            maximum: 1
            step: 0.05
            value: root.brightness
            fillColor: root.accent
            trackColor: root.trackColor
            knobColor: root.textColor
            onMoved: function(v) { root.setBrightness(v) }
            onReleased: function(v) { root.setBrightness(v) }
          }
        }

        SliderRow {
          id: volumeRow
          icon: root.muted || root.volume <= 0 ? "󰝟" : "󰕾"
          label: root.muted ? strings.t("unmute", "Unmute") : strings.t("mute", "Mute")
          onIconTapped: root.toggleMute()
          PanelSlider {
            id: volumeSlider
            width: volumeRow.sliderWidth
            anchors.verticalCenter: parent.verticalCenter
            minimum: 0
            maximum: 1
            step: 0.05
            value: root.volume
            opacity: root.muted ? 0.5 : 1
            fillColor: root.accent
            trackColor: root.trackColor
            knobColor: root.textColor
            onMoved: function(v) { root.setVolume(v) }
            onReleased: function(v) { root.setVolume(v) }
          }
        }

        Row {
          spacing: card.gap
          WideTile {
            icon: "󰌌"
            label: strings.t("keyboard", "Keyboard")
            onTapped: { root.dismiss(); root.run("keyboardpop toggle") }
          }
          WideTile {
            icon: "󰐥"
            label: strings.t("power", "Power")
            onTapped: { root.dismiss(); root.run("omarchy-menu summon power") }
          }
        }
      }
    }
  }

  // A square toggle: filled with the accent while on.
  component Tile: Rectangle {
    id: tile
    property string icon: ""
    property string label: ""
    property bool active: false
    signal tapped()

    width: card.tileSize
    height: card.tileSize
    radius: Math.max(root.radius, Style.space(4))
    color: active ? root.accent : Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, tileArea.pressed ? 0.18 : 0.08)
    scale: tileArea.pressed ? 0.94 : 1
    Behavior on scale { NumberAnimation { duration: Style.duration(90) } }

    Column {
      anchors.centerIn: parent
      width: parent.width - Style.space(6)
      spacing: Style.space(4)
      Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: tile.icon
        color: tile.active ? root.accentText : root.textColor
        font.family: root.iconFont
        font.pixelSize: Style.font.title * 2
      }
      Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: tile.label
        color: tile.active ? root.accentText : root.textColor
        font.family: Style.font.menuFamily
        font.pixelSize: Style.font.caption
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
      }
    }

    MouseArea {
      id: tileArea
      anchors.fill: parent
      onClicked: tile.tapped()
    }
  }

  // A two-wide action button.
  component WideTile: Rectangle {
    id: wide
    property string icon: ""
    property string label: ""
    signal tapped()

    width: card.tileSize * 2 + card.gap
    height: Style.space(52)
    radius: Math.max(root.radius, Style.space(4))
    color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, wideArea.pressed ? 0.18 : 0.08)
    scale: wideArea.pressed ? 0.96 : 1
    Behavior on scale { NumberAnimation { duration: Style.duration(90) } }

    Row {
      anchors.centerIn: parent
      spacing: Style.space(8)
      Text {
        anchors.verticalCenter: parent.verticalCenter
        text: wide.icon
        color: root.textColor
        font.family: root.iconFont
        font.pixelSize: Style.font.title * 1.5
      }
      Text {
        anchors.verticalCenter: parent.verticalCenter
        text: wide.label
        color: root.textColor
        font.family: Style.font.menuFamily
        font.pixelSize: Style.font.body
      }
    }

    MouseArea {
      id: wideArea
      anchors.fill: parent
      onClicked: wide.tapped()
    }
  }

  // An icon followed by a full-width slider, on a tile-colored strip tall
  // enough to drag comfortably with a finger.
  component SliderRow: Rectangle {
    id: sliderRow
    property string icon: ""
    property string label: ""
    readonly property real iconWidth: Style.space(44)
    readonly property real sliderWidth: width - iconWidth - Style.space(28)
    default property alias sliderData: sliderHolder.data
    signal iconTapped()

    width: content.width
    height: Style.space(56)
    radius: Math.max(root.radius, Style.space(4))
    color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.08)

    Text {
      id: iconText
      x: Style.space(10)
      width: sliderRow.iconWidth
      anchors.verticalCenter: parent.verticalCenter
      horizontalAlignment: Text.AlignHCenter
      text: sliderRow.icon
      color: root.textColor
      font.family: root.iconFont
      font.pixelSize: Style.font.title * 1.6
      Accessible.name: sliderRow.label
      MouseArea {
        anchors.fill: parent
        anchors.margins: -Style.space(6)
        onClicked: sliderRow.iconTapped()
      }
    }

    Item {
      id: sliderHolder
      anchors.left: iconText.right
      anchors.leftMargin: Style.space(8)
      anchors.right: parent.right
      anchors.rightMargin: Style.space(14)
      anchors.top: parent.top
      anchors.bottom: parent.bottom
    }
  }
}

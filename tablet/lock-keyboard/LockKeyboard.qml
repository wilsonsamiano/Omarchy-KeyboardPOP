import QtQuick
import qs.Commons
import qs.Commons as Commons

// A touch keyboard drawn inside the lock screen. While the session is
// locked the compositor shows nothing but the lock surface, so an
// on-screen keyboard app (KeyboardPOP) can't appear there; this one lives
// in the lock itself. It only emits what was tapped: the lock view owns
// the password.
Item {
  id: root

  property bool enabled: true
  signal typed(string text)
  signal backspace()
  signal enter()
  signal hideRequested()

  property bool shifted: false
  property bool capsLock: false
  property bool symbols: false

  readonly property color base: Commons.Color.lock.background
  readonly property color keyFace: Util.alpha(Commons.Color.lock.text, 0.14)
  readonly property color actionFace: Util.alpha(Commons.Color.lock.text, 0.07)
  readonly property color pressedFace: Util.alpha(Commons.Color.lock.text, 0.3)
  readonly property color label: Commons.Color.lock.text
  readonly property color accent: Commons.Color.lock.borderActive

  readonly property int keyHeight: 58
  readonly property int gap: 7
  readonly property real unit: (width - gap * 11) / 10

  readonly property var letterRows: [
    ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
    ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
    ["z", "x", "c", "v", "b", "n", "m"]
  ]
  readonly property var symbolRows: [
    ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
    ["@", "#", "$", "%", "&", "*", "-", "+", "(", ")"],
    ["!", "\"", "'", ":", ";", "/", "?", "_", "="]
  ]

  implicitHeight: gap * 5 + keyHeight * 4

  function keyText(key) {
    if (symbols) return key
    return (shifted || capsLock) ? key.toUpperCase() : key
  }

  function press(key) {
    if (!root.enabled) return
    root.typed(keyText(key))
    if (shifted && !capsLock) shifted = false
  }

  Rectangle {
    anchors.fill: parent
    radius: Style.cornerRadius + 6
    color: Util.alpha(root.base, 0.92)
    border.width: 1
    border.color: Util.alpha(root.label, 0.12)
  }

  component Key: Rectangle {
    id: key
    property string text: ""
    property string glyph: ""
    property bool action: false
    property bool active: false
    property real units: 1
    readonly property bool down: tap.pressed
    signal tapped()
    signal held()
    width: root.unit * units + root.gap * (units - 1)
    height: root.keyHeight
    radius: Math.max(6, Style.cornerRadius)
    color: tap.pressed ? root.pressedFace : key.active ? root.accent : key.action ? root.actionFace : root.keyFace
    opacity: root.enabled ? 1 : 0.45

    Text {
      anchors.centerIn: parent
      textFormat: Text.PlainText
      text: key.glyph || key.text
      color: key.active ? Commons.Color.lock.background : root.label
      font.family: Style.font.family
      font.pixelSize: key.glyph ? 24 : 22
    }
    TapHandler {
      id: tap
      enabled: root.enabled
      onTapped: key.tapped()
      onLongPressed: key.held()
    }
  }

  Column {
    anchors.fill: parent
    anchors.margins: root.gap
    spacing: root.gap

    // Character rows.
    Repeater {
      model: root.symbols ? root.symbolRows : root.letterRows
      delegate: Row {
        required property var modelData
        required property int index
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.gap

        // Shift sits at the start of the last letter row.
        Key {
          visible: !root.symbols && index === 2
          units: 1.5
          action: true
          glyph: root.capsLock ? "⇪" : "⇧"
          active: root.shifted || root.capsLock
          onTapped: {
            if (root.capsLock) { root.capsLock = false; root.shifted = false }
            else root.shifted = !root.shifted
          }
          onHeld: { root.capsLock = true; root.shifted = false }
        }
        Repeater {
          model: modelData
          delegate: Key {
            required property string modelData
            text: root.keyText(modelData)
            onTapped: root.press(modelData)
          }
        }
        Key {
          id: deleteKey
          visible: index === 2
          units: 1.5
          action: true
          glyph: "⌫"
          onTapped: root.backspace()
          onHeld: repeatDelete.start()
          // Holding Delete keeps deleting until the finger lifts.
          Timer {
            id: repeatDelete
            interval: 70
            repeat: true
            onTriggered: { if (deleteKey.down) root.backspace(); else stop() }
          }
        }
      }
    }

    // Bottom row.
    Row {
      anchors.horizontalCenter: parent.horizontalCenter
      spacing: root.gap
      Key {
        units: 1.5
        action: true
        text: root.symbols ? "abc" : "123"
        onTapped: root.symbols = !root.symbols
      }
      Key {
        units: 1
        action: true
        glyph: "󰌐"
        onTapped: root.hideRequested()
      }
      Key {
        units: 4.5
        text: "space"
        onTapped: if (root.enabled) root.typed(" ")
      }
      Key {
        units: 3
        action: true
        active: true
        text: "Unlock ⏎"
        onTapped: if (root.enabled) root.enter()
      }
    }
  }
}

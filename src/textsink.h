#pragma once

#include <QString>
#include <optional>

// Where typed text goes. On the desktop that is Wayland (the input-method
// protocol when KeyboardPOP is the input method, otherwise a virtual keyboard);
// in tests it is a string.
class TextSink {
public:
    enum class Key { Return, BackSpace, Tab, Left, Right, Up, Down };

    virtual ~TextSink() = default;

    virtual void commitText(const QString &text) = 0;
    // Deletes this many characters (grapheme clusters) before the cursor.
    virtual void deleteBefore(int characters) = 0;
    virtual void sendKey(Key key, bool control = false) = 0;

    // Modifiers for sendCombo, matching the X11/XKB modifier bits.
    enum Modifier { ShiftModifier = 1, ControlModifier = 4, AltModifier = 8, SuperModifier = 64 };
    // Presses a key with modifiers held, for shortcuts and for keys that
    // aren't text (Escape, the arrows). `key` is one character or an XKB
    // keysym name ("Escape", "Tab", "Left", "space", "Return", ...).
    virtual void sendCombo(const QString &key, int modifiers) = 0;

    // The text before the cursor, or nothing when it isn't known (the
    // cursor moved somewhere this keyboard can't see).
    virtual std::optional<QString> textBeforeCursor() const = 0;
    virtual bool isPassword() const { return false; }
    // False in e-mail, URL, and number fields.
    virtual bool wantsAutoCapitalization() const { return true; }
};

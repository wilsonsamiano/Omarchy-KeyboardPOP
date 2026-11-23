#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

#include "textsink.h"

struct wl_display;
struct wl_registry;
struct wl_seat;
struct wl_callback;
struct zwp_input_method_manager_v2;
struct zwp_input_method_v2;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;

// Types into other apps over Wayland. When KeyboardPOP is the seat's input
// method (input-method-unstable-v2) it learns when a text field is focused,
// what is around the cursor, and whether the field is a password, and it
// commits text straight into the field. Only one input method can hold a
// seat, so when another one (fcitx5) has it, everything goes through a
// virtual keyboard (virtual-keyboard-unstable-v1) instead, which works in
// any app but can't see the field.
class WaylandInput : public QObject, public TextSink {
    Q_OBJECT

public:
    enum class Mode { Connecting, InputMethod, VirtualKeyboard, Unavailable };
    Q_ENUM(Mode)

    explicit WaylandInput(wl_display *display, QObject *parent = nullptr);
    ~WaylandInput() override;

    Mode mode() const { return m_mode; }
    bool fieldActive() const { return m_active; }

    void commitText(const QString &text) override;
    void deleteBefore(int characters) override;
    void sendKey(Key key, bool control = false) override;
    std::optional<QString> textBeforeCursor() const override;
    bool isPassword() const override;
    bool wantsAutoCapitalization() const override;

    // The keyboard was shown by hand: start from an empty, known context.
    void resetContext();

    // KeyboardPOP itself pressed a key through the virtual keyboard within
    // the last `ms` milliseconds, so a key the compositor saw then was ours.
    bool sentKeyWithin(qint64 ms) const { return m_lastKey.isValid() && m_lastKey.elapsed() <= ms; }

signals:
    void modeChanged();
    void fieldActivated();
    void fieldDeactivated();
    void contextChanged();

private:
    // Wayland callbacks.
    static void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    static void registryGlobalRemove(void *data, wl_registry *registry, uint32_t name);
    static void syncDone(void *data, wl_callback *callback, uint32_t serial);
    static void imActivate(void *data, zwp_input_method_v2 *im);
    static void imDeactivate(void *data, zwp_input_method_v2 *im);
    static void imSurroundingText(void *data, zwp_input_method_v2 *im, const char *text, uint32_t cursor, uint32_t anchor);
    static void imTextChangeCause(void *data, zwp_input_method_v2 *im, uint32_t cause);
    static void imContentType(void *data, zwp_input_method_v2 *im, uint32_t hint, uint32_t purpose);
    static void imDone(void *data, zwp_input_method_v2 *im);
    static void imUnavailable(void *data, zwp_input_method_v2 *im);

    void setUp();
    void setMode(Mode mode);
    bool committingThroughInputMethod() const;
    void typeCharacter(uint32_t codepoint);
    void pressKeysym(uint32_t keysym, bool control = false);
    int keycodeFor(uint32_t keysym);
    void uploadKeymap();
    void flush();
    void shadowDelete(int characters);

    wl_display *m_display;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    wl_callback *m_sync = nullptr;
    zwp_input_method_manager_v2 *m_imManager = nullptr;
    zwp_input_method_v2 *m_im = nullptr;
    zwp_virtual_keyboard_manager_v1 *m_vkManager = nullptr;
    zwp_virtual_keyboard_v1 *m_vk = nullptr;
    Mode m_mode = Mode::Connecting;

    // Input-method state: pending until `done`, then current.
    struct State {
        bool active = false;
        bool surroundingKnown = false;
        QString before;
        uint32_t hint = 0, purpose = 0;
    } m_pendingState, m_state;
    bool m_active = false;
    uint32_t m_serial = 0;

    // What this keyboard typed since it last knew the cursor: the only
    // context there is without the input method.
    QString m_shadow;
    bool m_shadowKnown = true;

    QVector<uint32_t> m_keysyms; // keycode - 9 -> keysym
    QElapsedTimer m_clock;
    QElapsedTimer m_lastKey;
};

#include "waylandinput.h"

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

#include <QTextBoundaryFinder>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace {

// text-input-unstable-v3 content purposes and hints, which input-method-v2
// passes through unchanged.
enum : uint32_t {
    PurposeDigits = 2, PurposeNumber = 3, PurposePhone = 4, PurposeUrl = 5, PurposeEmail = 6,
    PurposePassword = 8, PurposePin = 9, PurposeTerminal = 13,
    HintLowercase = 0x8, HintHiddenText = 0x40, HintSensitiveData = 0x80,
};

// The X11 Control modifier bit, which "complete" compat maps to Control_L.
constexpr uint32_t ControlMask = 1 << 2;

// Removes the last n grapheme clusters, so an emoji or an accent built
// from two code points goes in one press.
QString dropGraphemes(const QString &text, int n) {
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.toEnd();
    for (int i = 0; i < n && finder.position() > 0; ++i)
        finder.toPreviousBoundary();
    return text.left(std::max<qsizetype>(0, finder.position()));
}

} // namespace

#define LISTENER(name) &WaylandInput::name

WaylandInput::WaylandInput(wl_display *display, QObject *parent) : QObject(parent), m_display(display) {
    m_clock.start();
    if (!m_display) {
        setMode(Mode::Unavailable);
        return;
    }
    static const wl_registry_listener registryListener = {LISTENER(registryGlobal), LISTENER(registryGlobalRemove)};
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &registryListener, this);
    // Every global is announced before this sync answers, so its callback
    // is where the protocols KeyboardPOP needs are known to be there or not.
    static const wl_callback_listener syncListener = {LISTENER(syncDone)};
    m_sync = wl_display_sync(m_display);
    wl_callback_add_listener(m_sync, &syncListener, this);
    flush();
}

WaylandInput::~WaylandInput() {
    if (m_vk)
        zwp_virtual_keyboard_v1_destroy(m_vk);
    if (m_im)
        zwp_input_method_v2_destroy(m_im);
    if (m_vkManager)
        zwp_virtual_keyboard_manager_v1_destroy(m_vkManager);
    if (m_imManager)
        zwp_input_method_manager_v2_destroy(m_imManager);
    if (m_sync)
        wl_callback_destroy(m_sync);
    if (m_seat)
        wl_seat_destroy(m_seat);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display)
        wl_display_flush(m_display);
}

void WaylandInput::registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
    auto *self = static_cast<WaylandInput *>(data);
    if (!std::strcmp(interface, wl_seat_interface.name) && !self->m_seat)
        self->m_seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    else if (!std::strcmp(interface, zwp_input_method_manager_v2_interface.name))
        self->m_imManager = static_cast<zwp_input_method_manager_v2 *>(
            wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1));
    else if (!std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name))
        self->m_vkManager = static_cast<zwp_virtual_keyboard_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
}

void WaylandInput::registryGlobalRemove(void *, wl_registry *, uint32_t) {}

void WaylandInput::syncDone(void *data, wl_callback *callback, uint32_t) {
    auto *self = static_cast<WaylandInput *>(data);
    wl_callback_destroy(callback);
    self->m_sync = nullptr;
    self->setUp();
}

void WaylandInput::setUp() {
    if (!m_seat || !m_vkManager) {
        setMode(Mode::Unavailable);
        return;
    }
    m_vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(m_vkManager, m_seat);

    // Printable ASCII and the editing keys are there from the start, so
    // ordinary typing never changes the keymap; anything else is added the
    // first time it's typed.
    m_keysyms = {XKB_KEY_Return, XKB_KEY_BackSpace, XKB_KEY_Tab, XKB_KEY_Left, XKB_KEY_Right,
                 XKB_KEY_Up, XKB_KEY_Down, XKB_KEY_Control_L};
    for (uint32_t c = 0x20; c < 0x7f; ++c)
        m_keysyms << xkb_utf32_to_keysym(c);
    uploadKeymap();

    if (m_imManager) {
        static const zwp_input_method_v2_listener imListener = {
            LISTENER(imActivate), LISTENER(imDeactivate), LISTENER(imSurroundingText), LISTENER(imTextChangeCause),
            LISTENER(imContentType), LISTENER(imDone), LISTENER(imUnavailable),
        };
        m_im = zwp_input_method_manager_v2_get_input_method(m_imManager, m_seat);
        zwp_input_method_v2_add_listener(m_im, &imListener, this);
        // Hold off on the mode until the compositor says whether the seat
        // already has an input method; `unavailable` comes right away if so.
        static const wl_callback_listener settled = {[](void *data, wl_callback *callback, uint32_t) {
            wl_callback_destroy(callback);
            auto *self = static_cast<WaylandInput *>(data);
            if (self->m_mode == Mode::Connecting)
                self->setMode(Mode::InputMethod);
        }};
        wl_callback_add_listener(wl_display_sync(m_display), &settled, this);
    } else {
        setMode(Mode::VirtualKeyboard);
    }
    flush();
}

void WaylandInput::setMode(Mode mode) {
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit modeChanged();
}

void WaylandInput::imActivate(void *data, zwp_input_method_v2 *) {
    auto *self = static_cast<WaylandInput *>(data);
    // Activation resets everything the compositor said about the old field.
    self->m_pendingState = State{};
    self->m_pendingState.active = true;
}

void WaylandInput::imDeactivate(void *data, zwp_input_method_v2 *) {
    static_cast<WaylandInput *>(data)->m_pendingState.active = false;
}

void WaylandInput::imSurroundingText(void *data, zwp_input_method_v2 *, const char *text, uint32_t cursor, uint32_t) {
    auto *self = static_cast<WaylandInput *>(data);
    const QByteArray bytes(text);
    self->m_pendingState.surroundingKnown = true;
    self->m_pendingState.before = QString::fromUtf8(bytes.left(std::min<qsizetype>(cursor, bytes.size())));
}

void WaylandInput::imTextChangeCause(void *, zwp_input_method_v2 *, uint32_t) {}

void WaylandInput::imContentType(void *data, zwp_input_method_v2 *, uint32_t hint, uint32_t purpose) {
    auto *self = static_cast<WaylandInput *>(data);
    self->m_pendingState.hint = hint;
    self->m_pendingState.purpose = purpose;
}

void WaylandInput::imDone(void *data, zwp_input_method_v2 *) {
    auto *self = static_cast<WaylandInput *>(data);
    ++self->m_serial;
    const bool wasActive = self->m_active;
    // Surrounding text and content type are only sent when they change, so
    // a `done` without them keeps what was known.
    State next = self->m_pendingState;
    if (next.active && wasActive && !next.surroundingKnown && self->m_state.surroundingKnown) {
        next.surroundingKnown = true;
        next.before = self->m_state.before;
    }
    self->m_state = next;
    self->m_pendingState.surroundingKnown = false;
    self->m_active = next.active;
    if (self->m_active && !wasActive) {
        self->m_shadow.clear();
        self->m_shadowKnown = true;
        emit self->fieldActivated();
    } else if (!self->m_active && wasActive) {
        emit self->fieldDeactivated();
    } else {
        emit self->contextChanged();
    }
}

void WaylandInput::imUnavailable(void *data, zwp_input_method_v2 *im) {
    auto *self = static_cast<WaylandInput *>(data);
    zwp_input_method_v2_destroy(im);
    self->m_im = nullptr;
    self->setMode(Mode::VirtualKeyboard);
}

bool WaylandInput::committingThroughInputMethod() const {
    return m_im && m_active;
}

std::optional<QString> WaylandInput::textBeforeCursor() const {
    if (committingThroughInputMethod() && m_state.surroundingKnown)
        return m_state.before;
    if (m_shadowKnown)
        return m_shadow;
    return std::nullopt;
}

bool WaylandInput::isPassword() const {
    if (!committingThroughInputMethod())
        return false;
    return m_state.purpose == PurposePassword || m_state.purpose == PurposePin
           || (m_state.hint & (HintHiddenText | HintSensitiveData));
}

bool WaylandInput::wantsAutoCapitalization() const {
    if (!committingThroughInputMethod())
        return true;
    switch (m_state.purpose) {
    case PurposeDigits: case PurposeNumber: case PurposePhone: case PurposeUrl:
    case PurposeEmail: case PurposePassword: case PurposePin: case PurposeTerminal:
        return false;
    default:
        return !(m_state.hint & HintLowercase);
    }
}

void WaylandInput::resetContext() {
    m_shadow.clear();
    m_shadowKnown = true;
}

void WaylandInput::shadowDelete(int characters) {
    if (committingThroughInputMethod() && m_state.surroundingKnown)
        m_state.before = dropGraphemes(m_state.before, characters);
    m_shadow = dropGraphemes(m_shadow, characters);
}

void WaylandInput::commitText(const QString &text) {
    if (text.isEmpty() || m_mode == Mode::Unavailable || m_mode == Mode::Connecting)
        return;
    // Newlines are Return presses, so single-line fields submit as usual.
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            sendKey(Key::Return);
        const QString &line = lines[i];
        if (line.isEmpty())
            continue;
        if (committingThroughInputMethod()) {
            zwp_input_method_v2_commit_string(m_im, line.toUtf8().constData());
            zwp_input_method_v2_commit(m_im, m_serial);
            if (m_state.surroundingKnown)
                m_state.before += line;
        } else {
            for (uint32_t codepoint : line.toUcs4())
                typeCharacter(codepoint);
        }
        m_shadow += line;
    }
    flush();
}

void WaylandInput::deleteBefore(int characters) {
    if (characters <= 0 || m_mode == Mode::Unavailable || m_mode == Mode::Connecting)
        return;
    if (committingThroughInputMethod() && m_state.surroundingKnown) {
        // The protocol counts bytes of UTF-8.
        const QString kept = dropGraphemes(m_state.before, characters);
        const int bytes = m_state.before.toUtf8().size() - kept.toUtf8().size();
        zwp_input_method_v2_delete_surrounding_text(m_im, bytes, 0);
        zwp_input_method_v2_commit(m_im, m_serial);
        m_state.before = kept;
        m_shadow = dropGraphemes(m_shadow, characters);
        flush();
        return;
    }
    for (int i = 0; i < characters; ++i)
        pressKeysym(XKB_KEY_BackSpace);
    shadowDelete(characters);
    flush();
}

void WaylandInput::sendKey(Key key, bool control) {
    if (!m_vk || m_mode == Mode::Unavailable || m_mode == Mode::Connecting)
        return;
    switch (key) {
    case Key::Return:
        pressKeysym(XKB_KEY_Return);
        m_shadow += '\n';
        if (committingThroughInputMethod() && m_state.surroundingKnown)
            m_state.before += '\n';
        break;
    case Key::BackSpace:
        pressKeysym(XKB_KEY_BackSpace, control);
        if (control) {
            m_shadowKnown = false;
            m_state.surroundingKnown = false;
        } else {
            shadowDelete(1);
        }
        break;
    default: {
        const uint32_t keysym = key == Key::Tab ? XKB_KEY_Tab : key == Key::Left ? XKB_KEY_Left
                              : key == Key::Right ? XKB_KEY_Right : key == Key::Up ? XKB_KEY_Up : XKB_KEY_Down;
        pressKeysym(keysym, control);
        // The cursor went somewhere only the app knows.
        m_shadow.clear();
        m_shadowKnown = false;
        break;
    }
    }
    flush();
}

void WaylandInput::typeCharacter(uint32_t codepoint) {
    const uint32_t keysym = xkb_utf32_to_keysym(codepoint);
    if (keysym != XKB_KEY_NoSymbol)
        pressKeysym(keysym);
}

int WaylandInput::keycodeFor(uint32_t keysym) {
    int index = int(m_keysyms.indexOf(keysym));
    if (index < 0) {
        // Keycodes run out at 255; start over from the fixed keys if needed.
        if (m_keysyms.size() >= 255 - 9)
            m_keysyms.resize(8 + 95);
        m_keysyms << keysym;
        index = m_keysyms.size() - 1;
        uploadKeymap();
    }
    return index + 9;
}

void WaylandInput::pressKeysym(uint32_t keysym, bool control) {
    if (!m_vk)
        return;
    const int keycode = keycodeFor(keysym);
    const uint32_t time = uint32_t(m_clock.elapsed());
    if (control)
        zwp_virtual_keyboard_v1_modifiers(m_vk, ControlMask, 0, 0, 0);
    // The protocol takes evdev codes, which are XKB keycodes minus 8.
    zwp_virtual_keyboard_v1_key(m_vk, time, keycode - 8, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(m_vk, time, keycode - 8, WL_KEYBOARD_KEY_STATE_RELEASED);
    if (control)
        zwp_virtual_keyboard_v1_modifiers(m_vk, 0, 0, 0, 0);
}

void WaylandInput::uploadKeymap() {
    if (!m_vk)
        return;
    // One key per symbol, each on its own level-one key, like wtype: an
    // uppercase letter or an emoji is its own key, so no modifier state is
    // needed to type it.
    QByteArray keycodes, symbols;
    for (int i = 0; i < m_keysyms.size(); ++i) {
        char name[64];
        if (xkb_keysym_get_name(m_keysyms[i], name, sizeof name) < 0)
            std::snprintf(name, sizeof name, "0x%x", m_keysyms[i]);
        keycodes += QByteArray("    <K") + QByteArray::number(i) + "> = " + QByteArray::number(i + 9) + ";\n";
        symbols += QByteArray("    key <K") + QByteArray::number(i) + "> {[ " + name + " ]};\n";
    }
    const int control = int(m_keysyms.indexOf(uint32_t(XKB_KEY_Control_L)));
    if (control >= 0)
        symbols += QByteArray("    modifier_map Control { <K") + QByteArray::number(control) + "> };\n";
    const QByteArray keymap = "xkb_keymap {\n"
                              "  xkb_keycodes \"keyboardpop\" {\n    minimum = 8;\n    maximum = 255;\n"
                              + keycodes + "  };\n"
                              "  xkb_types \"keyboardpop\" { include \"complete\" };\n"
                              "  xkb_compatibility \"keyboardpop\" { include \"complete\" };\n"
                              "  xkb_symbols \"keyboardpop\" {\n" + symbols + "  };\n"
                              "};\n";

    const int fd = memfd_create("keyboardpop-keymap", MFD_CLOEXEC);
    if (fd < 0)
        return;
    const size_t size = size_t(keymap.size()) + 1; // with its terminating NUL
    if (write(fd, keymap.constData(), size) == ssize_t(size))
        zwp_virtual_keyboard_v1_keymap(m_vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, uint32_t(size));
    close(fd);
}

void WaylandInput::flush() {
    if (m_display)
        wl_display_flush(m_display);
}

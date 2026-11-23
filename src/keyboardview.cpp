// Ported from PressPop Keyboard's KeyboardView.kt (MIT, Wilson Samiano).
#include "keyboardview.h"

#include "backend.h"
#include "dictionary.h"
#include "swipe.h"

#include <QCoreApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTouchEvent>
#include <algorithm>
#include <cmath>

namespace {

const QColor Background{"#151517"};
const QColor KeyTop{"#3a3a40"}, KeyBottom{"#2e2e33"};
const QColor ActionTop{"#27272b"}, ActionBottom{"#202024"};
const QColor PressedTop{"#55555c"}, PressedBottom{"#47474e"};
const QColor KeyEdge{"#08080a"};
const QColor Label{"#f2f2f4"}, Hint{"#8e8e93"};
constexpr double Gap = 6;
constexpr double Tolerance = 26; // how far outside a key still counts as it

const QStringList Emoji{
    "😀", "😂", "😍", "🥰", "😎", "🤔", "😢", "😡", "👍", "👏",
    "❤️", "🔥", "💯", "🎉", "🙏", "🙌", "⭐", "🌙", "☀️", "🎂",
    "🍕", "🚗", "🏠", "⚽", "🎵", "📱", "💡", "😊", "😅", "😴"};

double distance(QPointF a, QPointF b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QColor mix(const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

} // namespace

QString KeyboardView::secondaryFor(QChar letter) {
    // Pull a key down to type the symbol printed in its corner.
    static const QHash<QChar, QString> map{
        {'q', "1"}, {'w', "2"}, {'e', "3"}, {'r', "4"}, {'t', "5"}, {'y', "6"}, {'u', "7"}, {'i', "8"},
        {'o', "9"}, {'p', "0"}, {'a', "@"}, {'s', "#"}, {'d', "$"}, {'f', "&"}, {'g', "*"}, {'h', "("},
        {'j', ")"}, {'k', "'"}, {'l', "\""}, {'z', "%"}, {'x', "-"}, {'c', "+"}, {'v', "="}, {'b', "/"},
        {'n', ";"}, {'m', ":"}, {',', "!"}, {'.', "?"}};
    return map.value(letter.toLower());
}

QStringList KeyboardView::accentsFor(QChar letter) {
    static const QHash<QChar, QStringList> map{
        {'a', {"à", "á", "â", "ä", "ã", "å", "æ"}}, {'e', {"è", "é", "ê", "ë"}}, {'i', {"ì", "í", "î", "ï"}},
        {'o', {"ò", "ó", "ô", "ö", "õ", "ø"}}, {'u', {"ù", "ú", "û", "ü"}}, {'n', {"ñ"}}, {'c', {"ç"}},
        {'s', {"ß", "ś", "š"}}, {'y', {"ý", "ÿ"}}, {'l', {"ł"}}, {'z', {"ź", "ż", "ž"}}, {'r', {"ř"}},
        {'t', {"ť"}}, {'d', {"ď"}}, {'g', {"ğ"}}};
    return map.value(letter.toLower());
}

KeyboardView::KeyboardView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptTouchEvents(true);
    setAntialiasing(true);
    setOpaquePainting(false);
    m_clock.start();

    m_longPress.setSingleShot(true);
    m_longPress.setInterval(500);
    connect(&m_longPress, &QTimer::timeout, this, &KeyboardView::onLongPress);
    m_spaceHold.setSingleShot(true);
    m_spaceHold.setInterval(400);
    connect(&m_spaceHold, &QTimer::timeout, this, &KeyboardView::onSpaceHold);
    connect(&m_deleteRepeat, &QTimer::timeout, this, &KeyboardView::onDeleteRepeat);
    rebuild();
}

void KeyboardView::setBackend(Backend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend)
        connect(m_backend, &Backend::shiftChanged, this, [this] {
            rebuild();
            update();
        });
    rebuild();
    emit backendChanged();
}

void KeyboardView::setAccent(const QColor &color) {
    if (color == m_accent)
        return;
    m_accent = color;
    update();
    emit appearanceChanged();
}

void KeyboardView::setAccentForeground(const QColor &color) {
    if (color == m_accentForeground)
        return;
    m_accentForeground = color;
    update();
    emit appearanceChanged();
}

void KeyboardView::setRadius(int radius) {
    if (radius == m_radius)
        return;
    m_radius = radius;
    update();
    emit appearanceChanged();
}

void KeyboardView::setKeysTop(qreal top) {
    if (qFuzzyCompare(top, m_keysTop))
        return;
    m_keysTop = top;
    layoutKeys();
    update();
    emit appearanceChanged();
}

void KeyboardView::setMaxKeysWidth(qreal width) {
    if (qFuzzyCompare(width, m_maxKeysWidth))
        return;
    m_maxKeysWidth = width;
    layoutKeys();
    update();
    emit appearanceChanged();
}

// Type sizes are designed for 60 px tall keys and follow the real height.
int KeyboardView::px(double size) const {
    const double keyHeight = m_keys.isEmpty() ? 60 : m_keys.first().rect.height();
    return qRound(size * std::clamp(keyHeight / 60, .7, 1.2));
}

QString KeyboardView::modeName() const {
    switch (m_mode) {
    case Mode::Letters: return "letters";
    case Mode::Numbers: return "numbers";
    case Mode::Symbols: return "symbols";
    case Mode::Emoji: return "emoji";
    case Mode::Numpad: return "numpad";
    }
    return {};
}

void KeyboardView::setMode(Mode mode) {
    if (mode == m_mode)
        return;
    m_mode = mode;
    rebuild();
    update();
    emit modeChanged();
}

QString KeyboardView::labelFor(QChar c) const {
    const bool upper = m_backend && m_backend->shiftOn();
    return upper ? QString(c).toUpper() : QString(c);
}

void KeyboardView::rebuild() {
    auto chars = [](const QString &keys) {
        QVector<Key> row;
        for (QChar c : keys)
            row.push_back({QString(c), Action::Char, QString(c)});
        return row;
    };
    auto list = [](const QStringList &keys) {
        QVector<Key> row;
        for (const QString &k : keys)
            row.push_back({k, Action::Char, k});
        return row;
    };
    auto letters = [this](const QString &keys) {
        QVector<Key> row;
        for (QChar c : keys)
            row.push_back({labelFor(c), Action::Char, labelFor(c)});
        return row;
    };
    const QString space = QCoreApplication::translate("KeyboardView", "space");
    const Key shift{m_backend && m_backend->shiftLock() ? "⇪" : "⇧", Action::Shift, {}}, del{"⌫", Action::Delete, {}, {}, 1.5}, enter{"⏎", Action::Enter, {}, {}, 1.3};
    const Key emoji{"☺︎", Action::Mode, {}, Mode::Emoji, .9};
    auto mode = [](const QString &label, Mode target, double weight) { return Key{label, Action::Mode, {}, target, weight}; };

    switch (m_mode) {
    case Mode::Letters:
        m_rows = {letters("qwertyuiop"), letters("asdfghjkl"),
                  QVector<Key>{{shift.label, Action::Shift, {}, {}, 1.5}} + letters("zxcvbnm") + QVector<Key>{del},
                  {mode("123", Mode::Numbers, 1.3), {",", Action::Char, ","}, emoji, {space, Action::Space, {}, {}, 4},
                   {".", Action::Char, "."}, enter}};
        break;
    case Mode::Numbers:
        m_rows = {chars("1234567890"), list({"-", "/", ":", ";", "(", ")", "$", "&", "@", "\""}),
                  QVector<Key>{mode("#+=", Mode::Symbols, 1.5)} + list({".", ",", "?", "!", "'"}) + QVector<Key>{del},
                  {mode("ABC", Mode::Letters, 1.5), emoji, mode("🔢", Mode::Numpad, 1), {space, Action::Space, {}, {}, 3.2},
                   {".", Action::Char, "."}, {enter.label, Action::Enter, {}, {}, 1.5}}};
        break;
    case Mode::Symbols:
        m_rows = {list({"[", "]", "{", "}", "#", "%", "^", "*", "+", "="}),
                  list({"_", "\\", "|", "~", "<", ">", "€", "£", "¥", "•"}),
                  QVector<Key>{mode("123", Mode::Numbers, 1.5)} + list({".", ",", "?", "!", "'"}) + QVector<Key>{del},
                  {mode("ABC", Mode::Letters, 1.5), emoji, {space, Action::Space, {}, {}, 4}, {".", Action::Char, "."},
                   {enter.label, Action::Enter, {}, {}, 1.5}}};
        break;
    case Mode::Numpad:
        m_rows = {chars("123"), chars("456"), chars("789"), chars("*0#"),
                  {mode("123", Mode::Numbers, 1.5), mode("ABC", Mode::Letters, 1.5), {space, Action::Space, {}, {}, 2},
                   del, {".", Action::Char, "."}, {enter.label, Action::Enter, {}, {}, 1.5}}};
        break;
    case Mode::Emoji:
        m_rows = {list(Emoji.mid(0, 10)), list(Emoji.mid(10, 10)), list(Emoji.mid(20, 10)),
                  {mode("ABC", Mode::Letters, 1.5), {space, Action::Space, {}, {}, 3.8}, del, {".", Action::Char, "."},
                   {enter.label, Action::Enter, {}, {}, 1.5}}};
        break;
    }
    layoutKeys();
}

void KeyboardView::layoutKeys() {
    m_keys.clear();
    // On a wide landscape screen the rows stop growing and sit centered,
    // so keys stay a comfortable size for thumbs.
    const double w = m_maxKeysWidth > 0 ? std::min(width(), m_maxKeysWidth) : width();
    const double offset = (width() - w) / 2, h = height() - m_keysTop;
    if (w <= 0 || h <= 0 || m_rows.isEmpty())
        return;
    const double rowHeight = (h - Gap * (m_rows.size() + 1)) / m_rows.size();
    double top = m_keysTop + Gap;
    for (int r = 0; r < m_rows.size(); ++r) {
        const auto &row = m_rows[r];
        double total = 0;
        for (const Key &k : row)
            total += k.weight;
        // The home row sits in a little, like a real keyboard's.
        const bool inset = m_mode == Mode::Letters && r == 1;
        const double rowWidth = (w - Gap * 2) * (inset ? .9 : 1);
        double left = offset + Gap + (inset ? (w - rowWidth) / 2 - Gap : 0);
        for (const Key &k : row) {
            const double kw = rowWidth * k.weight / total - Gap;
            m_keys.push_back({k, QRectF(left, top, kw, rowHeight)});
            left += kw + Gap;
        }
        top += rowHeight + Gap;
    }
}

void KeyboardView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    layoutKeys();
}

bool KeyboardView::contains(const QPointF &point) const {
    return point.y() >= m_keysTop && point.y() <= height() && point.x() >= 0 && point.x() <= width();
}

int KeyboardView::keyAt(QPointF point) const {
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < m_keys.size(); ++i) {
        const QRectF &r = m_keys[i].rect;
        const double dx = point.x() < r.left() ? r.left() - point.x() : point.x() > r.right() ? point.x() - r.right() : 0;
        const double dy = point.y() < r.top() ? r.top() - point.y() : point.y() > r.bottom() ? point.y() - r.bottom() : 0;
        const double d = std::hypot(dx, dy);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return bestDistance <= Tolerance ? best : -1;
}

bool KeyboardView::isLetterKey(int index) const {
    if (index < 0 || index >= m_keys.size() || m_mode != Mode::Letters)
        return false;
    const Key &k = m_keys[index].key;
    return k.action == Action::Char && k.output.size() == 1 && k.output[0].isLetter();
}

// ---- Gestures -------------------------------------------------------------

void KeyboardView::mousePressEvent(QMouseEvent *event) {
    press(event->position());
    event->accept();
}

void KeyboardView::mouseMoveEvent(QMouseEvent *event) {
    move(event->position());
}

void KeyboardView::mouseReleaseEvent(QMouseEvent *event) {
    release(event->position());
}

void KeyboardView::touchEvent(QTouchEvent *event) {
    for (const QEventPoint &point : event->points()) {
        switch (point.state()) {
        case QEventPoint::Pressed:
            if (m_down) {
                // Two-thumb typing: a second finger landing finishes a tap
                // in progress, so no letter is lost. A glide or a held key
                // keeps the finger it started with.
                const bool simpleTap = m_visited.size() <= 1 && !m_variantMode && !m_cursorMode && m_flick.isEmpty()
                                       && !m_deleteRepeated && distance(m_lastPoint, m_downPoint) < 20;
                if (!simpleTap)
                    break;
                release(m_lastPoint);
            }
            m_touchId = point.id();
            press(point.position());
            break;
        case QEventPoint::Updated:
            if (point.id() == m_touchId)
                move(point.position());
            break;
        case QEventPoint::Released:
            if (point.id() == m_touchId) {
                release(point.position());
                m_touchId = -1;
            }
            break;
        default:
            break;
        }
    }
    event->accept();
}

void KeyboardView::touchUngrabEvent() {
    if (m_down)
        release(m_lastPoint, true);
}

void KeyboardView::press(QPointF point) {
    resetGesture();
    m_down = true;
    m_downPoint = m_lastPoint = point;
    m_downTime = m_clock.elapsed();
    m_points = {point};
    m_times = {m_downTime};
    m_pressed = m_downIndex = keyAt(point);
    if (m_pressed >= 0) {
        const Key &k = m_keys[m_pressed].key;
        if (k.action == Action::Space)
            m_spaceHold.start();
        if (k.action == Action::Delete) {
            m_deleteRepeated = false;
            m_deleteRepeat.start(450);
        }
        if (isLetterKey(m_pressed)) {
            m_visited << k.output.toLower();
            m_longPress.start();
        }
    }
    update();
}

void KeyboardView::move(QPointF point) {
    if (!m_down)
        return;
    const QPointF previous = m_lastPoint;
    m_lastPoint = point;
    if (!m_flick.isEmpty())
        return;

    // A quick pull straight down types the key's corner symbol.
    if (!m_variantMode && !m_cursorMode && isLetterKey(m_downIndex) && m_visited.size() <= 1) {
        const double dx = point.x() - m_downPoint.x(), dy = point.y() - m_downPoint.y();
        const QString secondary = secondaryFor(m_keys[m_downIndex].key.output[0]);
        const bool pulling = !secondary.isEmpty() && dy > 0 && dy > std::abs(dx) * 1.5;
        const double pull = pulling ? std::clamp(dy / 30.0, 0.0, 1.0) : 0.0;
        if (pull != m_flickPull) {
            m_flickPull = pull;
            update();
        }
        if (!secondary.isEmpty() && dy > 30 && dy > std::abs(dx) * 2.5 && m_clock.elapsed() - m_downTime < 350) {
            m_flick = secondary;
            m_flickPull = 1;
            m_longPress.stop();
            update();
            return;
        }
    }

    if (m_variantMode) {
        const QRectF rect = m_keys.value(m_downIndex).rect;
        const double totalWidth = rect.width() * m_variants.size();
        const double startX = std::clamp(rect.center().x() - totalWidth / 2, 0.0, std::max(0.0, width() - totalWidth));
        const int index = std::clamp(int((point.x() - startX) / rect.width()), 0, int(m_variants.size()) - 1);
        if (index != m_variantSelected) {
            m_variantSelected = index;
            update();
        }
        return;
    }

    if (m_cursorMode) {
        // Every 8 px sideways is a character; every 30 px up or down a line.
        const int columns = int((point.x() - m_cursorAnchor.x()) / 8);
        const int lines = int((point.y() - m_cursorAnchor.y()) / 30);
        if ((columns || lines) && m_backend) {
            m_backend->moveCursor(columns, lines);
            m_cursorAnchor += QPointF(columns * 8, lines * 30);
        }
        return;
    }

    m_points << point;
    m_times << m_clock.elapsed();
    if (distance(point, m_downPoint) > 20) {
        m_spaceHold.stop();
        m_longPress.stop();
    }

    // Letters the glide crosses, sampled every 8 px so a fast finger
    // doesn't skip any.
    if (isLetterKey(m_downIndex)) {
        const int steps = std::max(1, int(distance(point, previous) / 8));
        for (int i = 1; i <= steps; ++i) {
            const QPointF sample = previous + (point - previous) * (double(i) / steps);
            const int hit = keyAt(sample);
            if (isLetterKey(hit) && m_keys[hit].rect.contains(sample)) {
                const QString letter = m_keys[hit].key.output.toLower();
                if (m_visited.isEmpty() || m_visited.last() != letter)
                    m_visited << letter;
            }
        }
    }

    const int hit = keyAt(point);
    if (hit >= 0 && hit != m_pressed) {
        m_pressed = hit;
        if (m_keys[hit].key.action == Action::Delete && !isLetterKey(m_downIndex)) {
            m_deleteRepeated = false;
            m_deleteRepeat.start(450);
        } else {
            m_deleteRepeat.stop();
        }
    }
    update();
}

void KeyboardView::release(QPointF point, bool cancelled) {
    if (!m_down)
        return;
    m_lastPoint = point;
    m_longPress.stop();
    m_spaceHold.stop();
    m_deleteRepeat.stop();

    const qint64 duration = m_clock.elapsed() - m_downTime;
    QStringList distinct = m_visited;
    distinct.removeDuplicates();
    double traceLength = 0;
    for (int i = 1; i < m_points.size(); ++i)
        traceLength += distance(m_points[i], m_points[i - 1]);
    // Two letters brushed quickly is a sloppy tap, not a glide.
    const bool shortFastTwoLetter = distinct.size() == 2 && traceLength <= 90 && duration < 300;
    const bool wasSwipe = m_mode == Mode::Letters && distinct.size() >= 2 && traceLength > 50 && !shortFastTwoLetter;

    const int pressed = m_pressed, downIndex = m_downIndex;
    const QString flick = m_flick;
    const bool wasCursor = m_cursorMode, wasVariant = m_variantMode, deleteRepeated = m_deleteRepeated;
    const QString variant = m_variants.value(m_variantSelected);
    const QString swipe = wasSwipe ? matchSwipe() : QString();
    resetGesture();
    update();

    if (cancelled || wasCursor || !m_backend)
        return;
    if (!flick.isEmpty()) {
        m_backend->typeCharacter(flick);
        return;
    }
    if (wasVariant) {
        if (!variant.isEmpty())
            m_backend->typeCharacter(m_backend->shiftOn() ? variant.toUpper() : variant);
        return;
    }
    // A quick swipe left off Delete removes the whole word.
    if (downIndex >= 0 && m_keys.value(downIndex).key.action == Action::Delete && duration < 450
        && m_downPoint.x() - point.x() > 60) {
        m_backend->deleteWord();
        return;
    }
    if (wasSwipe) {
        if (!swipe.isEmpty())
            m_backend->swipeWord(swipe);
        return;
    }
    if (pressed < 0)
        return;
    const Key key = m_keys[pressed].key;
    switch (key.action) {
    case Action::Shift: m_backend->toggleShift(); break;
    case Action::Mode: setMode(key.target); break;
    case Action::Char: m_backend->typeCharacter(key.output); break;
    case Action::Space: m_backend->space(); break;
    case Action::Delete:
        if (!deleteRepeated)
            m_backend->backspace();
        break;
    case Action::Enter: m_backend->enter(); break;
    }
}

void KeyboardView::resetGesture() {
    m_down = false;
    m_pressed = m_downIndex = -1;
    m_points.clear();
    m_times.clear();
    m_visited.clear();
    m_flick.clear();
    m_flickPull = 0;
    m_variantMode = false;
    m_variants.clear();
    m_variantSelected = -1;
    m_cursorMode = false;
    m_deleteRepeated = false;
}

void KeyboardView::onLongPress() {
    // Hold a letter for its accents; slide to one and lift.
    if (!m_down || !isLetterKey(m_downIndex) || m_visited.size() > 1)
        return;
    const QStringList accents = accentsFor(m_keys[m_downIndex].key.output[0]);
    if (accents.isEmpty())
        return;
    m_variantMode = true;
    m_variants = accents;
    m_variantSelected = 0;
    update();
}

void KeyboardView::onSpaceHold() {
    // Hold Space, then slide, to move the text cursor.
    if (!m_down || m_pressed < 0 || m_keys[m_pressed].key.action != Action::Space)
        return;
    m_cursorMode = true;
    m_cursorAnchor = m_lastPoint;
    update();
}

void KeyboardView::onDeleteRepeat() {
    if (!m_down || m_pressed < 0 || m_keys[m_pressed].key.action != Action::Delete || !m_backend) {
        m_deleteRepeat.stop();
        return;
    }
    m_deleteRepeated = true;
    m_backend->backspace();
    m_deleteRepeat.setInterval(60);
}

QString KeyboardView::matchSwipe() const {
    if (!m_backend)
        return {};
    SwipeTrace trace;
    trace.points = m_points;
    trace.times = m_times;
    QVector<double> sizes;
    for (int i = 0; i < m_keys.size(); ++i)
        if (isLetterKey(i)) {
            const QChar letter = m_keys[i].key.output.toLower()[0];
            if (!trace.centers.contains(letter))
                trace.centers.insert(letter, m_keys[i].rect.center());
            // On a phone a key is narrower than it is tall; on a landscape
            // tablet it is much wider. Measuring in the smaller side keeps
            // the frequency bonuses from drowning out row changes.
            sizes << std::min(m_keys[i].rect.width(), m_keys[i].rect.height());
        }
    std::sort(sizes.begin(), sizes.end());
    trace.keyWidth = sizes.isEmpty() ? 60 : sizes[sizes.size() / 2];
    trace.previousWord = m_backend->previousWord();
    trace.contextWords = m_backend->contextWords();
    trace.personalWords = m_backend->personalWords();
    const QString shape = matchSwipeShape(trace, m_backend->dictionary());
    if (!shape.isEmpty())
        return shape;
    // The letters crossed, when the shape alone isn't convincing.
    return m_backend->dictionary().bestSwipeMatch(m_visited.join(QString()), trace.personalWords);
}

// ---- Painting -------------------------------------------------------------

void KeyboardView::paint(QPainter *painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->fillRect(QRectF(0, m_keysTop, width(), height() - m_keysTop), Background);
    for (int i = 0; i < m_keys.size(); ++i)
        drawKey(painter, m_keys[i], i == m_pressed && m_down && !m_cursorMode);
    drawTrail(painter);
    drawPopup(painter);
}

void KeyboardView::drawKey(QPainter *painter, const PlacedKey &placed, bool pressed) {
    const Key &key = placed.key;
    const QRectF rect = placed.rect;
    const bool action = key.action == Action::Shift || key.action == Action::Delete || key.action == Action::Mode
                        || key.action == Action::Enter;
    const bool shiftOn = key.action == Action::Shift && m_backend && m_backend->shiftOn();
    const double r = std::min(6, m_radius);

    QColor top = KeyTop, bottom = KeyBottom, text = Label;
    if (shiftOn) {
        top = m_accent.lighter(pressed ? 100 : 112);
        bottom = m_accent.darker(pressed ? 125 : 108);
        text = m_accentForeground;
    } else if (pressed) {
        top = PressedTop;
        bottom = PressedBottom;
    } else if (action) {
        top = ActionTop;
        bottom = ActionBottom;
    }

    // A darker edge under each key gives it depth; a pressed key sinks.
    painter->setPen(Qt::NoPen);
    painter->setBrush(KeyEdge);
    painter->drawRoundedRect(rect.translated(0, 2), r, r);
    QRectF face = rect;
    double drop = pressed ? 1.5 : 0;
    if (pressed && m_flickPull > 0)
        drop += 4 * m_flickPull;
    face.translate(0, drop);
    QLinearGradient gradient(face.topLeft(), face.bottomLeft());
    gradient.setColorAt(0, top);
    gradient.setColorAt(1, bottom);
    painter->setBrush(gradient);
    painter->drawRoundedRect(face, r, r);

    QFont font = painter->font();
    const bool emojiPage = m_mode == Mode::Emoji && key.action == Action::Char;
    font.setPixelSize(px(emojiPage ? 26 : key.label.size() > 2 ? 15 : key.action == Action::Mode ? 20 : 22));
    font.setWeight(action ? QFont::DemiBold : QFont::Normal);
    painter->setFont(font);

    // The corner symbol of a letter, which a pull slides into the middle.
    const QString hint = m_mode == Mode::Letters && key.action == Action::Char && key.output.size() == 1
                                 && key.output[0].isLetter()
                             ? secondaryFor(key.output[0])
                             : QString();
    if (m_cursorMode && key.action == Action::Space) {
        painter->setPen(m_accent);
        painter->drawText(face, Qt::AlignCenter, "◂  ▸");
        return;
    }
    if (pressed && m_flickPull > 0 && !hint.isEmpty()) {
        const double p = m_flickPull;
        painter->save();
        painter->setClipRect(rect);
        QColor faded = text;
        faded.setAlphaF(1 - p);
        painter->setPen(faded);
        painter->drawText(face.translated(0, rect.height() * .65 * p), Qt::AlignCenter, key.label);
        QFont hintFont = font;
        hintFont.setPixelSize(px(11 + 11 * p));
        painter->setFont(hintFont);
        painter->setPen(mix(Hint, text, p));
        const QPointF corner(face.right() - 10, face.top() + 12);
        const QPointF center = face.center();
        const QPointF at = corner + (center - corner) * p;
        painter->drawText(QRectF(at.x() - 20, at.y() - 16, 40, 32), Qt::AlignCenter, hint);
        painter->restore();
        return;
    }
    painter->setPen(text);
    painter->drawText(face, Qt::AlignCenter, key.label);
    if (!hint.isEmpty()) {
        QFont hintFont = font;
        hintFont.setPixelSize(px(11));
        hintFont.setWeight(QFont::Normal);
        painter->setFont(hintFont);
        painter->setPen(Hint);
        painter->drawText(face.adjusted(0, 4, -6, 0), Qt::AlignRight | Qt::AlignTop, hint);
    }
}

void KeyboardView::drawTrail(QPainter *painter) {
    if (!m_down || m_points.size() < 2 || m_visited.size() < 2)
        return;
    // One stroke, so the joints don't stack up where they overlap; the
    // last stretch of the glide is drawn again on top, brighter.
    auto stroke = [&](int from, double alpha, double width) {
        QPainterPath path(m_points[from]);
        for (int i = from + 1; i < m_points.size(); ++i)
            path.lineTo(m_points[i]);
        QColor c = m_accent;
        c.setAlphaF(alpha);
        painter->setPen(QPen(c, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    };
    stroke(0, .45, 5);
    stroke(std::max(0, int(m_points.size()) - 12), 1, 6);
}

void KeyboardView::drawPopup(QPainter *painter) {
    if (!m_down || m_pressed < 0 || m_cursorMode || m_visited.size() > 1)
        return;
    const PlacedKey &placed = m_keys[m_pressed];
    const QRectF key = placed.rect;
    const double r = std::min(6, m_radius);

    QFont font = painter->font();
    font.setWeight(QFont::Normal);

    if (m_variantMode) {
        // A row of accents above the key; the one under the finger lights up.
        const double cell = key.width();
        const double total = cell * m_variants.size();
        const double x = std::clamp(key.center().x() - total / 2, 0.0, std::max(0.0, width() - total));
        const QRectF row(x, std::max(2.0, key.top() - key.height() - 8), total, key.height());
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 90));
        painter->drawRoundedRect(row.translated(0, 3), r, r);
        painter->setBrush(PressedTop);
        painter->drawRoundedRect(row, r, r);
        font.setPixelSize(px(24));
        painter->setFont(font);
        for (int i = 0; i < m_variants.size(); ++i) {
            const QRectF c(row.left() + i * cell, row.top(), cell, row.height());
            const bool selected = i == m_variantSelected;
            if (selected) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(m_accent);
                painter->drawRoundedRect(c.adjusted(3, 3, -3, -3), r, r);
            }
            painter->setPen(selected ? m_accentForeground : Label);
            const QString v = m_backend && m_backend->shiftOn() ? m_variants[i].toUpper() : m_variants[i];
            painter->drawText(c, Qt::AlignCenter, v);
        }
        return;
    }

    QString label = m_flick.isEmpty() ? placed.key.label : m_flick;
    if (placed.key.action == Action::Space)
        label = QCoreApplication::translate("KeyboardView", "space");

    // The iPhone-style bubble: wider than the key, rising above it, joined
    // to it by a neck.
    const double bubbleWidth = std::max(key.width() * 1.4, key.width() + 16);
    const double bubbleHeight = key.height() * 1.15;
    const double x = std::clamp(key.center().x() - bubbleWidth / 2, 2.0, std::max(2.0, width() - bubbleWidth - 2));
    const double top = std::max(2.0, key.top() - bubbleHeight + 4);
    const QRectF bubble(x, top, bubbleWidth, std::max(key.height() * .8, key.top() + 4 - top));

    QPainterPath path;
    path.addRoundedRect(bubble, std::max(r, 2.0), std::max(r, 2.0));
    QPainterPath neck;
    neck.moveTo(bubble.left() + std::min(8.0, r + 4), bubble.bottom() - 1);
    neck.lineTo(bubble.right() - std::min(8.0, r + 4), bubble.bottom() - 1);
    neck.lineTo(key.right(), key.top() + key.height() * .55);
    neck.lineTo(key.left(), key.top() + key.height() * .55);
    neck.closeSubpath();
    path = path.united(neck);
    QPainterPath body;
    body.addRoundedRect(key.adjusted(0, key.height() * .4, 0, 0), r, r);
    path = path.united(body);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 110));
    painter->drawPath(path.translated(0, 3));
    QLinearGradient gradient(bubble.topLeft(), QPointF(bubble.left(), key.bottom()));
    gradient.setColorAt(0, PressedTop);
    gradient.setColorAt(1, PressedBottom);
    painter->setBrush(gradient);
    painter->drawPath(path);

    const bool emoji = m_mode == Mode::Emoji && placed.key.action == Action::Char;
    font.setPixelSize(px(label.size() > 2 && !emoji ? 18 : emoji ? 34 : 32));
    painter->setFont(font);
    painter->setPen(m_flick.isEmpty() ? Label : m_accent);
    painter->drawText(bubble, Qt::AlignCenter, label);
}

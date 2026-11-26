#include "placement.h"
#include "touchwatcher.h"

#include <QDir>
#include <QFile>

#include <LayerShellQt/window.h>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>

// Room kept clear at the top and bottom of the screen. Margins count from
// the edge of the area other layers leave free, and a bar's exclusive zone
// shifts that area, so the keyboard keeps this much away from the edges and
// its drag handle always stays on screen.
static constexpr int EdgeReserve = 48;

Placement::Placement(QSettings &settings, QObject *parent) : QObject(parent), m_settings(settings) {
    // After a frame with the new position is shown, give the compositor a
    // moment to deliver touches that were already on their way.
    m_settle.setSingleShot(true);
    m_settle.setInterval(24);
    connect(&m_settle, &QTimer::timeout, this, [this] { m_moving = false; });
    m_floating = settings.value("floating", false).toBool();
    m_placed = settings.contains("floatLeft") && settings.contains("floatBottom");
    m_offset = QPoint(settings.value("floatLeft", 0).toInt(), settings.value("floatBottom", 24).toInt());
}

void Placement::attach(QQuickWindow *window, LayerShellQt::Window *layer) {
    m_window = window;
    m_layer = layer;
    if (!window)
        return;
    // The size follows the mode (Main.qml), and docked the exclusive zone
    // follows the size; a turned screen can leave a floating card off its edge.
    connect(window, &QWindow::heightChanged, this, &Placement::apply);
    connect(window, &QWindow::widthChanged, this, &Placement::apply);
    connect(window, &QWindow::screenChanged, this, &Placement::apply);
    connect(window, &QQuickWindow::frameSwapped, this, [this] {
        if (m_moving && !m_settle.isActive())
            m_settle.start();
    });
    if (auto *screen = window->screen())
        connect(screen, &QScreen::geometryChanged, this, &Placement::apply);
    apply();
}

void Placement::setFloating(bool floating) {
    if (floating == m_floating)
        return;
    m_floating = floating;
    m_settings.setValue("floating", floating);
    emit floatingChanged();
    apply();
}

QSize Placement::screenSize() const {
    if (m_window && m_window->screen())
        return m_window->screen()->geometry().size();
    return {};
}

void Placement::clamp() {
    if (!m_window)
        return;
    const QSize screen = screenSize();
    if (screen.isEmpty())
        return;
    const int maxLeft = qMax(0, screen.width() - m_window->width());
    const int maxBottom = qMax(0, screen.height() - m_window->height() - 2 * EdgeReserve);
    if (!m_placed) {
        // Until it is dragged somewhere: centred, a little above the bottom
        // edge. Worked out again as the size settles.
        m_offset = QPoint(maxLeft / 2, 0);
    }
    m_offset.setX(qBound(0, m_offset.x(), maxLeft));
    m_offset.setY(qBound(0, m_offset.y(), maxBottom));
}

void Placement::apply() {
    if (!m_layer || !m_window)
        return;
    if (m_floating) {
        clamp();
        m_layer->setAnchors({LayerShellQt::Window::AnchorBottom, LayerShellQt::Window::AnchorLeft});
        m_layer->setExclusiveZone(0);
        m_layer->setMargins(QMargins(m_offset.x(), 0, 0, m_offset.y()));
    } else {
        m_layer->setAnchors({LayerShellQt::Window::AnchorBottom, LayerShellQt::Window::AnchorLeft,
                             LayerShellQt::Window::AnchorRight});
        m_layer->setMargins(QMargins());
        // Windows make room for the keyboard instead of hiding under it.
        m_layer->setExclusiveZone(m_window->height());
    }
}

void Placement::setTouch(TouchWatcher *touch) {
    m_touch = touch;
    if (touch)
        connect(touch, &TouchWatcher::fingerMoved, this, &Placement::followFinger);
}

int Placement::screenTransform() {
    QFile file(QDir::homePath() + "/.local/state/autorotate/transform");
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return QString::fromUtf8(file.readAll()).trimmed().toInt() & 3;
}

void Placement::beginDrag() {
    m_fingerDrag = false;
    if (!m_floating || !m_touch)
        return;
    QPointF finger;
    if (!m_touch->fingerPosition(&finger))
        return; // a mouse: dragBy does it
    m_fingerDrag = true;
    m_fingerStart = finger;
    m_dragStartOffset = m_offset;
    m_dragTransform = screenTransform();
}

void Placement::followFinger() {
    if (!m_fingerDrag || !m_touch)
        return;
    QPointF finger;
    if (!m_touch->fingerPosition(&finger))
        return;
    // How far the finger went on the panel, turned to the screen as it is
    // rotated (the same mapping as touchswipe), in screen pixels.
    const double dx = finger.x() - m_fingerStart.x(), dy = finger.y() - m_fingerStart.y();
    double sx = dx, sy = dy;
    switch (m_dragTransform) {
    case 1: sx = -dy; sy = dx; break;
    case 2: sx = -dx; sy = -dy; break;
    case 3: sx = dy; sy = -dx; break;
    default: break;
    }
    const QSize screen = screenSize();
    m_offset = m_dragStartOffset + QPoint(qRound(sx * screen.width()), -qRound(sy * screen.height()));
    m_placed = true;
    apply();
}

void Placement::dragBy(qreal dx, qreal dy) {
    // A finger drag follows the touchscreen instead (followFinger).
    if (!m_floating || m_fingerDrag)
        return;
    if (m_moving && m_movedAt.isValid() && m_movedAt.elapsed() < 120)
        return;
    const QPoint before = m_offset;
    m_offset += QPoint(qRound(dx), -qRound(dy));
    m_placed = true;
    apply();
    if (m_offset != before && m_window) {
        // Wait for this move to show before taking the next touch.
        m_moving = true;
        m_movedAt.start();
        m_window->requestUpdate();
    }
}

void Placement::endDrag() {
    m_fingerDrag = false;
    m_moving = false;
    m_settle.stop();
    m_settings.setValue("floatLeft", m_offset.x());
    m_settings.setValue("floatBottom", m_offset.y());
}

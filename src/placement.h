#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>

class QQuickWindow;
class QSettings;
namespace LayerShellQt { class Window; }

// Where the keyboard sits: docked across the bottom of the screen (windows
// make room for it), or floating as a smaller card the user drags around
// (windows stay full size under it). Floating, its position is kept as the
// layer surface's left and bottom margins, and remembered between runs.
class Placement : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool floating READ floating WRITE setFloating NOTIFY floatingChanged)

public:
    explicit Placement(QSettings &settings, QObject *parent = nullptr);

    // Called once the window exists, before it first shows.
    void attach(QQuickWindow *window, LayerShellQt::Window *layer);

    bool floating() const { return m_floating; }
    void setFloating(bool floating);

    // A drag on the floating keyboard's handle, by this much (window pixels).
    Q_INVOKABLE void dragBy(qreal dx, qreal dy);
    Q_INVOKABLE void endDrag();

signals:
    void floatingChanged();

private:
    void apply();
    void clamp();
    QSize screenSize() const;

    QSettings &m_settings;
    QPointer<QQuickWindow> m_window;
    LayerShellQt::Window *m_layer = nullptr;
    bool m_floating = false;
    bool m_placed = false;  // a floating position has been chosen
    QPoint m_offset;        // left margin, bottom margin
};

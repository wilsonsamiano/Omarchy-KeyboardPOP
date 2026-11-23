#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QTimer>
#include <QVector>

#include "backend.h"

// The keys, ported from PressPop's KeyboardView.kt: drawing, the
// iPhone-style popup bubble, the long-press accent picker, flick-down
// symbols, swipe typing with its trail, the Space-bar cursor, and Delete
// repeat. What a key *does* is the Backend's business.
//
// The item covers the suggestion strip too, so a top-row popup can rise
// over it, but it only takes touches below `keysTop`.
class KeyboardView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *keyboardBackend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY appearanceChanged)
    Q_PROPERTY(QColor accentForeground READ accentForeground WRITE setAccentForeground NOTIFY appearanceChanged)
    Q_PROPERTY(int radius READ radius WRITE setRadius NOTIFY appearanceChanged)
    Q_PROPERTY(qreal keysTop READ keysTop WRITE setKeysTop NOTIFY appearanceChanged)
    Q_PROPERTY(qreal maxKeysWidth READ maxKeysWidth WRITE setMaxKeysWidth NOTIFY appearanceChanged)
    Q_PROPERTY(QString mode READ modeName NOTIFY modeChanged)

public:
    // Modifier latches Ctrl, Alt, or Super (output holds the TextSink bit);
    // Control sends a key that isn't text (output holds its keysym name).
    enum class Action { Char, Space, Shift, Delete, Mode, Enter, Modifier, Control };
    enum class Mode { Letters, Numbers, Symbols, Emoji, Numpad };

    struct Key {
        QString label;
        Action action = Action::Char;
        QString output;
        Mode target = Mode::Letters;
        double weight = 1;
    };
    struct PlacedKey {
        Key key;
        QRectF rect;
    };

    explicit KeyboardView(QQuickItem *parent = nullptr);

    Backend *backend() const { return m_backend; }
    void setBackend(Backend *backend);
    QColor accent() const { return m_accent; }
    void setAccent(const QColor &color);
    QColor accentForeground() const { return m_accentForeground; }
    void setAccentForeground(const QColor &color);
    int radius() const { return m_radius; }
    void setRadius(int radius);
    qreal keysTop() const { return m_keysTop; }
    void setKeysTop(qreal top);
    qreal maxKeysWidth() const { return m_maxKeysWidth; }
    void setMaxKeysWidth(qreal width);
    QString modeName() const;
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    const QVector<PlacedKey> &keys() const { return m_keys; }
    bool contains(const QPointF &point) const override;
    void paint(QPainter *painter) override;

    static QString secondaryFor(QChar letter);
    static QStringList accentsFor(QChar letter);

signals:
    void backendChanged();
    void appearanceChanged();
    void modeChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void touchEvent(QTouchEvent *event) override;
    void touchUngrabEvent() override;

private:
    void rebuild();
    void layoutKeys();
    QString labelFor(QChar c) const;
    int px(double size) const;
    int keyAt(QPointF point) const;
    bool isLetterKey(int index) const;

    void press(QPointF point);
    void move(QPointF point);
    void release(QPointF point, bool cancelled = false);
    // Does what the key at `index` does when tapped.
    void fireKey(int index, bool deleteRepeated = false);
    void resetGesture();
    void onLongPress();
    void onSpaceHold();
    void onDeleteRepeat();
    QString matchSwipe() const;

    void drawKey(QPainter *painter, const PlacedKey &placed, bool pressed);
    void drawPopup(QPainter *painter);
    void drawTrail(QPainter *painter);

    QPointer<Backend> m_backend;
    QColor m_accent{"#FFD60A"}, m_accentForeground{"black"};
    int m_radius = 0;
    qreal m_keysTop = 0;
    qreal m_maxKeysWidth = 0; // 0: as wide as the screen
    Mode m_mode = Mode::Letters;
    QVector<QVector<Key>> m_rows;
    QVector<PlacedKey> m_keys;

    // The gesture in progress.
    QElapsedTimer m_clock;
    bool m_down = false;
    int m_touchId = -1;
    // Fingers that landed while another was busy (holding Delete, gliding):
    // each is a plain tap on the key it landed on, fired when it lifts.
    QHash<int, int> m_extraTouches;
    int m_pressed = -1;
    int m_downIndex = -1;
    QPointF m_downPoint, m_lastPoint;
    qint64 m_downTime = 0;
    QVector<QPointF> m_points;
    QVector<qint64> m_times;
    QStringList m_visited;
    QString m_flick;
    double m_flickPull = 0;
    bool m_variantMode = false;
    QStringList m_variants;
    int m_variantSelected = -1;
    bool m_cursorMode = false;
    QPointF m_cursorAnchor;
    bool m_deleteRepeated = false;
    QTimer m_longPress, m_spaceHold, m_deleteRepeat;
};

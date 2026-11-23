#pragma once

#include <QHash>
#include <QPointF>
#include <QStringList>
#include <QVector>

class Dictionary;

// One glide across the letters, in item coordinates.
struct SwipeTrace {
    QVector<QPointF> points;
    QVector<qint64> times;            // milliseconds, one per point
    QHash<QChar, QPointF> centers;    // letter -> key center
    double keyWidth = 60;
    QString previousWord;
    QStringList contextWords;         // predicted next words, best first
    QStringList personalWords;
};

// The word whose key-to-key path best matches the trace's shape, ported
// from PressPop's KeyboardView.shapeMatch(); empty when nothing is close.
QString matchSwipeShape(const SwipeTrace &trace, const Dictionary &dictionary);

// Path helpers, public for tests.
QVector<QPointF> resamplePath(const QVector<QPointF> &points, int n);

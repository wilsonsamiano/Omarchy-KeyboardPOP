// Ported from PressPop Keyboard's KeyboardView.shapeMatch() (MIT, Wilson
// Samiano). Distances are in logical pixels, which on the desktop play the
// part Android's dp did.
#include "swipe.h"

#include "dictionary.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

double distance(QPointF a, QPointF b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double pathLength(const QVector<QPointF> &points) {
    double total = 0;
    for (int i = 1; i < points.size(); ++i)
        total += distance(points[i], points[i - 1]);
    return total;
}

double pointToSegment(QPointF p, QPointF a, QPointF b) {
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double lengthSquared = dx * dx + dy * dy;
    double t = lengthSquared == 0 ? 0 : ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / lengthSquared;
    t = std::clamp(t, 0.0, 1.0);
    return distance(p, QPointF(a.x() + t * dx, a.y() + t * dy));
}

// Symmetric mean distance from each path's points to the other path.
double chamfer(const QVector<QPointF> &actual, const QVector<QPointF> &candidate) {
    if (actual.isEmpty() || candidate.isEmpty())
        return std::numeric_limits<double>::max();
    double sumActual = 0;
    for (QPointF p : actual) {
        double best = std::numeric_limits<double>::max();
        for (int i = 1; i < candidate.size(); ++i)
            best = std::min(best, pointToSegment(p, candidate[i - 1], candidate[i]));
        if (candidate.size() == 1)
            best = distance(p, candidate[0]);
        sumActual += best;
    }
    double sumCandidate = 0;
    for (QPointF p : candidate) {
        double best = std::numeric_limits<double>::max();
        for (int i = 1; i < actual.size(); ++i)
            best = std::min(best, pointToSegment(p, actual[i - 1], actual[i]));
        sumCandidate += best;
    }
    return (sumActual / actual.size() + sumCandidate / candidate.size()) / 2;
}

QVector<QPointF> smoothOnce(const QVector<QPointF> &points) {
    if (points.size() < 3)
        return points;
    QVector<QPointF> out{points.first()};
    for (int i = 1; i < points.size() - 1; ++i)
        out << (points[i - 1] + points[i] * 2 + points[i + 1]) / 4;
    out << points.last();
    return out;
}

QString nearestLetter(QPointF p, const QHash<QChar, QPointF> &centers, double *bestDistance) {
    QString letter;
    *bestDistance = std::numeric_limits<double>::max();
    for (auto it = centers.begin(); it != centers.end(); ++it) {
        const double d = distance(p, it.value());
        if (d < *bestDistance) {
            *bestDistance = d;
            letter = it.key();
        }
    }
    return letter;
}

struct Scored {
    QString word;
    double geometry;
    double rescore;
};

} // namespace

QVector<QPointF> resamplePath(const QVector<QPointF> &points, int n) {
    if (points.isEmpty())
        return {};
    const double total = pathLength(points);
    if (points.size() == 1 || total == 0)
        return QVector<QPointF>(n, points.first());
    const double step = total / (n - 1);
    QVector<QPointF> out{points.first()};
    double walked = 0;
    int index = 1;
    while (out.size() < n - 1 && index < points.size()) {
        const double segment = distance(points[index], points[index - 1]);
        if (walked + segment >= step * out.size()) {
            const double ratio = segment == 0 ? 0 : (step * out.size() - walked) / segment;
            out << points[index - 1] + (points[index] - points[index - 1]) * ratio;
        } else {
            walked += segment;
            ++index;
        }
    }
    while (out.size() < n)
        out << points.last();
    return out;
}

QString matchSwipeShape(const SwipeTrace &trace, const Dictionary &dictionary) {
    const auto &points = trace.points;
    const auto &centers = trace.centers;
    const double keyWidth = trace.keyWidth;
    if (points.size() < 2 || centers.isEmpty() || keyWidth <= 0)
        return {};

    // Where the glide starts is its most reliable part; where it lifts is next.
    double startDistance = 0, liftDistance = 0;
    QString startLetter = nearestLetter(points.first(), centers, &startDistance);
    QString liftLetter = nearestLetter(points.last(), centers, &liftDistance);
    if (startDistance > keyWidth * .9)
        startLetter.clear();
    if (liftDistance > keyWidth * .9)
        liftLetter.clear();

    const QVector<QPointF> smoothed = smoothOnce(smoothOnce(points));
    const QVector<QPointF> actual = resamplePath(smoothed, 32);
    const double actualLength = pathLength(smoothed);
    if (actualLength <= 0)
        return {};

    // A fast glide cuts corners, so its point order counts for less.
    const qint64 duration = trace.times.size() >= 2 ? std::max<qint64>(1, trace.times.last() - trace.times.first()) : 0;
    const bool fast = duration > 0 && actualLength / double(duration) > 1.2;
    const double orderedWeight = fast ? .15 : .30;
    const double chamferWeight = 1 - orderedWeight;

    // A finger that rests on a key mid-glide (180 ms or more) usually means
    // a double letter there.
    QSet<QString> dwelled;
    if (trace.times.size() == points.size() && points.size() >= 3) {
        QString runLetter;
        qint64 runStart = 0, runLast = 0;
        auto flushRun = [&] {
            if (!runLetter.isEmpty() && runLast - runStart >= 180)
                dwelled.insert(runLetter);
        };
        for (int i = 0; i < points.size(); ++i) {
            double d = 0;
            QString near = nearestLetter(points[i], centers, &d);
            if (d > keyWidth * .5 || i == 0 || i == points.size() - 1)
                near.clear();
            if (near == runLetter) {
                runLast = trace.times[i];
            } else {
                flushRun();
                runLetter = near;
                runStart = runLast = trace.times[i];
            }
        }
        flushRun();
    }

    const QStringList &dictionaryWords = dictionary.swipeWords();
    const QStringList &dictionaryLower = dictionary.swipeWordsLower();
    QSet<QString> personalSet;
    QStringList personalList, personalLower;
    for (const QString &w : trace.personalWords) {
        const QString lower = w.toLower();
        if (!personalSet.contains(lower)) {
            personalSet.insert(lower);
            personalList << w;
            personalLower << lower;
        }
    }
    const QStringList bigramNext = dictionary.bigramNext(trace.previousWord.toLower());
    QHash<QString, int> contextRank;
    for (int i = 0; i < trace.contextWords.size(); ++i)
        if (!contextRank.contains(trace.contextWords[i].toLower()))
            contextRank.insert(trace.contextWords[i].toLower(), i);

    auto scoreCandidates = [&](const QSet<QString> *allowedStarts) {
        QVector<Scored> out;
        const int personalCount = personalList.size();
        for (int index = 0; index < personalCount + dictionaryWords.size(); ++index) {
            QString candidate, lower;
            if (index < personalCount) {
                candidate = personalList[index];
                lower = personalLower[index];
            } else {
                lower = dictionaryLower[index - personalCount];
                if (personalSet.contains(lower))
                    continue;
                candidate = dictionaryWords[index - personalCount];
            }
            if (lower.isEmpty() || (allowedStarts && !allowedStarts->contains(QString(lower[0]))))
                continue;
            if (candidate.size() < 2)
                continue;

            // The ideal glide for this word: key center to key center.
            QVector<QPointF> path;
            bool ok = true;
            for (QChar c : lower) {
                if (!c.isLetter())
                    continue;
                const auto center = centers.constFind(c);
                if (center == centers.constEnd()) {
                    ok = false;
                    break;
                }
                if (path.isEmpty() || distance(*center, path.last()) > 1)
                    path << *center;
            }
            if (!ok || path.size() < 2)
                continue;
            const double candidateLength = pathLength(path);
            if (candidateLength > actualLength * 1.6 || candidateLength < actualLength * .25)
                continue;

            const QVector<QPointF> ordered = resamplePath(path, 32);
            double orderedSum = 0;
            for (int i = 0; i < actual.size(); ++i)
                orderedSum += distance(actual[i], ordered[i]);
            const double geometry = chamfer(actual, path) * chamferWeight + orderedSum / actual.size() * orderedWeight;

            // Familiar words get a head start in key widths.
            double rescore = geometry;
            if (personalSet.contains(lower))
                rescore -= keyWidth * .30;
            const int rank = dictionary.rankOf(lower);
            if (rank >= 0 && rank < 500)
                rescore -= keyWidth * .25;
            else if (rank >= 0 && rank < 4000)
                rescore -= keyWidth * .12;
            if (contextRank.contains(lower))
                rescore -= keyWidth * (contextRank.value(lower) < 3 ? .40 : .30);
            else if (bigramNext.contains(lower))
                rescore -= keyWidth * .30;
            for (const QString &letter : dwelled)
                if (lower.contains(letter + letter)) {
                    rescore -= keyWidth * .25;
                    break;
                }
            out.push_back({candidate, geometry, rescore});
        }
        std::sort(out.begin(), out.end(), [](const Scored &a, const Scored &b) { return a.geometry < b.geometry; });
        if (out.size() > 8)
            out.resize(8);
        return out;
    };

    const double thresholdNormal = keyWidth * .90, thresholdLift = keyWidth * 1.05;
    auto endsOnLift = [&](const Scored &s) { return !liftLetter.isEmpty() && s.word.toLower().endsWith(liftLetter); };
    auto pickFrom = [&](const QVector<Scored> &scored) -> std::optional<Scored> {
        QVector<Scored> passing;
        for (const Scored &s : scored)
            if (s.geometry <= thresholdLift)
                passing << s;
        if (passing.isEmpty())
            return std::nullopt;
        Scored best = *std::min_element(passing.begin(), passing.end(),
                                        [](const Scored &a, const Scored &b) { return a.rescore < b.rescore; });
        if (best.geometry > thresholdNormal && !endsOnLift(best))
            return std::nullopt;
        // Prefer a word that ends where the finger lifted when it's close.
        std::optional<Scored> liftCandidate;
        for (const Scored &s : passing)
            if (endsOnLift(s) && (!liftCandidate || s.rescore < liftCandidate->rescore))
                liftCandidate = s;
        if (liftCandidate && liftCandidate->word != best.word
            && liftCandidate->rescore <= best.rescore + keyWidth * .45)
            best = *liftCandidate;
        return best;
    };

    std::optional<Scored> picked;
    if (startLetter.isEmpty()) {
        picked = pickFrom(scoreCandidates(nullptr));
    } else {
        const QSet<QString> first{startLetter};
        picked = pickFrom(scoreCandidates(&first));
        if (!picked) {
            // The glide may have started on the edge of a neighbouring key.
            QVector<QPair<double, QString>> near;
            for (auto it = centers.begin(); it != centers.end(); ++it) {
                const double d = distance(it.value(), points.first());
                if (d <= keyWidth * 1.2 && QString(it.key()) != startLetter)
                    near.push_back({d, QString(it.key())});
            }
            std::sort(near.begin(), near.end());
            QSet<QString> neighbours;
            for (int i = 0; i < near.size() && i < 3; ++i)
                neighbours.insert(near[i].second);
            if (!neighbours.isEmpty())
                picked = pickFrom(scoreCandidates(&neighbours));
        }
    }
    if (!picked)
        return {};
    const bool ok = picked->geometry <= thresholdNormal || (endsOnLift(*picked) && picked->geometry <= thresholdLift);
    return ok ? picked->word : QString();
}

// Ported from PressPop Keyboard's Dictionary.kt and NgramModel.kt
// (MIT, Wilson Samiano). The algorithms and their thresholds are kept as
// they are on Android, so both keyboards suggest and correct alike.
#include "dictionary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

QStringList readLines(const QString &path) {
    QStringList lines;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return lines;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (!line.isEmpty())
            lines << line;
    }
    return lines;
}

QHash<QString, QString> stringMap(const QJsonObject &object) {
    QHash<QString, QString> map;
    for (auto it = object.begin(); it != object.end(); ++it)
        map.insert(it.key(), it.value().toString());
    return map;
}

QHash<QString, QStringList> listMap(const QJsonObject &object) {
    QHash<QString, QStringList> map;
    for (auto it = object.begin(); it != object.end(); ++it) {
        QStringList words;
        for (const auto &value : it.value().toArray())
            words << value.toString();
        map.insert(it.key(), words);
    }
    return map;
}

bool isAscii(const QString &word) {
    for (QChar c : word)
        if (c.unicode() > 127)
            return false;
    return true;
}

// Key positions on a QWERTY layout, rows offset like the real keyboard.
struct KeyPosition { float x = 0, y = 0; bool known = false; };

KeyPosition keyPosition(QChar c) {
    static const char *rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const float offsets[] = {0.f, .25f, .75f};
    for (int r = 0; r < 3; ++r) {
        const char *hit = std::strchr(rows[r], c.toLatin1());
        if (c.unicode() < 128 && c.unicode() != 0 && hit)
            return {float(hit - rows[r]) + offsets[r], float(r), true};
    }
    return {};
}

// 0 for the same key, 0.6 for physically adjacent keys, 1 otherwise.
float substitutionCost(QChar a, QChar b) {
    if (a == b)
        return 0.f;
    const KeyPosition pa = keyPosition(a), pb = keyPosition(b);
    if (!pa.known || !pb.known)
        return 1.f;
    const float dx = pa.x - pb.x, dy = pa.y - pb.y;
    return dx * dx + dy * dy <= 2.f ? .6f : 1.f;
}

} // namespace

Dictionary::Dictionary(const QString &dataDirectory) : m_directory(dataDirectory) {
    for (const QString &code : languages())
        m_lists.insert(code, readLines(m_directory + "/words_" + code + ".txt"));

    QFile tablesFile(m_directory + "/tables.json");
    if (tablesFile.open(QIODevice::ReadOnly)) {
        const QJsonObject tables = QJsonDocument::fromJson(tablesFile.readAll()).object();
        m_contractions = stringMap(tables.value("contractions").toObject());
        m_rankCasing = stringMap(tables.value("rankCasing").toObject());
        m_brandCasing = stringMap(tables.value("brandCasing").toObject());
        m_bigrams = listMap(tables.value("bigrams").toObject());
        m_trigrams = listMap(tables.value("trigrams").toObject());
        for (const auto &groupValue : tables.value("confusables").toArray()) {
            QStringList group;
            for (const auto &w : groupValue.toArray())
                group << w.toString();
            for (const QString &w : group) {
                QSet<QString> others(group.begin(), group.end());
                others.remove(w);
                m_confusables.insert(w, others);
            }
        }
    }

    // "a bit of a|mess,problem,false": the last three or four words, then
    // the likely next words in order.
    QFile ngrams(m_directory + "/ngram_model.txt");
    if (ngrams.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = ngrams.readAll().split('\n');
        m_ngrams.reserve(lines.size());
        for (const QByteArray &line : lines) {
            const int bar = line.indexOf('|');
            if (bar <= 0)
                continue;
            const QStringList next = QString::fromUtf8(line.mid(bar + 1)).split(',', Qt::SkipEmptyParts);
            if (!next.isEmpty())
                m_ngrams.insert(QString::fromUtf8(line.left(bar)), next);
        }
    }

    rebuildIndex();
}

void Dictionary::setLanguage(const QString &code) {
    const QString language = m_lists.contains(code) ? code : "en";
    if (language == m_language)
        return;
    m_language = language;
    rebuildIndex();
}

void Dictionary::rebuildIndex() {
    // Brand names join the pool so they can be swiped and completed.
    QStringList pool = m_lists.value(m_language);
    QStringList brands = m_brandCasing.keys();
    std::sort(brands.begin(), brands.end());
    pool += brands;

    m_wordSet.clear();
    m_rank.clear();
    m_poolOriginal.clear();
    m_poolLower.clear();
    for (int i = 0; i < pool.size(); ++i) {
        const QString lower = pool[i].toLower();
        if (!m_rank.contains(lower)) {
            m_rank.insert(lower, i);
            m_poolOriginal << pool[i];
            m_poolLower << lower;
        }
        m_wordSet.insert(lower);
    }
    m_sorted = QStringList(m_wordSet.begin(), m_wordSet.end());
    std::sort(m_sorted.begin(), m_sorted.end());
}

bool Dictionary::isWord(const QString &word) const {
    return m_wordSet.contains(word.toLower());
}

int Dictionary::rankOf(const QString &lowerWord) const {
    return m_rank.value(lowerWord, -1);
}

QString Dictionary::specialCasing(const QString &word) const {
    const QString lower = word.toLower();
    if (m_rankCasing.contains(lower))
        return m_rankCasing.value(lower);
    // A brand that is also an ordinary word ("apple") is never forced; only
    // a tap on its suggestion picks the company.
    if (m_brandCasing.contains(lower) && !m_lists.value(m_language).contains(lower))
        return m_brandCasing.value(lower);
    return {};
}

bool Dictionary::isContractionForm(const QString &word) const {
    const QString lower = word.toLower();
    for (const QString &value : m_contractions)
        if (value.toLower() == lower)
            return true;
    return false;
}

QString Dictionary::apostropheTwin(const QString &word) const {
    const QString lower = word.toLower();
    if (lower.contains('\''))
        return {};
    for (const QString &candidate : m_poolOriginal)
        if (candidate.contains('\'') && QString(candidate).remove('\'') == lower)
            return candidate;
    return {};
}

QStringList Dictionary::completions(const QString &prefix, const QStringList &personal,
                                    const QStringList &boost) const {
    if (prefix.size() < 2)
        return {};
    const QString p = prefix.toLower();

    // Personal words keep their own casing ("Angel").
    QSet<QString> personalLower;
    QStringList personalHits;
    for (const QString &word : personal) {
        const QString lower = word.toLower();
        personalLower.insert(lower);
        if (lower.startsWith(p))
            personalHits << word;
    }
    QStringList boosted;
    for (const QString &word : boost) {
        const QString lower = word.toLower();
        if (lower.startsWith(p) && !personalLower.contains(lower) && m_wordSet.contains(lower)
            && !boosted.contains(lower))
            boosted << lower;
    }

    auto it = std::lower_bound(m_sorted.begin(), m_sorted.end(), p);
    QStringList hits;
    for (; it != m_sorted.end() && it->startsWith(p); ++it)
        if (!personalLower.contains(*it) && !boosted.contains(*it))
            hits << *it;
    std::stable_sort(hits.begin(), hits.end(), [this](const QString &a, const QString &b) {
        return m_rank.value(a, INT_MAX) < m_rank.value(b, INT_MAX);
    });
    QStringList apostrophe, common;
    for (const QString &word : hits)
        (word.contains('\'') ? apostrophe : common) << word;

    QStringList result;
    for (const QString &word : personalHits + boosted + apostrophe + common) {
        const QString lower = word.toLower();
        const QString cased = m_rankCasing.value(lower, m_brandCasing.value(lower, word));
        if (!result.contains(cased))
            result << cased;
        if (result.size() == 5)
            break;
    }
    return result;
}

int Dictionary::levenshtein(const QString &a, const QString &b) {
    // Optimal string alignment: an adjacent transposition is one edit, so
    // "teh" is one edit from "the".
    std::vector<std::vector<int>> dp(a.size() + 1, std::vector<int>(b.size() + 1));
    for (int i = 0; i <= a.size(); ++i) dp[i][0] = i;
    for (int j = 0; j <= b.size(); ++j) dp[0][j] = j;
    for (int i = 1; i <= a.size(); ++i)
        for (int j = 1; j <= b.size(); ++j) {
            dp[i][j] = std::min({dp[i - 1][j] + 1, dp[i][j - 1] + 1,
                                 dp[i - 1][j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                dp[i][j] = std::min(dp[i][j], dp[i - 2][j - 2] + 1);
        }
    return dp[a.size()][b.size()];
}

float Dictionary::weightedDistance(const QString &a, const QString &b, float limit) {
    // Keyboard-aware OSA distance with three rolling rows and an early exit
    // once two consecutive rows are past the limit (a transposition reads
    // two rows back).
    const int n = a.size(), m = b.size();
    std::vector<float> prev2(m + 1), prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j) prev[j] = float(j);
    float previousMin = 0.f;
    for (int i = 1; i <= n; ++i) {
        cur[0] = float(i);
        float rowMin = cur[0];
        for (int j = 1; j <= m; ++j) {
            float v = std::min({prev[j] + 1.f, cur[j - 1] + 1.f,
                                prev[j - 1] + substitutionCost(a[i - 1], b[j - 1])});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                v = std::min(v, prev2[j - 2] + .6f);
            cur[j] = v;
            rowMin = std::min(rowMin, v);
        }
        if (rowMin > limit && previousMin > limit)
            return std::numeric_limits<float>::max();
        previousMin = rowMin;
        std::swap(prev2, prev);
        std::swap(prev, cur);
    }
    return prev[m];
}

QString Dictionary::bestCorrection(const QString &word, const QStringList &personal,
                                   const QStringList &boost) const {
    const QString w = word.toLower();
    if (w.size() < 3 || !isAscii(w) || m_wordSet.contains(w))
        return {};
    for (const QString &p : personal)
        if (p.toLower() == w)
            return {};

    // Frequency only breaks ties inside one edit-cost step: a two-edit word
    // never beats a one-edit word.
    const float limit = w.size() <= 4 ? 1.f : 2.f;
    const int maxLengthGap = int(limit);
    const float rankScale = std::log(1.f + m_wordSet.size());
    QSet<QString> boostLower;
    for (const QString &b : boost)
        boostLower.insert(b.toLower());

    QString best;
    float bestScore = std::numeric_limits<float>::max();
    auto consider = [&](const QString &candidate, int rank, bool isPersonal) {
        const QString lower = candidate.toLower();
        if (std::abs(lower.size() - w.size()) > maxLengthGap)
            return;
        const float distance = weightedDistance(w, lower, limit);
        if (distance > limit)
            return;
        const float frequencyPenalty = rank < 0 ? .5f : .5f * std::log(1.f + rank) / rankScale;
        float score = distance + frequencyPenalty - (isPersonal ? .2f : 0.f);
        if (boostLower.contains(lower))
            score -= .35f;
        if (score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    };
    for (const QString &p : personal)
        consider(p, -1, true);
    for (int i = 0; i < m_poolLower.size(); ++i)
        consider(m_poolOriginal[i], i, false);
    return best;
}

QString Dictionary::contextualAlternative(const QString &word, const QStringList &predicted) const {
    const QString w = word.toLower();
    if (w.size() < 2 || predicted.isEmpty() || !m_wordSet.contains(w))
        return {};
    QStringList top;
    for (const QString &p : predicted) {
        const QString lower = p.toLower();
        if (!top.contains(lower))
            top << lower;
        if (top.size() >= 8)
            break;
    }
    if (top.contains(w))
        return {};
    const int wordRank = m_rank.value(w, INT_MAX);
    const QSet<QString> confusables = m_confusables.value(w);
    for (const QString &candidate : top) {
        if (candidate == w)
            continue;
        // Known mix-ups (their/there) are the point, even for common words.
        if (confusables.contains(candidate))
            return candidate;
        if (!m_wordSet.contains(candidate) || wordRank < 150 || w.size() < 3 || candidate.size() < 3)
            continue;
        if (std::abs(candidate.size() - w.size()) > 1)
            continue;
        const int candidateRank = m_rank.value(candidate, INT_MAX);
        if (candidateRank >= wordRank)
            continue;
        if (weightedDistance(w, candidate, .6f) <= .6f)
            return candidate;
    }
    return {};
}

QString Dictionary::bestSwipeMatch(const QString &traced, const QStringList &personal) const {
    const QString t = traced.toLower();
    if (t.size() < 3)
        return {};
    if (m_wordSet.contains(t))
        return m_poolOriginal.value(m_rank.value(t), t);
    for (const QString &p : personal)
        if (p.toLower() == t)
            return p;

    const QStringList candidates = personal + m_poolOriginal;
    QString best;
    int bestDistance = INT_MAX;
    for (const QString &candidate : candidates) {
        const QString lower = candidate.toLower();
        if (lower.isEmpty() || lower.front() != t.front() || lower.back() != t.back())
            continue;
        const int d = levenshtein(t, lower);
        if (d < bestDistance) {
            bestDistance = d;
            best = candidate;
        }
    }
    if (bestDistance <= 2)
        return best;

    // A trace crosses extra keys on the way, so look for a word that appears
    // in order inside it, starting on the first traced letter; the most
    // compact span wins, then the longest word.
    QString subBest;
    int subBestSpan = INT_MAX;
    for (const QString &candidate : candidates) {
        const QString c = candidate.toLower();
        if (c.size() < 3 || c.front() != t.front())
            continue;
        int ti = 0, first = -1, last = -1;
        bool matched = true;
        for (QChar ch : c) {
            while (ti < t.size() && t[ti] != ch)
                ++ti;
            if (ti >= t.size()) {
                matched = false;
                break;
            }
            if (first < 0)
                first = ti;
            last = ti++;
        }
        if (!matched)
            continue;
        const int span = last - first;
        if (subBest.isEmpty() || span < subBestSpan || (span == subBestSpan && c.size() > subBest.size())) {
            subBest = c;
            subBestSpan = span;
        }
    }
    return subBest;
}

QStringList Dictionary::ngramNext(const QStringList &words) const {
    if (words.size() >= 4) {
        const auto it = m_ngrams.constFind(words.mid(words.size() - 4).join(' '));
        if (it != m_ngrams.constEnd())
            return *it;
    }
    if (words.size() >= 3) {
        const auto it = m_ngrams.constFind(words.mid(words.size() - 3).join(' '));
        if (it != m_ngrams.constEnd())
            return *it;
    }
    return {};
}

QStringList Dictionary::trigramNext(const QString &previous2, const QString &previous) const {
    if (previous2.isEmpty())
        return {};
    return m_trigrams.value(previous2 + ' ' + previous);
}

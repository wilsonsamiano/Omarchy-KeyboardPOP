#include "personal.h"

#include "dictionary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace {

bool allLetters(const QString &word) {
    return !word.isEmpty() && std::all_of(word.begin(), word.end(), [](QChar c) { return c.isLetter(); });
}

} // namespace

Personal::Personal(const Dictionary &dictionary, const QString &path, QObject *parent)
    : QObject(parent), m_dictionary(dictionary),
      m_path(path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                  + "/keyboardpop/personal.json"
                            : path) {
    // Learning happens on every Space, so writes are batched a few seconds.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(3000);
    connect(&m_saveTimer, &QTimer::timeout, this, &Personal::flush);
    load();
}

Personal::~Personal() {
    flush();
}

void Personal::load() {
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly))
        importJson(file.readAll());
    m_dirty = false;
}

QString Personal::displayCase(const QString &lower) const {
    // A learned word typed capitalized mid-sentence most of the time is a
    // name, so it is offered capitalized.
    const int capitalized = m_caps.value(lower), total = m_counts.value(lower);
    if (capitalized >= 3 && capitalized * 2 >= total && !lower.isEmpty())
        return lower.left(1).toUpper() + lower.mid(1);
    return lower;
}

QStringList Personal::allWords() const {
    QStringList words = m_manual;
    QStringList learned(m_learned.begin(), m_learned.end());
    std::sort(learned.begin(), learned.end());
    for (const QString &w : learned) {
        const QString shown = displayCase(w);
        if (!words.contains(shown, Qt::CaseInsensitive))
            words << shown;
    }
    return words;
}

bool Personal::isPersonal(const QString &word) const {
    const QString lower = word.toLower();
    return m_learned.contains(lower) || m_manual.contains(word, Qt::CaseInsensitive);
}

void Personal::addManual(const QString &word) {
    const QString w = word.trimmed();
    if (w.isEmpty() || m_manual.contains(w, Qt::CaseInsensitive))
        return;
    m_manual << w;
    m_dirty = true;
    flush();
}

void Personal::removeWord(const QString &word) {
    const QString lower = word.trimmed().toLower();
    m_manual.removeIf([&](const QString &w) { return w.toLower() == lower; });
    m_counts.remove(lower);
    m_caps.remove(lower);
    m_learned.remove(lower);
    m_dirty = true;
    flush();
}

bool Personal::count(const QString &word, int weight, bool capitalizedMidSentence, bool *crossed) {
    const QString w = word.toLower();
    if (w.size() < 3 || !allLetters(w) || m_dictionary.isWord(w) || m_manual.contains(w, Qt::CaseInsensitive))
        return false;
    const int before = m_counts.value(w);
    const int after = before + weight;
    m_counts.insert(w, after);
    if (capitalizedMidSentence)
        m_caps.insert(w, m_caps.value(w) + weight);
    if (after >= LearnAt)
        m_learned.insert(w);
    *crossed = before < LearnAt && after >= LearnAt;
    scheduleSave();
    return true;
}

bool Personal::noteTyped(const QString &word, bool capitalizedMidSentence) {
    bool crossed = false;
    count(word, 1, capitalizedMidSentence, &crossed);
    return crossed;
}

bool Personal::noteKept(const QString &word, bool capitalizedMidSentence) {
    bool crossed = false;
    count(word, KeptWeight, capitalizedMidSentence, &crossed);
    return crossed;
}

void Personal::notePair(const QString &previous, const QString &next) {
    const QString a = previous.toLower(), b = next.toLower();
    if (!allLetters(a) || !allLetters(b))
        return;
    m_pairs[a][b] += 1;
    scheduleSave();
}

QStringList Personal::learnedNextWords(const QString &previous) const {
    const auto pairs = m_pairs.value(previous.toLower());
    QStringList words = pairs.keys();
    std::sort(words.begin(), words.end(), [&](const QString &x, const QString &y) {
        const int cx = pairs.value(x), cy = pairs.value(y);
        return cx != cy ? cx > cy : x < y;
    });
    return words;
}

QByteArray Personal::exportJson() const {
    QJsonObject json{{"format", "safepop-backup"}, {"version", 1}};
    QStringList manual = m_manual, counts, caps, pairs;
    std::sort(manual.begin(), manual.end());
    for (auto it = m_counts.begin(); it != m_counts.end(); ++it)
        counts << it.key() + ':' + QString::number(it.value());
    for (auto it = m_caps.begin(); it != m_caps.end(); ++it)
        caps << it.key() + ':' + QString::number(it.value());
    for (auto it = m_pairs.begin(); it != m_pairs.end(); ++it)
        for (auto jt = it.value().begin(); jt != it.value().end(); ++jt)
            pairs << it.key() + ':' + jt.key() + ':' + QString::number(jt.value());
    counts.sort();
    caps.sort();
    pairs.sort();
    json.insert("manual", QJsonArray::fromStringList(manual));
    json.insert("counts", QJsonArray::fromStringList(counts));
    json.insert("caps", QJsonArray::fromStringList(caps));
    json.insert("bigrams", QJsonArray::fromStringList(pairs));
    return QJsonDocument(json).toJson(QJsonDocument::Indented);
}

int Personal::importJson(const QByteArray &data) {
    const QJsonObject json = QJsonDocument::fromJson(data).object();
    int added = 0;
    for (const auto &value : json.value("manual").toArray()) {
        const QString w = value.toString().trimmed();
        if (!w.isEmpty() && !m_manual.contains(w, Qt::CaseInsensitive)) {
            m_manual << w;
            ++added;
        }
    }
    auto merge = [](QHash<QString, int> &into, const QJsonArray &entries) {
        for (const auto &value : entries) {
            const QStringList parts = value.toString().split(':');
            bool ok = false;
            const int n = parts.value(1).toInt(&ok);
            if (parts.size() == 2 && ok && allLetters(parts[0]) && n > into.value(parts[0]))
                into.insert(parts[0], n);
        }
    };
    const int learnedBefore = m_learned.size();
    merge(m_counts, json.value("counts").toArray());
    merge(m_caps, json.value("caps").toArray());
    for (auto it = m_counts.begin(); it != m_counts.end(); ++it)
        if (it.value() >= LearnAt)
            m_learned.insert(it.key());
    added += m_learned.size() - learnedBefore;
    for (const auto &value : json.value("bigrams").toArray()) {
        const QStringList parts = value.toString().split(':');
        bool ok = false;
        const int n = parts.value(2).toInt(&ok);
        if (parts.size() == 3 && ok && n > m_pairs[parts[0]].value(parts[1]))
            m_pairs[parts[0]].insert(parts[1], n);
    }
    m_dirty = true;
    return added;
}

void Personal::scheduleSave() {
    m_dirty = true;
    m_saveTimer.start();
}

void Personal::flush() {
    m_saveTimer.stop();
    if (!m_dirty)
        return;
    // Written beside the target and renamed over it, so a crash mid-write
    // never leaves a half-written dictionary.
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly) && file.write(exportJson()) >= 0 && file.commit())
        m_dirty = false;
}

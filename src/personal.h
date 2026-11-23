#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

class Dictionary;

// The personal keyboard, ported from PressPop's PersonalDictionary.kt:
// words added by hand are active at once, and an unknown word typed often
// enough (77 times, or sooner when a suggested correction is turned down)
// is learned, so it stops being corrected away. Word pairs the user types
// feed next-word prediction.
//
// Stored only on this machine, as the same JSON the Android app exports
// ("safepop-backup"), so a phone's words can be imported as they are.
class Personal : public QObject {
    Q_OBJECT

public:
    static constexpr int LearnAt = 77;
    static constexpr int KeptWeight = 10;

    explicit Personal(const Dictionary &dictionary, const QString &path = {}, QObject *parent = nullptr);
    ~Personal() override;

    QString path() const { return m_path; }

    // Manual words in their own casing, then learned words.
    QStringList allWords() const;
    bool isPersonal(const QString &word) const;

    void addManual(const QString &word);
    void removeWord(const QString &word);
    // True when this use just taught the keyboard the word.
    bool noteTyped(const QString &word, bool capitalizedMidSentence = false);
    bool noteKept(const QString &word, bool capitalizedMidSentence = false);
    void notePair(const QString &previous, const QString &next);
    QStringList learnedNextWords(const QString &previous) const;

    QByteArray exportJson() const;
    // Merges a backup in, keeping the larger count; returns words added.
    int importJson(const QByteArray &json);

    // Writes now instead of after the usual short delay.
    void flush();

private:
    bool count(const QString &word, int weight, bool capitalizedMidSentence, bool *crossed);
    QString displayCase(const QString &lower) const;
    void load();
    void scheduleSave();

    const Dictionary &m_dictionary;
    QString m_path;
    QStringList m_manual;
    QHash<QString, int> m_counts, m_caps;
    QHash<QString, QHash<QString, int>> m_pairs;
    QSet<QString> m_learned;
    QTimer m_saveTimer;
    bool m_dirty = false;
};

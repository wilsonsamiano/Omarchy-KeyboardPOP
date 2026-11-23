#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

// The word knowledge behind suggestions, autocorrect, and swipe typing,
// ported from PressPop's Dictionary.kt and NgramModel.kt. Everything is
// on-device: the word lists and phrase tables are compiled into the binary.
class Dictionary {
public:
    // Loads from a directory holding words_<code>.txt, tables.json and
    // ngram_model.txt; the app uses the compiled-in ":/data".
    explicit Dictionary(const QString &dataDirectory = ":/data");

    QString language() const { return m_language; }
    void setLanguage(const QString &code);
    static QStringList languages() { return {"en", "tl", "es"}; }

    bool isWord(const QString &word) const;
    int rankOf(const QString &lowerWord) const; // -1 when not a word

    // Up to five words starting with prefix: personal first, then the
    // context's predictions, then apostrophe forms, then by frequency.
    QStringList completions(const QString &prefix, const QStringList &personal = {},
                            const QStringList &boost = {}) const;
    // The closest word for a typo, or empty when it is a word or nothing is close.
    QString bestCorrection(const QString &word, const QStringList &personal = {},
                           const QStringList &boost = {}) const;
    // A valid word the context says is probably another (form/from, their/there).
    QString contextualAlternative(const QString &word, const QStringList &predicted) const;
    // The word a traced letter sequence most likely spells, by letters only.
    QString bestSwipeMatch(const QString &traced, const QStringList &personal = {}) const;
    // "well" -> "we'll": a swipe cannot see the apostrophe.
    QString apostropheTwin(const QString &word) const;

    // Rank abbreviations and brand names, or empty.
    QString specialCasing(const QString &word) const;
    QString contraction(const QString &lowerWord) const { return m_contractions.value(lowerWord); }
    bool isContractionForm(const QString &word) const;

    // Next words for the words before the cursor (n-gram, then the phrase
    // and pair tables).
    QStringList ngramNext(const QStringList &words) const;
    QStringList trigramNext(const QString &previous2, const QString &previous) const;
    QStringList bigramNext(const QString &previous) const { return m_bigrams.value(previous); }

    // Swipe candidates in frequency order, original casing and lowercase.
    const QStringList &swipeWords() const { return m_poolOriginal; }
    const QStringList &swipeWordsLower() const { return m_poolLower; }

    static int levenshtein(const QString &a, const QString &b);
    static float weightedDistance(const QString &a, const QString &b, float limit);

private:
    void rebuildIndex();

    QString m_directory;
    QString m_language = "en";
    QHash<QString, QStringList> m_lists; // language -> words by frequency
    QHash<QString, QString> m_contractions, m_rankCasing, m_brandCasing;
    QHash<QString, QStringList> m_bigrams, m_trigrams, m_ngrams;
    QHash<QString, QSet<QString>> m_confusables;

    // Index of the active list plus brand names.
    QSet<QString> m_wordSet;
    QHash<QString, int> m_rank;
    QStringList m_sorted, m_poolOriginal, m_poolLower;
};

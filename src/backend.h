#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <functional>
#include <optional>

#include "textsink.h"

class Dictionary;
class Personal;

// What happens when a key is pressed, ported from PressPop's
// PressPopIME.kt: the word being typed, auto-capitalization, Space-time
// fixes (I, contractions, rank and brand casing), suggested corrections,
// next-word prediction, and learning. Exposed to QML as `backend`; the
// text itself goes to a TextSink.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList suggestions READ suggestions NOTIFY suggestionsChanged)
    Q_PROPERTY(bool shiftOn READ shiftOn NOTIFY shiftChanged)
    Q_PROPERTY(bool shiftLock READ shiftLock NOTIFY shiftChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)
    Q_PROPERTY(bool password READ password NOTIFY suggestionsChanged)
    Q_PROPERTY(int modifiers READ modifiers NOTIFY modifiersChanged)

public:
    Backend(Dictionary &dictionary, Personal &personal, QObject *parent = nullptr);

    void setSink(TextSink *sink) { m_sink = sink; }
    // Tests drive time by hand; the app uses a monotonic clock.
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }

    const Dictionary &dictionary() const { return m_dictionary; }
    QStringList suggestions() const { return m_suggestions; }
    bool shiftOn() const { return m_shiftOn; }
    bool shiftLock() const { return m_shiftLock; }
    // Ctrl, Alt, and Super latched from the control row (TextSink bits):
    // the next key goes out with them held, then they let go.
    int modifiers() const { return m_modifiers; }
    QString language() const;
    QString status() const { return m_status; }
    bool statusIsError() const { return m_statusIsError; }
    bool password() const;
    QString typedWord() const { return m_typed; }

    // What the swipe matcher needs to know about the context.
    QStringList personalWords() const;
    QString previousWord() const { return m_previousWord; }
    QStringList contextWords() const { return m_contextWords; }

    Q_INVOKABLE void typeCharacter(const QString &text);
    Q_INVOKABLE void space();
    Q_INVOKABLE void backspace();
    Q_INVOKABLE void deleteWord();
    Q_INVOKABLE void enter();
    Q_INVOKABLE void swipeWord(const QString &word);
    Q_INVOKABLE void moveCursor(int columns, int lines);
    Q_INVOKABLE void pickSuggestion(int index);
    Q_INVOKABLE void toggleShift();
    Q_INVOKABLE void toggleModifier(int modifier);
    // A key from the control row: an XKB keysym name like "Escape" or "Left".
    Q_INVOKABLE void controlKey(const QString &key);
    Q_INVOKABLE void cycleLanguage();

    // A new field or a freshly shown keyboard: forget the word in progress.
    void reset();
    // The text around the cursor changed outside the keyboard.
    void contextChanged();
    void setStatus(const QString &status, bool error = false);
    void setLanguage(const QString &code);

signals:
    void suggestionsChanged();
    void shiftChanged();
    void modifiersChanged();
    void languageChanged();
    void statusChanged();

private:
    // Sends `key` as a shortcut when a modifier is latched, then lets the
    // modifiers go. False when nothing is latched.
    bool sendLatched(const QString &key);
    int m_modifiers = 0;
    struct Correction { QString original, replacement; };

    std::optional<QString> before() const;
    QString finishWord();
    void replaceBefore(int characters, const QString &text);
    void recordCommittedWord(const QString &word);
    void learnWord(const QString &word);
    bool offerRealWordFix(const QString &word);
    bool tryUndoLastCorrection();
    QString capitalizeForSentenceStart(const QString &word, int charactersBeforeWord) const;
    QString applyShift(const QString &word);
    std::optional<QString> fieldWordBeforeCursor() const;
    QStringList lastWordsBeforeCursor() const;
    QStringList contextWordsBeforeCurrent() const;
    QStringList predictedNext(const QStringList &context) const;
    void setShift(bool on, bool lock);
    void updateAutoShift();
    void refresh();
    qint64 now() const { return m_clock(); }

    Dictionary &m_dictionary;
    Personal &m_personal;
    TextSink *m_sink = nullptr;
    std::function<qint64()> m_clock;
    QElapsedTimer m_elapsed;

    QString m_typed;
    bool m_typedHadUpper = false;
    bool m_autoSpaced = false; // a space this keyboard added after a word
    bool m_shiftOn = false, m_shiftLock = false;
    qint64 m_lastSpace = -100000, m_lastShift = -100000;

    std::optional<Correction> m_pending;        // suggested, not applied
    std::optional<Correction> m_lastCorrected;  // applied; Delete undoes it
    std::optional<Correction> m_swipeAlternative;

    QString m_lastCommitted;
    QStringList m_history;
    QString m_previousWord;
    QStringList m_contextWords;
    QStringList m_suggestions{"", "", ""};
    QString m_status;
    bool m_statusIsError = false;
};

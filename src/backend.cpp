// Ported from PressPop Keyboard's PressPopIME.kt (MIT, Wilson Samiano).
#include "backend.h"

#include "dictionary.h"
#include "personal.h"

#include <QRegularExpression>
#include <algorithm>

namespace {

bool isAscii(const QString &word) {
    return std::all_of(word.begin(), word.end(), [](QChar c) { return c.unicode() < 128; });
}

QString trimEnd(const QString &text, const QString &characters = " \t\n\r") {
    int end = text.size();
    while (end > 0 && characters.contains(text[end - 1]))
        --end;
    return text.left(end);
}

// Shift turns on after ". ", "? ", "! " and at the start of a field.
bool isCapitalizeStart(const QString &text) {
    const QString trimmed = trimEnd(text);
    return trimmed.isEmpty() || QStringLiteral(".?!").contains(trimmed.back());
}

bool endsSentence(const QString &text) {
    const QString trimmed = trimEnd(text, " \t");
    return trimmed.isEmpty() || QStringLiteral("\n.?!").contains(trimmed.back());
}

QStringList wordsIn(const QString &text) {
    static const QRegularExpression word("[A-Za-z']+");
    QStringList words;
    for (auto it = word.globalMatch(text); it.hasNext();)
        words << it.next().captured().toLower();
    return words;
}

QString capitalizeFirst(const QString &word) {
    return word.isEmpty() ? word : word.left(1).toUpper() + word.mid(1);
}

// "I" is always a capital in English.
QString pronounI(const QString &word) {
    if (word == "i")
        return "I";
    static const QHash<QString, QString> forms{{"i'm", "I'm"}, {"i've", "I've"}, {"i'll", "I'll"}, {"i'd", "I'd"}};
    return forms.value(word.toLower());
}

bool isWordCharacter(QChar c) {
    return c.isLetter() || c == '\'';
}

const QString Punctuation = QStringLiteral(".,!?;:");

} // namespace

Backend::Backend(Dictionary &dictionary, Personal &personal, QObject *parent)
    : QObject(parent), m_dictionary(dictionary), m_personal(personal) {
    m_elapsed.start();
    m_clock = [this] { return m_elapsed.elapsed(); };
}

QString Backend::language() const {
    return m_dictionary.language();
}

void Backend::setLanguage(const QString &code) {
    if (code == m_dictionary.language())
        return;
    m_dictionary.setLanguage(code);
    emit languageChanged();
    refresh();
}

void Backend::cycleLanguage() {
    const QStringList codes = Dictionary::languages();
    setLanguage(codes.value((codes.indexOf(language()) + 1) % codes.size()));
}

void Backend::setStatus(const QString &status, bool error) {
    if (status == m_status && error == m_statusIsError)
        return;
    m_status = status;
    m_statusIsError = error;
    emit statusChanged();
}

bool Backend::password() const {
    return m_sink && m_sink->isPassword();
}

QStringList Backend::personalWords() const {
    return m_personal.allWords();
}

std::optional<QString> Backend::before() const {
    return m_sink ? m_sink->textBeforeCursor() : std::nullopt;
}

void Backend::reset() {
    if (m_modifiers) {
        m_modifiers = 0;
        emit modifiersChanged();
    }
    m_typed.clear();
    m_typedHadUpper = false;
    m_autoSpaced = false;
    m_pending.reset();
    m_lastCorrected.reset();
    m_swipeAlternative.reset();
    m_lastCommitted.clear();
    m_history.clear();
    if (!m_shiftLock)
        setShift(false, false);
    updateAutoShift();
    refresh();
}

void Backend::contextChanged() {
    updateAutoShift();
    refresh();
}

void Backend::setShift(bool on, bool lock) {
    if (on == m_shiftOn && lock == m_shiftLock)
        return;
    m_shiftOn = on;
    m_shiftLock = lock;
    emit shiftChanged();
}

void Backend::toggleShift() {
    // Double-tap is caps lock; a tap while locked turns it all off.
    const qint64 t = now();
    if (m_shiftLock)
        setShift(false, false);
    else if (t - m_lastShift < 350)
        setShift(true, true);
    else
        setShift(!m_shiftOn, false);
    m_lastShift = t;
}

void Backend::updateAutoShift() {
    if (m_shiftLock || !m_sink || m_sink->isPassword() || !m_sink->wantsAutoCapitalization())
        return;
    const auto text = before();
    if (text && isCapitalizeStart(*text))
        setShift(true, false);
}

std::optional<QString> Backend::fieldWordBeforeCursor() const {
    const auto text = before();
    if (!text)
        return std::nullopt;
    int start = text->size();
    while (start > 0 && isWordCharacter(text->at(start - 1)))
        --start;
    if (start == text->size())
        return std::nullopt;
    return text->mid(start);
}

QStringList Backend::lastWordsBeforeCursor() const {
    if (password())
        return {};
    if (const auto text = before()) {
        if (endsSentence(*text))
            return {};
        const QStringList words = wordsIn(*text);
        if (!words.isEmpty())
            return words.mid(std::max<qsizetype>(0, words.size() - 4));
    }
    return m_history;
}

QStringList Backend::contextWordsBeforeCurrent() const {
    if (password())
        return {};
    if (const auto text = before()) {
        int start = text->size();
        while (start > 0 && isWordCharacter(text->at(start - 1)))
            --start;
        const QString head = text->left(start);
        if (endsSentence(head))
            return {};
        const QStringList words = wordsIn(head);
        if (!words.isEmpty())
            return words.mid(std::max<qsizetype>(0, words.size() - 4));
    }
    return m_history.mid(std::max<qsizetype>(0, m_history.size() - 4));
}

QStringList Backend::predictedNext(const QStringList &context) const {
    if (context.isEmpty())
        return {};
    const QString previous = context.last();
    const QString previous2 = context.size() >= 2 ? context[context.size() - 2] : QString();
    // The user's own sentences first, then the language model, then the tables.
    QStringList all = m_personal.learnedNextWords(previous) + m_dictionary.ngramNext(context)
                      + m_dictionary.trigramNext(previous2, previous) + m_dictionary.bigramNext(previous);
    all.removeDuplicates();
    return all.mid(0, 12);
}

QString Backend::capitalizeForSentenceStart(const QString &word, int charactersBeforeWord) const {
    if (word.isEmpty() || m_dictionary.isContractionForm(word) || !m_sink || !m_sink->wantsAutoCapitalization())
        return word;
    const auto text = before();
    if (!text)
        return m_typedHadUpper ? capitalizeFirst(word) : word;
    const QString beforeWord = text->left(text->size() - std::min<qsizetype>(charactersBeforeWord, text->size()));
    return isCapitalizeStart(beforeWord) ? capitalizeFirst(word) : word;
}

// A swiped or picked word follows Shift like typed letters do.
QString Backend::applyShift(const QString &word) {
    if (m_shiftLock)
        return word.toUpper();
    if (m_shiftOn) {
        setShift(false, false);
        return capitalizeFirst(word);
    }
    return word;
}

void Backend::replaceBefore(int characters, const QString &text) {
    m_sink->deleteBefore(characters);
    m_sink->commitText(text);
}

void Backend::learnWord(const QString &word) {
    const bool capitalizedMidSentence = !word.isEmpty() && word[0].isUpper()
                                        && !contextWordsBeforeCurrent().isEmpty()
                                        && m_dictionary.specialCasing(word.toLower()) != word;
    m_personal.noteTyped(word, capitalizedMidSentence);
}

void Backend::recordCommittedWord(const QString &word) {
    if (password())
        return;
    const QString w = word.trimmed().toLower();
    if (w.isEmpty())
        return;
    if (!m_lastCommitted.isEmpty())
        m_personal.notePair(m_lastCommitted, w);
    m_lastCommitted = w;
    m_history << w;
    while (m_history.size() > 4)
        m_history.removeFirst();
}

bool Backend::offerRealWordFix(const QString &word) {
    if (word.size() < 2 || password() || !isAscii(word))
        return false;
    if (std::any_of(word.begin() + 1, word.end(), [](QChar c) { return c.isUpper(); }))
        return false;
    if (!m_dictionary.isWord(word) || m_personal.isPersonal(word))
        return false;
    const QString alternative = m_dictionary.contextualAlternative(word, predictedNext(contextWordsBeforeCurrent()));
    if (alternative.isEmpty())
        return false;
    const QString special = m_dictionary.specialCasing(alternative);
    m_pending = Correction{word, special.isEmpty() ? capitalizeForSentenceStart(alternative, word.size()) : special};
    return true;
}

// The word just typed is complete (Space, Enter, punctuation): fix what is
// always fixed, and suggest what might be a typo. Returns the word as it
// now stands in the field.
QString Backend::finishWord() {
    const QString word = m_typed;
    const auto fieldWord = fieldWordBeforeCursor();
    const QString check = fieldWord.value_or(word);
    const bool wholeWord = !fieldWord || fieldWord->toLower() == word.toLower();

    const QString pronoun = pronounI(word);
    if (wholeWord && !pronoun.isEmpty() && word != pronoun) {
        replaceBefore(word.size(), pronoun);
        return pronoun;
    }
    const QString lower = word.toLower();
    const QString contraction = m_dictionary.contraction(lower);
    const QString special = m_dictionary.specialCasing(lower);
    QString personalCased;
    for (const QString &p : m_personal.allWords())
        if (p.toLower() == lower && p != word) {
            personalCased = p;
            break;
        }

    if (wholeWord && !special.isEmpty() && word != special) {
        replaceBefore(word.size(), special);
        return special;
    }
    if (wholeWord && !personalCased.isEmpty()) {
        replaceBefore(word.size(), personalCased);
        return personalCased;
    }
    if (wholeWord && !contraction.isEmpty()) {
        replaceBefore(word.size(), contraction);
        m_lastCorrected = Correction{word, contraction};
        return contraction;
    }
    const bool innerCapital = std::any_of(check.begin() + std::min<qsizetype>(1, check.size()), check.end(),
                                          [](QChar c) { return c.isUpper(); });
    if (!m_dictionary.isWord(check) && !m_personal.isPersonal(check) && isAscii(check) && !innerCapital) {
        // Suggested in the strip, never replaced: a tap applies it, and
        // turning it down counts towards learning the word.
        const QString correction = m_dictionary.bestCorrection(check, m_personal.allWords());
        if (!correction.isEmpty()) {
            const QString correctionCasing = m_dictionary.specialCasing(correction);
            m_pending = Correction{check, correctionCasing.isEmpty()
                                              ? capitalizeForSentenceStart(correction, check.size())
                                              : correctionCasing};
        }
        learnWord(check);
    } else {
        learnWord(check);
        offerRealWordFix(check);
    }
    return check;
}

void Backend::typeCharacter(const QString &text) {
    if (!m_sink || text.isEmpty())
        return;
    // Shortcuts use the plain key. Shift stays out of them: it is often on
    // by itself at the start of a field, which would turn Ctrl+C into
    // Ctrl+Shift+C.
    if (m_modifiers && sendLatched(text.toLower()))
        return;
    m_lastCorrected.reset();
    m_swipeAlternative.reset();
    const QChar first = text.front();
    const bool continuesWord = first.isLetter() || (!m_typed.isEmpty() && (first == '\'' || first.isDigit()));

    if (continuesWord) {
        if (m_typed.isEmpty())
            m_typedHadUpper = false;
        m_pending.reset();
        m_typed += text;
        m_typedHadUpper = m_typedHadUpper || first.isUpper();
        m_sink->commitText(text);
        m_autoSpaced = false;
        if (m_shiftOn && !m_shiftLock)
            setShift(false, false);
    } else {
        if (!m_typed.isEmpty() && !password() && Punctuation.contains(first)) {
            const QString committed = finishWord();
            recordCommittedWord(committed);
        } else {
            m_pending.reset();
        }
        m_typed.clear();
        // "word " + "." reads "word. ": the space a swipe or a pick added
        // moves after the mark.
        if (m_autoSpaced && Punctuation.contains(first) && text.size() == 1) {
            m_sink->deleteBefore(1);
            m_sink->commitText(text + ' ');
        } else {
            m_sink->commitText(text);
            m_autoSpaced = false;
        }
    }
    updateAutoShift();
    refresh();
}

void Backend::space() {
    if (!m_sink || sendLatched("space"))
        return;
    const qint64 t = now();
    // Double-tap Space ends the sentence.
    if (m_typed.isEmpty() && t - m_lastSpace < 400 && !password()) {
        int spaces = 1;
        if (const auto text = before()) {
            int trailing = 0;
            while (trailing < text->size() && text->at(text->size() - 1 - trailing) == ' ')
                ++trailing;
            if (trailing > 0)
                spaces = trailing;
        }
        m_sink->deleteBefore(spaces);
        m_sink->commitText(". ");
        m_lastSpace = -100000;
        m_pending.reset();
        m_autoSpaced = true;
        updateAutoShift();
        refresh();
        return;
    }
    m_lastSpace = t;
    m_pending.reset();
    if (password()) {
        m_sink->commitText(" ");
        m_typed.clear();
        refresh();
        return;
    }
    if (!m_typed.isEmpty())
        recordCommittedWord(finishWord());
    m_sink->commitText(" ");
    m_typed.clear();
    m_autoSpaced = false;
    updateAutoShift();
    refresh();
}

bool Backend::tryUndoLastCorrection() {
    if (password() || !m_lastCorrected || !m_typed.isEmpty())
        return false;
    const auto text = before();
    const Correction corrected = *m_lastCorrected;
    QString terminator;
    if (text && text->endsWith(corrected.replacement + ' '))
        terminator = " ";
    else if (text && text->endsWith(corrected.replacement + '\n'))
        terminator = "\n";
    else
        return false;
    replaceBefore(corrected.replacement.size() + 1, corrected.original + terminator);
    m_personal.addManual(corrected.original);
    m_lastCorrected.reset();
    refresh();
    return true;
}

void Backend::backspace() {
    if (!m_sink || sendLatched("BackSpace") || tryUndoLastCorrection())
        return;
    m_sink->sendKey(TextSink::Key::BackSpace);
    if (!m_typed.isEmpty())
        m_typed.chop(m_typed.size() >= 2 && m_typed.back().isLowSurrogate() ? 2 : 1);
    m_autoSpaced = false;
    m_swipeAlternative.reset();
    updateAutoShift();
    refresh();
}

void Backend::deleteWord() {
    if (!m_sink)
        return;
    if (const auto text = before(); text && !text->isEmpty()) {
        // The spaces before the cursor, then the word before them.
        int n = text->size();
        while (n > 0 && text->at(n - 1).isSpace())
            --n;
        while (n > 0 && (text->at(n - 1).isLetterOrNumber() || text->at(n - 1) == '\''))
            --n;
        m_sink->deleteBefore(std::max<qsizetype>(1, text->size() - n));
    } else {
        m_sink->sendKey(TextSink::Key::BackSpace, true);
    }
    m_typed.clear();
    m_pending.reset();
    m_lastCorrected.reset();
    m_swipeAlternative.reset();
    m_autoSpaced = false;
    updateAutoShift();
    refresh();
}

void Backend::enter() {
    if (!m_sink || sendLatched("Return"))
        return;
    m_pending.reset();
    if (!m_typed.isEmpty() && !password())
        recordCommittedWord(finishWord());
    m_sink->sendKey(TextSink::Key::Return);
    m_typed.clear();
    m_autoSpaced = false;
    updateAutoShift();
    refresh();
}

void Backend::moveCursor(int columns, int lines) {
    if (!m_sink)
        return;
    for (int i = 0; i < std::abs(columns); ++i)
        m_sink->sendKey(columns < 0 ? TextSink::Key::Left : TextSink::Key::Right);
    for (int i = 0; i < std::abs(lines); ++i)
        m_sink->sendKey(lines < 0 ? TextSink::Key::Up : TextSink::Key::Down);
    m_typed.clear();
    m_pending.reset();
    m_lastCorrected.reset();
    m_swipeAlternative.reset();
    m_autoSpaced = false;
    refresh();
}

void Backend::swipeWord(const QString &word) {
    if (!m_sink || word.isEmpty())
        return;
    if (!m_typed.isEmpty())
        space();
    m_pending.reset();
    m_lastCorrected.reset();

    const QString base = m_dictionary.contraction(word.toLower()).isEmpty() ? word : m_dictionary.contraction(word.toLower());
    QString swiped = pronounI(base);
    if (swiped.isEmpty())
        swiped = m_dictionary.specialCasing(base);
    if (swiped.isEmpty())
        swiped = base;
    const QString committed = applyShift(capitalizeForSentenceStart(swiped, 0));

    // A glide straight after a mark ("Hi," + glide) still gets its space.
    if (const auto text = before(); text && !password() && !text->isEmpty() && Punctuation.contains(text->back()))
        m_sink->commitText(" ");
    m_sink->commitText(password() ? committed : committed + ' ');
    m_autoSpaced = !password();

    // "well" and "we'll" glide the same; offer the other in the strip.
    const QString twin = m_dictionary.apostropheTwin(word);
    if (!twin.isEmpty() && !password())
        m_swipeAlternative = Correction{committed, capitalizeForSentenceStart(twin, committed.size() + 1)};
    else
        m_swipeAlternative.reset();
    recordCommittedWord(committed);
    m_typed.clear();
    updateAutoShift();
    refresh();
}

void Backend::pickSuggestion(int index) {
    if (!m_sink || password())
        return;
    const QString picked = m_suggestions.value(index);
    if (picked.isEmpty())
        return;
    const auto text = before();

    if (m_pending && m_typed.isEmpty()) {
        const Correction pending = *m_pending;
        m_pending.reset();
        if (index == 0) {
            // Keeping the word as typed teaches it faster.
            m_personal.noteKept(pending.original);
            refresh();
            return;
        }
        if (index == 1) {
            for (const QString &terminator : {QStringLiteral(" "), QStringLiteral("\n")})
                if (text && text->endsWith(pending.original + terminator)) {
                    replaceBefore(pending.original.size() + 1, pending.replacement + terminator);
                    m_lastCorrected = Correction{pending.original, pending.replacement};
                    break;
                }
            refresh();
            return;
        }
        // The third slot is a predicted next word, picked as usual below.
    }

    if (index == 0 && m_lastCorrected && m_typed.isEmpty() && picked == m_lastCorrected->original) {
        tryUndoLastCorrection();
        return;
    }
    if (index == 0 && m_swipeAlternative && m_typed.isEmpty() && picked == m_swipeAlternative->replacement) {
        const Correction alternative = *m_swipeAlternative;
        m_swipeAlternative.reset();
        if (text && text->endsWith(alternative.original + ' ')) {
            replaceBefore(alternative.original.size() + 1, alternative.replacement + ' ');
            if (!m_history.isEmpty())
                m_history.last() = alternative.replacement.toLower();
            m_lastCommitted = alternative.replacement.toLower();
        }
        refresh();
        return;
    }

    if (!m_typed.isEmpty()) {
        if (index == 0 && picked == m_typed) {
            m_personal.noteKept(m_typed);
            m_sink->commitText(" ");
            recordCommittedWord(m_typed);
        } else {
            const int characters = fieldWordBeforeCursor().value_or(m_typed).size();
            const QString contraction = m_dictionary.contraction(picked.toLower());
            QString word = pronounI(contraction.isEmpty() ? picked : contraction);
            if (word.isEmpty())
                word = contraction.isEmpty() ? picked : contraction;
            word = capitalizeForSentenceStart(word, characters);
            if (m_typedHadUpper)
                word = capitalizeFirst(word);
            replaceBefore(characters, word + ' ');
            m_autoSpaced = true;
            recordCommittedWord(word);
        }
        m_typed.clear();
    } else {
        // A predicted next word.
        if (text && !text->isEmpty() && !text->back().isSpace())
            m_sink->commitText(" ");
        const QString word = applyShift(capitalizeForSentenceStart(picked, 0));
        m_sink->commitText(word + ' ');
        m_autoSpaced = true;
        recordCommittedWord(word);
    }
    updateAutoShift();
    refresh();
}

void Backend::refresh() {
    QStringList next{"", "", ""};
    if (password()) {
        if (next != m_suggestions) {
            m_suggestions = next;
            emit suggestionsChanged();
        }
        return;
    }
    const QString word = m_typed;
    const QStringList personal = m_personal.allWords();
    const QStringList predicted = word.isEmpty() ? predictedNext(lastWordsBeforeCursor())
                                  : word.size() >= 2 ? predictedNext(contextWordsBeforeCurrent())
                                                     : QStringList();
    if (word.isEmpty()) {
        const QStringList last = lastWordsBeforeCursor();
        m_previousWord = last.isEmpty() ? QString() : last.last();
        m_contextWords = predicted;
    }
    const QStringList nextWords = word.isEmpty() ? predicted.mid(0, 2) : QStringList();
    QStringList completions = word.size() >= 2 ? m_dictionary.completions(word, personal, predicted) : QStringList();
    if (word.size() >= 3 && completions.size() < 2) {
        const QString fix = m_dictionary.bestCorrection(word, personal, predicted);
        if (!fix.isEmpty() && !completions.contains(fix))
            completions << fix;
    }

    // Offers about the last word stay only while the cursor is still right
    // after it.
    const auto text = before();
    auto stillAfter = [&](const QString &w) {
        return !text || text->endsWith(w + ' ') || text->endsWith(w + '\n');
    };
    if (!word.isEmpty() || (m_swipeAlternative && !stillAfter(m_swipeAlternative->original)))
        m_swipeAlternative.reset();
    if (!word.isEmpty() || (m_pending && !stillAfter(m_pending->original)))
        m_pending.reset();
    if (m_lastCorrected && !stillAfter(m_lastCorrected->replacement))
        m_lastCorrected.reset();

    if (m_pending && word.isEmpty()) {
        next = {m_pending->original, m_pending->replacement, nextWords.value(0)};
    } else if (word.isEmpty()) {
        QString left;
        if (m_lastCorrected)
            left = m_lastCorrected->original;
        else if (m_swipeAlternative)
            left = m_swipeAlternative->replacement;
        next = {left, nextWords.value(0), nextWords.value(1)};
    } else {
        next = {word, completions.value(0), completions.value(1)};
    }
    if (next != m_suggestions) {
        m_suggestions = next;
        emit suggestionsChanged();
    }
}

bool Backend::sendLatched(const QString &key) {
    if (!m_modifiers || !m_sink)
        return false;
    m_sink->sendCombo(key, m_modifiers);
    m_modifiers = 0;
    emit modifiersChanged();
    // Whatever the shortcut did, the word in progress is over.
    m_typed.clear();
    m_pending.reset();
    m_autoSpaced = false;
    refresh();
    return true;
}

void Backend::toggleModifier(int modifier) {
    m_modifiers ^= modifier;
    emit modifiersChanged();
}

void Backend::controlKey(const QString &key) {
    if (!m_sink)
        return;
    if (sendLatched(key))
        return;
    m_sink->sendCombo(key, 0);
    m_typed.clear();
    m_pending.reset();
    m_autoSpaced = false;
    refresh();
}

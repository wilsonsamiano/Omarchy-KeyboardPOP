#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QQuickWindow>
#include <QtTest>

#include "backend.h"
#include "dictionary.h"
#include "keyboardview.h"
#include "personal.h"
#include "swipe.h"
#include "textsink.h"
#include "theme.h"
#include "translator.h"

// A text field with the cursor at the end.
class FakeField : public TextSink {
public:
    QString text;
    bool known = true;
    bool secret = false;

    void commitText(const QString &t) override { text += t; }
    void deleteBefore(int n) override { text.chop(std::min<qsizetype>(n, text.size())); }
    void sendKey(Key key, bool control) override {
        if (key == Key::Return)
            text += '\n';
        else if (key == Key::BackSpace && !control)
            text.chop(1);
    }
    std::optional<QString> textBeforeCursor() const override {
        return known ? std::optional<QString>(text) : std::nullopt;
    }
    bool isPassword() const override { return secret; }
};

class KeyboardpopTests : public QObject {
    Q_OBJECT

    // The dictionary takes a moment to load, so tests share one.
    Dictionary *dictionary = nullptr;
    QTemporaryDir home;
    qint64 time = 100000;

    struct Keyboard {
        FakeField field;
        Personal personal;
        Backend backend;
        Keyboard(Dictionary &d, const QString &path, qint64 *time) : personal(d, path), backend(d, personal) {
            backend.setClock([time] { return *time; });
            backend.setSink(&field);
            backend.reset();
        }
        void type(const QString &letters) {
            for (QChar c : letters) {
                if (c == ' ')
                    backend.space();
                else
                    backend.typeCharacter(QString(c));
            }
        }
    };
    std::unique_ptr<Keyboard> keyboard() {
        time += 10000; // far from any earlier tap, so nothing counts as a double tap
        return std::make_unique<Keyboard>(*dictionary, home.filePath(QString("personal-%1.json").arg(time)), &time);
    }

private slots:
    void initTestCase() {
        dictionary = new Dictionary(DATA_DIR);
        QVERIFY(dictionary->isWord("the"));
    }
    void cleanupTestCase() { delete dictionary; }

    // ---- Dictionary ----

    void dictionaryKnowsWords() {
        QVERIFY(dictionary->isWord("Hello"));
        QVERIFY(!dictionary->isWord("helllo"));
        QCOMPARE(dictionary->rankOf("the"), 0);
    }

    void correctsTyposByKeyboardDistance() {
        QCOMPARE(dictionary->bestCorrection("teh"), QString("the"));
        QCOMPARE(dictionary->bestCorrection("keybaord"), QString("keyboard"));
        QCOMPARE(dictionary->bestCorrection("helo"), QString("help")); // o and p are neighbours
        QVERIFY(dictionary->bestCorrection("the").isEmpty());
        QVERIFY(dictionary->bestCorrection("café").isEmpty()); // accented words are left alone
        QCOMPARE(Dictionary::levenshtein("teh", "the"), 1);
    }

    void completesByFrequencyAndContext() {
        // Apostrophe forms come before plain words, as on Android.
        QCOMPARE(dictionary->completions("th").first(), QString("that's"));
        QCOMPARE(dictionary->completions("th", {}, {"the"}).first(), QString("the")); // unless context says otherwise
        QCOMPARE(dictionary->completions("th", {"Thalia"}).first(), QString("Thalia"));
        QVERIFY(dictionary->completions("t").isEmpty());
        QCOMPARE(dictionary->completions("sg").first(), QString("SGT"));
    }

    void predictsNextWords() {
        QVERIFY(dictionary->bigramNext("thank").contains("you"));
        QVERIFY(!dictionary->ngramNext({"a", "bit", "of", "a"}).isEmpty());
    }

    void suggestsKnownMixUps() {
        QCOMPARE(dictionary->contextualAlternative("there", {"their", "own"}), QString("their"));
        QVERIFY(dictionary->contextualAlternative("their", {"their", "own"}).isEmpty());
    }

    void matchesTracedLetters() {
        QCOMPARE(dictionary->bestSwipeMatch("hello"), QString("hello"));
        QCOMPARE(dictionary->bestSwipeMatch("hrello"), QString("hello"));
        QCOMPARE(dictionary->apostropheTwin("well"), QString("we'll"));
    }

    void switchesLanguage() {
        dictionary->setLanguage("tl");
        QVERIFY(dictionary->isWord("ako"));
        QVERIFY(!dictionary->isWord("the"));
        dictionary->setLanguage("en");
        QVERIFY(dictionary->isWord("the"));
    }

    // ---- Swipe ----

    // ---- Multitouch ----

    // A second finger taps its own key while the first holds Delete.
    void secondFingerTapsWhileDeleteRepeats() {
        auto kb = keyboard();
        kb->field.text = "hello world, this is long";
        QQuickWindow window;
        window.resize(1200, 300);
        auto *view = new KeyboardView;
        view->setParentItem(window.contentItem());
        view->setSize(QSizeF(1200, 300));
        view->setBackend(&kb->backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QPointF deleteAt, enterAt;
        for (const auto &placed : view->keys()) {
            if (placed.key.action == KeyboardView::Action::Delete)
                deleteAt = placed.rect.center();
            if (placed.key.action == KeyboardView::Action::Enter)
                enterAt = placed.rect.center();
        }
        QVERIFY(!deleteAt.isNull() && !enterAt.isNull());

        QPointingDevice *device = QTest::createTouchDevice();
        QTest::touchEvent(&window, device).press(0, deleteAt.toPoint());
        QTest::qWait(700); // Delete starts repeating
        const QString afterRepeat = kb->field.text;
        QVERIFY(afterRepeat.size() < 25);
        QTest::touchEvent(&window, device).stationary(0).press(1, enterAt.toPoint());
        QTest::touchEvent(&window, device).stationary(0).release(1, enterAt.toPoint());
        QVERIFY(kb->field.text.contains('\n'));
        QTest::touchEvent(&window, device).release(0, deleteAt.toPoint());
    }

    void matchesGlideShapes() {
        KeyboardView view;
        view.setSize(QSizeF(1200, 300));
        SwipeTrace trace;
        for (const auto &placed : view.keys())
            if (placed.key.output.size() == 1 && placed.key.output[0].isLetter()) {
                trace.centers.insert(placed.key.output[0], placed.rect.center());
                trace.keyWidth = std::min(placed.rect.width(), placed.rect.height());
            }
        auto glide = [&](const QString &word) {
            trace.points.clear();
            trace.times.clear();
            qint64 t = 0;
            for (int i = 1; i < word.size(); ++i) {
                const QPointF a = trace.centers.value(word[i - 1]), b = trace.centers.value(word[i]);
                for (int s = 0; s < 10; ++s) {
                    trace.points << a + (b - a) * (s / 10.0);
                    trace.times << (t += 8);
                }
            }
            trace.points << trace.centers.value(word.back());
            trace.times << (t += 8);
            return matchSwipeShape(trace, *dictionary);
        };
        QCOMPARE(glide("hello"), QString("hello"));
        QCOMPARE(glide("keyboard"), QString("keyboard"));
        QCOMPARE(glide("world"), QString("world"));
        QCOMPARE(glide("people"), QString("people"));
    }

    void resamplesEvenly() {
        const auto points = resamplePath({QPointF(0, 0), QPointF(100, 0)}, 5);
        QCOMPARE(points.size(), 5);
        QCOMPARE(points[2], QPointF(50, 0));
    }

    // ---- Personal dictionary ----

    void learnsWordsTypedOften() {
        Personal personal(*dictionary, home.filePath("learn.json"));
        for (int i = 0; i < Personal::LearnAt - 1; ++i)
            QVERIFY(!personal.noteTyped("omarchy"));
        QVERIFY(!personal.isPersonal("omarchy"));
        QVERIFY(personal.noteTyped("omarchy"));
        QVERIFY(personal.isPersonal("omarchy"));
        QVERIFY(!personal.noteTyped("the")); // dictionary words aren't counted
    }

    void keptCorrectionsCountMore() {
        Personal personal(*dictionary, home.filePath("kept.json"));
        for (int i = 0; i < 7; ++i)
            personal.noteKept("hyprland");
        QVERIFY(!personal.isPersonal("hyprland"));
        personal.noteKept("hyprland");
        QVERIFY(personal.isPersonal("hyprland"));
    }

    void savesAndImportsAndroidBackups() {
        const QString path = home.filePath("saved.json");
        {
            Personal personal(*dictionary, path);
            personal.addManual("Samiano");
            personal.notePair("good", "morning");
        }
        Personal reloaded(*dictionary, path);
        QVERIFY(reloaded.isPersonal("samiano"));
        QCOMPARE(reloaded.learnedNextWords("good"), QStringList{"morning"});

        Personal imported(*dictionary, home.filePath("imported.json"));
        const int added = imported.importJson(R"({"format":"safepop-backup","version":1,"manual":["Eala"],
            "counts":["lumpia:80","typo:2"],"caps":[],"bigrams":["see:you:3"]})");
        QCOMPARE(added, 2);
        QVERIFY(imported.isPersonal("lumpia"));
        QVERIFY(!imported.isPersonal("typo"));
        QCOMPARE(imported.learnedNextWords("see"), QStringList{"you"});
    }

    // ---- Typing ----

    void capitalizesSentenceStarts() {
        auto k = keyboard();
        QVERIFY(k->backend.shiftOn());
        k->type("H");
        QVERIFY(!k->backend.shiftOn());
        k->type("i. ");
        QVERIFY(k->backend.shiftOn());
    }

    void fixesPronounAndContractions() {
        auto k = keyboard();
        k->backend.toggleShift(); // off: type lowercase on purpose
        k->type("i think im sure ");
        QCOMPARE(k->field.text, QString("I think I'm sure "));
        k->field.text.clear();
        k->type("dont ");
        QCOMPARE(k->field.text, QString("don't "));
    }

    void deleteUndoesAContraction() {
        auto k = keyboard();
        k->backend.toggleShift();
        k->type("cant ");
        QCOMPARE(k->field.text, QString("can't "));
        QCOMPARE(k->backend.suggestions().first(), QString("cant"));
        k->backend.backspace();
        QCOMPARE(k->field.text, QString("cant "));
        QVERIFY(k->personal.isPersonal("cant"));
    }

    void doubleSpaceEndsTheSentence() {
        auto k = keyboard();
        k->backend.toggleShift();
        k->type("hi");
        k->backend.space();
        time += 200;
        k->backend.space();
        QCOMPARE(k->field.text, QString("hi. "));
        QVERIFY(k->backend.shiftOn());
    }

    void suggestsCorrectionsWithoutReplacing() {
        auto k = keyboard();
        k->backend.toggleShift();
        k->type("see teh ");
        QCOMPARE(k->field.text, QString("see teh "));
        QCOMPARE(k->backend.suggestions().value(1), QString("the"));
        k->backend.pickSuggestion(1);
        QCOMPARE(k->field.text, QString("see the "));
        k->field.text.clear();
        k->backend.reset();
        k->type("teh ");
        QCOMPARE(k->backend.suggestions().value(1), QString("The")); // a sentence start
    }

    void completesTheWordBeingTyped() {
        auto k = keyboard();
        k->backend.toggleShift();
        k->type("my keyb");
        QCOMPARE(k->backend.suggestions().value(0), QString("keyb"));
        QCOMPARE(k->backend.suggestions().value(1), QString("keyboard"));
        k->backend.pickSuggestion(1);
        QCOMPARE(k->field.text, QString("my keyboard "));
    }

    void predictsTheNextWord() {
        auto k = keyboard();
        k->type("Thank ");
        QVERIFY(k->backend.suggestions().contains("you"));
        k->backend.pickSuggestion(int(k->backend.suggestions().indexOf("you")));
        QCOMPARE(k->field.text, QString("Thank you "));
    }

    void glidedWordsGetSpacesAndCapitals() {
        auto k = keyboard();
        k->backend.swipeWord("hello");
        QCOMPARE(k->field.text, QString("Hello "));
        k->type(",");
        QCOMPARE(k->field.text, QString("Hello, "));
        k->backend.swipeWord("well");
        QCOMPARE(k->field.text, QString("Hello, well "));
        QCOMPARE(k->backend.suggestions().value(0), QString("we'll"));
        k->backend.pickSuggestion(0);
        QCOMPARE(k->field.text, QString("Hello, we'll "));
    }

    void ranksKeepTheirCasing() {
        auto k = keyboard();
        k->backend.toggleShift();
        k->type("ask sgt smith");
        k->backend.enter();
        QCOMPARE(k->field.text, QString("ask SGT smith\n"));
    }

    void doubleShiftLocksCaps() {
        auto k = keyboard();
        k->backend.toggleShift(); // auto-shift was on; now off
        time += 1000;
        k->backend.toggleShift();
        time += 100;
        k->backend.toggleShift();
        QVERIFY(k->backend.shiftLock());
        k->backend.swipeWord("ok");
        QCOMPARE(k->field.text, QString("OK "));
    }

    void passwordsStayPlain() {
        auto k = keyboard();
        k->field.secret = true;
        k->backend.reset();
        k->type("teh");
        k->backend.space();
        QCOMPARE(k->field.text, QString("teh "));
        QCOMPARE(k->backend.suggestions(), QStringList({"", "", ""}));
    }

    void deletesWholeWords() {
        auto k = keyboard();
        k->field.text = "one two  ";
        k->backend.deleteWord();
        QCOMPARE(k->field.text, QString("one "));
    }

    // ---- Theme and language (from the Omarchy app template) ----

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        Translator translator(dir.path() + "/language", QFINDTESTDATA("../src/i18n"));
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("KeyboardView", "space"), QString("espaço")); // from pt
        QVERIFY(translator.translate("KeyboardView", "missing").isNull());
    }

    void catalogsMatchTheSource() {
        // Every catalog key must be a string the app actually shows.
        QStringList sources;
        for (const QString &file : {"../src/main.cpp", "../src/keyboardview.cpp", "../src/Main.qml"}) {
            QFile f(QFINDTESTDATA(file));
            QVERIFY(f.open(QIODevice::ReadOnly));
            sources << QString::fromUtf8(f.readAll());
        }
        const QString all = sources.join('\n');
        const QDir catalogs(QFINDTESTDATA("../src/i18n"));
        for (const QString &name : catalogs.entryList({"*.json"})) {
            QFile f(catalogs.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const auto keys = QJsonDocument::fromJson(f.readAll()).object().keys();
            for (const QString &key : keys)
                QVERIFY2(all.contains('"' + key + '"') || all.contains(key.left(40)),
                         qPrintable(name + ": " + key));
        }
    }

private:
    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(KeyboardpopTests)
#include "keyboardpop_tests.moc"

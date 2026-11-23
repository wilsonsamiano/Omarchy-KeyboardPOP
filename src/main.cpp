// KeyboardPOP — a touch keyboard for Omarchy with key popups and swipe typing.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTextStream>
#include <QUrl>
#include <unistd.h>

#include <LayerShellQt/window.h>

#include "backend.h"
#include "dictionary.h"
#include "keyboardview.h"
#include "personal.h"
#include "theme.h"
#include "touchwatcher.h"
#include "translator.h"
#include "waylandinput.h"

namespace {

const char *Usage =
    "Usage: keyboardpop [command]\n"
    "\n"
    "Commands:\n"
    "  toggle   Show the keyboard, or hide it if it's showing (the default)\n"
    "  show     Show the keyboard\n"
    "  hide     Hide the keyboard\n"
    "  quit     Stop the running keyboard\n"
    "  import <file>  Merge a SafePop/PressPop personal dictionary backup\n"
    "  export <file>  Save the personal dictionary as a backup\n";

QString socketName() {
    return QStringLiteral("keyboardpop-%1").arg(getuid());
}

int dictionaryCommand(const QString &command, const QString &path) {
    QTextStream err(stderr);
    if (path.isEmpty()) {
        err << "keyboardpop " << command << ": a file is needed\n";
        return 1;
    }
    Dictionary dictionary;
    Personal personal(dictionary);
    if (command == "export") {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(personal.exportJson()) < 0) {
            err << "keyboardpop export: can't write " << path << "\n";
            return 1;
        }
        return 0;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "keyboardpop import: can't read " << path << "\n";
        return 1;
    }
    const int added = personal.importJson(file.readAll());
    personal.flush();
    QTextStream(stdout) << "Imported " << added << " words\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("toggle");
    // `physical-key` is sent by the Hyprland config when a key is pressed
    // while the keyboard shows, so a hardware keyboard puts it away.
    const QStringList commands{"toggle", "show", "hide", "quit", "import", "export", "physical-key"};
    if (command == "--help" || command == "-h" || !commands.contains(command)) {
        QTextStream(command.startsWith("-h") || command == "--help" ? stdout : stderr) << Usage;
        return commands.contains(command) || command.startsWith("-h") || command == "--help" ? 0 : 1;
    }

    // Sent on keypresses, so it skips starting a GUI app: tell the running
    // keyboard, or do nothing when none is running.
    if (command == "physical-key") {
        QCoreApplication core(argc, argv);
        QLocalSocket socket;
        socket.connectToServer(socketName());
        if (socket.waitForConnected(300)) {
            socket.write("physical-key\n");
            socket.waitForBytesWritten(300);
            socket.waitForDisconnected(300);
        }
        return 0;
    }

    const bool offscreen = qgetenv("QT_QPA_PLATFORM") == "offscreen";

    QGuiApplication app(argc, argv);
    app.setApplicationName("keyboardpop");
    app.setOrganizationName("keyboardpop");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("keyboardpop");
    app.setWindowIcon(QIcon::fromTheme("keyboardpop"));
    app.setQuitOnLastWindowClosed(false);

    if (command == "import" || command == "export")
        return dictionaryCommand(command, argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString());

    // One keyboard at a time: a second launch (the app launcher, a key
    // binding) tells the running one what to do and exits.
    {
        QLocalSocket socket;
        socket.connectToServer(socketName());
        if (socket.waitForConnected(300)) {
            socket.write(command.toUtf8() + '\n');
            socket.waitForBytesWritten(300);
            socket.waitForDisconnected(300);
            return 0;
        }
    }
    // Nothing to stop. ("hide" goes on to start the keyboard hidden, which
    // is how it waits at login for a text field.)
    if (command == "quit")
        return 0;
    QLocalServer server;
    QLocalServer::removeServer(socketName());
    if (!server.listen(socketName())) {
        QTextStream(stderr) << "keyboardpop: can't listen on " << socketName() << ": " << server.errorString() << "\n";
        return 1;
    }

    QQuickStyle::setStyle("Material");
    Translator translator;
    app.installTranslator(&translator);
    Theme theme;

    Dictionary dictionary;
    Personal personal(dictionary);
    Backend backend(dictionary, personal);
    QSettings settings;
    backend.setLanguage(settings.value("language", "en").toString());
    QObject::connect(&backend, &Backend::languageChanged, [&] { settings.setValue("language", backend.language()); });

    auto *wayland = offscreen ? nullptr : app.nativeInterface<QNativeInterface::QWaylandApplication>();
    WaylandInput input(wayland ? wayland->display() : nullptr);
    backend.setSink(&input);

    auto updateStatus = [&] {
        switch (input.mode()) {
        case WaylandInput::Mode::Connecting:
            break;
        case WaylandInput::Mode::InputMethod:
            backend.setStatus(QObject::tr("KeyboardPOP opens by itself when you tap a text field."));
            break;
        case WaylandInput::Mode::VirtualKeyboard:
            backend.setStatus(QObject::tr("Typing into the focused window. Another input method (like fcitx5) is "
                                          "running, so KeyboardPOP can't open by itself on text fields."));
            break;
        case WaylandInput::Mode::Unavailable:
            backend.setStatus(QObject::tr("This compositor has no virtual keyboard support, so KeyboardPOP can't type."),
                              true);
            break;
        }
    };
    QObject::connect(&input, &WaylandInput::modeChanged, updateStatus);
    QObject::connect(&translator, &Translator::changed, updateStatus);
    updateStatus();

    qmlRegisterType<KeyboardView>("KeyboardPOP", 1, 0, "KeyboardView");
    qmlRegisterUncreatableType<Backend>("KeyboardPOP", 1, 0, "Backend", "The app provides the backend");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    // The keyboard is a layer surface docked to the bottom of the screen,
    // not a regular window, so it never takes focus from the app being
    // typed into. It has to be set up before the window first shows.
    if (auto *layer = offscreen ? nullptr : LayerShellQt::Window::get(window)) {
        // The overlay layer, so password dialogs (polkit) don't cover it:
        // they sit on that layer too, and the keyboard shows after them.
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors({LayerShellQt::Window::AnchorBottom, LayerShellQt::Window::AnchorLeft,
                           LayerShellQt::Window::AnchorRight});
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScope("keyboardpop");
        // Windows make room for the keyboard instead of hiding under it.
        layer->setExclusiveZone(window->height());
        QObject::connect(window, &QWindow::heightChanged, layer, [layer](int h) { layer->setExclusiveZone(h); });
    }

    auto show = [&] {
        if (window->isVisible())
            return;
        input.resetContext();
        backend.reset();
        window->show();
    };
    auto hide = [&] { window->hide(); };
    // A hardware key was pressed: the hardware keyboard is in use, so the
    // on-screen one gets out of the way. Keys KeyboardPOP sent itself
    // (Enter, Backspace, arrows) reach the compositor too, and don't count.
    const bool debug = qEnvironmentVariableIsSet("KEYBOARDPOP_DEBUG");
    auto hideForHardware = [&](const char *why) {
        if (!window->isVisible())
            return;
        if (debug)
            qInfo("keyboardpop: hiding (%s)", why);
        window->hide();
    };
    auto physicalKey = [&] {
        if (!input.sentKeyWithin(500))
            hideForHardware("hardware key");
    };
    auto run = [&](const QString &c) {
        if (c == "show")
            show();
        else if (c == "hide")
            hide();
        else if (c == "toggle")
            window->isVisible() ? hide() : show();
        else if (c == "quit")
            app.quit();
        else if (c == "physical-key")
            physicalKey();
    };
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            QObject::connect(client, &QLocalSocket::readyRead, client, [&, client] {
                while (client->canReadLine())
                    run(QString::fromUtf8(client->readLine()).trimmed());
                client->disconnectFromServer();
            });
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });

    // As the input method, the keyboard comes and goes with text fields, but
    // only when the field was tapped with a finger: clicking one with the
    // mouse leaves it hidden. Without access to the touchscreen there is no
    // telling the two apart, so any focused field brings it up.
    TouchWatcher touch;
    QElapsedTimer activatedAt, hiddenAt;
    constexpr qint64 TapWindowMs = 700;
    auto popUp = [&](const char *why) {
        if (window->isVisible())
            return;
        if (debug)
            qInfo("keyboardpop: showing (%s)", why);
        backend.reset();
        window->show();
    };
    QObject::connect(window, &QWindow::visibleChanged, [&](bool visible) {
        if (!visible)
            hiddenAt.start();
    });
    QObject::connect(&input, &WaylandInput::fieldActivated, [&] {
        activatedAt.start();
        touch.rescanIfNeeded();
        if (debug)
            qInfo("keyboardpop: field focused, touchscreen %s, touched recently: %d",
                  touch.available() ? "watched" : "not readable", touch.touchedWithin(TapWindowMs));
        if (!touch.available() || touch.touchedWithin(TapWindowMs))
            popUp("field focused by touch");
    });
    QObject::connect(&input, &WaylandInput::fieldDeactivated, [&] {
        activatedAt.invalidate();
        hide();
    });
    // The touch can also be read after the field reports focus.
    QObject::connect(&touch, &TouchWatcher::touched, [&] {
        if (input.fieldActive() && activatedAt.isValid() && activatedAt.elapsed() <= TapWindowMs)
            popUp("touch right after focus");
    });
    // Tapping while a field already has focus (one clicked earlier, or a
    // terminal that is always a text field) brings the keyboard back, the
    // way tapping a focused field does on a tablet. Taps on the keyboard
    // itself, like the one that just hid it, don't count.
    QObject::connect(&touch, &TouchWatcher::tapped, [&] {
        if (debug)
            qInfo("keyboardpop: tap, field focused: %d", input.fieldActive());
        if (input.fieldActive() && !(hiddenAt.isValid() && hiddenAt.elapsed() < 600))
            popUp("tap on focused field");
    });
    // A touchpad touched or a keyboard attached means the hardware keyboard
    // is being used.
    QObject::connect(&touch, &TouchWatcher::touchpadTouched, [&] { hideForHardware("touchpad"); });
    QObject::connect(&touch, &TouchWatcher::keyboardAttached, [&] { hideForHardware("keyboard attached"); });
    QObject::connect(&input, &WaylandInput::contextChanged, &backend, &Backend::contextChanged);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &personal, &Personal::flush);

    run(command == "toggle" ? "show" : command);
    return app.exec();
}

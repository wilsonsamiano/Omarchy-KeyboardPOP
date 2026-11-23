#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

class QFileSystemWatcher;
class QSocketNotifier;
class QTimer;

// Notices finger touches on the touchscreen, so the keyboard can pop up
// when a text field is tapped but stay away when it is clicked with the
// mouse. Wayland doesn't tell one client about touches on another's
// windows, so this reads the touchscreen's evdev node directly; the
// packaged udev rule lets the logged-in user read touchscreens (and only
// touchscreens). Without that access, `available()` is false.
//
// It also notices the signs of a hardware keyboard being used, so the
// on-screen one can get out of the way: a finger on a touchpad (the rule
// covers touchpads too) and a keyboard being attached. Attaching is seen
// from udev's device database, which needs no access to the keyboard.
//
// Touchscreens often disappear and come back across suspend, so /dev/input
// is watched and the devices are opened again when it changes.
class TouchWatcher : public QObject {
    Q_OBJECT

public:
    explicit TouchWatcher(QObject *parent = nullptr);
    ~TouchWatcher() override;

    // At least one touchscreen could be opened.
    bool available() const;

    // A finger went down within the last `ms` milliseconds.
    bool touchedWithin(qint64 ms) const;

    // Looks for touchscreens again if none were open (e.g. the udev rule
    // was installed after KeyboardPOP started).
    void rescanIfNeeded();

signals:
    // A finger went down.
    void touched();
    // A finger went down and came up quickly without sliding: a tap, not a
    // scroll, swipe, or long press.
    void tapped();
    // A finger went down on a touchpad.
    void touchpadTouched();
    // A hardware keyboard (USB or Bluetooth, like a type cover) appeared.
    void keyboardAttached();

private:
    struct Device {
        int fd;
        QString path;
        bool touchpad = false;
        QSocketNotifier *notifier;
        // Axis ranges, for telling a tap from a slide.
        int rangeX = 0, rangeY = 0;
        // The current touch: where and when it started, how far it went.
        bool down = false;
        int startX = 0, startY = 0, x = 0, y = 0;
        bool moved = false;
        bool needStart = false;
        QElapsedTimer since;
    };
    void scan();
    void closeDevice(int index);
    void readDevice(int fd);

    // Whether a hardware keyboard is at this node, from udev's database.
    static bool isExternalKeyboard(const QString &node);

    QVector<Device> m_devices;
    QStringList m_keyboards;
    bool m_scanned = false;
    QElapsedTimer m_lastTouch;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_rescan = nullptr;
};

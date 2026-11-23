#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

class QSocketNotifier;

// Notices finger touches on the touchscreen, so the keyboard can pop up
// when a text field is tapped but stay away when it is clicked with the
// mouse. Wayland doesn't tell one client about touches on another's
// windows, so this reads the touchscreen's evdev node directly; the
// packaged udev rule lets the logged-in user read touchscreens (and only
// touchscreens). Without that access, `available()` is false.
class TouchWatcher : public QObject {
    Q_OBJECT

public:
    explicit TouchWatcher(QObject *parent = nullptr);
    ~TouchWatcher() override;

    // At least one touchscreen could be opened.
    bool available() const { return !m_devices.isEmpty(); }

    // A finger went down within the last `ms` milliseconds.
    bool touchedWithin(qint64 ms) const;

    // Looks for touchscreens again if none were open (e.g. the udev rule
    // was installed after PressPop started).
    void rescanIfNeeded();

signals:
    void touched();

private:
    struct Device {
        int fd;
        QSocketNotifier *notifier;
    };
    void scan();
    void readDevice(int fd);

    QVector<Device> m_devices;
    QElapsedTimer m_lastTouch;
};

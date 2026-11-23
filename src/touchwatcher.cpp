#include "touchwatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace {

// A tap is shorter than this and slides less than TapSlop of the screen.
constexpr qint64 TapMaxMs = 450;
constexpr double TapSlop = 0.03;

bool testBit(const unsigned long *bits, int bit) {
    constexpr int perLong = sizeof(unsigned long) * 8;
    return bits[bit / perLong] & (1UL << (bit % perLong));
}

enum class Kind { Other, Touchscreen, Touchpad };

// Both report multitouch positions. A touchscreen is a direct-input device
// (it maps onto the screen); a touchpad moves a pointer.
Kind kindOf(int fd) {
    unsigned long props[(INPUT_PROP_CNT + sizeof(unsigned long) * 8 - 1) / (sizeof(unsigned long) * 8)] = {};
    unsigned long abs[(ABS_CNT + sizeof(unsigned long) * 8 - 1) / (sizeof(unsigned long) * 8)] = {};
    if (ioctl(fd, EVIOCGPROP(sizeof(props)), props) < 0)
        return Kind::Other;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) < 0)
        return Kind::Other;
    if (!testBit(abs, ABS_MT_POSITION_X))
        return Kind::Other;
    if (testBit(props, INPUT_PROP_DIRECT))
        return Kind::Touchscreen;
    if (testBit(props, INPUT_PROP_POINTER))
        return Kind::Touchpad;
    return Kind::Other;
}

int axisRange(int fd, int axis) {
    input_absinfo info{};
    if (ioctl(fd, EVIOCGABS(axis), &info) < 0)
        return 0;
    return info.maximum - info.minimum;
}

} // namespace

TouchWatcher::TouchWatcher(QObject *parent) : QObject(parent) {
    // Device nodes come and go in bursts (and get their permissions a
    // moment after appearing), so changes are handled once things settle.
    m_rescan = new QTimer(this);
    m_rescan->setSingleShot(true);
    m_rescan->setInterval(1500);
    connect(m_rescan, &QTimer::timeout, this, &TouchWatcher::scan);
    m_watcher = new QFileSystemWatcher({"/dev/input"}, this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_rescan, qOverload<>(&QTimer::start));
    scan();
}

TouchWatcher::~TouchWatcher() {
    while (!m_devices.isEmpty())
        closeDevice(0);
}

void TouchWatcher::scan() {
    const QStringList nodes = QDir("/dev/input").entryList({"event*"}, QDir::System);
    // Drop devices whose node is gone.
    for (int i = m_devices.size() - 1; i >= 0; --i)
        if (!QFileInfo::exists(m_devices[i].path))
            closeDevice(i);
    for (const QString &node : nodes) {
        const QString path = "/dev/input/" + node;
        if (std::any_of(m_devices.begin(), m_devices.end(), [&](const Device &d) { return d.path == path; }))
            continue;
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        const Kind kind = kindOf(fd);
        if (kind == Kind::Other) {
            ::close(fd);
            continue;
        }
        Device d;
        d.fd = fd;
        d.path = path;
        d.touchpad = kind == Kind::Touchpad;
        d.rangeX = axisRange(fd, ABS_X);
        d.rangeY = axisRange(fd, ABS_Y);
        d.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(d.notifier, &QSocketNotifier::activated, this, [this, fd] { readDevice(fd); });
        m_devices.append(d);
        if (qEnvironmentVariableIsSet("KEYBOARDPOP_DEBUG"))
            qInfo("keyboardpop: watching %s %s", d.touchpad ? "touchpad" : "touchscreen", qPrintable(path));
    }

    // Keyboards that weren't there on the last look were just attached.
    QStringList keyboards;
    for (const QString &node : nodes)
        if (isExternalKeyboard(node))
            keyboards << node;
    const bool attached = m_scanned
        && std::any_of(keyboards.begin(), keyboards.end(), [&](const QString &k) { return !m_keyboards.contains(k); });
    m_keyboards = keyboards;
    m_scanned = true;
    if (attached) {
        if (qEnvironmentVariableIsSet("KEYBOARDPOP_DEBUG"))
            qInfo("keyboardpop: hardware keyboard attached");
        emit keyboardAttached();
    }
}

bool TouchWatcher::isExternalKeyboard(const QString &node) {
    struct stat st{};
    if (::stat(("/dev/input/" + node).toLocal8Bit().constData(), &st) < 0)
        return false;
    QFile db(QStringLiteral("/run/udev/data/c%1:%2").arg(major(st.st_rdev)).arg(minor(st.st_rdev)));
    if (!db.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = db.readAll();
    // The laptop-style built-in keyboard port (i8042) is there whether or
    // not anything is plugged in, so only USB and Bluetooth count.
    return data.contains("E:ID_INPUT_KEYBOARD=1\n")
        && (data.contains("E:ID_BUS=usb\n") || data.contains("E:ID_BUS=bluetooth\n"));
}

bool TouchWatcher::available() const {
    return std::any_of(m_devices.begin(), m_devices.end(), [](const Device &d) { return !d.touchpad; });
}

void TouchWatcher::closeDevice(int index) {
    Device &d = m_devices[index];
    d.notifier->setEnabled(false);
    d.notifier->deleteLater();
    ::close(d.fd);
    m_devices.removeAt(index);
}

void TouchWatcher::rescanIfNeeded() {
    if (m_devices.isEmpty())
        scan();
}

void TouchWatcher::readDevice(int fd) {
    int index = -1;
    for (int i = 0; i < m_devices.size(); ++i)
        if (m_devices[i].fd == fd)
            index = i;
    if (index < 0)
        return;

    input_event events[64];
    for (;;) {
        const ssize_t n = ::read(fd, events, sizeof(events));
        if (n <= 0) {
            if (n < 0 && errno == EAGAIN)
                return;
            // The device went away (e.g. on suspend); it is opened again
            // when it comes back.
            closeDevice(index);
            m_rescan->start();
            return;
        }
        Device &d = m_devices[index];
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
            const input_event &e = events[i];
            if (e.type == EV_ABS && e.code == ABS_X) {
                d.x = e.value;
            } else if (e.type == EV_ABS && e.code == ABS_Y) {
                d.y = e.value;
            } else if (d.touchpad) {
                if (e.type == EV_KEY && e.code == BTN_TOUCH && e.value == 1)
                    emit touchpadTouched();
            } else if (e.type == EV_KEY && e.code == BTN_TOUCH && e.value == 1) {
                d.down = true;
                d.moved = false;
                d.needStart = true;
                d.since.start();
                m_lastTouch.start();
                emit touched();
            } else if (e.type == EV_KEY && e.code == BTN_TOUCH && e.value == 0) {
                const bool tap = d.down && !d.moved && d.since.elapsed() <= TapMaxMs;
                d.down = false;
                if (tap)
                    emit tapped();
            } else if (e.type == EV_SYN && e.code == SYN_REPORT && d.down && !d.moved) {
                // The position can come after BTN_TOUCH in the same report,
                // so the start point is taken when the report ends.
                if (d.needStart) {
                    d.startX = d.x;
                    d.startY = d.y;
                    d.needStart = false;
                }
                if (std::abs(d.x - d.startX) > d.rangeX * TapSlop || std::abs(d.y - d.startY) > d.rangeY * TapSlop)
                    d.moved = true;
            }
        }
    }
}

bool TouchWatcher::touchedWithin(qint64 ms) const {
    return m_lastTouch.isValid() && m_lastTouch.elapsed() <= ms;
}

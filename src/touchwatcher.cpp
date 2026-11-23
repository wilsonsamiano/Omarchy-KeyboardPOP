#include "touchwatcher.h"

#include <QDir>
#include <QSocketNotifier>

#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

bool testBit(const unsigned long *bits, int bit) {
    constexpr int perLong = sizeof(unsigned long) * 8;
    return bits[bit / perLong] & (1UL << (bit % perLong));
}

// A touchscreen is a direct-input device (it maps onto the screen, unlike a
// touchpad) that reports multitouch positions.
bool isTouchscreen(int fd) {
    unsigned long props[(INPUT_PROP_CNT + sizeof(unsigned long) * 8 - 1) / (sizeof(unsigned long) * 8)] = {};
    unsigned long abs[(ABS_CNT + sizeof(unsigned long) * 8 - 1) / (sizeof(unsigned long) * 8)] = {};
    if (ioctl(fd, EVIOCGPROP(sizeof(props)), props) < 0)
        return false;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) < 0)
        return false;
    return testBit(props, INPUT_PROP_DIRECT) && testBit(abs, ABS_MT_POSITION_X);
}

} // namespace

TouchWatcher::TouchWatcher(QObject *parent) : QObject(parent) {
    scan();
}

TouchWatcher::~TouchWatcher() {
    for (const Device &d : m_devices) {
        delete d.notifier;
        ::close(d.fd);
    }
}

void TouchWatcher::scan() {
    const QStringList nodes = QDir("/dev/input").entryList({"event*"}, QDir::System);
    for (const QString &node : nodes) {
        const QByteArray path = QByteArray("/dev/input/") + node.toLocal8Bit();
        const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (!isTouchscreen(fd)) {
            ::close(fd);
            continue;
        }
        auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this, fd] { readDevice(fd); });
        m_devices.append({fd, notifier});
    }
}

void TouchWatcher::rescanIfNeeded() {
    if (m_devices.isEmpty())
        scan();
}

void TouchWatcher::readDevice(int fd) {
    input_event events[64];
    for (;;) {
        const ssize_t n = ::read(fd, events, sizeof(events));
        if (n <= 0) {
            if (n < 0 && errno == EAGAIN)
                return;
            // The device went away: stop watching it.
            for (int i = 0; i < m_devices.size(); ++i) {
                if (m_devices[i].fd == fd) {
                    m_devices[i].notifier->deleteLater();
                    ::close(fd);
                    m_devices.removeAt(i);
                    break;
                }
            }
            return;
        }
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
            const input_event &e = events[i];
            const bool down = (e.type == EV_KEY && e.code == BTN_TOUCH && e.value == 1)
                || (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID && e.value >= 0);
            if (down) {
                m_lastTouch.start();
                emit touched();
            }
        }
    }
}

bool TouchWatcher::touchedWithin(qint64 ms) const {
    return m_lastTouch.isValid() && m_lastTouch.elapsed() <= ms;
}

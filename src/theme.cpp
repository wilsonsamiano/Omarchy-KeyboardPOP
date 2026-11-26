// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QMap<QString, QString> Theme::readColors(const QString &path) {
    QMap<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    const QRegularExpression expression(R"re(^\s*([a-z_0-9]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                        QRegularExpression::MultilineOption);
    auto it = expression.globalMatch(QString::fromUtf8(file.readAll()));
    while (it.hasNext()) {
        const auto match = it.next();
        colors.insert(match.captured(1), match.captured(2));
    }
    return colors;
}

namespace {
QColor mixed(const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}
} // namespace

QVariantMap Theme::paletteFor(const QString &style, const QMap<QString, QString> &colors) {
    // KeyboardPOP's own look, after iOS's dark keyboard.
    QVariantMap classic{
        {"background", QColor("#151517")}, {"strip", QColor("#1d1d20")},
        {"keyTop", QColor("#3a3a40")},     {"keyBottom", QColor("#2e2e33")},
        {"actionTop", QColor("#27272b")},  {"actionBottom", QColor("#202024")},
        {"pressedTop", QColor("#55555c")}, {"pressedBottom", QColor("#47474e")},
        {"keyEdge", QColor("#08080a")},    {"label", QColor("#f2f2f4")},
        {"hint", QColor("#8e8e93")},       {"divider", QColor("#38383d")},
        {"error", QColor("#ff6961")},
    };
    if (style != "omarchy" || !colors.contains("background") || !colors.contains("foreground"))
        return classic;

    auto pick = [&](const char *name, const QColor &fallback) {
        return colors.contains(name) ? QColor(colors.value(name)) : fallback;
    };
    const QColor bg(colors.value("background")), fg(colors.value("foreground"));
    const bool light = bg.lightnessF() > 0.5;
    const QColor black("black"), white("white");
    // Older themes only name background and foreground; derive the rest.
    // The keyboard's base sits a step darker than the theme's background,
    // and the keys stand off it: raised a step lighter on a dark theme,
    // near-white cards on a light one (like iOS's light keyboard).
    const QColor lighter = pick("lighter_background", mixed(bg, fg, 0.12));
    const QColor dark = pick("dark_background", light ? mixed(bg, fg, 0.1) : mixed(bg, black, 0.2));
    const QColor darker = pick("darker_background", light ? mixed(bg, fg, 0.2) : mixed(bg, black, 0.4));
    const QColor label = pick("bright_foreground", fg);
    const QColor hint = pick("dark_foreground", mixed(fg, bg, 0.45));
    const QColor keyFace = light ? mixed(bg, white, 0.7) : lighter;
    const QColor base = dark;
    return {
        {"background", base},
        {"strip", bg},
        {"keyTop", keyFace},
        {"keyBottom", mixed(keyFace, base, 0.25)},
        {"actionTop", mixed(keyFace, base, 0.55)},
        {"actionBottom", mixed(keyFace, base, 0.7)},
        {"pressedTop", mixed(keyFace, fg, 0.28)},
        {"pressedBottom", mixed(keyFace, fg, 0.18)},
        {"keyEdge", light ? mixed(darker, fg, 0.15) : darker.darker(140)},
        {"label", label},
        {"hint", hint},
        {"divider", mixed(bg, fg, 0.2)},
        {"error", pick("red", QColor("#ff6961"))},
    };
}

void Theme::setStyle(const QString &style) {
    const QString next = style == "classic" ? "classic" : "omarchy";
    if (next == m_style)
        return;
    m_style = next;
    updatePalette();
    emit changed();
}

void Theme::updatePalette() {
    m_palette = paletteFor(m_style, m_colors);
}

QString Theme::readAccent(const QString &path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return "#FFD60A";
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto accent = readAccent(m_directory + "/theme/colors.toml");
    const auto colors = readColors(m_directory + "/theme/colors.toml");
    if (accent != m_accent || colors != m_colors || m_palette.isEmpty()) {
        m_accent = accent;
        m_colors = colors;
        updatePalette();
        emit changed();
    }
}

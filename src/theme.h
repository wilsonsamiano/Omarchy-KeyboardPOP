#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // "omarchy" (the default) takes every color from the Omarchy theme;
    // "classic" is KeyboardPOP's own dark grey look with the theme's accent.
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY changed)
    // The colors in use, keyed by role: background (behind the keys), strip,
    // keyTop, keyBottom, actionTop, actionBottom, pressedTop, pressedBottom,
    // keyEdge, label, hint, divider, error.
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QString style() const { return m_style; }
    void setStyle(const QString &style);
    QVariantMap palette() const { return m_palette; }

    static QString readAccent(const QString &path);
    // Every `name = "#rrggbb"` in an Omarchy colors.toml.
    static QMap<QString, QString> readColors(const QString &path);
    // The palette for a style, from a theme's colors (empty: the classic look).
    static QVariantMap paletteFor(const QString &style, const QMap<QString, QString> &colors);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();
    void updatePalette();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QString m_style = "omarchy";
    QMap<QString, QString> m_colors;
    QVariantMap m_palette;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};

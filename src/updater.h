#pragma once
// Sprawdzanie i instalacja aktualizacji z GitHub Releases (Sprytnyhuj/logihub).
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject *parent = nullptr);
    static QString currentVersion();
    // Czy LogiHub działa z gotowej paczki (~/.local/share/logihub-app) - wtedy można aktualizować automatycznie.
    static bool isBundle();
    void check(bool manual);
    // Pobiera paczkę, rozpakowuje i uruchamia jej install.sh (który restartuje aplikację).
    void install();
    QString latestVersion() const { return m_latest; }
    QUrl releasePage() const { return m_page; }
signals:
    void updateAvailable(const QString &version, const QString &notes, bool manual);
    void upToDate(bool manual);
    void failed(const QString &message, bool manual);
    void progress(const QString &message);
private:
    QNetworkAccessManager *m_net;
    QString m_latest, m_notes;
    QUrl m_tarball, m_page;
};

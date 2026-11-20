#include "updater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QVersionNumber>

namespace {
const char *kApi = "https://api.github.com/repos/Sprytnyhuj/logihub/releases/latest";
}

Updater::Updater(QObject *parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString Updater::currentVersion()
{
    return QStringLiteral(LOGIHUB_VERSION);
}

bool Updater::isBundle()
{
    return QCoreApplication::applicationDirPath().endsWith("/logihub-app/bin");
}

void Updater::check(bool manual)
{
    QNetworkRequest req{QUrl(kApi)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LogiHub/" + currentVersion());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, manual] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(tr("Nie udało się sprawdzić aktualizacji: %1").arg(r->errorString()), manual);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        QString tag = o.value("tag_name").toString();
        if (tag.startsWith('v'))
            tag.remove(0, 1);
        m_page = QUrl(o.value("html_url").toString());
        m_notes = o.value("body").toString();
        m_tarball.clear();
        for (const auto &a : o.value("assets").toArray()) {
            const QString url = a.toObject().value("browser_download_url").toString();
            if (url.endsWith("-x86_64.tar.gz"))
                m_tarball = QUrl(url);
        }
        const QVersionNumber latest = QVersionNumber::fromString(tag);
        if (latest.isNull()) {
            emit failed(tr("Brak wydań na GitHubie."), manual);
            return;
        }
        m_latest = tag;
        if (latest > QVersionNumber::fromString(currentVersion()))
            emit updateAvailable(tag, m_notes, manual);
        else
            emit upToDate(manual);
    });
}

void Updater::install()
{
    if (!isBundle() || !m_tarball.isValid()) {
        emit failed(tr("Automatyczna aktualizacja działa dla instalacji z gotowej paczki."), true);
        return;
    }
    emit progress(tr("Pobieranie wersji %1…").arg(m_latest));
    QNetworkRequest req(m_tarball);
    req.setHeader(QNetworkRequest::UserAgentHeader, "LogiHub/" + currentVersion());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(tr("Błąd pobierania: %1").arg(r->errorString()), true);
            return;
        }
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/update";
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        QFile f(dir + "/logihub.tar.gz");
        if (!f.open(QIODevice::WriteOnly) || f.write(r->readAll()) <= 0) {
            emit failed(tr("Nie można zapisać paczki."), true);
            return;
        }
        f.close();
        emit progress(tr("Rozpakowywanie…"));
        QProcess tar;
        tar.start("tar", {"-xzf", f.fileName(), "-C", dir});
        if (!tar.waitForFinished(60000) || tar.exitCode() != 0 || !QFile::exists(dir + "/logihub/install.sh")) {
            emit failed(tr("Nieprawidłowa paczka aktualizacji."), true);
            return;
        }
        emit progress(tr("Instalowanie – LogiHub uruchomi się ponownie…"));
        // install.sh zamyka działający LogiHub, podmienia pliki i uruchamia nową wersję
        QProcess::startDetached("setsid", {"bash", dir + "/logihub/install.sh", "--restart"});
    });
}

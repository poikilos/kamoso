/*************************************************************************************
 *  Copyright (C) 2008-2011 by Aleix Pol <aleixpol@kde.org>                          *
 *  Copyright (C) 2008-2011 by Alex Fiestas <alex@eyeos.org>                         *
 *  Copyright (C) 2013      by Michael G. Hansen <mike@mghansen.de>                  *
 *                                                                                   *
 *  This program is free software; you can redistribute it and/or                    *
 *  modify it under the terms of the GNU General Public License                      *
 *  as published by the Free Software Foundation; either version 2                   *
 *  of the License, or (at your option) any later version.                           *
 *                                                                                   *
 *  This program is distributed in the hope that it will be useful,                  *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of                   *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                    *
 *  GNU General Public License for more details.                                     *
 *                                                                                   *
 *  You should have received a copy of the GNU General Public License                *
 *  along with this program; if not, write to the Free Software                      *
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA   *
 *************************************************************************************/


#include "kamoso.h"

#include "kamosoSettings.h"
#include "video/webcamcontrol.h"
#include "devicemanager.h"
#include <KIO/Global>
#include <KIO/CopyJob>
#include <KIO/FileUndoManager>
#include <KIO/JobUiDelegate>
#include <KFormat>
#include <KJobWidgets/KJobWidgets>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QDir>
#include <QtCore/QTemporaryFile>
#include <QIcon>
#include <QProcess>

Kamoso::Kamoso(WebcamControl *webcamControl)
    : m_webcamControl(webcamControl)
{
    m_recordingTime.restart();
    m_recordingTimer.setInterval(30);

    connect(m_webcamControl, &WebcamControl::photoTaken, this, &Kamoso::photoTaken);
    connect(&m_recordingTimer, &QTimer::timeout, this, &Kamoso::recordingTimeChanged);

    if (Settings::saveUrl().isLocalFile()) {
        const QDir dir(Settings::saveUrl().toLocalFile());
        const auto dirlist = dir.entryInfoList({"*.jpg"}, QDir::Files, QDir::SortFlag::Time);
        if (!dirlist.isEmpty())
            m_sampleImagePath = dirlist.first().absoluteFilePath();
    }

    if (m_sampleImagePath.isEmpty()) {
        QTemporaryFile* temporary = new QTemporaryFile(QDir::temp().absoluteFilePath("XXXXXX-sampleimage.jpg"), this);
        temporary->open();
        auto sampleIcon = QIcon::fromTheme(QStringLiteral("kde"));
        sampleIcon.pixmap(200, 200).save(temporary);

        m_sampleImagePath = temporary->fileName();
        temporary->close();
    }
}

Kamoso::~Kamoso() = default;

QUrl Kamoso::fileNameSuggestion(const QUrl &saveUrl, const QString &name, const QString& extension) const
{
    const QString date = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_hh-mm-ss"));
    const QString initialName =  QStringLiteral("%1_%2.%3").arg(name, date, extension);

    QUrl path(saveUrl.toString() + '/' + initialName);

    if (path.isLocalFile() && QFile::exists(path.toLocalFile())) {
        path = saveUrl.toString() + '/' + KIO::suggestName(saveUrl, initialName);
    }

    return path;
}
QUrl Kamoso::fileNameSuggestion(const QUrl &saveUrl, const QString& extension) const
{
    QUrl path(saveUrl.toString() + "/" + QStringLiteral("%1.%2").arg(QString::number(this->photoNumber), extension));
    return path;
}
void Kamoso::startSession() {
    this->photoNumber = 1;
    QDateTime now = QDateTime::currentDateTime();
    
    this->sessionName = now.toString("yyyy-MM-dd_hh-mm-ss");
    this->sessionUrl = QUrl(Settings::saveUrl().toString() + "/" + this->sessionName);
    if (!QDir(this->sessionUrl.toLocalFile()).exists()) {
        QDir().mkdir(this->sessionUrl.toLocalFile());
    }
}
void Kamoso::printSession() {
    QString templateSrcPath = QDir::cleanPath(Settings::saveUrl().toLocalFile() + QDir::separator() + "visagebox-print.sla");
    QString templateDestPath = QDir::cleanPath(this->sessionUrl.toLocalFile() + QDir::separator() + "visagebox-print.sla");
    QString pyPath = QDir::cleanPath(Settings::saveUrl().toLocalFile() + QDir::separator() + "printfilmstrip.py");
    QFile::copy(templateSrcPath, templateDestPath);
    QProcess scribusProc;
    scribusProc.start("scribus", QStringList() << "-g" << "-ns" << templateDestPath << "-py" << pyPath);
    scribusProc.waitForFinished();
    QByteArray scribusOutput = scribusProc.readAll();
    QString pdfPath = QDir::cleanPath(this->sessionUrl.toLocalFile() + QDir::separator() + "visagebox-print.pdf");
    //visagebox-print
    QProcess lpProc;
    lpProc.start("lp", QStringList() << pdfPath);
    lpProc.waitForFinished();
    QByteArray lpOutput = lpProc.readAll();
}
const QString Kamoso::takePhoto()
{
    const QUrl path = fileNameSuggestion(this->sessionUrl, "jpg");
    m_webcamControl->takePhoto(path);

    if (path.isLocalFile()) {
        m_sampleImagePath = path.toLocalFile();
        Q_EMIT sampleImageChanged(m_sampleImagePath);
    }
    this->photoNumber++;
    return path.toDisplayString();
}

void Kamoso::resetDeviceSettings()
{
    Device *device = DeviceManager::self()->playingDevice();
    device->reset();
}

void Kamoso::setRecording(bool recording)
{
    if (recording == m_recordingTimer.isActive())
        return;

    if (recording) {
        m_webcamControl->startRecording();
        m_recordingTime.restart();
        m_recordingTimer.start();
    } else {
        const QUrl path = fileNameSuggestion(Settings::saveVideos(), "video", "mkv");

        KJob *job = KIO::move(QUrl::fromLocalFile(m_webcamControl->stopRecording()), path);
        job->start();

        m_webcamControl->playDevice(DeviceManager::self()->playingDevice());
        m_recordingTimer.stop();
    }

    Q_EMIT isRecordingChanged(m_recordingTimer.isActive());
}

QString Kamoso::recordingTime() const
{
    return KFormat().formatDuration(m_recordingTime.elapsed());
}

bool Kamoso::isRecording() const
{
    return m_recordingTimer.isActive();
}

void Kamoso::trashFiles(const QJsonArray& urls)
{
    QList<QUrl> list;
    Q_FOREACH(const QJsonValue& val, urls) {
        list += QUrl(val.toString());
    }

    KIO::Job* job = KIO::trash(list);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, list, QUrl("trash:/"), job);
//     KJobWidgets::setWindow(job, window);
}

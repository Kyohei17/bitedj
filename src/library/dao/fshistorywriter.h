#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "track/trackid.h"

/// Serial worker for the USB-backed history store.
///
/// Instances are intended to live on a dedicated QThread. Every operation is
/// blocking inside that thread, but callers enqueue requests through a queued
/// connection so slow removable-media I/O never stalls the GUI thread.
class FsHistoryWriter : public QObject {
    Q_OBJECT

  public:
    explicit FsHistoryWriter(QObject* pParent = nullptr);

  public slots:
    void appendTrack(const QString& mountRoot,
            quint64 sessionGeneration,
            const QString& trackLocation,
            int durationSeconds,
            TrackId trackId);
    void forgetMount(const QString& mountRoot);

    /// Queue barrier used during orderly application shutdown. A blocking
    /// invocation of this slot runs only after earlier append requests from the
    /// same sender have completed.
    void drain();

  signals:
    void trackAppended(const QString& mountRoot,
            quint64 sessionGeneration,
            const QString& sessionName,
            int trackCount,
            int durationSeconds,
            TrackId trackId);

  private:
    struct CurrentSession {
        quint64 generation = 0;
        QString name;
    };

    QHash<QString, CurrentSession> m_currentSessionByMount;
};

#include "library/dao/fshistorywriter.h"

#include "library/dao/fshistorystore.h"
#include "moc_fshistorywriter.cpp"

FsHistoryWriter::FsHistoryWriter(QObject* pParent)
        : QObject(pParent) {
}

void FsHistoryWriter::appendTrack(const QString& mountRoot,
        quint64 sessionGeneration,
        const QString& trackLocation,
        int durationSeconds,
        TrackId trackId) {
    CurrentSession session = m_currentSessionByMount.value(mountRoot);
    if (session.name.isEmpty() || session.generation != sessionGeneration) {
        session.name = FsHistoryStore::newSessionName(mountRoot);
        session.generation = sessionGeneration;
        if (session.name.isEmpty()) {
            return;
        }
    }

    FsHistorySession summary;
    if (!FsHistoryStore::appendTrack(mountRoot,
                session.name,
                trackLocation,
                durationSeconds,
                &summary)) {
        return;
    }

    m_currentSessionByMount.insert(mountRoot, session);
    emit trackAppended(mountRoot,
            sessionGeneration,
            session.name,
            summary.trackCount,
            summary.durationSeconds,
            trackId);
}

void FsHistoryWriter::forgetMount(const QString& mountRoot) {
    m_currentSessionByMount.remove(mountRoot);
}

void FsHistoryWriter::drain() {
}

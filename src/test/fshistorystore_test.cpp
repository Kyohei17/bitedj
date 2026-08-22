#include "library/dao/fshistorystore.h"
#include "library/dao/fshistorywriter.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QSignalSpy>
#include <QStorageInfo>

#include "test/mixxxtest.h"

namespace {

// The history of a drive is stored on that drive, so every case here needs a
// filesystem mounted under one of the removable roots. Without root, `unshare`
// provides one:
//
//   unshare -Umr --propagation private sh -c \
//     'mount -t tmpfs tmpfs /mnt && mkdir -p /mnt/usbtest && \
//      mount -t tmpfs tmpfs /mnt/usbtest && \
//      QT_QPA_PLATFORM=offscreen ./mixxx-test \
//        --gtest_filter="FsHistoryStoreTest.*"'
//
// The cases skip when that mount is not there, so an ordinary run of the suite
// stays green.
const QString kFakeUsb = QStringLiteral("/mnt/usbtest");
const QString kDbPath = kFakeUsb + QStringLiteral("/.bitedj/history.sqlite");

class FsHistoryStoreTest : public MixxxTest {
  protected:
    void SetUp() override {
        MixxxTest::SetUp();
        if (haveFakeUsb()) {
            // Start from a clean drive, so a rerun in the same namespace still
            // counts.
            ASSERT_TRUE(FsHistoryStore::clearFilesystemHistory(kFakeUsb));
        }
    }

    static bool haveFakeUsb() {
        // An unmounted path reports itself as its own root, so validity is what
        // tells a real mount from a missing one.
        const QStorageInfo usb(kFakeUsb);
        return usb.isValid() && usb.isReady() && usb.rootPath() == kFakeUsb;
    }

    static QString onUsb(const QString& relPath) {
        return kFakeUsb + QLatin1Char('/') + relPath;
    }
};

} // anonymous namespace

// The whole cycle on a drive: a stick with no history writes nothing, the first
// track opens the session, and what was played comes back in play order.
TEST_F(FsHistoryStoreTest, LogsAndReadsBackASession) {
    if (!haveFakeUsb()) {
        GTEST_SKIP() << "needs a filesystem mounted at " << qPrintable(kFakeUsb);
    }

    // A drive that was never played from is not a drive with an empty history:
    // nothing at all is written to it.
    QList<FsHistorySession> sessions;
    EXPECT_FALSE(FsHistoryStore::readSessions(kFakeUsb, &sessions));
    EXPECT_FALSE(QFile::exists(kDbPath));

    const QString session = FsHistoryStore::newSessionName(kFakeUsb);
    ASSERT_FALSE(session.isEmpty());
    EXPECT_FALSE(QFile::exists(kDbPath));

    FsHistorySession appendedSession;
    ASSERT_TRUE(FsHistoryStore::appendTrack(kFakeUsb,
            session,
            onUsb(QStringLiteral("House/first.mp3")),
            300,
            &appendedSession));
    EXPECT_EQ(session, appendedSession.name);
    EXPECT_EQ(1, appendedSession.trackCount);
    EXPECT_EQ(300, appendedSession.durationSeconds);
    ASSERT_TRUE(FsHistoryStore::appendTrack(kFakeUsb,
            session,
            onUsb(QStringLiteral("House/second.mp3")),
            240,
            &appendedSession));
    EXPECT_EQ(2, appendedSession.trackCount);
    EXPECT_EQ(540, appendedSession.durationSeconds);
    EXPECT_TRUE(QFile::exists(kDbPath));

    ASSERT_TRUE(FsHistoryStore::readSessions(kFakeUsb, &sessions));
    ASSERT_EQ(1, sessions.size());
    EXPECT_EQ(session, sessions.first().name);
    EXPECT_EQ(2, sessions.first().trackCount);
    EXPECT_EQ(540, sessions.first().durationSeconds);

    // Play order, not the order the filesystem or the query happens to return.
    QStringList locations;
    ASSERT_TRUE(FsHistoryStore::readSessionTracks(kFakeUsb, session, &locations));
    ASSERT_EQ(2, locations.size());
    EXPECT_EQ(onUsb(QStringLiteral("House/first.mp3")), locations.at(0));
    EXPECT_EQ(onUsb(QStringLiteral("House/second.mp3")), locations.at(1));
}

TEST_F(FsHistoryStoreTest, WriterKeepsSessionsSerialAndReturnsRunningTotals) {
    if (!haveFakeUsb()) {
        GTEST_SKIP() << "needs a filesystem mounted at " << qPrintable(kFakeUsb);
    }

    FsHistoryWriter writer;
    QSignalSpy appendedSpy(&writer, &FsHistoryWriter::trackAppended);

    writer.appendTrack(kFakeUsb,
            0,
            onUsb(QStringLiteral("first.mp3")),
            120,
            TrackId(QVariant(1)));
    ASSERT_EQ(1, appendedSpy.count());
    QList<QVariant> first = appendedSpy.takeFirst();
    const QString firstSession = first.at(2).toString();
    EXPECT_FALSE(firstSession.isEmpty());
    EXPECT_EQ(1, first.at(3).toInt());
    EXPECT_EQ(120, first.at(4).toInt());

    writer.appendTrack(kFakeUsb,
            0,
            onUsb(QStringLiteral("second.mp3")),
            180,
            TrackId(QVariant(2)));
    ASSERT_EQ(1, appendedSpy.count());
    QList<QVariant> second = appendedSpy.takeFirst();
    EXPECT_EQ(firstSession, second.at(2).toString());
    EXPECT_EQ(2, second.at(3).toInt());
    EXPECT_EQ(300, second.at(4).toInt());

    writer.appendTrack(kFakeUsb,
            1,
            onUsb(QStringLiteral("third.mp3")),
            240,
            TrackId(QVariant(3)));
    ASSERT_EQ(1, appendedSpy.count());
    QList<QVariant> third = appendedSpy.takeFirst();
    EXPECT_NE(firstSession, third.at(2).toString());
    EXPECT_EQ(1, third.at(3).toInt());
    EXPECT_EQ(240, third.at(4).toInt());
}

// Paths are stored relative to the mount root, which is what lets a set come
// back when the automounter puts the stick somewhere else — or when it is
// played on another unit entirely.
TEST_F(FsHistoryStoreTest, StoresPathsRelativeToTheDrive) {
    if (!haveFakeUsb()) {
        GTEST_SKIP() << "needs a filesystem mounted at " << qPrintable(kFakeUsb);
    }

    const QString session = FsHistoryStore::newSessionName(kFakeUsb);
    ASSERT_TRUE(FsHistoryStore::appendTrack(
            kFakeUsb, session, onUsb(QStringLiteral("Techno/track.flac")), 420));

    QFile db(kDbPath);
    ASSERT_TRUE(db.open(QIODevice::ReadOnly));
    const QByteArray raw = db.readAll();
    EXPECT_TRUE(raw.contains("Techno/track.flac"));
    EXPECT_FALSE(raw.contains(kFakeUsb.toUtf8()));
}

// A track that is not on the drive has no relative path to be stored under, so
// it is refused rather than written as an absolute path that would mean
// nothing on the next unit.
TEST_F(FsHistoryStoreTest, RefusesTracksFromAnotherFilesystem) {
    if (!haveFakeUsb()) {
        GTEST_SKIP() << "needs a filesystem mounted at " << qPrintable(kFakeUsb);
    }

    const QString session = FsHistoryStore::newSessionName(kFakeUsb);
    EXPECT_FALSE(FsHistoryStore::appendTrack(kFakeUsb,
            session,
            QStringLiteral("/home/dj/Music/elsewhere.mp3"),
            180));
}

// Sessions of one day are told apart by a suffix, and the newest one is listed
// first — that is the order the sidebar shows them in.
TEST_F(FsHistoryStoreTest, SeparatesSessionsAndListsNewestFirst) {
    if (!haveFakeUsb()) {
        GTEST_SKIP() << "needs a filesystem mounted at " << qPrintable(kFakeUsb);
    }

    const QString first = FsHistoryStore::newSessionName(kFakeUsb);
    ASSERT_TRUE(FsHistoryStore::appendTrack(
            kFakeUsb, first, onUsb(QStringLiteral("a.mp3")), 100));

    const QString second = FsHistoryStore::newSessionName(kFakeUsb);
    EXPECT_NE(first, second);
    ASSERT_TRUE(FsHistoryStore::appendTrack(
            kFakeUsb, second, onUsb(QStringLiteral("b.mp3")), 200));

    QList<FsHistorySession> sessions;
    ASSERT_TRUE(FsHistoryStore::readSessions(kFakeUsb, &sessions));
    ASSERT_EQ(2, sessions.size());
    EXPECT_EQ(second, sessions.at(0).name);
    EXPECT_EQ(first, sessions.at(1).name);

    // Each session holds only its own tracks.
    QStringList locations;
    ASSERT_TRUE(FsHistoryStore::readSessionTracks(kFakeUsb, first, &locations));
    ASSERT_EQ(1, locations.size());
    EXPECT_EQ(onUsb(QStringLiteral("a.mp3")), locations.at(0));
}

// Deleting one session leaves the others alone; clearing takes the whole store
// with it, the way the per-drive settings actions do.
TEST_F(FsHistoryStoreTest, DeletesOneSessionAndTheWholeStore) {
    if (!haveFakeUsb()) {
        GTEST_SKIP() << "needs a filesystem mounted at " << qPrintable(kFakeUsb);
    }

    const QString first = FsHistoryStore::newSessionName(kFakeUsb);
    ASSERT_TRUE(FsHistoryStore::appendTrack(
            kFakeUsb, first, onUsb(QStringLiteral("a.mp3")), 100));
    const QString second = FsHistoryStore::newSessionName(kFakeUsb);
    ASSERT_TRUE(FsHistoryStore::appendTrack(
            kFakeUsb, second, onUsb(QStringLiteral("b.mp3")), 200));

    ASSERT_TRUE(FsHistoryStore::deleteSession(kFakeUsb, first));
    QList<FsHistorySession> sessions;
    ASSERT_TRUE(FsHistoryStore::readSessions(kFakeUsb, &sessions));
    ASSERT_EQ(1, sessions.size());
    EXPECT_EQ(second, sessions.first().name);

    FsHistorySession summary;
    EXPECT_FALSE(FsHistoryStore::readSessionSummary(kFakeUsb, first, &summary));
    ASSERT_TRUE(FsHistoryStore::readSessionSummary(kFakeUsb, second, &summary));
    EXPECT_EQ(1, summary.trackCount);
    EXPECT_EQ(200, summary.durationSeconds);

    ASSERT_TRUE(FsHistoryStore::clearFilesystemHistory(kFakeUsb));
    EXPECT_FALSE(QFile::exists(kDbPath));
    EXPECT_FALSE(FsHistoryStore::readSessions(kFakeUsb, &sessions));
}

// Nothing is stored anywhere but on a removable drive: the boot volume has no
// history of its own, and a path that is not a mount point cannot stand in for
// a stick.
TEST_F(FsHistoryStoreTest, RefusesNonRemovableTargets) {
    const QString home = QDir::homePath();
    EXPECT_TRUE(FsHistoryStore::newSessionName(home).isEmpty());
    EXPECT_FALSE(FsHistoryStore::appendTrack(home,
            QStringLiteral("2026-01-01"),
            home + QStringLiteral("/Music/track.mp3"),
            120));

    QList<FsHistorySession> sessions;
    EXPECT_FALSE(FsHistoryStore::readSessions(home, &sessions));
}

// Tests for the Bite DJ drag scrolling of skin Scrollables (the FX parameter
// list): dragging anywhere on the content scrolls it, a tap still reaches the
// widget under the finger, and knobs keep their own drag. Covers both input
// paths the touchscreen produces: touch events for WWidgets and mouse events
// for everything else.
#include "widget/contentdragscroller.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventPoint>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <memory>

#include "control/controlpushbutton.h"
#include "test/mixxxtest.h"
#include "widget/wknobcomposed.h"

namespace {

constexpr int kRowCount = 20;
constexpr int kRowHeight = 40;
constexpr int kAreaHeight = 200;

// Well above the drag start distance
constexpr int kDragDistance = 60;

/// Stands in for a skin widget: counts the presses and releases it gets, and
/// takes touch events like a WWidget does.
class RecordingWidget : public QWidget {
  public:
    explicit RecordingWidget(QWidget* pParent)
            : QWidget(pParent) {
        setAttribute(Qt::WA_AcceptTouchEvents);
        setFixedHeight(kRowHeight);
    }

    int presses = 0;
    int releases = 0;
    int touches = 0;

  protected:
    bool event(QEvent* pEvent) override {
        switch (pEvent->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
            ++touches;
            pEvent->accept();
            return true;
        default:
            return QWidget::event(pEvent);
        }
    }
    void mousePressEvent(QMouseEvent* pEvent) override {
        ++presses;
        pEvent->accept();
    }
    void mouseReleaseEvent(QMouseEvent* pEvent) override {
        ++releases;
        pEvent->accept();
    }
};

class ContentDragScrollerTest : public MixxxTest {
  protected:
    void SetUp() override {
        // Every WWidget looks this up
        m_pTouchShift = std::make_unique<ControlPushButton>(
                ConfigKey("[Controls]", "touch_shift"));
        m_pArea = std::make_unique<QScrollArea>();
        m_pArea->setWidgetResizable(true);
        auto* pContent = new QWidget();
        auto* pLayout = new QVBoxLayout(pContent);
        pLayout->setContentsMargins(0, 0, 0, 0);
        pLayout->setSpacing(0);
        for (int i = 0; i < kRowCount; ++i) {
            auto* pRow = new RecordingWidget(pContent);
            pLayout->addWidget(pRow);
            m_rows.append(pRow);
        }
        m_pKnob = new WKnobComposed(pContent);
        m_pKnob->setFixedHeight(kRowHeight);
        pLayout->addWidget(m_pKnob);
        m_pArea->setWidget(pContent);
        m_pArea->resize(200, kAreaHeight);
        m_pArea->show();
        QCoreApplication::processEvents();

        m_pTouchDevice = QTest::createTouchDevice();

        ContentDragScroller::install(m_pArea.get());
    }

    void TearDown() override {
        m_pArea.reset();
        m_pTouchShift.reset();
    }

    int scrollPosition() const {
        return m_pArea->verticalScrollBar()->value();
    }

    /// A finger stays where it is on the screen while the content moves
    /// under it, so gestures are given in coordinates of the viewport, which
    /// doesn't move, and mapped into the widget they are sent to.
    QPointF globalPos(int viewportY) const {
        return m_pArea->viewport()->mapToGlobal(QPointF(10, viewportY));
    }

    void sendMouse(QWidget* pWidget,
            QEvent::Type type,
            int viewportY,
            Qt::MouseButton button,
            Qt::MouseButtons buttons) {
        const QPointF global = globalPos(viewportY);
        const QPointF pos = pWidget->mapFromGlobal(global);
        QMouseEvent event(type,
                pos,
                pos,
                global,
                button,
                buttons,
                Qt::NoModifier,
                QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(pWidget, &event);
    }

    void mouseDrag(QWidget* pWidget, int fromY, int toY) {
        sendMouse(pWidget, QEvent::MouseButtonPress, fromY, Qt::LeftButton, Qt::LeftButton);
        sendMouse(pWidget, QEvent::MouseMove, (fromY + toY) / 2, Qt::NoButton, Qt::LeftButton);
        sendMouse(pWidget, QEvent::MouseMove, toY, Qt::NoButton, Qt::LeftButton);
        sendMouse(pWidget, QEvent::MouseButtonRelease, toY, Qt::LeftButton, Qt::NoButton);
    }

    void sendTouch(QWidget* pWidget, QEvent::Type type, int viewportY) {
        const QPointF global = globalPos(viewportY);
        const QPointF pos = pWidget->mapFromGlobal(global);
        const QEventPoint::State state = type == QEvent::TouchBegin
                ? QEventPoint::State::Pressed
                : type == QEvent::TouchEnd ? QEventPoint::State::Released
                                           : QEventPoint::State::Updated;
        QEventPoint point(1, state, pos, global);
        QTouchEvent event(type, m_pTouchDevice, Qt::NoModifier, {point});
        QCoreApplication::sendEvent(pWidget, &event);
    }

    std::unique_ptr<ControlPushButton> m_pTouchShift;
    std::unique_ptr<QScrollArea> m_pArea;
    QList<RecordingWidget*> m_rows;
    WKnobComposed* m_pKnob = nullptr;
    QPointingDevice* m_pTouchDevice = nullptr;
};

TEST_F(ContentDragScrollerTest, MouseDragOnAWidgetScrollsWithoutPressingIt) {
    RecordingWidget* pRow = m_rows.at(3);
    mouseDrag(pRow, 3 * kRowHeight + 30, 3 * kRowHeight + 30 - kDragDistance);

    EXPECT_EQ(kDragDistance, scrollPosition());
    EXPECT_EQ(0, pRow->presses);
    EXPECT_EQ(0, pRow->releases);
}

TEST_F(ContentDragScrollerTest, MouseTapStillReachesTheWidget) {
    RecordingWidget* pRow = m_rows.at(1);
    const int y = kRowHeight + 20;
    sendMouse(pRow, QEvent::MouseButtonPress, y, Qt::LeftButton, Qt::LeftButton);
    EXPECT_EQ(0, pRow->presses);
    sendMouse(pRow, QEvent::MouseButtonRelease, y, Qt::LeftButton, Qt::NoButton);

    EXPECT_EQ(1, pRow->presses);
    EXPECT_EQ(1, pRow->releases);
    EXPECT_EQ(0, scrollPosition());
}

TEST_F(ContentDragScrollerTest, TouchDragScrollsWithoutPressingTheWidget) {
    m_pArea->verticalScrollBar()->setValue(300);
    // Its top is at 9 * 40 - 300 = 60 in the viewport
    RecordingWidget* pRow = m_rows.at(9);
    sendTouch(pRow, QEvent::TouchBegin, 70);
    sendTouch(pRow, QEvent::TouchUpdate, 70 + kDragDistance / 2);
    sendTouch(pRow, QEvent::TouchUpdate, 70 + kDragDistance);
    sendTouch(pRow, QEvent::TouchEnd, 70 + kDragDistance);

    EXPECT_EQ(300 - kDragDistance, scrollPosition());
    EXPECT_EQ(0, pRow->touches);
    EXPECT_EQ(0, pRow->presses);
    EXPECT_EQ(0, pRow->releases);
}

TEST_F(ContentDragScrollerTest, TouchTapBecomesAClickOnTheWidget) {
    RecordingWidget* pRow = m_rows.at(0);
    sendTouch(pRow, QEvent::TouchBegin, 20);
    // Fingertip jitter below the drag threshold
    sendTouch(pRow, QEvent::TouchUpdate, 24);
    sendTouch(pRow, QEvent::TouchEnd, 24);

    EXPECT_EQ(0, pRow->touches);
    EXPECT_EQ(1, pRow->presses);
    EXPECT_EQ(1, pRow->releases);
    EXPECT_EQ(0, scrollPosition());
}

TEST_F(ContentDragScrollerTest, KnobsKeepTheirOwnDrag) {
    m_pArea->verticalScrollBar()->setValue(m_pArea->verticalScrollBar()->maximum());
    const int before = scrollPosition();
    // The knob is the last row, at the bottom of the scrolled-down viewport
    mouseDrag(m_pKnob, kAreaHeight - 10, kAreaHeight - 10 - kDragDistance);

    EXPECT_EQ(before, scrollPosition());
}

TEST_F(ContentDragScrollerTest, WidgetsOutsideTheContentAreIgnored) {
    RecordingWidget outside(nullptr);
    outside.show();
    // Positions are only mapped into the widget, which isn't in the area
    mouseDrag(&outside, 30, 30 - kDragDistance);

    EXPECT_EQ(1, outside.presses);
    EXPECT_EQ(0, scrollPosition());
}

} // namespace

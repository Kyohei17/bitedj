#include "widget/contentdragscroller.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QScrollBar>
#include <QTouchEvent>
#include <cmath>

#include "moc_contentdragscroller.cpp"
#include "util/assert.h"
#include "util/math.h"

namespace {

/// Same threshold as TouchScrollFilter: Qt's drag distance is meant for a
/// mouse and is easily exceeded by the jitter of a fingertip.
constexpr int kMinDragStartDistancePx = 12;

int dragStartDistance() {
    return math_max(QApplication::startDragDistance(), kMinDragStartDistancePx);
}

/// Widgets whose own interaction is a drag. Scrolling from them would make
/// them impossible to operate.
bool handlesOwnDrag(const QWidget* pWidget) {
    return pWidget->inherits("WKnobComposed") ||
            pWidget->inherits("WKnob") ||
            pWidget->inherits("WSliderComposed");
}

QPointF touchGlobalPos(const QTouchEvent* pEvent) {
    const auto& points = pEvent->points();
    return points.isEmpty() ? QPointF() : points.first().globalPosition();
}

} // anonymous namespace

// static
void ContentDragScroller::install(QAbstractScrollArea* pScrollArea) {
    VERIFY_OR_DEBUG_ASSERT(pScrollArea) {
        return;
    }
    // Owned by the scroll area
    new ContentDragScroller(pScrollArea);
}

ContentDragScroller::ContentDragScroller(QAbstractScrollArea* pScrollArea)
        : QObject(pScrollArea),
          m_pScrollArea(pScrollArea),
          m_state(State::Idle),
          m_touch(false),
          m_replaying(false),
          m_remainingDy(0) {
    // The content's widgets receive the input, not the scroll area, and they
    // may be created or replaced after this point. Qt drops the filter again
    // when this object is destroyed.
    qApp->installEventFilter(this);
}

bool ContentDragScroller::claims(QWidget* pWidget) const {
    if (!pWidget || handlesOwnDrag(pWidget)) {
        return false;
    }
    QWidget* pViewport = m_pScrollArea->viewport();
    return pWidget == pViewport || pViewport->isAncestorOf(pWidget);
}

bool ContentDragScroller::eventFilter(QObject* pWatched, QEvent* pEvent) {
    if (m_replaying) {
        return false;
    }

    switch (pEvent->type()) {
    case QEvent::TouchBegin: {
        auto* pWidget = qobject_cast<QWidget*>(pWatched);
        if (!claims(pWidget)) {
            return false;
        }
        begin(pWidget, touchGlobalPos(static_cast<QTouchEvent*>(pEvent)), true);
        // Accepting keeps the implicit touch grab on the widget, so the rest
        // of the gesture is delivered to it and passes through here.
        pEvent->accept();
        return true;
    }
    case QEvent::TouchUpdate:
        if (m_state == State::Idle || !m_touch || pWatched != m_pTarget) {
            return false;
        }
        follow(touchGlobalPos(static_cast<QTouchEvent*>(pEvent)));
        pEvent->accept();
        return true;
    case QEvent::TouchEnd:
        if (m_state == State::Idle || !m_touch || pWatched != m_pTarget) {
            return false;
        }
        if (m_state == State::Pending) {
            replayTap(true);
        }
        m_state = State::Idle;
        pEvent->accept();
        return true;
    case QEvent::TouchCancel:
        if (m_touch) {
            m_state = State::Idle;
        }
        return false;
    case QEvent::MouseButtonPress: {
        auto* pMouseEvent = static_cast<QMouseEvent*>(pEvent);
        auto* pWidget = qobject_cast<QWidget*>(pWatched);
        if (pMouseEvent->button() != Qt::LeftButton || !claims(pWidget)) {
            return false;
        }
        begin(pWidget, pMouseEvent->globalPosition(), false);
        pEvent->accept();
        return true;
    }
    case QEvent::MouseMove:
        if (m_state == State::Idle || m_touch || pWatched != m_pTarget) {
            return false;
        }
        follow(static_cast<QMouseEvent*>(pEvent)->globalPosition());
        pEvent->accept();
        return true;
    case QEvent::MouseButtonRelease: {
        if (m_state == State::Idle || m_touch || pWatched != m_pTarget) {
            return false;
        }
        const State state = m_state;
        m_state = State::Idle;
        if (state == State::Scrolling) {
            // The gesture was a scroll, the widget must not act on it.
            pEvent->accept();
            return true;
        }
        // A tap after all: give the widget the press it never got. The
        // release we return to is delivered right after it.
        replayTap(false);
        return false;
    }
    case QEvent::MouseButtonDblClick:
        // Qt sends this instead of the second press of a double tap, so there
        // is nothing to hold back.
        if (!m_touch) {
            m_state = State::Idle;
        }
        return false;
    default:
        return false;
    }
}

void ContentDragScroller::begin(QWidget* pTarget, QPointF globalPos, bool touch) {
    m_state = State::Pending;
    m_touch = touch;
    m_pTarget = pTarget;
    m_pressGlobalPos = globalPos;
    m_lastGlobalPos = globalPos;
    m_remainingDy = 0;
}

void ContentDragScroller::follow(QPointF globalPos) {
    if (m_state == State::Pending) {
        if (std::abs(globalPos.y() - m_pressGlobalPos.y()) < dragStartDistance()) {
            // Might still become a tap
            return;
        }
        m_state = State::Scrolling;
        // m_lastGlobalPos is still the press position, so the content catches
        // up with the finger in this first step and stays pinned to it.
    }

    // Scroll bar values are integers, carry the remainder over to the next
    // move so slow drags don't get lost in rounding.
    m_remainingDy += m_lastGlobalPos.y() - globalPos.y();
    const int scrollBy = static_cast<int>(m_remainingDy);
    if (scrollBy != 0) {
        m_remainingDy -= scrollBy;
        QScrollBar* pScrollBar = m_pScrollArea->verticalScrollBar();
        pScrollBar->setValue(pScrollBar->value() + scrollBy);
    }
    m_lastGlobalPos = globalPos;
}

void ContentDragScroller::replayTap(bool withRelease) {
    QWidget* pTarget = m_pTarget;
    if (!pTarget) {
        return;
    }
    const QPointF localPos = pTarget->mapFromGlobal(m_pressGlobalPos);
    const QPointingDevice* pDevice = QPointingDevice::primaryPointingDevice();
    m_replaying = true;
    QMouseEvent pressEvent(QEvent::MouseButtonPress,
            localPos,
            localPos,
            m_pressGlobalPos,
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier,
            pDevice);
    QCoreApplication::sendEvent(pTarget, &pressEvent);
    // The press handler may have hidden or deleted the widget.
    if (withRelease && m_pTarget) {
        QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                localPos,
                localPos,
                m_pressGlobalPos,
                Qt::LeftButton,
                Qt::NoButton,
                Qt::NoModifier,
                pDevice);
        QCoreApplication::sendEvent(m_pTarget, &releaseEvent);
    }
    m_replaying = false;
}

#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>

class QAbstractScrollArea;
class QEvent;
class QWidget;

/// Scrolls a scroll area by dragging anywhere on its content, including on the
/// skin widgets inside it.
///
/// TouchScrollFilter covers item views, whose viewport receives the input
/// itself. A skin Scrollable is different: its content is made of WWidgets,
/// and every WWidget takes touch events and turns them into mouse events for
/// itself only (see WWidget::event), so neither the viewport nor QScroller ever
/// sees the gesture. This filter therefore watches the whole application for
/// input aimed at the content and holds each press back until it knows what
/// the gesture is:
/// * moved far enough vertically: the content follows the finger and the
///   widget under it never sees the press
/// * released without moving: the widget gets the press followed by the
///   release, so taps keep working
///
/// Widgets that are themselves operated by dragging (knobs, sliders) get their
/// input untouched: dragging on them turns them, dragging next to them scrolls.
class ContentDragScroller : public QObject {
    Q_OBJECT

  public:
    /// Enables drag scrolling for pScrollArea. The scroller is owned by the
    /// scroll area.
    static void install(QAbstractScrollArea* pScrollArea);

  protected:
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;

  private:
    enum class State {
        /// No press to act on
        Idle,
        /// Press held back, gesture not classified yet
        Pending,
        /// Content is following the finger
        Scrolling,
    };

    explicit ContentDragScroller(QAbstractScrollArea* pScrollArea);

    /// Whether pWidget is part of the scrolled content and does not handle
    /// drags itself
    bool claims(QWidget* pWidget) const;
    void begin(QWidget* pTarget, QPointF globalPos, bool touch);
    void follow(QPointF globalPos);
    /// Delivers the held back press, and for touch the release as well, to
    /// the widget the gesture started on.
    void replayTap(bool withRelease);

    QAbstractScrollArea* const m_pScrollArea;

    State m_state;
    /// Whether the current gesture arrives as touch or as mouse events
    bool m_touch;
    /// Guards the events we send ourselves against being filtered again
    bool m_replaying;

    QPointer<QWidget> m_pTarget;
    QPointF m_pressGlobalPos;
    QPointF m_lastGlobalPos;
    /// Sub-pixel remainder of the movement not yet applied to the scroll bar
    qreal m_remainingDy;
};

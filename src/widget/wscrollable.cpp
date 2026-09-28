#include "widget/wscrollable.h"

#include <QScrollBar>
#include <QTimer>

#include "control/controlproxy.h"
#include "moc_wscrollable.cpp"
#include "skin/legacy/skincontext.h"
#include "widget/contentdragscroller.h"

WScrollable::WScrollable(QWidget* pParent)
        : QScrollArea(pParent),
          WBaseWidget(this) {
}

void WScrollable::setup(const QDomNode& node, const SkinContext& context) {
    // Lets the child follow the width of the scroll area, so only its height
    // can overflow.
    if (context.selectBool(node, "WidgetResizable", false)) {
        setWidgetResizable(true);
    }
    if (context.selectBool(node, "DragScroll", false)) {
        ContentDragScroller::install(this);
    }
    // Content that is swapped out as a whole, e.g. the parameters of the
    // loaded effect, should be shown from its start again.
    QString scrollToTopOn;
    if (context.hasNodeSelectString(node, "ScrollToTopOn", &scrollToTopOn)) {
        const ConfigKey key = ConfigKey::parseCommaSeparated(scrollToTopOn);
        if (key.isValid()) {
            auto* pControl = new ControlProxy(key, this);
            pControl->connectValueChanged(this, [this](double) {
                // Deferred until the new content has been laid out.
                QTimer::singleShot(0, this, [this] {
                    verticalScrollBar()->setValue(0);
                });
            });
        } else {
            SKIN_WARNING(node,
                    context,
                    QStringLiteral("Invalid ScrollToTopOn control: %1")
                            .arg(scrollToTopOn));
        }
    }
    QString horizontalPolicy;
    // The QT default is "As Needed", so we don't need a selector for that.
    if (context.hasNodeSelectString(node, "HorizontalScrollBarPolicy", &horizontalPolicy)) {
        if (horizontalPolicy == "on") {
            setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        } else if (horizontalPolicy == "off") {
            setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }
    QString verticalPolicy;
    if (context.hasNodeSelectString(node, "VerticalScrollBarPolicy", &verticalPolicy)) {
        if (verticalPolicy == "on") {
            setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        } else if (verticalPolicy == "off") {
            setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }
}

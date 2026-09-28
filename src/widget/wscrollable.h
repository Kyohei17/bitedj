#pragma once

#include <QScrollArea>

#include "widget/wbasewidget.h"

class QDomNode;
class SkinContext;

/// Creates a QScrollArea. The QT default is to show scrollbars if needed.
class WScrollable : public QScrollArea, public WBaseWidget {
    Q_OBJECT
  public:
    WScrollable(QWidget* pParent);

    /// WScrollable supports the following attributes: HorizontalScrollBarPolicy,
    /// VerticalScrollBarPolicy, WidgetResizable, DragScroll (touch drag on
    /// the content scrolls it) and ScrollToTopOn (a group,item control whose
    /// every change scrolls back to the top).
    void setup(const QDomNode& node, const SkinContext& context);
};

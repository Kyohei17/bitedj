#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDomDocument>
#include <QMouseEvent>
#include <QScopedPointer>
#include <QTest>
#include <QTestEventList>

#include <memory>

#include "mixxxtest.h"
#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "control/controlpushbutton.h"
#include "widget/wpushbutton.h"
#include "skin/legacy/skincontext.h"
#include "widget/controlwidgetconnection.h"

class WPushButtonTest : public MixxxTest {
  public:
    WPushButtonTest()
          : m_pGroup("[Channel1]") {
    }

  protected:
    void SetUp() override {
        m_pTouchShift.reset(new ControlPushButton(ConfigKey("[Controls]", "touch_shift")));
        m_pButton.reset(new WPushButton());
        m_pButton->setStates(2);
    }

    QScopedPointer<WPushButton> m_pButton;
    QScopedPointer<ControlPushButton> m_pTouchShift;
    QTestEventList m_Events;
    const char* m_pGroup;
};

TEST_F(WPushButtonTest, QuickPressNoLatchTest) {
    QScopedPointer<ControlPushButton> pPushControl(
        new ControlPushButton(ConfigKey("[Test]", "push")));
    pPushControl->setButtonMode(ControlPushButton::LONGPRESSLATCHING);

    m_pButton.reset(new WPushButton(NULL, ControlPushButton::LONGPRESSLATCHING,
                                    ControlPushButton::PUSH));
    m_pButton->setStates(2);
    m_pButton->addLeftConnection(
        new ControlParameterWidgetConnection(
            m_pButton.data(),
            pPushControl->getKey(), NULL,
            ControlParameterWidgetConnection::DIR_FROM_AND_TO_WIDGET,
            ControlParameterWidgetConnection::EMIT_ON_PRESS_AND_RELEASE));

    // This test can be flaky if the event simulator takes too long to deliver
    // the event.
    m_Events.addMousePress(Qt::LeftButton);
    m_Events.addDelay(100);
    m_Events.addMouseRelease(Qt::LeftButton);

    m_Events.simulate(m_pButton.data());

    ASSERT_EQ(0.0, m_pButton->getControlParameterLeft());
}

TEST_F(WPushButtonTest, LongPressLatchTest) {
    QScopedPointer<ControlPushButton> pPushControl(
        new ControlPushButton(ConfigKey("[Test]", "push")));
    pPushControl->setButtonMode(ControlPushButton::LONGPRESSLATCHING);

    m_pButton.reset(new WPushButton(NULL, ControlPushButton::LONGPRESSLATCHING,
                                    ControlPushButton::PUSH));
    m_pButton->setStates(2);
    m_pButton->addLeftConnection(
        new ControlParameterWidgetConnection(
            m_pButton.data(),
            pPushControl->getKey(), NULL,
            ControlParameterWidgetConnection::DIR_FROM_AND_TO_WIDGET,
            ControlParameterWidgetConnection::EMIT_ON_PRESS_AND_RELEASE));

    m_Events.addMousePress(Qt::LeftButton);
    m_Events.addDelay(1000);
    m_Events.addMouseRelease(Qt::LeftButton);

    m_Events.simulate(m_pButton.data());

    ASSERT_EQ(1.0, m_pButton->getControlParameterLeft());
}

// Hold-to-repeat, the skin's <Repeat> option. The beat grid editor leans on it:
// beats_translate_earlier moves the grid 10 ms and beats_adjust_faster changes
// it by 0.01 BPM, so a correction is dozens of presses unless holding the
// button keeps it firing.
class WPushButtonRepeatTest : public WPushButtonTest {
  protected:
    // Builds a repeating button bound to pControl. Connections have to be in
    // place before setup(), which is the order LegacySkinParser uses.
    void buildButton(ControlPushButton* pControl, const QString& extraXml, int states = 1) {
        m_pButton.reset(new WPushButton());
        m_pButton->addLeftConnection(
                new ControlParameterWidgetConnection(m_pButton.data(),
                        pControl->getKey(),
                        nullptr,
                        ControlParameterWidgetConnection::DIR_FROM_AND_TO_WIDGET,
                        ControlParameterWidgetConnection::EMIT_DEFAULT));

        QDomDocument doc;
        ASSERT_TRUE(doc.setContent(
                QStringLiteral("<PushButton><NumberStates>%1</NumberStates>").arg(states) +
                extraXml + QStringLiteral("</PushButton>")));
        m_pContext = std::make_unique<SkinContext>(config(), QString());
        m_pButton->setup(doc.documentElement(), *m_pContext);
    }

    // Counts the presses the control sees, which is what a BpmControl slot
    // acts on: it steps the grid once per 0 -> 1 edge.
    void countPresses(ControlPushButton* pControl) {
        m_presses = 0;
        QObject::connect(pControl,
                &ControlObject::valueChanged,
                pControl,
                [this](double v) {
                    if (v > 0.0) {
                        ++m_presses;
                    }
                });
    }

    std::unique_ptr<SkinContext> m_pContext;
    int m_presses = 0;
};

TEST_F(WPushButtonRepeatTest, HoldKeepsFiring) {
    ControlPushButton control(ConfigKey("[Test]", "repeat"));
    countPresses(&control);
    buildButton(&control,
            QStringLiteral("<Repeat>true</Repeat>"
                           "<RepeatDelay>30</RepeatDelay>"
                           "<RepeatInterval>10</RepeatInterval>"));

    m_Events.addMousePress(Qt::LeftButton);
    m_Events.addDelay(200);
    m_Events.addMouseRelease(Qt::LeftButton);
    m_Events.simulate(m_pButton.data());

    // 30 ms of delay then one every 10 ms: ~17 in 200 ms. Timer scheduling is
    // not exact, so only assert that holding does far more than one step.
    EXPECT_GT(m_presses, 3);
    // A held button that never stops is a grid edit that runs away, so the
    // release has to leave the control down and the timer stopped.
    EXPECT_EQ(0.0, control.get());
    const int pressesAtRelease = m_presses;
    QTest::qWait(100);
    EXPECT_EQ(pressesAtRelease, m_presses);
}

TEST_F(WPushButtonRepeatTest, HoldAccelerates) {
    ControlPushButton control(ConfigKey("[Test]", "repeat"));
    countPresses(&control);
    buildButton(&control,
            QStringLiteral("<Repeat>true</Repeat>"
                           "<RepeatDelay>20</RepeatDelay>"
                           "<RepeatInterval>40</RepeatInterval>"
                           "<RepeatMinInterval>5</RepeatMinInterval>"));

    // Two equal windows of holding. A fixed rate would put the same number of
    // steps in each; the point of accelerating is that the second is worth
    // more, so a long hold moves the grid a useful distance.
    QMouseEvent press(QEvent::MouseButtonPress,
            QPointF(), QPointF(), QPointF(),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_pButton.data(), &press);

    QTest::qWait(300);
    const int firstWindow = m_presses;
    QTest::qWait(300);
    const int secondWindow = m_presses - firstWindow;

    QMouseEvent release(QEvent::MouseButtonRelease,
            QPointF(), QPointF(), QPointF(),
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_pButton.data(), &release);

    EXPECT_GT(firstWindow, 0);
    EXPECT_GT(secondWindow, firstWindow);
}

TEST_F(WPushButtonRepeatTest, TapFiresOnce) {
    ControlPushButton control(ConfigKey("[Test]", "repeat"));
    countPresses(&control);
    buildButton(&control,
            QStringLiteral("<Repeat>true</Repeat>"
                           "<RepeatDelay>300</RepeatDelay>"
                           "<RepeatInterval>10</RepeatInterval>"));

    m_Events.addMousePress(Qt::LeftButton);
    m_Events.addDelay(20);
    m_Events.addMouseRelease(Qt::LeftButton);
    m_Events.simulate(m_pButton.data());

    // Released well inside the delay, so this is an ordinary single step.
    EXPECT_EQ(1, m_presses);
}

TEST_F(WPushButtonRepeatTest, ToggleDoesNotRepeat) {
    ControlPushButton control(ConfigKey("[Test]", "repeat"));
    control.setButtonMode(ControlPushButton::TOGGLE);
    countPresses(&control);
    buildButton(&control,
            QStringLiteral("<Repeat>true</Repeat>"
                           "<RepeatDelay>30</RepeatDelay>"
                           "<RepeatInterval>10</RepeatInterval>"),
            2);

    m_Events.addMousePress(Qt::LeftButton);
    m_Events.addDelay(200);
    m_Events.addMouseRelease(Qt::LeftButton);
    m_Events.simulate(m_pButton.data());

    // Re-firing a toggle would only flip it back and forth, so setup() refuses
    // the repeat and the hold stays a single toggle. A one-state button is a
    // push whatever its control says, which is why this one needs two.
    EXPECT_EQ(1, m_presses);
    EXPECT_EQ(1.0, control.get());
}

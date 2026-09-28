// Tests for the Bite DJ period-in-beats alias of Beats-typed effect knobs
// (parameterN_beat_period), which the FX panel's bucket picker and the
// DDJ-400 / FLX2 / FLX4 Beat buttons write. Built-in effects encode their
// beat division differently (Tremolo's rate in cycles per beat, Echo and
// Phaser as a period with a quantizer that needs 0 for its minimum), and the
// alias has to hide that while reporting what the effect actually clamped to.
#include <gtest/gtest.h>

#include "control/controlobject.h"
#include "effects/backends/effectsbackendmanager.h"
#include "effects/effectchain.h"
#include "effects/effectslot.h"
#include "effects/effectsmanager.h"
#include "engine/channelhandle.h"
#include "test/mixxxtest.h"

namespace {

class EffectBeatPeriodTest : public MixxxTest {
  protected:
    void SetUp() override {
        m_pFactory = std::make_shared<ChannelHandleFactory>();
        m_pEffectsManager = std::make_unique<EffectsManager>(config(), m_pFactory);
        ChannelHandleAndGroup output(
                m_pFactory->getOrCreateHandle("[MasterOutput]"), "[MasterOutput]");
        ChannelHandleAndGroup deck(m_pFactory->getOrCreateHandle("[Channel1]"), "[Channel1]");
        m_pEffectsManager->registerOutputChannel(output);
        m_pEffectsManager->registerInputChannel(output);
        m_pEffectsManager->registerInputChannel(deck);
        m_pEffectsManager->setup();
        m_pSlot = m_pEffectsManager->getStandardEffectChain(0)->getEffectSlot(0);
        ASSERT_TRUE(m_pSlot);
    }

    void TearDown() override {
        m_pSlot.reset();
        m_pEffectsManager.reset();
    }

    /// Loads a built-in effect and returns the key prefix of its Beats-typed
    /// parameter, or an empty string if it has none.
    QString loadBeatsParameter(const QString& effectId) {
        const auto pManifest = m_pEffectsManager->getBackendManager()->getManifest(
                effectId, EffectBackendType::BuiltIn);
        if (!pManifest) {
            ADD_FAILURE() << "no manifest for" << effectId.toStdString();
            return QString();
        }
        m_pSlot->loadEffectWithDefaults(pManifest);
        for (int i = 1; i <= 16; ++i) {
            const QString prefix = QStringLiteral("parameter%1").arg(i);
            if (get(prefix + "_loaded") == 1 && get(prefix + "_units") == 1) {
                return prefix;
            }
        }
        ADD_FAILURE() << "no Beats parameter in" << effectId.toStdString();
        return QString();
    }

    double get(const QString& item) const {
        return ControlObject::get(ConfigKey(m_pSlot->getGroup(), item));
    }

    void set(const QString& item, double value) {
        ControlObject::set(ConfigKey(m_pSlot->getGroup(), item), value);
    }

    std::shared_ptr<ChannelHandleFactory> m_pFactory;
    std::unique_ptr<EffectsManager> m_pEffectsManager;
    EffectSlotPointer m_pSlot;
};

TEST_F(EffectBeatPeriodTest, TremoloRateIsInvertedToAPeriod) {
    const QString prefix = loadBeatsParameter(QStringLiteral("org.mixxx.effects.tremolo"));
    ASSERT_FALSE(prefix.isEmpty());

    set(prefix + "_beat_period", 0.25);
    EXPECT_DOUBLE_EQ(4.0, get(prefix + "_value"));
    EXPECT_DOUBLE_EQ(0.25, get(prefix + "_beat_period"));

    set(prefix + "_beat_period", 4.0);
    EXPECT_DOUBLE_EQ(0.25, get(prefix + "_value"));
    EXPECT_DOUBLE_EQ(4.0, get(prefix + "_beat_period"));
}

TEST_F(EffectBeatPeriodTest, EchoMinimumUsesTheQuantizerEncodingAndClamps) {
    const QString prefix = loadBeatsParameter(QStringLiteral("org.mixxx.effects.echo"));
    ASSERT_FALSE(prefix.isEmpty());

    // The native quantizer rounds 1/8 up to 1/4, zero selects 1/8.
    set(prefix + "_beat_period", 0.125);
    EXPECT_DOUBLE_EQ(0.0, get(prefix + "_value"));
    EXPECT_DOUBLE_EQ(0.125, get(prefix + "_beat_period"));

    set(prefix + "_beat_period", 1.0);
    EXPECT_DOUBLE_EQ(1.0, get(prefix + "_value"));
    EXPECT_DOUBLE_EQ(1.0, get(prefix + "_beat_period"));

    // Echo goes up to 2 beats, the alias reports the clamped value.
    set(prefix + "_beat_period", 4.0);
    EXPECT_DOUBLE_EQ(2.0, get(prefix + "_beat_period"));
    EXPECT_DOUBLE_EQ(2.0, get(prefix + "_beat_period_max"));
}

TEST_F(EffectBeatPeriodTest, PhaserMinimumUsesTheQuantizerEncoding) {
    const QString prefix = loadBeatsParameter(QStringLiteral("org.mixxx.effects.phaser"));
    ASSERT_FALSE(prefix.isEmpty());

    set(prefix + "_beat_period", 0.25);
    EXPECT_DOUBLE_EQ(0.0, get(prefix + "_value"));
    EXPECT_DOUBLE_EQ(0.25, get(prefix + "_beat_period"));

    set(prefix + "_beat_period", 2.0);
    EXPECT_DOUBLE_EQ(2.0, get(prefix + "_value"));
    EXPECT_DOUBLE_EQ(2.0, get(prefix + "_beat_period"));
}

TEST_F(EffectBeatPeriodTest, GlitchKeepsItsLiteralEighthBeat) {
    const QString prefix = loadBeatsParameter(QStringLiteral("org.mixxx.effects.glitch"));
    ASSERT_FALSE(prefix.isEmpty());

    set(prefix + "_beat_period", 0.125);
    EXPECT_DOUBLE_EQ(0.125, get(prefix + "_value"));
    EXPECT_DOUBLE_EQ(0.125, get(prefix + "_beat_period"));
}

TEST_F(EffectBeatPeriodTest, KnobChangesAreMirroredIntoThePeriod) {
    const QString prefix = loadBeatsParameter(QStringLiteral("org.mixxx.effects.tremolo"));
    ASSERT_FALSE(prefix.isEmpty());

    // A controller or the knob widget writing the raw rate directly
    set(prefix + "_value", 2.0);
    EXPECT_DOUBLE_EQ(0.5, get(prefix + "_beat_period"));
}

TEST_F(EffectBeatPeriodTest, OnlyTheEffectsOwnDryWetIsFlaggedAsMix) {
    const auto pManifest = m_pEffectsManager->getBackendManager()->getManifest(
            QStringLiteral("org.mixxx.effects.flanger"), EffectBackendType::BuiltIn);
    ASSERT_TRUE(pManifest);
    m_pSlot->loadEffectWithDefaults(pManifest);
    int mixCount = 0;
    for (int i = 1; i <= 16; ++i) {
        mixCount += get(QStringLiteral("parameter%1_is_mix").arg(i)) == 1 ? 1 : 0;
    }
    EXPECT_EQ(1, mixCount);

    m_pSlot->loadEffectWithDefaults(m_pEffectsManager->getBackendManager()->getManifest(
            QStringLiteral("org.mixxx.effects.echo"), EffectBackendType::BuiltIn));
    for (int i = 1; i <= 16; ++i) {
        EXPECT_EQ(0, get(QStringLiteral("parameter%1_is_mix").arg(i)));
    }
}

} // namespace

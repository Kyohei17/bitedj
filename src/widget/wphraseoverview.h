#pragma once

#include "control/controlproxy.h"
#include "track/track.h"
#include "waveform/renderers/phrasestrip.h"
#include "widget/wwidget.h"

class WPhraseOverview final : public WWidget {
  public:
    explicit WPhraseOverview(double scale, QWidget* parent = nullptr)
            : WWidget(parent),
              m_waveformDivisions("[BiteDJ]", "waveform_divisions", this),
              m_showTrackTimeRemaining(
                      "[Controls]", "ShowDurationRemaining", this),
              m_scale(scale) {
        setFocusPolicy(Qt::NoFocus);
        m_waveformDivisions.connectValueChanged(this, [this](double) {
            update();
        });
        m_showTrackTimeRemaining.connectValueChanged(this, [this](double) {
            update();
        });
    }

    void setTrack(TrackPointer track) {
        QObject::disconnect(m_phraseConnection);
        m_track = std::move(track);
        if (m_track) {
            m_phraseConnection = connect(
                    m_track.get(), &Track::phrasesUpdated, this, [this] {
                        update();
                    });
        }
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (!m_track)
            return;
        const auto phrases = m_track->getPhrases();
        if (m_waveformDivisions.toBool() && !phrases.isEmpty()) {
            mixxx::paintPhraseStrip(painter,
                    phrases,
                    rect().adjusted(1, int(2 * m_scale), -1, 0),
                    Qt::Horizontal,
                    0,
                    m_track->getDuration(),
                    m_scale,
                    true);
        } else {
            paintTimeScale(painter);
        }
    }

  private:
    void paintTimeScale(QPainter& painter) {
        const double duration = m_track->getDuration();
        if (duration <= 0 || width() <= 0) {
            return;
        }

        QFont font = painter.font();
        font.setPixelSize(std::max(7, int(9 * m_scale)));
        font.setBold(true);
        painter.setFont(font);

        const double pixelsPerDivision = width() * 30.0 / duration;
        int labelStep = 1;
        while (labelStep * pixelsPerDivision < 34.0 * m_scale) {
            ++labelStep;
        }

        const bool remainingMode = m_showTrackTimeRemaining.toBool();
        for (int division = 1; division * 30.0 < duration; ++division) {
            const int scaleSeconds = division * 30;
            const int labelSeconds = scaleSeconds;
            const qreal x = remainingMode
                    ? width() * (duration - scaleSeconds) / duration
                    : width() * scaleSeconds / duration;
            const bool majorDivision = labelSeconds % 60 == 0;
            painter.setPen(QPen(QColor(255, 255, 255, 190),
                    majorDivision ? 2 * m_scale : m_scale));
            painter.drawLine(QPointF(x, 0), QPointF(x, height()));

            if (majorDivision && division % labelStep == 0) {
                const QString label = QStringLiteral("%1%2:%3")
                                              .arg(m_showTrackTimeRemaining.toBool()
                                                              ? QStringLiteral("-")
                                                              : QString())
                                              .arg(labelSeconds / 60)
                                              .arg(labelSeconds % 60, 2, 10, QLatin1Char('0'));
                const int labelWidth = painter.fontMetrics().horizontalAdvance(label);
                painter.setPen(QColor(255, 255, 255, 230));
                painter.drawText(QPointF(x - labelWidth - 3 * m_scale,
                                         height() - painter.fontMetrics().descent()),
                        label);
            }
        }
    }

    TrackPointer m_track;
    QMetaObject::Connection m_phraseConnection;
    ControlProxy m_waveformDivisions;
    ControlProxy m_showTrackTimeRemaining;
    double m_scale;
};

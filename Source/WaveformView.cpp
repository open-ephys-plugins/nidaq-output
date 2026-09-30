/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2014 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "WaveformView.h"

WaveformView::WaveformView()
{
    playButton = std::make_unique<UtilityButton> ("Play");
    playButton->addListener (this);
    addAndMakeVisible (playButton.get());

    updateWaveform();
}

void WaveformView::resized()
{
    playButton->setBounds (getWidth() - 52, 4, 48, 18);
    rebuildPath();
}

void WaveformView::setWaveformType (WaveformType type)
{
    waveformType = type;
    updateWaveform();
}

void WaveformView::setSampleRate (float sampleRateHz)
{
    if (sampleRateHz > 0.0f)
        sampleRate = sampleRateHz;

    updateWaveform();
}

void WaveformView::setPlayButtonVisible (bool shouldBeVisible)
{
    playButton->setVisible (shouldBeVisible);
}

void WaveformView::buttonClicked (Button* button)
{
    if (button == playButton.get() && onPlayRequested)
        onPlayRequested();
}

void WaveformView::updateWaveform()
{
    if (waveformType == WaveformType::sine)
        buildSineWaveform();
    else
        buildPulseWaveform();

    rebuildPath();
    repaint();
}

void WaveformView::buildPulseWaveform()
{
    // Pulse shape within a 100 ms window (matches mockup axis)
    const float onDurationMs = 40.0f;
    const float offDurationMs = 60.0f;
    maxVoltage = 5.0f;
    durationMs = onDurationMs + offDurationMs;

    samples.clear();

    const float numOnSamples = sampleRate * onDurationMs / 1000.0f;
    const float numOffSamples = sampleRate * offDurationMs / 1000.0f;

    for (float i = 0; i < numOnSamples; i++)
        samples.add (maxVoltage);

    for (float i = 0; i < numOffSamples; i++)
        samples.add (0.0f);

    samples.add (0.0f);
}

void WaveformView::buildSineWaveform()
{
    // Matches wave-player SinePatternGenerator defaults (5 Hz, 1 cycle)
    const float frequency = 5.0f;
    const float cycles = 1.0f;
    maxVoltage = 5.0f;
    durationMs = 1000.0f * cycles / frequency;

    samples.clear();

    const int numSamples = juce::jmax (1, (int) (sampleRate * durationMs / 1000.0f));

    for (int i = 0; i < numSamples; i++)
    {
        const float t = (float) i / sampleRate;
        samples.add (maxVoltage * std::sin (MathConstants<float>::twoPi * frequency * t));
    }
}

void WaveformView::rebuildPath()
{
    currentWaveform.clear();

    if (samples.isEmpty() || getWidth() <= 0 || getHeight() <= 0)
        return;

    const float numSamples = (float) samples.size();
    float voltageScale = maxVoltage == 0.0f ? 5.0f : maxVoltage;

    // Normalize to 0..1 for both axes (y: 1 = 0 V, 0 = maxVoltage for positive pulses;
    // allow negative for sine by mapping [-max, +max] -> [1, 0])
    const bool bipolar = (waveformType == WaveformType::sine);

    auto sampleToY = [bipolar, voltageScale] (float sample) -> float
    {
        if (bipolar)
            return 0.5f - 0.5f * (sample / voltageScale);
        return 1.0f - sample / voltageScale;
    };

    currentWaveform.startNewSubPath (0.0f, sampleToY (samples[0]));

    float lastSample = samples[0];

    for (int i = 0; i < samples.size(); i++)
    {
        if (samples[i] != lastSample)
        {
            currentWaveform.lineTo ((float) i / numSamples, sampleToY (lastSample));
            currentWaveform.lineTo ((float) i / numSamples, sampleToY (samples[i]));
            lastSample = samples[i];
        }
    }

    currentWaveform.lineTo (1.0f, sampleToY (lastSample));

    auto plotArea = getLocalBounds().toFloat().reduced (4.0f).withTrimmedRight (28.0f).withTrimmedBottom (14.0f);
    pathTransform = currentWaveform.getTransformToScaleToFit (plotArea, false);
}

void WaveformView::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto plotArea = bounds.reduced (4.0f).withTrimmedRight (28.0f).withTrimmedBottom (14.0f);

    g.setColour (findColour (ThemeColours::widgetBackground).darker (0.2f));
    g.fillRoundedRectangle (plotArea, 2.0f);

    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.25f));
    g.drawRoundedRectangle (plotArea, 2.0f, 1.0f);

    for (int i = 1; i < 4; i++)
    {
        const float y = plotArea.getY() + plotArea.getHeight() * i / 4.0f;
        g.drawLine (plotArea.getX(), y, plotArea.getRight(), y, 0.5f);
    }

    g.setColour (Colours::cornflowerblue);
    g.strokePath (currentWaveform, PathStrokeType (1.5f), pathTransform);

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (10.0f);

    const String maxLabel = String (maxVoltage, maxVoltage == (float) (int) maxVoltage ? 0 : 1) + " V";
    const String minLabel = (waveformType == WaveformType::sine)
                                ? String (-maxVoltage, maxVoltage == (float) (int) maxVoltage ? 0 : 1) + " V"
                                : "0 V";

    g.drawText (maxLabel, (int) plotArea.getRight() + 2, (int) plotArea.getY(), 26, 12, Justification::centredLeft);
    g.drawText (minLabel, (int) plotArea.getRight() + 2, (int) plotArea.getBottom() - 12, 26, 12, Justification::centredLeft);

    g.drawText ("0 ms", (int) plotArea.getX(), (int) plotArea.getBottom() + 1, 40, 12, Justification::centredLeft);
    g.drawText (String ((int) durationMs) + " ms",
                (int) plotArea.getRight() - 50,
                (int) plotArea.getBottom() + 1,
                50,
                12,
                Justification::centredRight);
}

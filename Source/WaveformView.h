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

#ifndef __WAVEFORMVIEW_H__
#define __WAVEFORMVIEW_H__

#include <EditorHeaders.h>
#include "AnalogPatternGenerator.h"

/**
    Waveform preview adapted from wave-player WavePlayerBackground.
*/
class WaveformView : public Component,
                     public Button::Listener
{
public:
    WaveformView();
    ~WaveformView() override = default;

    void paint (Graphics& g) override;
    void resized() override;
    void buttonClicked (Button* button) override;

    void updateFromPattern (Pattern* pattern);
    void setPlayButtonVisible (bool shouldBeVisible);

    const Array<float>& getSamples() const { return samples; }

    std::function<void()> onPlayRequested;

private:
    void rebuildPath();

    PatternType patternType = PatternType::pulse;
    float maxVoltage = 5.0f;
    float durationMs = 100.0f;
    float sampleRate = 30000.0f;

    Array<float> samples;
    Path currentWaveform;
    AffineTransform pathTransform;

    std::unique_ptr<UtilityButton> playButton;
};

#endif // __WAVEFORMVIEW_H__

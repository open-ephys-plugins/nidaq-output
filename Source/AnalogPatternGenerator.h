/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

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

#ifndef __ANALOGPATTERNGENERATOR_H__
#define __ANALOGPATTERNGENERATOR_H__

#include <EditorHeaders.h>

/** Host interface used by pattern generators (sample rate + preview refresh) */
class AnalogPatternHost
{
public:
    virtual ~AnalogPatternHost() = default;
    virtual float getSampleRate() = 0;
    virtual void updateWaveform() = 0;
};

enum class PatternType
{
    pulse = 0,
    sine = 1,
    custom = 2
};

struct Pattern
{
    Array<float> samples;
    PatternType patternType = PatternType::pulse;
    float sampleRate = 30000.0f;
    float maxVoltage = 5.0f;

    int id = 1;
    String name = "PATTERN 1";

    int triggerChannel = 0;
    int gateChannel = -1;
    int analogOutputChannel = 0;

    struct Pulse
    {
        int onDuration = 100;
        int offDuration = 100;
        int delayDuration = 0;
        int repeatNumber = 1;
        int rampOnDuration = 0;
        int rampOffDuration = 0;
        float maxVoltage = 5.0f;
    };

    Pulse pulse;

    struct Sine
    {
        int frequency = 5;
        int cycles = 1;
        int delayDuration = 0;
        float maxVoltage = 5.0f;
    };

    Sine sine;

    struct Custom
    {
        Array<float> samples = { 0, 0, 0 };
        String string = "0,0,0";
    };

    Custom custom;
};

class AnalogPatternGenerator : public Component
{
public:
    AnalogPatternGenerator (AnalogPatternHost* host, Pattern* pattern);
    ~AnalogPatternGenerator() override;

    bool editable = true;

    virtual void updatePattern() = 0;
    virtual void buildWaveform() = 0;
    virtual void setState (Pattern* p) = 0;

    virtual void setReadOnly (bool shouldBeReadOnly);

protected:
    AnalogPatternHost* host;
    Pattern* pattern;
};

template <typename T>
class EditableTextInput : public Component, public Label::Listener
{
public:
    EditableTextInput<T> (String mainlabelText,
                          String unitsLabelText,
                          T minValue,
                          T maxValue,
                          T defaultValue,
                          AnalogPatternGenerator* apg);

    void labelTextChanged (Label* label);

    ~EditableTextInput();

    T getValue();
    void setValue (T);
    void setReadOnly (bool shouldBeReadOnly);

    AnalogPatternGenerator* apg;

private:
    std::unique_ptr<Label> mainLabel;
    std::unique_ptr<Label> unitsLabel;
    std::unique_ptr<Label> inputBox;

    T minValue;
    T maxValue;
    T defaultValue;
    T lastValue;
};

class PulsePatternGenerator : public AnalogPatternGenerator
{
public:
    PulsePatternGenerator (AnalogPatternHost* host, Pattern* pattern);
    ~PulsePatternGenerator() override;

    void updatePattern() override;
    void setState (Pattern* p) override;
    void buildWaveform() override;
    void setReadOnly (bool shouldBeReadOnly) override;

private:
    std::unique_ptr<EditableTextInput<int>> onDuration;
    std::unique_ptr<EditableTextInput<int>> offDuration;
    std::unique_ptr<EditableTextInput<int>> delayDuration;
    std::unique_ptr<EditableTextInput<int>> repeatNumber;
    std::unique_ptr<EditableTextInput<int>> rampOnDuration;
    std::unique_ptr<EditableTextInput<int>> rampOffDuration;
    std::unique_ptr<EditableTextInput<float>> maxVoltage;
};

class SinePatternGenerator : public AnalogPatternGenerator
{
public:
    SinePatternGenerator (AnalogPatternHost* host, Pattern* pattern);
    ~SinePatternGenerator() override;

    void updatePattern() override;
    void setState (Pattern* p) override;
    void buildWaveform() override;
    void setReadOnly (bool shouldBeReadOnly) override;

private:
    std::unique_ptr<EditableTextInput<int>> frequency;
    std::unique_ptr<EditableTextInput<int>> cycles;
    std::unique_ptr<EditableTextInput<int>> delay;
    std::unique_ptr<EditableTextInput<float>> maxVoltage;
};

class CustomPatternGenerator : public AnalogPatternGenerator,
                               public TextEditor::Listener
{
public:
    CustomPatternGenerator (AnalogPatternHost* host, Pattern* pattern);
    ~CustomPatternGenerator() override;

    void updatePattern() override;
    void setState (Pattern* p) override;
    void buildWaveform() override;
    void setReadOnly (bool shouldBeReadOnly) override;

    void textEditorReturnKeyPressed (TextEditor&) override;

private:
    std::unique_ptr<TextEditor> textEditor;
    std::unique_ptr<Label> mainLabel;

    float constrainVoltage (float input, float min = -5, float max = 5);

    float maxVoltage;
};

#endif

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

#ifndef __NIDAQOUTPUTEDITOR_H_28EB4CC9__
#define __NIDAQOUTPUTEDITOR_H_28EB4CC9__


#include <EditorHeaders.h>
#include "NIDAQOutput.h"
#include "WaveformView.h"
#include "AnalogPatternGenerator.h"
#include <SerialLib.h>

class NIDAQOutputEditor;

/**

  User interface for the NIDAQOutput processor.

  @see NIDAQOutput

*/

class PopupConfigurationWindow : public Component, public ComboBox::Listener, public Button::Listener
{

public:
    
    /** Constructor */
    PopupConfigurationWindow(NIDAQOutputEditor* editor);

    /** Destructor */
    ~PopupConfigurationWindow() { }

	void comboBoxChanged(ComboBox*);
	void buttonClicked(Button* button) override;

	void paint(Graphics& g) override;

private:

	NIDAQOutputEditor* editor;

    ScopedPointer<Label>  analogLabel;
    ScopedPointer<ComboBox> analogChannelCountSelect;

	ScopedPointer<Label>  digitalLabel;
	ScopedPointer<ComboBox> digitalChannelCountSelect;

	ScopedPointer<Label>  digitalWriteLabel;
	ScopedPointer<ComboBox> digitalWriteSelect;

	ScopedPointer<Label> digitalPortsLabel;
	ScopedPointer<ComboBox> digitalPortsSelect;

	OwnedArray<ToggleButton> digitalPortButtons;

};

class NIDAQOutputEditor : public GenericEditor,
                            public Button::Listener,
                            public ComboBox::Listener,
                            public AnalogPatternHost
{
public:
    /** Constructor*/
    NIDAQOutputEditor(GenericProcessor* parentNode);

    /** Destructor*/
    ~NIDAQOutputEditor() { }

    /** Called when device configuration is changed */
    void update(int analogCount, int digitalCount, int digitalWrite);

    /** Respond to button presses */
	void buttonClicked(Button* button) override;

    /** Called when a button gets pressed */
    void buttonEvent(Button* button);

    /** ComboBox callback */
    void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;

    /** AnalogPatternHost */
    float getSampleRate() override;
    void updateWaveform() override;

    /** Gets the latest device from the processor*/
    void updateDevice(String deviceName);

    /** Sync Play button visibility with trigger mode */
    void updateTriggerUI();

    /** Sync Trigger / Input / Play visibility with Config mode */
    void updateConfigUI();

    /** Refresh waveform preview / sample rate */
    void refreshWaveformView();

    int getTotalAvailableAnalogOutputs() { return processor->getTotalAvailableAnalogOutputs(); };
	int getTotalAvailableDigitalOutputs() { return processor-> getTotalAvailableDigitalOutputs(); };

	int getNumActiveAnalogOutputs() { return processor->getNumActiveAnalogOutputs(); };
	int getNumActiveDigitalOutputs() { return processor->getNumActiveDigitalOutputs(); };

	int getDigitalWriteSize() { return processor->getDigitalWriteSize(); };

	int getNumPorts() { return processor->getNumPorts(); };
	bool getPortState(int idx) { return processor->getPortState(idx); };
	void setPortState(int idx, bool state) { processor->setPortState(idx, state); };

	void saveCustomParametersToXml(XmlElement*) override;
	void loadCustomParametersFromXml(XmlElement*) override;
	
private:

    void openDeviceSettings(Button* button);
    void openWaveformSettings(Button* button);
    void rebuildCurrentPattern();

    NIDAQOutput* processor;

    std::unique_ptr<Pattern> currentPattern;

    std::unique_ptr<Label> waveformLabel;
    std::unique_ptr<ComboBox> waveformSelector;
	std::unique_ptr<UtilityButton> settingsButton;
	std::unique_ptr<UtilityButton> configureDeviceButton;
    std::unique_ptr<WaveformView> waveformView;

	PopupConfigurationWindow* currentConfigWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIDAQOutputEditor);

};

#endif  // __NIDAQOUTPUTEDITOR_H_28EB4CC9__

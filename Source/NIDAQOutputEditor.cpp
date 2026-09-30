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

#include "NIDAQOutputEditor.h"
#include <stdio.h>

EditorBackground::EditorBackground(int settingsOffsetX) : settingsOffsetX(settingsOffsetX) {}

void EditorBackground::paint(Graphics& g)
{
	g.setColour(Colours::darkgrey);
	g.setFont(10);

	g.drawText(String("DEVICE"), settingsOffsetX, 13, 100, 10, Justification::centredLeft);
	g.drawText(String("SAMPLE RATE"), settingsOffsetX, 45, 100, 10, Justification::centredLeft);
	g.drawText(String("AO VOLTAGE RANGE"), settingsOffsetX, 77, 100, 10, Justification::centredLeft);
}

NIDAQOutputEditor::NIDAQOutputEditor(GenericProcessor* parentNode)
    : GenericEditor(parentNode)

{
    draw();
}

void NIDAQOutputEditor::draw()
{
    processor = (NIDAQOutput*)getProcessor();

	int xOffset = 15;

	deviceSelectBox = new ComboBox("DeviceSelectBox");
	deviceSelectBox->setBounds(xOffset, 39, 85, 20);
    deviceSelectBox->addListener(this);
    Array<NIDAQDevice*> devices = processor->getDevices();
	for (int i = 0; i < devices.size(); i++)
	{
		deviceSelectBox->addItem(processor->getDeviceSelectLabel(i), i + 1);
	}
    deviceSelectBox->setSelectedId(processor->getDeviceIndex() + 1, dontSendNotification);
	deviceSelectBox->setTooltip(processor->getDeviceOutputInfo(processor->getDeviceName()).getSummary());
    addAndMakeVisible(deviceSelectBox.get());

	if (processor->getDevices().size() == 1)	// disable device selection if only one device is available
		deviceSelectBox->setEnabled(false);

	sampleRateSelectBox = new ComboBox("SampleRateSelectBox");
	sampleRateSelectBox->setBounds(xOffset, 70, 85, 20);
	Array<NIDAQ::float64> sampleRates = processor->getSampleRates();
	for (int i = 0; i < sampleRates.size(); i++)
	{
		sampleRateSelectBox->addItem(String(sampleRates[i]) + " S/s", i + 1);
	}
	sampleRateSelectBox->setSelectedItemIndex(processor->getSampleRateIndex(), false);
	sampleRateSelectBox->addListener(this);
	addAndMakeVisible(sampleRateSelectBox);

	voltageRangeSelectBox = new ComboBox("VoltageRangeSelectBox");
	voltageRangeSelectBox->setBounds(xOffset, 101, 85, 20);
	Array<SettingsRange> voltageRanges = processor->getVoltageRanges();
	for (int i = 0; i < voltageRanges.size(); i++)
	{
		String rangeString = String(voltageRanges[i].min) + " - " + String(voltageRanges[i].max) + " V";
		voltageRangeSelectBox->addItem(rangeString, i + 1);
	}
	voltageRangeSelectBox->setSelectedItemIndex(processor->getVoltageRangeIndex(), false);
	voltageRangeSelectBox->addListener(this);
	addAndMakeVisible(voltageRangeSelectBox);

	configureDeviceButton = new UtilityButton("...");
	configureDeviceButton->setBounds(xOffset + 60, 25, 24, 12);
	configureDeviceButton->addListener(this);
	configureDeviceButton->setAlpha(0.5f);
	addAndMakeVisible(configureDeviceButton);
	
	desiredWidth = xOffset + 100;

	background = new EditorBackground(xOffset);
	background->setBounds(0, 15, 1000, 150);
	addAndMakeVisible(background);
	background->toBack();
	background->repaint();

}

void NIDAQOutputEditor::update(int numAnalog, int numDigital, int digitalWriteSize)
{

	if (numAnalog != processor->getNumActiveAnalogOutputs())
	{
		processor->setNumActiveAnalogChannels(numAnalog);
		processor->updateAnalogChannels();

		((CallOutBox*)currentConfigWindow->getParentComponent())->dismiss();
	}

	if (numDigital != processor->getNumActiveDigitalOutputs())
	{
		processor->setNumActiveDigitalChannels(numDigital);
		processor->updateDigitalChannels();

		((CallOutBox*)currentConfigWindow->getParentComponent())->dismiss();
	}

	if (digitalWriteSize != processor->getDigitalWriteSize())
	{
		processor->setDigitalWriteSize(digitalWriteSize);
	}

	draw();

	CoreServices::updateSignalChain(this);

}

void NIDAQOutputEditor::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == deviceSelectBox)
    {
        int deviceIndex = deviceSelectBox->getSelectedItemIndex();
        const String deviceName = processor->getDevices()[deviceIndex]->getName();
        processor->setDevice(deviceName);
		const DeviceOutputInfo& outputs = processor->getDeviceOutputInfo(deviceName);
		deviceSelectBox->setTooltip(outputs.getSummary());
		LOGC("Selected device ", deviceName, ": ", outputs.getSummary());
        CoreServices::updateSignalChain(this);
    }
    else if (comboBoxThatHasChanged == sampleRateSelectBox)
    {
        const int sampleRateIndex = sampleRateSelectBox->getSelectedItemIndex();
        LOGD("NIDAQOutputEditor::comboBoxChanged sample-rate dropdown selected index=",
             sampleRateIndex,
             ", text=", sampleRateSelectBox->getText());
        processor->setSampleRate(sampleRateIndex);
        LOGC("Selected analog output sample rate: ", processor->getAnalogOutputSampleRate(), " Hz");
    }
    else if (comboBoxThatHasChanged == voltageRangeSelectBox)
    {
        processor->setVoltageRange(voltageRangeSelectBox->getSelectedItemIndex());
    }
}

void NIDAQOutputEditor::buttonClicked(Button* button)
{
	buttonEvent(button);
}

void NIDAQOutputEditor::buttonEvent(Button* button)
{
	if (button == configureDeviceButton)
	{
		if (!CoreServices::getAcquisitionStatus())
        {
            currentConfigWindow = new PopupConfigurationWindow(this);
            CallOutBox& myBox
                = CallOutBox::launchAsynchronously(std::unique_ptr<Component>(currentConfigWindow),
                    button->getScreenBounds(),
                    nullptr);

            myBox.setDismissalMouseClicksAreAlwaysConsumed(true);

            return;

        }
	}
}

void NIDAQOutputEditor::updateDevice(String deviceName)
{
	juce::ignoreUnused(deviceName);
	if (deviceSelectBox != nullptr && processor != nullptr)
	{
		const int idx = processor->getDeviceIndex();
		if (idx >= 0 && idx < deviceSelectBox->getNumItems())
			deviceSelectBox->setSelectedId(idx + 1, dontSendNotification);
	}

	draw();
}

PopupConfigurationWindow::PopupConfigurationWindow(NIDAQOutputEditor* editor_)
    : editor(editor_)
{
    //tableHeader.reset(new TableHeaderComponent());

	analogLabel = new Label ("Analog", "Analog Outputs: ");
	analogLabel->setFont(Font(16.0f, Font::bold));
	analogLabel->setColour(Label::textColourId, Colours::white);
    analogLabel->setBounds (2, 8, 110, 20);
    addAndMakeVisible(analogLabel);

	int activeAnalogCount = editor->getNumActiveAnalogOutputs();
    analogChannelCountSelect = new ComboBox ("Analog Count Selector");
	for (int i = 0; i <= editor->getTotalAvailableAnalogOutputs(); i++)
	{
		analogChannelCountSelect->addItem(String(i), i + 1);
		if (i == activeAnalogCount)
			analogChannelCountSelect->setSelectedId(i + 1, dontSendNotification);
	}
    analogChannelCountSelect->setBounds (115, 8, 60, 20);
    analogChannelCountSelect->addListener (this);
    addAndMakeVisible (analogChannelCountSelect);

	digitalLabel = new Label ("Digital", "Digital Outputs: ");
	digitalLabel->setColour(Label::textColourId, Colours::white);
    digitalLabel->setBounds (2, 33, 110, 20);
    addAndMakeVisible(digitalLabel);

	int activeDigitalCount = editor->getNumActiveDigitalOutputs();
    digitalChannelCountSelect = new ComboBox ("Digital Count Selector");
	for (int i = 0; i <= editor->getTotalAvailableDigitalOutputs(); i+=4)
	{
		digitalChannelCountSelect->addItem(String(i), i / 4 + 1);
		if (i == activeDigitalCount)
			digitalChannelCountSelect->setSelectedId(i / 4 + 1, dontSendNotification);
	}
    digitalChannelCountSelect->setBounds (115, 33, 60, 20);
    digitalChannelCountSelect->addListener (this);
    addAndMakeVisible (digitalChannelCountSelect);

	digitalWriteLabel = new Label ("Digital Write", "Digital Write: ");
	digitalWriteLabel->setColour(Label::textColourId, Colours::white);
	digitalWriteLabel->setBounds (2, 58, 110, 20);
	addAndMakeVisible(digitalWriteLabel);

	digitalWriteSelect = new ComboBox("Digital Write Selector");
	Array<int> digitalWriteOptions = { 8, 16, 32 };
	for (int i = 0; i < digitalWriteOptions.size(); i++)
	{
		digitalWriteSelect->addItem(String(digitalWriteOptions[i]) + " bits", i + 1);
		if (digitalWriteOptions[i] == editor->getDigitalWriteSize())
			digitalWriteSelect->setSelectedId(i + 1, dontSendNotification);
	}
	digitalWriteSelect->setBounds(115, 58, 60, 20);
	digitalWriteSelect->addListener(this);
	addAndMakeVisible(digitalWriteSelect);

	for (int i = 0; i < editor->getNumPorts(); i++)
	{
		ToggleButton* button = new ToggleButton("P"+String(i));
		button->setBounds(i * 60 + 5, 85, 58, 20);
		button->addListener(this);
		button->setToggleState(editor->getPortState(i), juce::dontSendNotification);
		addAndMakeVisible(button);
		digitalPortButtons.add(button);
	}

	setSize(180, 110);

}

void PopupConfigurationWindow::comboBoxChanged(ComboBox* comboBox)
{
	int numAnalogOutputs = int(analogChannelCountSelect->getItemText(analogChannelCountSelect->getSelectedId() - 1).getFloatValue());
	int numDigitalOutputs = int(digitalChannelCountSelect->getItemText(digitalChannelCountSelect->getSelectedId() - 1).getFloatValue());
	int digitalWrite = int(digitalWriteSelect->getItemText(digitalWriteSelect->getSelectedId() - 1).getFloatValue());

	editor->update(numAnalogOutputs, numDigitalOutputs, digitalWrite);
}

void PopupConfigurationWindow::paint(juce::Graphics& g)
{
	// Set the background color of toggle buttons based on their state
	for (int i = 0; i < digitalPortButtons.size(); ++i)
	{
		ToggleButton* button = digitalPortButtons[i];
		g.setColour(button->getToggleState() ? juce::Colours::green : juce::Colours::red);
		g.fillRect(button->getBounds());
	}
}

void PopupConfigurationWindow::buttonClicked(juce::Button* button)
{
	int portIdx = button->getName().getLastCharacter()-'0';
	editor->setPortState(portIdx, button->getToggleState());
	repaint();
}

void NIDAQOutputEditor::saveCustomParametersToXml(XmlElement* xml)
{
    xml->setAttribute("device", processor->getDeviceName());
    xml->setAttribute("sampleRate", processor->getAnalogOutputSampleRate());
	xml->setAttribute("voltageRange", processor->getVoltageRangeIndex());

	xml->setAttribute("numAnalog", getNumActiveAnalogOutputs());
	xml->setAttribute("numDigital", getNumActiveDigitalOutputs());
	xml->setAttribute("digitalWriteSize", getDigitalWriteSize());

	String digitalPortStates = "";
	for (int i = 0; i < getNumPorts(); i++)
		digitalPortStates += getPortState(i) ? "1" : "0";
	xml->setAttribute("digitalPortStates", digitalPortStates);
}

void NIDAQOutputEditor::loadCustomParametersFromXml(XmlElement* xml)
{
	if (xml == nullptr)
		return;

    processor->setDevice(xml->getStringAttribute("device", ""));
	updateDevice(xml->getStringAttribute("device", ""));

    float sampleRate = xml->getStringAttribute("sampleRate", "0.0").getFloatValue();

	// Load sample rate
	if (sampleRate > 0.0f)
	{
		int idx = 0;
		for (auto& sr : processor->getSampleRates())
		{
			if (sr == sampleRate)
			{
				LOGD("Setting saved sample rate: " + String(sampleRate) + " (" + String(idx) + ")");
				sampleRateSelectBox->setSelectedItemIndex(idx, true);
				break;
			}
			idx++;
		}
	}

	// Load voltage range
	int voltageRangeIndex = xml->getStringAttribute("voltageRange", "-1").getIntValue();

	if (voltageRangeIndex >= 0)
	{
		processor->setVoltageRange(voltageRangeIndex);
		voltageRangeSelectBox->setSelectedItemIndex(processor->getVoltageRangeIndex(), false);
	}

	// Load number of active analog channels
	int numAnalog = xml->getStringAttribute("numAnalog", "0").getIntValue();

	if (numAnalog >= 0)
	{
		processor->setNumActiveAnalogChannels(numAnalog);
		processor->updateAnalogChannels();
	}

	// Load number of active digital channels
	int numDigital = xml->getStringAttribute("numDigital", "0").getIntValue();

	if (numDigital >= 0)
	{
		processor->setNumActiveDigitalChannels(numDigital);
		processor->updateDigitalChannels();
	}

	// Load digital read size
	int digitalWriteSize = xml->getStringAttribute("digitalWriteSize", "0").getIntValue();

	if (digitalWriteSize >= 0)
	{
		processor->setDigitalWriteSize(digitalWriteSize);
	}

	String digitalPortStates = xml->getStringAttribute("digitalPortStates", "000");

	const int nPorts = processor->getNumPorts();
	for (int i = 0; i < digitalPortStates.length() && i < nPorts; i++)
		processor->setPortState(i, digitalPortStates[i] == '1');

	draw();

}
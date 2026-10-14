/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2016 Open Ephys

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

#include "NIDAQComponents.h"
#include "NIDAQOutput.h"
#include "NIDAQOutputEditor.h"

namespace
{
int getRequestedAnalogChannel(const var& root)
{
    if (root.hasProperty("pulse"))
        return (int) root["pulse"].getProperty("analogOutputChannel", 0);
    if (root.hasProperty("sine"))
        return (int) root["sine"].getProperty("analogOutputChannel", 0);
    if (root.hasProperty("custom"))
        return (int) root["custom"].getProperty("analogOutputChannel", 0);
    return 0;
}

int millisecondsToSamples(double milliseconds, double sampleRate)
{
    return juce::jmax(0, static_cast<int>((milliseconds * sampleRate / 1000.0) + 0.5));
}

bool isCosineRampProfile(const String& rampProfile)
{
    return rampProfile.equalsIgnoreCase("cosine")
        || rampProfile.equalsIgnoreCase("raisedCosine")
        || rampProfile.equalsIgnoreCase("raised_cosine");
}

float raisedCosineRampOn(float maxVoltage, int sample, int rampSamples)
{
    if (rampSamples <= 0)
        return maxVoltage;

    const double progress = juce::jlimit(0.0, 1.0, static_cast<double>(sample) / rampSamples);
    return 0.5f * maxVoltage * static_cast<float>(1.0 - std::cos(MathConstants<double>::pi * progress));
}

float raisedCosineRampOff(float maxVoltage, int sampleIntoRamp, int rampSamples)
{
    if (rampSamples <= 0)
        return maxVoltage;

    const double progress = juce::jlimit(0.0, 1.0, static_cast<double>(sampleIntoRamp) / rampSamples);
    return 0.5f * maxVoltage * static_cast<float>(1.0 + std::cos(MathConstants<double>::pi * progress));
}
}

NIDAQOutput::NIDAQOutput() : GenericProcessor("NIDAQ Output")
{

    dm = new NIDAQmxDeviceManager();

    dm->scanForDevices();

    LOGD("Num devices found: ", dm->getNumAvailableDevices());

    openConnection();
    
    customWaveform = new CustomWaveform();

}

NIDAQOutput::~NIDAQOutput()
{
	if (mNIDAQ != nullptr)
	{
		mNIDAQ->requestShutdown();
		mNIDAQ->stopThread(2000);
	}
}

void NIDAQOutput::registerParameters()
{
    addCategoricalParameter(
        Parameter::ParameterScope::STREAM_SCOPE, 
        "outputMode", 
        "Output Mode",
        "Analog output mode",
        {"Mirror Input", "Custom Waveform"},
        1);

    Array<String> deviceNames;
    for (int i = 0; i < dm->getNumAvailableDevices(); i++)
        deviceNames.add(getDeviceSelectLabel(i));
    if (deviceNames.isEmpty())
        deviceNames.add("No Device");

    addCategoricalParameter(Parameter::PROCESSOR_SCOPE, "device", "Device",
        "The NIDAQ device to use", deviceNames, deviceIndex, true);

    addCategoricalParameter(Parameter::PROCESSOR_SCOPE, "config", "Config",
        "Output configuration mode", {"Local", "Remote", "Mirror"}, 0, true);

    Array<String> outputChannels;
    const int numAo = mNIDAQ != nullptr ? getTotalAvailableAnalogOutputs() : 0;
    for (int i = 0; i < numAo; i++)
        outputChannels.add("AO" + String(i));
    if (outputChannels.isEmpty())
        outputChannels.add("AO0");

    addCategoricalParameter(Parameter::PROCESSOR_SCOPE, "output", "Output",
        "Analog output channel", outputChannels, 0, true);

    Array<String> triggerOptions {"Manual", "Network"};
    for (int i = 1; i <= 8; i++)
        triggerOptions.add("TTL" + String(i));

    addCategoricalParameter(Parameter::PROCESSOR_SCOPE, "trigger", "Trigger",
        "Trigger source", triggerOptions, 0, true);

    addSelectedChannelsParameter(Parameter::STREAM_SCOPE, "input", "Input",
        "Continuous channel to mirror", 1);
}

String NIDAQOutput::handleConfigMessage(const String& message)
{
    LOGC("Got config message: ", message);
    outputMode = CUSTOM_WAVEFORM;

    bool playImmediately = false;
    var root;
    if (JSON::parse(message, root).wasOk())
    {
        playImmediately = static_cast<bool>(root.getProperty("playImmediately", var(false)));
        const int requestedChannel = getRequestedAnalogChannel(root);
        const int availableChannels = juce::jmax(1, (int) mNIDAQ->device->numAOChannels);
        if (requestedChannel >= availableChannels)
        {
            MessageManager::callAsync([requestedChannel, availableChannels]() {
                AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                                                 "NIDAQ Output",
                                                 "Requested analog output channel AO" + String(requestedChannel)
                                                     + ", but this device has only " + String(availableChannels)
                                                     + " analog output channel(s).");
            });
            LOGE("Requested AO channel ", requestedChannel, " exceeds available channels: ", availableChannels);
            outputEnabled = false;
            return message;
        }
    }

    double sampleRate = mNIDAQ->getSampleRate();
    LOGD("NIDAQOutput::handleConfigMessage parsing custom waveform with AO sampleRate=", sampleRate);

    if (customWaveform->parseProtocol(message, sampleRate))
    {
        LOGC("Successfully parsed protocol and generated waveform at ", sampleRate, " Hz");
        // Update NIDAQ channel count to match waveform
        int numWaveformChannels = customWaveform->getNumChannels();
        const int maxAoChannels = juce::jmax(1, (int) mNIDAQ->device->numAOChannels);
        if (numWaveformChannels > maxAoChannels)
        {
            LOGC("Waveform requested ", numWaveformChannels,
                 " AO channels but device supports ", maxAoChannels,
                 ". Clamping to available channels.");
            numWaveformChannels = maxAoChannels;
        }
        else if (numWaveformChannels <= 0)
        {
            numWaveformChannels = 1;
        }
        juce::ignoreUnused(numWaveformChannels);
        playCustomWaveform();
        LOGC("Output enabled from received waveform config", playImmediately ? " (playImmediately=true)" : "");
        LOGC("Total waveform samples: ", customWaveform->getTotalSamples(),
             " (duration: ", customWaveform->getTotalSamples() / mNIDAQ->getSampleRate(), " seconds)");
        LOGC("Looping: ", customWaveform->isLooping() ? "enabled" : "disabled");
    }
    else
    {
        LOGE("Failed to parse protocol");
    }

    return message;
}

void NIDAQOutput::handleBroadcastMessage(const String& msg, const int64 messageTimeMilliseconds)
{
    LOGC("Got broadcast message: ", msg, " at time: ", messageTimeMilliseconds);
    // Assume message is a flag to trigger the custom_waveform to start
    if (msg == "enable_output")
    {
        playCustomWaveform();
        LOGC("Output enabled, waveform reset to start");
        LOGC("Total waveform samples: ", customWaveform->getTotalSamples(), 
             " (duration: ", customWaveform->getTotalSamples() / mNIDAQ->getSampleRate(), " seconds)");
        LOGC("Looping: ", customWaveform->isLooping() ? "enabled" : "disabled");
    }
}

void NIDAQOutput::playCustomWaveform()
{
    if (customWaveform != nullptr && customWaveform->isValid())
    {
        customWaveform->reset();
        outputEnabled = true;
        mNIDAQ->writeAnalogWaveform(customWaveform->getBuffer());
    }
    else
    {
        LOGE("Cannot play custom waveform: waveform is not valid");
        outputEnabled = false;
    }
}

void NIDAQOutput::playWaveformSamples(const Array<float>& samples)
{
    if (samples.isEmpty() || mNIDAQ == nullptr)
    {
        LOGE("Cannot play waveform: no samples or device unavailable");
        outputEnabled = false;
        return;
    }

    int outputChannel = 0;
    if (auto* outputParam = dynamic_cast<CategoricalParameter*>(getParameter("output")))
        outputChannel = outputParam->getSelectedIndex();

    AudioBuffer<float> buffer(1, samples.size());
    for (int i = 0; i < samples.size(); ++i)
        buffer.setSample(0, i, samples[i]);

    outputEnabled = true;
    mNIDAQ->writeAnalogWaveform(buffer, outputChannel);
}

void NIDAQOutput::setTriggeredWaveformSamples(const Array<float>& samples)
{
    const ScopedLock lock(waveformLock);
    triggeredWaveformSamples = samples;
}

void NIDAQOutput::playTriggeredWaveform()
{
    Array<float> samples;
    {
        const ScopedLock lock(waveformLock);
        samples = triggeredWaveformSamples;
    }
    playWaveformSamples(samples);
}

int NIDAQOutput::getSelectedTtlTriggerLine() const
{
    auto* triggerParam = dynamic_cast<CategoricalParameter*>(getParameter("trigger"));
    if (triggerParam == nullptr)
        return -1;

    const String trigger = triggerParam->getSelectedString();
    if (!trigger.startsWithIgnoreCase("TTL"))
        return -1;

    const int line = trigger.fromFirstOccurrenceOf("TTL", false, false).getIntValue();
    return line >= 1 ? line : -1;
}

String NIDAQOutput::getConfigMode() const
{
    auto* configParam = dynamic_cast<CategoricalParameter*>(getParameter("config"));
    return configParam != nullptr ? configParam->getSelectedString() : "Local";
}

void NIDAQOutput::applyConfigMode(const String& mode)
{
    if (mode.equalsIgnoreCase("Mirror"))
        outputMode = MIRROR_INPUT;
    else
        outputMode = CUSTOM_WAVEFORM;

    getParameter("output")->setEnabled(!mode.equalsIgnoreCase("Remote"));

    if (auto* editor = dynamic_cast<NIDAQOutputEditor*>(getEditor()))
        editor->updateConfigUI();
}

int NIDAQOutput::getSelectedMirrorInputChannel() const
{
    auto* ed = dynamic_cast<GenericEditor*>(getEditor());
    if (ed == nullptr)
        return -1;

    const uint16 streamId = ed->getCurrentStream();
    DataStream* stream = getDataStream(streamId);
    if (stream == nullptr)
        return -1;

    auto* inputParam = dynamic_cast<SelectedChannelsParameter*>(stream->getParameter("input"));
    if (inputParam == nullptr)
        return -1;

    Array<var>* selected = inputParam->getValue().getArray();
    if (selected == nullptr || selected->isEmpty())
        return -1;

    const int localIndex = int(selected->getFirst());
    const auto& channels = stream->getContinuousChannels();
    if (localIndex < 0 || localIndex >= channels.size())
        return -1;

    return channels[localIndex]->getGlobalIndex();
}

void NIDAQOutput::parameterValueChanged(Parameter* parameter)
{
    if (parameter->getName() == "device")
    {
        const int idx = (int) parameter->getValue();
        Array<NIDAQDevice*> devices = getDevices();
        if (idx >= 0 && idx < devices.size())
        {
            setDevice(devices[idx]->getName());

            Array<String> outputChannels;
            for (int i = 0; i < getTotalAvailableAnalogOutputs(); i++)
                outputChannels.add("AO" + String(i));
            if (outputChannels.isEmpty())
                outputChannels.add("AO0");

            auto* outputParam = (CategoricalParameter*) getParameter("output");
            outputParam->setCategories(outputChannels);

            if (auto* editor = dynamic_cast<NIDAQOutputEditor*>(getEditor()))
            {
                if (auto* pe = editor->getParameterEditor("output"))
                    pe->updateView();
                editor->refreshWaveformView();
            }

            const DeviceOutputInfo& outputs = getDeviceOutputInfo(devices[idx]->getName());
            LOGC("Selected device ", devices[idx]->getName(), ": ", outputs.getSummary());
            CoreServices::updateSignalChain(this);
        }
    }
    else if (parameter->getName() == "config")
    {
        applyConfigMode(((CategoricalParameter*) parameter)->getSelectedString());
    }
    else if (parameter->getName() == "trigger")
    {
        if (auto* editor = dynamic_cast<NIDAQOutputEditor*>(getEditor()))
            editor->updateTriggerUI();
    }
    else if (parameter->getName() == "outputMode")
    {
        // Legacy STREAM parameter; Config is the driver. Keep in sync if still used.
        const String mode = parameter->getValue().toString();
        if (mode.equalsIgnoreCase("MIRROR_INPUT") || mode.equalsIgnoreCase("Mirror Input"))
            outputMode = MIRROR_INPUT;
        else if (mode.equalsIgnoreCase("CUSTOM_WAVEFORM") || mode.equalsIgnoreCase("Custom Waveform"))
            outputMode = CUSTOM_WAVEFORM;
    }
}

AudioProcessorEditor* NIDAQOutput::createEditor()
{
    editor = std::make_unique<NIDAQOutputEditor>(this);
    return editor.get();
}

Array<NIDAQDevice*> NIDAQOutput::getDevices()
{
    Array<NIDAQDevice*> deviceList;

    for (int i = 0; i < dm->getNumAvailableDevices(); i++)
        deviceList.add(dm->getDeviceAtIndex(i));

    return deviceList;
}

const DeviceOutputInfo& NIDAQOutput::getDeviceOutputInfo(const String& deviceName)
{
    return dm->getDeviceOutputInfo(deviceName);
}

String NIDAQOutput::getDeviceSelectLabel(int index)
{
    if (index < 0 || index >= dm->getNumAvailableDevices())
        return {};

    NIDAQDevice* device = dm->getDeviceAtIndex(index);
    return dm->getDeviceSelectLabel(device->getName(), device->productName);
}

void NIDAQOutput::setDevice(String name)
{
    for (int i = 0; i < dm->getNumAvailableDevices(); i++)
    {
        if (dm->getDeviceAtIndex(i)->getName() == name)
        {
            deviceIndex = i;
            openConnection();
            break;
        }
    }
}

int NIDAQOutput::openConnection()
{
	if (mNIDAQ != nullptr)
	{
		mNIDAQ->requestShutdown();
		mNIDAQ->clearTasks();
		mNIDAQ->stopThread(2000);
	}

    mNIDAQ = new NIDAQmx(dm->getDeviceAtIndex(deviceIndex));

    sampleRateIndex = juce::jmax(0, mNIDAQ->sampleRates.size() - 1);
    setSampleRate(sampleRateIndex);

    voltageRangeIndex = juce::jmax(0, mNIDAQ->device->voltageRanges.size() - 1);
    setVoltageRange(voltageRangeIndex);

    return 0;

}

void NIDAQOutput::setSampleRate(int rateIndex)
{
    const int previousIndex = sampleRateIndex;
    const double previousRate = mNIDAQ->getSampleRate();
    sampleRateIndex = rateIndex;
    mNIDAQ->setSampleRate(rateIndex);
    sampleRateIndex = mNIDAQ->sampleRates.indexOf(mNIDAQ->getSampleRate());
    LOGD("NIDAQOutput::setSampleRate requested index=", rateIndex,
         ", previous index=", previousIndex,
         ", previous AO rate=", previousRate,
         ", selected index=", sampleRateIndex,
         ", selected AO rate=", mNIDAQ->getSampleRate());
    LOGC("Analog output sample rate set to: ", mNIDAQ->getSampleRate(), " Hz");

    if (customWaveform != nullptr && customWaveform->isValid())
    {
        customWaveform->regenerateWithNewSampleRate(mNIDAQ->getSampleRate());
        LOGC("Regenerated custom waveform at analog output sample rate: ", mNIDAQ->getSampleRate(), " Hz");
    }
}

Array<SettingsRange> NIDAQOutput::getVoltageRanges()
{
    return mNIDAQ->device->voltageRanges;
}

void NIDAQOutput::setVoltageRange(int rangeIndex)
{
    voltageRangeIndex = rangeIndex;
    mNIDAQ->setVoltageRange(rangeIndex);
}

void NIDAQOutput::updateAnalogChannels()
{
    //TODO 
}

void NIDAQOutput::updateDigitalChannels()
{
    //TODO 
}

void NIDAQOutput::updateSettings()
{
    isEnabled = dm->getNumAvailableDevices() > 0;
}

bool NIDAQOutput::startAcquisition()
{
    LOGD("Starting Tasks...");

    applyConfigMode(getConfigMode());
    outputEnabled = (outputMode == MIRROR_INPUT);

    const int maxAoChannels = juce::jmax(1, (int) mNIDAQ->device->numAOChannels);
    mNIDAQ->setNumActiveAnalogOutputs(maxAoChannels);
    
    lastSampleRate = mNIDAQ->getSampleRate();
    mNIDAQ->setAudioSampleRate(AudioProcessor::getSampleRate());
    LOGC("Set analog output sample rate: ", lastSampleRate);
    
    mNIDAQ->startTasks(outputMode == MIRROR_INPUT);
    
    // Load default waveform if none configured
    if (!customWaveform->isValid())
    {
        /*
        For Custom waveforms:
        - pulse_width:          Stretch array across this time (e.g., 0.1 = 100ms)
        - waveform_sample_rate: Each sample = 1/rate seconds (e.g., 40Hz = 25ms per sample)
        - duration:             Number of cycles to repeat the waveform
        
        Default: 4-step ramp at 40Hz, 3 cycles = 300ms total
        */
        String defaultProtocol = R"({
            "name": "Default Custom Waveform",
            "sequences": [{
                "conditions": [{
                    "num_repeats": 1,
                    "stimuli": [{
                        "pulse_shape": "Custom",
                        "waveform_sample_rate": 40.0,
                        "custom_waveform": [100.0, 200.0, 300.0, 400.0],
                        "duration": 3.0
                    }]
                }],
                "min_iti": 0.0
            }]
        })";
        
        if (customWaveform->parseProtocol(defaultProtocol, mNIDAQ->getSampleRate()))
        {
            LOGC("Loaded default custom waveform: 4-step ramp, 3 cycles (300ms total)");
            LOGC("Output sample rate: ", lastSampleRate);
        }
        else
        {
            LOGE("Failed to parse default protocol!");
        }
    }
    
    return true;
}

bool NIDAQOutput::stopAcquisition()
{
    //mNIDAQ->stopThread(5000);
    return true;
}

void NIDAQOutput::process (AudioBuffer<float>& buffer)
{
    /* Check for events */
    checkForEvents();

    // Mirror Input follows the incoming audio stream; custom waveforms are generated at the NI AO sample rate.
    double currentSampleRate = AudioProcessor::getSampleRate();
    if (outputMode == MIRROR_INPUT && currentSampleRate != lastSampleRate && currentSampleRate > 0)
    {
        lastSampleRate = currentSampleRate;
        mNIDAQ->setAudioSampleRate(currentSampleRate);
        LOGC("Sample rate changed to: ", currentSampleRate);
    }

    if (outputMode == MIRROR_INPUT)
    {
        const int inputChannel = getSelectedMirrorInputChannel();
        if (inputChannel < 0 || inputChannel >= buffer.getNumChannels() || mNIDAQ == nullptr)
            return;

        AudioBuffer<float> mirrorBuffer(1, buffer.getNumSamples());
        mirrorBuffer.copyFrom(0, 0, buffer, inputChannel, 0, buffer.getNumSamples());
        mNIDAQ->analogWrite(mirrorBuffer, buffer.getNumSamples());
    }
    else if (outputMode == CUSTOM_WAVEFORM)
    {
        // Custom waveforms are written as finite AO tasks when configured or triggered.
        // Do not stream them from process(), because process() is clocked by the GUI/audio callback.
    }
}

void NIDAQOutput::handleTTLEvent(TTLEventPtr event)
{
    const int eventBit = event->getLine() + 1;

    if (mNIDAQ != nullptr)
    {
        if (mNIDAQ->sendsSynchronizedEvents())
            mNIDAQ->addEvent(event->getSampleNumber(), eventBit, event->getState());
        else
            mNIDAQ->digitalWrite(eventBit, event->getState());
    }

    if (!getConfigMode().equalsIgnoreCase("Local"))
        return;

    const int triggerLine = getSelectedTtlTriggerLine();
    if (triggerLine > 0 && eventBit == triggerLine && event->getState())
    {
        LOGC("TTL", triggerLine, " trigger received; playing waveform");
        playTriggeredWaveform();
    }
}

// ===== CustomWaveform Implementation =====

bool CustomWaveform::parseProtocol(const String& jsonString, double sRate)
{
    sampleRate = sRate;
    currentSample = 0;
    lastProtocolJson = jsonString;
    shouldLoop = false;
    LOGD("CustomWaveform::parseProtocol using sampleRate=", sampleRate);
    
    var root;
    Result result = JSON::parse(jsonString, root);
    
    if (!result.wasOk())
        return false;
    
    // Detect format: wave_player format has pattern fields (pulse, sine, custom)
    if (root.hasProperty("patternType") || root.hasProperty("pulse") || 
        root.hasProperty("sine") || root.hasProperty("custom"))
        return parseWavePlayer(root);
    
    // Original protocol format with sequences
    if (!root.hasProperty("sequences") || !root["sequences"].isArray())
        return false;
    
    const var& sequences = root["sequences"];
    if (sequences.size() == 0)
        return false;
    
    // Calculate total buffer size needed
    int totalSamples = 0;
    int maxChannels = 0;
    
    for (int seqIdx = 0; seqIdx < sequences.size(); seqIdx++)
    {
        const var& sequence = sequences[seqIdx];
        
        if (!sequence.hasProperty("conditions") || !sequence["conditions"].isArray())
            continue;
            
        const var& conditions = sequence["conditions"];
        
        for (int condIdx = 0; condIdx < conditions.size(); condIdx++)
        {
            const var& condition = conditions[condIdx];
            int numRepeats = condition.getProperty("num_repeats", 1);
            
            if (!condition.hasProperty("stimuli") || !condition["stimuli"].isArray())
                continue;
                
            const var& stimuli = condition["stimuli"];
            
            for (int stimIdx = 0; stimIdx < stimuli.size(); stimIdx++)
            {
                const var& stimulus = stimuli[stimIdx];
                String pulseShape = stimulus.getProperty("pulse_shape", "Square").toString();
                double duration = stimulus.getProperty("duration", 0.0);
                double durationSeconds = 0.0;
                
                if (pulseShape == "Custom")
                {
                    // For Custom: duration = number of cycles
                    const var& customWaveformArray = stimulus["custom_waveform"];
                    double pulseWidth = stimulus.getProperty("pulse_width", 0.0);
                    double waveformSampleRate = stimulus.getProperty("waveform_sample_rate", 0.0);
                    
                    double waveformLengthSeconds = 0.0;
                    if (customWaveformArray.isArray() && customWaveformArray.size() > 0)
                    {
                        int numSteps = customWaveformArray.size();
                        if (pulseWidth > 0)
                            waveformLengthSeconds = pulseWidth;
                        else if (waveformSampleRate > 0)
                            waveformLengthSeconds = numSteps / waveformSampleRate;
                        else
                            waveformLengthSeconds = numSteps / sampleRate;
                    }
                    
                    int numCycles = (duration > 0) ? static_cast<int>(duration) : 1;
                    durationSeconds = waveformLengthSeconds * numCycles;
                }
                else
                {
                    // For Square/other: duration = time in seconds
                    if (duration == 0.0)
                    {
                        shouldLoop = true;
                        double pulseFrequency = stimulus.getProperty("pulse_frequency", 1.0);
                        if (pulseFrequency > 0)
                            duration = 1.0 / pulseFrequency;
                        else
                            duration = 1.0;
                    }
                    durationSeconds = duration;
                }
                
                totalSamples += static_cast<int>(durationSeconds * sampleRate) * numRepeats;
                maxChannels = jmax(maxChannels, stimIdx + 1);
            }
        }
        
        double minIti = sequence.getProperty("min_iti", 0.0);
        totalSamples += static_cast<int>(minIti * sampleRate);
    }
    
    numChannels = jmax(maxChannels, 1);
    waveformBuffer.setSize(numChannels, totalSamples);
    waveformBuffer.clear();
    
    // Generate waveforms for each stimulus
    int currentPosition = 0;
    
    for (int seqIdx = 0; seqIdx < sequences.size(); seqIdx++)
    {
        const var& sequence = sequences[seqIdx];
        
        if (!sequence.hasProperty("conditions") || !sequence["conditions"].isArray())
            continue;
            
        const var& conditions = sequence["conditions"];
        
        for (int condIdx = 0; condIdx < conditions.size(); condIdx++)
        {
            const var& condition = conditions[condIdx];
            int numRepeats = condition.getProperty("num_repeats", 1);
            
            if (!condition.hasProperty("stimuli") || !condition["stimuli"].isArray())
                continue;
                
            const var& stimuli = condition["stimuli"];
            
            for (int repeat = 0; repeat < numRepeats; repeat++)
            {
                for (int stimIdx = 0; stimIdx < stimuli.size(); stimIdx++)
                {
                    const var& stimulus = stimuli[stimIdx];
                    String pulseShape = stimulus.getProperty("pulse_shape", "Square").toString();
                    double duration = stimulus.getProperty("duration", 0.0);
                    int durationSamples;
                    
                    if (pulseShape == "Square")
                    {
                        // For Square: duration = time in seconds
                        if (duration == 0.0)
                        {
                            double pulseFrequency = stimulus.getProperty("pulse_frequency", 1.0);
                            if (pulseFrequency > 0)
                                duration = 1.0 / pulseFrequency;
                            else
                                duration = 1.0;
                        }
                        durationSamples = static_cast<int>(duration * sampleRate);
                        
                        double pulseWidth = stimulus.getProperty("pulse_width", 0.01);
                        double pulseFrequency = stimulus.getProperty("pulse_frequency", 0.0);
                        double power = stimulus.getProperty("power", 0.0);
                        
                        int pulseWidthSamples = static_cast<int>(pulseWidth * sampleRate);
                        int pulsePeriodSamples = pulseFrequency > 0 ? static_cast<int>(sampleRate / pulseFrequency) : durationSamples;
                        
                        for (int i = 0; i < durationSamples && (currentPosition + i) < waveformBuffer.getNumSamples(); i++)
                        {
                            int posInPeriod = i % pulsePeriodSamples;
                            float value = (posInPeriod < pulseWidthSamples) ? power : 0.0f;
                            waveformBuffer.setSample(stimIdx, currentPosition + i, value);
                        }
                    }
                    else if (pulseShape == "Custom")
                    {
                        const var& customWaveformArray = stimulus["custom_waveform"];
                        double pulseWidth = stimulus.getProperty("pulse_width", 0.0);
                        double waveformSampleRate = stimulus.getProperty("waveform_sample_rate", 0.0);
                        
                        if (customWaveformArray.isArray() && customWaveformArray.size() > 0)
                        {
                            int numSteps = customWaveformArray.size();
                            int pulseWidthSamples;
                            int samplesPerStep;
                            
                            if (pulseWidth > 0)
                            {
                                // pulse_width mode: stretch waveform across pulse_width seconds
                                pulseWidthSamples = static_cast<int>(pulseWidth * sampleRate);
                                samplesPerStep = pulseWidthSamples / numSteps;
                            }
                            else if (waveformSampleRate > 0)
                            {
                                // waveform_sample_rate mode: each sample = 1/waveformSampleRate seconds
                                samplesPerStep = static_cast<int>(sampleRate / waveformSampleRate);
                                pulseWidthSamples = samplesPerStep * numSteps;
                            }
                            else
                            {
                                // Default: 1:1 mapping (1 sample per output sample)
                                samplesPerStep = 1;
                                pulseWidthSamples = numSteps;
                            }
                            
                            // For Custom: duration = number of cycles
                            int numCycles = (duration > 0) ? static_cast<int>(duration) : 1;
                            durationSamples = pulseWidthSamples * numCycles;
                            
                            // Repeat waveform back-to-back for each cycle
                            int pulsePeriodSamples = pulseWidthSamples;
                            
                            for (int i = 0; i < durationSamples && (currentPosition + i) < waveformBuffer.getNumSamples(); i++)
                            {
                                int posInPeriod = i % pulsePeriodSamples;
                                
                                if (posInPeriod < pulseWidthSamples)
                                {
                                    int stepIndex = posInPeriod / samplesPerStep;
                                    if (stepIndex >= numSteps) stepIndex = numSteps - 1;
                                    
                                    float value = customWaveformArray[stepIndex];
                                    waveformBuffer.setSample(stimIdx, currentPosition + i, value);
                                }
                                else
                                {
                                    waveformBuffer.setSample(stimIdx, currentPosition + i, 0.0f);
                                }
                            }
                        }
                    }
                    
                    currentPosition += durationSamples;
                }
            }
        }
        
        double minIti = sequence.getProperty("min_iti", 0.0);
        currentPosition += static_cast<int>(minIti * sampleRate);
    }
    
    return true;
}

bool CustomWaveform::parseWavePlayer(const var& root)
{
    double maxVoltage = root.getProperty("maxVoltage", 5.0);
    LOGD("CustomWaveform::parseWavePlayer using AO sampleRate=", sampleRate,
         " Hz; WavePlayer duration fields are interpreted as milliseconds.");
    
    // Collect all enabled patterns and determine max channel and total samples
    struct PatternInfo {
        int channel;
        int samples;
    };
    std::vector<PatternInfo> patterns;
    numChannels = 1;
    int totalSamples = 0;
    
    if (root.hasProperty("pulse"))
    {
        const var& pulse = root["pulse"];
        int channel = pulse.getProperty("analogOutputChannel", 0);
        double onDuration = pulse.getProperty("onDuration", 100.0);
        double offDuration = pulse.getProperty("offDuration", 100.0);
        double delayDuration = pulse.getProperty("delayDuration", 0.0);
        int repeatNumber = pulse.getProperty("repeatNumber", 1);
        
        int delayOut = millisecondsToSamples(delayDuration, sampleRate);
        int onOut = millisecondsToSamples(onDuration, sampleRate);
        int offOut = millisecondsToSamples(offDuration, sampleRate);
        int samples = delayOut + repeatNumber * (onOut + offOut);
        LOGD("WavePlayer pulse sizing: channel=", channel,
             ", delayMs=", delayDuration,
             ", onMs=", onDuration,
             ", offMs=", offDuration,
             ", repeatNumber=", repeatNumber,
             ", delaySamples=", delayOut,
             ", onSamples=", onOut,
             ", offSamples=", offOut,
             ", totalSamples=", samples);
        
        patterns.push_back({channel, samples});
        numChannels = jmax(numChannels, channel + 1);
        totalSamples = jmax(totalSamples, samples);
    }
    
    if (root.hasProperty("sine"))
    {
        const var& sine = root["sine"];
        int channel = sine.getProperty("analogOutputChannel", 1);
        double frequency = sine.getProperty("frequency", 5.0);
        int cycles = sine.getProperty("cycles", 1);
        double delayDuration = sine.getProperty("delayDuration", 0.0);
        
        int delayOut = millisecondsToSamples(delayDuration, sampleRate);
        int samplesPerCycle = frequency > 0.0 ? static_cast<int>((sampleRate / frequency) + 0.5) : 0;
        int samples = delayOut + cycles * samplesPerCycle;
        LOGD("WavePlayer sine sizing: channel=", channel,
             ", delayMs=", delayDuration,
             ", frequency=", frequency,
             ", cycles=", cycles,
             ", delaySamples=", delayOut,
             ", samplesPerCycle=", samplesPerCycle,
             ", totalSamples=", samples);
        
        patterns.push_back({channel, samples});
        numChannels = jmax(numChannels, channel + 1);
        totalSamples = jmax(totalSamples, samples);
    }
    
    if (root.hasProperty("custom"))
    {
        const var& custom = root["custom"];
        int channel = custom.getProperty("analogOutputChannel", 0);
        String customString = custom.getProperty("string", "0").toString();
        
        StringArray tokens;
        tokens.addTokens(customString, ",", "");
        int samples = tokens.size();
        
        patterns.push_back({channel, samples});
        numChannels = jmax(numChannels, channel + 1);
        totalSamples = jmax(totalSamples, samples);
    }

    // Keep channel count stable across successive configs (e.g. pulse on ch0 vs sine on ch1)
    // so handleConfigMessage does not call setNumActiveAnalogOutputs + stopThread every trial.
    const int minChannels = (int) root.getProperty("minAnalogChannels", 0);
    if (minChannels > 0)
        numChannels = jmax(numChannels, minChannels);
    
    if (totalSamples == 0)
        return false;
    
    waveformBuffer.setSize(numChannels, totalSamples);
    waveformBuffer.clear();
    
    LOGD("parseWavePlayer: numChannels=", numChannels,
         ", totalSamples=", totalSamples,
         ", sampleRate=", sampleRate,
         ", durationSeconds=", totalSamples / sampleRate);
    
    // Generate pulse waveform
    if (root.hasProperty("pulse"))
    {
        const var& pulse = root["pulse"];
        int channel = pulse.getProperty("analogOutputChannel", 0);
        double onDuration = pulse.getProperty("onDuration", 100.0);
        double offDuration = pulse.getProperty("offDuration", 100.0);
        double delayDuration = pulse.getProperty("delayDuration", 0.0);
        int repeatNumber = pulse.getProperty("repeatNumber", 1);
        double rampOnDuration = pulse.getProperty("rampOnDuration", 0.0);
        double rampOffDuration = pulse.getProperty("rampOffDuration", 0.0);
        float pulseVoltage = pulse.getProperty("maxVoltage", maxVoltage);
		String rampProfile = pulse.getProperty("rampProfile", "linear").toString();
        
        int delayOut = millisecondsToSamples(delayDuration, sampleRate);
        int onOut = millisecondsToSamples(onDuration, sampleRate);
        int offOut = millisecondsToSamples(offDuration, sampleRate);
        int rampOnOut = millisecondsToSamples(rampOnDuration, sampleRate);
        int rampOffOut = millisecondsToSamples(rampOffDuration, sampleRate);
        const bool useCosineRamp = isCosineRampProfile(rampProfile);
        
        LOGD("Generating WavePlayer pulse: channel=", channel,
             ", delayMs=", delayDuration,
             ", onMs=", onDuration,
             ", offMs=", offDuration,
             ", rampOnMs=", rampOnDuration,
             ", rampOffMs=", rampOffDuration,
             ", delaySamples=", delayOut,
             ", onSamples=", onOut,
             ", offSamples=", offOut,
             ", rampOnSamples=", rampOnOut,
             ", rampOffSamples=", rampOffOut,
             ", repeatNumber=", repeatNumber,
			 ", rampProfile=", rampProfile,
             ", useCosineRamp=", useCosineRamp ? "true" : "false",
             ", sampleRate=", sampleRate);
        
        int pos = 0;
        for (int i = 0; i < delayOut; i++)
            waveformBuffer.setSample(channel, pos++, 0.0f);

        for (int rep = 0; rep < repeatNumber; rep++)
        {

            float lastVoltage = 0.0f;

            for (int i = 0; i < onOut; i++)
            {
                float value = pulseVoltage;
                if (rampOnOut > 0 && i < rampOnOut)
                    value = useCosineRamp
                        ? raisedCosineRampOn(pulseVoltage, i, rampOnOut)
                        : pulseVoltage * (float(i) / rampOnOut);
                if (rampOffOut > 0 && i >= (onOut - rampOffOut))
                {
                    const int sampleIntoRamp = i - (onOut - rampOffOut);
                    value = useCosineRamp
                        ? raisedCosineRampOff(pulseVoltage, sampleIntoRamp, rampOffOut)
                        : pulseVoltage * (float(onOut - i) / rampOffOut);
                }
                waveformBuffer.setSample(channel, pos++, value);
                lastVoltage = value;
            }
            for (int i = 0; i < offOut; i++)
                waveformBuffer.setSample(channel, pos++, 0.0f);


        }

    }
    
    // Generate sine waveform
    if (root.hasProperty("sine"))
    {
        const var& sine = root["sine"];
        int channel = sine.getProperty("analogOutputChannel", 1);
        double frequency = sine.getProperty("frequency", 5.0);
        int cycles = sine.getProperty("cycles", 1);
        double delayDuration = sine.getProperty("delayDuration", 0.0);
        float sineVoltage = sine.getProperty("maxVoltage", maxVoltage);
        
        int delayOut = millisecondsToSamples(delayDuration, sampleRate);
        int samplesPerCycle = frequency > 0.0 ? static_cast<int>((sampleRate / frequency) + 0.5) : 0;
        
        int pos = 0;
        for (int i = 0; i < delayOut; i++)
            waveformBuffer.setSample(channel, pos++, 0.0f);
        
        int sineSamples = cycles * samplesPerCycle;
        LOGD("Generating WavePlayer raised cosine: channel=", channel,
             ", delayMs=", delayDuration,
             ", delaySamples=", delayOut,
             ", frequency=", frequency,
             ", cycles=", cycles,
             ", samplesPerCycle=", samplesPerCycle,
             ", totalSamples=", sineSamples,
             ", sampleRate=", sampleRate);
        for (int i = 0; i < sineSamples && pos < totalSamples; i++)
        {
            const double phase = 2.0 * MathConstants<double>::pi * frequency * i / sampleRate;
            float value = 0.5f * sineVoltage * static_cast<float>(1.0 - std::cos(phase));
            waveformBuffer.setSample(channel, pos++, value);
        }
        LOGD("Raised cosine waveform generated: wrote ", pos - delayOut, " samples to channel ", channel);
    }
    
    // Generate custom waveform
    if (root.hasProperty("custom"))
    {
        const var& custom = root["custom"];
        int channel = custom.getProperty("analogOutputChannel", 0);
        String customString = custom.getProperty("string", "0").toString();
        
        StringArray tokens;
        tokens.addTokens(customString, ",", "");
        
        for (int i = 0; i < tokens.size(); i++)
            waveformBuffer.setSample(channel, i, tokens[i].getFloatValue());
    }
    
    return true;
}

bool CustomWaveform::regenerateWithNewSampleRate(double newSampleRate)
{
    if (lastProtocolJson.isEmpty())
        return false;
    
    LOGC("Regenerating waveform with new sample rate: ", newSampleRate);
    return parseProtocol(lastProtocolJson, newSampleRate);
}

void CustomWaveform::fillBuffer(AudioBuffer<float>& buffer, int numSamples)
{
    if (!isValid())
    {
        buffer.clear();
        return;
    }
    
    buffer.clear();
    
    static int debugCounter = 0;
    if (debugCounter++ % 1000 == 0)
    {
        LOGC("fillBuffer: buffer channels=", buffer.getNumChannels(), ", waveform channels=", waveformBuffer.getNumChannels(), ", currentSample=", currentSample);
    }
    
    for (int sample = 0; sample < numSamples; sample++)
    {
        for (int channel = 0; channel < jmin(buffer.getNumChannels(), waveformBuffer.getNumChannels()); channel++)
        {
            if (currentSample < waveformBuffer.getNumSamples())
            {
                float value = waveformBuffer.getSample(channel, currentSample);
                buffer.setSample(channel, sample, value);
                if (debugCounter % 1000 == 0 && sample == 0 && channel == 1)
                {
                    LOGC("Channel 1 sample[0]=", value);
                }
            }
            else
            {
                buffer.setSample(channel, sample, 0.0f);
            }
        }
        
        currentSample++;
        
        // If looping is enabled and we reached the end, wrap around
        if (shouldLoop && currentSample >= waveformBuffer.getNumSamples())
        {
            currentSample = 0;
        }
    }
}
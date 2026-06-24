/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2019 Allen Institute for Brain Science and Open Ephys

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

#ifndef __NIDAQCOMPONENTS_H__
#define __NIDAQCOMPONENTS_H__

#include <DataThreadHeaders.h>
#include <ProcessorHeaders.h>
#include <stdio.h>
#include <string.h>

#include "nidaq-api/NIDAQmx.h"

#include "CircularBuffer.h"

#define NUM_SAMPLE_RATES 18

#define PORT_SIZE 8 //number of bits in a port
#define DEFAULT_NUM_ANALOG_OUTPUTS 2
#define MAX_NUM_ANALOG_OUTPUTS 2
#define DEFAULT_NUM_DIGITAL_OUTPUTS 8

#define ERR_BUFF_SIZE 2048

#define STR2CHR( jString ) ((jString).toUTF8())
#define DAQmxErrChk(functionCall) if( DAQmxFailed(error=(functionCall)) ) goto Error; else

class NIDAQmx;
class OutputChannel;
class AnalogOutput;
class DigitalOutput;

enum SOURCE_TYPE {
	RSE = 0,
	NRSE,
	DIFF,
	PSEUDO_DIFF
};

struct DeviceAOProperties
{
    char physicalChans[256]; // Assuming max 256 characters
	char termCfgs[256]; // Assuming max 256 characters
    NIDAQ::int32 supportedOutputTypes[10]; // Assuming max 10 types
    NIDAQ::float64 maxRate;
    NIDAQ::float64 minRate;
    NIDAQ::bool32 sampClkSupported;
    NIDAQ::uInt32 numSampTimingEngines;
    NIDAQ::int32 sampModes[10]; // Assuming max 10 modes
    NIDAQ::uInt32 numSyncPulseSrcs;
    NIDAQ::int32 trigUsage;
    NIDAQ::float64 voltageRngs[10]; // Assuming max 10 ranges
    NIDAQ::float64 currentRngs[10]; // Assuming max 10 ranges
    NIDAQ::float64 gains[10]; // Assuming max 10 gains

	#define END_OF_BUFFER(x) (abs(x) > 1e8)
	
	void show()
    {
		StringArray channel_list;
		channel_list.addTokens(&physicalChans[0], ", ", "\"");

		LOGC("Detected analog input channels: ");

		for (int i = 0; i < channel_list.size(); i++) {
			if (channel_list[i].length())
				LOGC('\t', channel_list[i]);
		}
        
        LOGC("Supported Output Types:");
        for (int i = 0; i < sizeof(supportedOutputTypes)/sizeof(int32); ++i) {
			if (supportedOutputTypes[i] == 0) break;
            switch (supportedOutputTypes[i]) {
			case DAQmx_Val_Voltage:
            	std::cout << '\t' << "Voltage" << '\n';
				break;
			default:
				std::cout << '\t' << "Unknown output type: " << sampModes[i] << '\n';
				break;
			}
        }
        
        std::cout << "Max Rate: " << maxRate << '\n';
        std::cout << "Min Rate: " << minRate << '\n';
        std::cout << "Sample Clock Supported: " << (sampClkSupported ? "Yes" : "No") << '\n';
        std::cout << "Number of Sample Timing Engines: " << numSampTimingEngines << '\n';
        
        std::cout << "Sample Modes:\n";
        for (int i = 0; i < sizeof(sampModes)/sizeof(int32); ++i) {
			if (sampModes[i] < DAQmx_Val_FiniteSamps) break;
			switch (sampModes[i]) {
			case DAQmx_Val_FiniteSamps:
            	std::cout << '\t' << "Finite Samples" << '\n';
				break;
			case DAQmx_Val_ContSamps:
				std::cout << '\t' << "Continuous Samples" << '\n';
				break;
			case DAQmx_Val_HWTimedSinglePoint:
				std::cout << '\t' << "Hardware Timed Single Point" << '\n';
				break;
			default:
				std::cout << '\t' << sampModes[i] << '\n';
				break;
			}
        }

        std::cout << "Number of Sync Pulse Sources: " << numSyncPulseSrcs << '\n';
        std::cout << "Trigger Usage: " << trigUsage << '\n';
        
        std::cout << "Voltage Ranges:\n";
        for (int i = 0; i < sizeof(voltageRngs)/sizeof(NIDAQ::float64); ++i) {
			if (abs(voltageRngs[i]) < 1e-10) break;
            std::cout << '\t' << voltageRngs[i] << '\n';
        }
        
        std::cout << "Current Ranges:\n";
        for (int i = 0; i < sizeof(currentRngs)/sizeof(NIDAQ::float64); ++i) {
			if (abs(currentRngs[i]) < 1e-10) break;
            std::cout << '\t' << currentRngs[i] << '\n';
        }
        
        std::cout << "Gains:\n";
        for (int i = 0; i < sizeof(gains)/sizeof(NIDAQ::float64); ++i) {
			if (abs(gains[i]) < 1e-10) break;
            std::cout << '\t' << gains[i] << '\n';
        }
    }

};

struct SettingsRange {
	NIDAQ::float64 min, max;
	SettingsRange() : min(0), max(0) {}
	SettingsRange(NIDAQ::float64 min_, NIDAQ::float64 max_)
		: min(min_), max(max_) {}
};

class OutputChannel
{
public:
	OutputChannel(String name_) : name(name_) {};
	OutputChannel() : name("") {};
	~OutputChannel() {};

	String getName() { return name; }

	// Defines if the channel is available for use
	void setAvailable(bool available_) { available = available_; }
	bool isAvailable() { return available; }

	// Defines if the channel is enabled for use
	void setEnabled(bool enabled_) { enabled = enabled_; }
	bool isEnabled() { return enabled; }

private:

	String name;
	bool available = false;
	bool enabled = false;
};

class AnalogOutput : public OutputChannel
{

public:
	AnalogOutput(String name, NIDAQ::int32 terminalConfig);
	AnalogOutput() : OutputChannel() {};
	~AnalogOutput() {};

	SOURCE_TYPE getSourceType() { return sourceTypes[sourceTypeIndex]; }
	void setNextSourceType() { sourceTypeIndex = (sourceTypeIndex + 1) % sourceTypes.size(); }

private:
	int sourceTypeIndex = 0;
	Array<SOURCE_TYPE> sourceTypes;
};

struct DeviceOutputInfo
{
	StringArray analogOutputs;
	StringArray digitalOutputs;

	String getSummary() const
	{
		const String ao = analogOutputs.isEmpty() ? "none" : analogOutputs.joinIntoString(", ");
		const String doLines = digitalOutputs.isEmpty() ? "none" : digitalOutputs.joinIntoString(", ");
		return "AO: " + ao + "; DO: " + doLines;
	}
};

class NIDAQDevice
{

public:

	NIDAQDevice(String name_) : name(name_) {};
	NIDAQDevice() {};
	~NIDAQDevice() {};

	String getName() { return name; }

	String productName;

	NIDAQ::int32 deviceCategory;
	NIDAQ::uInt32 productNum;
	NIDAQ::uInt32 serialNum;
	NIDAQ::uInt32 numAOChannels;
	NIDAQ::uInt32 numDOChannels;
	NIDAQ::uInt32 numDOPorts;

	int digitalWriteSize;

	SettingsRange sampleRateRange;

	bool isUSBDevice;

	Array<SettingsRange> voltageRanges;
	Array<NIDAQ::float64> adcResolutions;

	StringArray analogOutputNames;
	
	Array<std::string> digitalPortNames;
	Array<bool> digitalPortStates;

private:

	String name;

};

class NIDAQmxDeviceManager
{
public:

	NIDAQmxDeviceManager() {};
	~NIDAQmxDeviceManager() {};

	void scanForDevices();

	int getNumAvailableDevices() { return devices.size(); }

	int getDeviceIndexFromName(String deviceName);
	NIDAQDevice* getDeviceAtIndex(int index) { return devices[index]; }

	NIDAQDevice* getDeviceFromName(String deviceName);

	const DeviceOutputInfo& getDeviceOutputInfo(const String& deviceName);
	String getDeviceSelectLabel(const String& deviceName, const String& productName);

	friend class NIDAQThread;

private:

	DeviceOutputInfo queryDeviceOutputInfo(const String& deviceName);

	OwnedArray<NIDAQDevice> devices;
	HashMap<String, DeviceOutputInfo> deviceOutputInfoCache;
	int activeDeviceIndex;
};

class NIDAQmx : public Thread
{
public:

	NIDAQmx(NIDAQDevice* device_);
	~NIDAQmx() {};

	/* Pointer to the active device */
	NIDAQDevice* device;

	/* Connects to the active device */
	void connect(); 

	/* Unique device properties */
	String getProductName() { return device->productName; };
	String getSerialNumber() { return String(device->serialNum); };

	DeviceAOProperties NIDAQmx::getDeviceAOProperties(const char* device);

	void setAudioSampleRate(NIDAQ::float64 sampleRate) { audioSampleRate = sampleRate; };

	/* Analog configuration */
	NIDAQ::float64 getSampleRate() { return sampleRates[sampleRateIndex]; };
	void setSampleRate(int index)
	{
		const int previousIndex = sampleRateIndex;
		sampleRateIndex = juce::jlimit(0, sampleRates.size() - 1, index);
		LOGD("NIDAQmx::setSampleRate requested index=", index,
			 ", previous index=", previousIndex,
			 ", selected index=", sampleRateIndex,
			 ", selected AO rate=", getSampleRate());
	};

	SettingsRange getVoltageRange() { return device->voltageRanges[voltageRangeIndex]; };
	void setVoltageRange(int index) { voltageRangeIndex = index; };

	SOURCE_TYPE getSourceTypeForOutput(int analogOutputIndex);
	void toggleSourceType(int analogOutputIndex) { aout[analogOutputIndex]->setNextSourceType(); }

	void setNumActiveAnalogOutputs(int numActiveAnalogOutputs_) { numActiveAnalogOutputs = numActiveAnalogOutputs_; };
	int getNumActiveAnalogOutputs() { return numActiveAnalogOutputs; };

	void setNumActiveDigitalOutputs(int numActiveDigitalOutputs_) { numActiveDigitalOutputs = numActiveDigitalOutputs_; };
	int getNumActiveDigitalOutputs() { return numActiveDigitalOutputs; };

	/*Digital configuration */
	void setDigitalWriteSize(int digitalWriteSize_) { digitalWriteSize = digitalWriteSize_; };
	int getDigitalWriteSize() { return digitalWriteSize; };

	int getNumPorts() { return device->digitalPortNames.size(); };
	bool getPortState(int idx) { return device->digitalPortStates[idx]; };
	void setPortState(int idx, bool state) { device->digitalPortStates.set(idx, state); };

	/* 32-bit mask indicating which lines are currently enabled */
	uint32 getActiveDigitalLines();

	int getDefaultOutputPort() { return defaultOutputPort; };
	std::vector<int> getActiveDigitalPorts() { return activeDigitalPorts; };

	void startTasks(bool startAnalogTask = true);
	void clearTasks();
	void clearAnalogTask();
	void clearContinuousDOTasks();
	void clearDigitalFiniteTask();
	void requestShutdown();

	void analogWrite(AudioBuffer<float>& buffer, int numSamples);
	void writeAnalogWaveform(const AudioBuffer<float>& buffer);
	void digitalWrite(int channelIdx, bool state);

	void run() override;

	void addEvent(int64 sampleNumber, uint8 ttlLine, bool state);

	bool shouldSendSynchronizedEvents(bool sendSynchronizedEvents_) { sendSynchronizedEvents =  sendSynchronizedEvents_; };
	bool sendsSynchronizedEvents() { return sendSynchronizedEvents; };

	Array<NIDAQ::float64> sampleRates;

	OwnedArray<AnalogOutput> 	aout;
	OwnedArray<OutputChannel> 	dout;

	NIDAQ::TaskHandle taskHandleAO = 0;
	NIDAQ::TaskHandle taskHandleDOFinite = 0;
	std::vector<NIDAQ::TaskHandle> taskHandlesDO;

private:

	/* Manages connected NIDAQ devices */
	ScopedPointer<NIDAQmxDeviceManager> dm;

	int deviceIndex = 0;
	int sampleRateIndex = 0;
	int voltageRangeIndex = 0;

	/* Assign a default port to be assigned as output */
	int defaultOutputPort = 0;

	/* Port numbers assigned as all output */
	std::vector<int> activeDigitalPorts { defaultOutputPort };

	int digitalWriteSize = 8;

	int numActiveAnalogOutputs = DEFAULT_NUM_ANALOG_OUTPUTS; //2
	int numActiveDigitalOutputs = DEFAULT_NUM_DIGITAL_OUTPUTS; //8

	NIDAQ::uInt8 eventCode;

	NIDAQ::uInt64 samplesPerChannel = 0;

	struct OutputEvent
	{
		OutputEvent(int64 sampleNumber_, int ttlLine_, bool state_) :
			sampleNumber(sampleNumber_),
			ttlLine(ttlLine_),
			state(state_)
		{}
		int64 sampleNumber;
		uint8 ttlLine;
		bool state;
	};

	CriticalSection lock;

	OwnedArray<OutputEvent> eventBuffer;
	std::unique_ptr<CircularBuffer<double>> analogOutBuffer;

	bool sendSynchronizedEvents = false;

	int writeCount = 0;

	NIDAQ::float64 audioSampleRate = 0;

	String getFirstSelectedDigitalLine() const;
};

#endif  // __NIDAQCOMPONENTS_H__
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

#include <chrono>
#include <cmath>
#include <math.h>

#include "NIDAQComponents.h"

AnalogOutput::AnalogOutput(String name, NIDAQ::int32 termCfgs) : OutputChannel(name)
{

	sourceTypes.clear();

	if (termCfgs & DAQmx_Val_Bit_TermCfg_RSE)
		sourceTypes.add(SOURCE_TYPE::RSE);
	
	if (termCfgs & DAQmx_Val_Bit_TermCfg_NRSE)
		sourceTypes.add(SOURCE_TYPE::NRSE);

	if (termCfgs & DAQmx_Val_Bit_TermCfg_Diff)
		sourceTypes.add(SOURCE_TYPE::DIFF);

	if (termCfgs & DAQmx_Val_Bit_TermCfg_PseudoDIFF)
		sourceTypes.add(SOURCE_TYPE::PSEUDO_DIFF);

}

static int32 GetTerminalNameWithDevPrefix(NIDAQ::TaskHandle taskHandle, const char terminalName[], char triggerName[]);

static int32 GetTerminalNameWithDevPrefix(NIDAQ::TaskHandle taskHandle, const char terminalName[], char triggerName[])
{

	NIDAQ::int32	error = 0;
	char			device[256];
	NIDAQ::int32	productCategory;
	NIDAQ::uInt32	numDevices, i = 1;

	DAQmxErrChk(NIDAQ::DAQmxGetTaskNumDevices(taskHandle, &numDevices));
	while (i <= numDevices) {
		DAQmxErrChk(NIDAQ::DAQmxGetNthTaskDevice(taskHandle, i++, device, 256));
		DAQmxErrChk(NIDAQ::DAQmxGetDevProductCategory(device, &productCategory));
		if (productCategory != DAQmx_Val_CSeriesModule && productCategory != DAQmx_Val_SCXIModule) {
			*triggerName++ = '/';
			strcat(strcat(strcpy(triggerName, device), "/"), terminalName);
			break;
		}
	}

Error:
	return error;
}

DeviceOutputInfo NIDAQmxDeviceManager::queryDeviceOutputInfo(const String& deviceName)
{
	DeviceOutputInfo info;

	if (deviceName == "Simulated")
	{
		for (int i = 0; i < MAX_NUM_ANALOG_OUTPUTS; ++i)
			info.analogOutputs.add("ao" + String(i));

		for (int i = 0; i < DEFAULT_NUM_DIGITAL_OUTPUTS; ++i)
			info.digitalOutputs.add("port0/line" + String(i));

		return info;
	}

	char aoData[2048] = { 0 };
	NIDAQ::DAQmxGetDevAOPhysicalChans(STR2CHR(deviceName), aoData, sizeof(aoData));

	StringArray aoList;
	aoList.addTokens(aoData, ", ", "\"");

	for (const auto& chan : aoList)
	{
		if (chan.isNotEmpty())
			info.analogOutputs.add(chan.fromLastOccurrenceOf("/", false, false));
	}

	char doData[2048] = { 0 };
	NIDAQ::DAQmxGetDevDOLines(STR2CHR(deviceName), doData, sizeof(doData));

	StringArray doList;
	doList.addTokens(doData, ", ", "\"");

	for (const auto& line : doList)
	{
		if (line.isNotEmpty())
			info.digitalOutputs.add(line.fromFirstOccurrenceOf("/", false, false));
	}

	return info;
}

const DeviceOutputInfo& NIDAQmxDeviceManager::getDeviceOutputInfo(const String& deviceName)
{
	if (!deviceOutputInfoCache.contains(deviceName))
		deviceOutputInfoCache.set(deviceName, queryDeviceOutputInfo(deviceName));

	return deviceOutputInfoCache.getReference(deviceName);
}

String NIDAQmxDeviceManager::getDeviceSelectLabel(const String& deviceName, const String& productName)
{
	juce::ignoreUnused(deviceName);
	return productName;
}

void NIDAQmxDeviceManager::scanForDevices()
{

	devices.clear();
	deviceOutputInfoCache.clear();

	char data[2048] = { 0 };
	NIDAQ::DAQmxGetSysDevNames(data, sizeof(data));

	StringArray deviceList; 
	deviceList.addTokens(&data[0], ", ", "\"");

	StringArray deviceNames;
	StringArray productList;

	for (int i = 0; i < deviceList.size(); i++)
	{
		if (deviceList[i].length() > 0)
		{
			String deviceName = deviceList[i].toUTF8();

			/* Get product name */
			char pname[2048] = { 0 };
			NIDAQ::DAQmxGetDevProductType(STR2CHR(deviceName), &pname[0], sizeof(pname));
			devices.add(new NIDAQDevice(deviceName));
			devices.getLast()->productName = String(&pname[0]);
			deviceOutputInfoCache.set(deviceName, queryDeviceOutputInfo(deviceName));
		}
	}

	if (!devices.size())
	{
		devices.add(new NIDAQDevice("Simulated")); //TODO: Make SimulatedClass derived from NIDAQDevice
		deviceOutputInfoCache.set("Simulated", queryDeviceOutputInfo("Simulated"));
	}

}

int NIDAQmxDeviceManager::getDeviceIndexFromName(String name)
{

	for (int i = 0; i < devices.size(); i++)
		if (devices[i]->getName() == name)
			return i;

	return -1;

}

NIDAQmx::NIDAQmx(NIDAQDevice* device_) 
: Thread("NIDAQmx-" + String(device_->getName())), device(device_)
{

	connect();

	digitalWriteSize = device->digitalWriteSize;

	// Pre-define reasonable sample rates
	float sample_rates[NUM_SAMPLE_RATES] = {
		1000.0f, 1250.0f, 1500.0f,
		2000.0f, 2500.0f,
		3000.0f, 3330.0f,
		4000.0f,
		5000.0f,
		6250.0f,
		8000.0f,
		10000.0f,
		12500.0f,
		15000.0f,
		20000.0f,
		25000.0f,
		30000.0f,
		40000.0f
	};

	sampleRates.clear();

	int idx = 0;
	while (sample_rates[idx] <= device->sampleRateRange.max && idx < NUM_SAMPLE_RATES)
		sampleRates.add(sample_rates[idx++]);

	// Default to highest sample rate
	sampleRateIndex = sampleRates.size() - 1;

	// Default to largest voltage range
	voltageRangeIndex = device->voltageRanges.size() - 1;

	analogOutBuffer = std::make_unique<CircularBuffer<double>>(200000);

}

SOURCE_TYPE NIDAQmx::getSourceTypeForOutput(int analogOutputIndex)
{
	const int n = aout.size();
	if (analogOutputIndex < 0 || analogOutputIndex >= n)
		return SOURCE_TYPE::RSE;
	return aout[analogOutputIndex]->getSourceType();
}

DeviceAOProperties NIDAQmx::getDeviceAOProperties(const char* device)
{
    DeviceAOProperties props {};

    NIDAQ::DAQmxGetDevAOPhysicalChans(device, props.physicalChans, sizeof(props.physicalChans));
    NIDAQ::DAQmxGetDevAOSupportedOutputTypes(device, props.supportedOutputTypes, sizeof(props.supportedOutputTypes) / sizeof(NIDAQ::int32));
    NIDAQ::DAQmxGetDevAOMaxRate(device, &props.maxRate);
    NIDAQ::DAQmxGetDevAOMinRate(device, &props.minRate);
    NIDAQ::DAQmxGetDevAOSampClkSupported(device, &props.sampClkSupported);
    NIDAQ::DAQmxGetDevAONumSampTimingEngines(device, &props.numSampTimingEngines);
    NIDAQ::DAQmxGetDevAOSampModes(device, props.sampModes, sizeof(props.sampModes) / sizeof(NIDAQ::int32));
    NIDAQ::DAQmxGetDevAONumSyncPulseSrcs(device, &props.numSyncPulseSrcs);
    NIDAQ::DAQmxGetDevAOTrigUsage(device, &props.trigUsage);
    NIDAQ::DAQmxGetDevAOVoltageRngs(device, props.voltageRngs, sizeof(props.voltageRngs) / sizeof(NIDAQ::float64));
    NIDAQ::DAQmxGetDevAOCurrentRngs(device, props.currentRngs, sizeof(props.currentRngs) / sizeof(NIDAQ::float64));
    NIDAQ::DAQmxGetDevAOGains(device, props.gains, sizeof(props.gains) / sizeof(NIDAQ::float64));

    return props;
}

void NIDAQmx::connect()
{

	String deviceName = device->getName();

	if (deviceName == "Simulated")
	{
		device->isUSBDevice = false;
		device->digitalWriteSize = 8;
		device->sampleRateRange = SettingsRange(1000.0f, 30000.0f);
		device->voltageRanges.add(SettingsRange(-10.0f, 10.0f));
		device->productName = String("No Device Detected");

		device->numAOChannels = 0;
		device->numDOChannels = 0;
		device->numDOPorts = 1;

		aout.clear();
		const auto rseTerminalCfg = (NIDAQ::int32) DAQmx_Val_Bit_TermCfg_RSE;
		for (int i = 0; i < numActiveAnalogOutputs && i < MAX_NUM_ANALOG_OUTPUTS; ++i)
		{
			const String chanName(device->getName() + "/ao" + String(i));
			aout.add(new AnalogOutput(chanName, rseTerminalCfg));
			aout.getLast()->setAvailable(true);
			aout.getLast()->setEnabled(true);
			++device->numAOChannels;
		}

		dout.clear();
		device->digitalPortNames.clear();
		device->digitalPortStates.clear();

		const String portPrefix = device->getName() + "/port0";
		device->digitalPortNames.add(portPrefix.toStdString());
		device->digitalPortStates.add(true);

		for (int i = 0; i < numActiveDigitalOutputs; ++i)
		{
			const String lineName(portPrefix + "/line" + String(i));
			dout.add(new OutputChannel(lineName));
			dout.getLast()->setAvailable(true);
			dout.getLast()->setEnabled(true);
			++device->numDOChannels;
		}

		return;
	}
	else
	{

		/* Get category type */
		NIDAQ::DAQmxGetDevProductCategory(STR2CHR(deviceName), &device->deviceCategory);
		LOGD("Product Category: ", device->deviceCategory);

		/* Check if USB device */
		device->isUSBDevice = device->productName.contains("USB");

		device->digitalWriteSize = device->isUSBDevice ? 32 : 8;

		/* Product name */
		NIDAQ::DAQmxGetDevProductNum(STR2CHR(deviceName), &device->productNum);
		LOGD("Product Num: ", device->productNum);

		/* Serial number */
		NIDAQ::DAQmxGetDevSerialNum(STR2CHR(deviceName), &device->serialNum);
		LOGD("Serial Num: ", device->serialNum);

		/* Get device analog output properties */
		DeviceAOProperties aoProps = getDeviceAOProperties(STR2CHR(deviceName));
		//aoProps.show();

		/* Define available voltage ranges (same pattern as nidaq-plugin DAQmxGetDevAIVoltageRngs) */
		device->voltageRanges.clear();
		NIDAQ::float64 aoVoltageRngData[512] {};
		NIDAQ::DAQmxGetDevAOVoltageRngs(STR2CHR(deviceName), &aoVoltageRngData[0], sizeof(aoVoltageRngData));
		for (int i = 0; i < 512; i += 2)
		{
			NIDAQ::float64 vmin = aoVoltageRngData[i];
			NIDAQ::float64 vmax = aoVoltageRngData[i + 1];
			if (vmin == vmax || std::abs(vmin) < 1e-10 || vmax < 1e-2)
				break;
			device->voltageRanges.add(SettingsRange(vmin, vmax));
		}

		/* Configure analog output channels */
		StringArray channel_list;
		channel_list.addTokens(&aoProps.physicalChans[0], ", ", "\"");

		device->numAOChannels = 0;
		aout.clear();

		LOGD("Detected ", channel_list.size(), " analog output channels");

		for (int i = 0; i < channel_list.size(); i++)
		{

			if (channel_list[i].length() > 0)
			{
				/* Get channel termination */
				NIDAQ::int32 termCfgs;
				NIDAQ::DAQmxGetPhysicalChanAOTermCfgs(channel_list[i].toUTF8(), &termCfgs);

				String name = channel_list[i].toRawUTF8();

				String terminalConfigurations = String::toHexString(termCfgs);

				aout.add(new AnalogOutput(name, termCfgs));

				if (device->numAOChannels++ <= numActiveAnalogOutputs)
				{
					aout.getLast()->setAvailable(true);
					aout.getLast()->setEnabled(true);
				}

				LOGD("Adding analog output channel: ", name, " with terminal config: ", " (", termCfgs, ") enabled: ", aout.getLast()->isEnabled() ? "YES" : "NO");

				if (aout.size() == MAX_NUM_ANALOG_OUTPUTS) break;
				
			}
		}

		NIDAQ::int32	error = 0;
		char			errBuff[ERR_BUFF_SIZE] = { '\0' };

		// Get ADC resolution for each voltage range (throwing error as is)
		NIDAQ::TaskHandle adcResolutionQuery = 0;

		NIDAQ::DAQmxCreateTask("ADCResolutionQuery", &adcResolutionQuery);

		SettingsRange vRange;

		for (int i = 0; i < device->voltageRanges.size(); i++)
		{
			vRange = device->voltageRanges[i];

			DAQmxErrChk(NIDAQ::DAQmxCreateAOVoltageChan(
				adcResolutionQuery,				//task handle
				STR2CHR(aout[i]->getName()),	//NIDAQ physical channel name (e.g. dev1/ai1)
				"",								//user-defined channel name (optional)
				//aout[i]->getSourceType(),		//input terminal configuration
				vRange.min,						//min input voltage
				vRange.max,						//max input voltage
				DAQmx_Val_Volts,				//voltage units
				NULL));

			NIDAQ::float64 adcResolution;
			DAQmxErrChk(NIDAQ::DAQmxGetAOResolution(adcResolutionQuery, STR2CHR(aout[i]->getName()), &adcResolution));

			//TODO: DAQmxGetAOResolutionUnits/ DAQmxSetAOResolutionUnits

			device->adcResolutions.add(adcResolution);

		}

		NIDAQ::DAQmxStopTask(adcResolutionQuery);
		NIDAQ::DAQmxClearTask(adcResolutionQuery);

		// Get Digital Output Channels

		char di_channel_data[2048];
		//NIDAQ::DAQmxGetDevTerminals(STR2CHR(deviceName), &data[0], sizeof(data)); //gets all terminals
		//NIDAQ::DAQmxGetDevDIPorts(STR2CHR(deviceName), &data[0], sizeof(data));	//gets line name
		NIDAQ::DAQmxGetDevDOLines(STR2CHR(deviceName), &di_channel_data[0], sizeof(di_channel_data));	//gets ports on line

		channel_list.clear();
		channel_list.addTokens(&di_channel_data[0], ", ", "\"");

		device->digitalPortNames.clear();
		device->digitalPortStates.clear();
		device->numDOChannels = 0;
		dout.clear();

		for (int i = 0; i < channel_list.size(); i++)
		{
			StringArray channel_type;
			channel_type.addTokens(channel_list[i], "/", "\"");
			if (channel_list[i].length() > 0)
			{
				String fullName = channel_list[i].toRawUTF8();

				String lineName = fullName.fromFirstOccurrenceOf("/", false, false);
				String portName = fullName.upToLastOccurrenceOf("/", false, false);

				// Add port to list of ports
				if (!device->digitalPortNames.contains(portName.toRawUTF8()))
				{
					device->digitalPortNames.add(portName.toRawUTF8());
					if (device->numDOChannels < numActiveDigitalOutputs)
						device->digitalPortStates.add(true);
					else
						device->digitalPortStates.add(false);
				}

				dout.add(new OutputChannel(fullName));

				dout.getLast()->setAvailable(true);
				if (device->numDOChannels < numActiveDigitalOutputs)
					dout.getLast()->setEnabled(true);

				device->numDOChannels++;

			}
		}

		device->sampleRateRange = SettingsRange(aoProps.minRate, aoProps.maxRate);

		analogOutBuffer.reset();

Error:

		if (DAQmxFailed(error))
			NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

		if (adcResolutionQuery != 0) {
			// DAQmx Stop Code
			NIDAQ::DAQmxStopTask(adcResolutionQuery);
			NIDAQ::DAQmxClearTask(adcResolutionQuery);
		}

		if (DAQmxFailed(error))
			LOGE("DAQmx Error: ", errBuff);
		fflush(stdout);

		return;

	}

}

void NIDAQmx::startTasks(bool startAnalogTask)
{

	StringArray port_list;

	clearTasks();

    NIDAQ::int32 error = 0;
    char errBuff[2048] = { '\0' };

    NIDAQ::int32 activeEdge = DAQmx_Val_Rising;
   	NIDAQ::int32 sampleMode = DAQmx_Val_ContSamps;

	if (startAnalogTask)
	{
		// Create an analog output task
		if (device->isUSBDevice)
			DAQmxErrChk(NIDAQ::DAQmxCreateTask("AOTask_USB", &taskHandleAO));
		else
			DAQmxErrChk(NIDAQ::DAQmxCreateTask("AOTask_PXI", &taskHandleAO));

		// Create analog output channels for ao0 and ao1
		DAQmxErrChk(NIDAQ::DAQmxCreateAOVoltageChan(
			taskHandleAO,
			STR2CHR(device->getName() + "/ao0:" + String(numActiveAnalogOutputs - 1)), 
			"", -10.0, 10.0,
			DAQmx_Val_Volts,
			nullptr)
		);

		// Configure the sample clock timing for the analog task
		DAQmxErrChk(NIDAQ::DAQmxCfgSampClkTiming(
			taskHandleAO,
			"", 
			getSampleRate(),
			activeEdge, 
			sampleMode, 
			samplesPerChannel)
		);
	}

	char ports[2048];
	NIDAQ::DAQmxGetDevDOPorts(STR2CHR(device->getName()), &ports[0], sizeof(ports));

	port_list.addTokens(&ports[0], ", ", "\"");

	int portIdx = 0;
	for (auto& port : port_list)
	{

		if (port.length() && (portIdx*PORT_SIZE < dout.size()) && device->digitalPortStates[portIdx])
		{

			LOGD("Adding digital output task on port ", portIdx, " (", port, ")");

			NIDAQ::TaskHandle taskHandleDO = 0;

			//Create a digital input task using device serial number to gurantee unique task name per device
			if (device->isUSBDevice)
				DAQmxErrChk(NIDAQ::DAQmxCreateTask(STR2CHR("DOTask_USB"+getSerialNumber()+"port"+std::to_string(portIdx)), &taskHandleDO));
			else
				DAQmxErrChk(NIDAQ::DAQmxCreateTask(STR2CHR("DOTask_PXI"+getSerialNumber()+"port"+std::to_string(portIdx)), &taskHandleDO));

			LOGD("Creating channel for port: ", port);

			// Create a channel for each digital output port
			DAQmxErrChk(NIDAQ::DAQmxCreateDOChan(
				taskHandleDO,
				STR2CHR(port),
				"",
				DAQmx_Val_ChanForAllLines)
			);

			//Configure timing
			if (portIdx == 0)
			{

				// Configure sample clock timing
				if (sendsSynchronizedEvents())
				{
					DAQmxErrChk(NIDAQ::DAQmxCfgSampClkTiming(
						taskHandleDO,
						"",
						getSampleRate(),
						activeEdge,
						sampleMode,
						samplesPerChannel)
					);
					LOGC("Configured sample clk timing for: ", port);
				}
			}

			taskHandlesDO.push_back(taskHandleDO);

		}

		if (port.length()) portIdx++;

	}

	// Start analog and digital output tasks
	if (taskHandleAO > 0)
		DAQmxErrChk(NIDAQ::DAQmxStartTask(taskHandleAO));
	for (auto& taskHandleDO : taskHandlesDO)
		DAQmxErrChk(NIDAQ::DAQmxStartTask(taskHandleDO));

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);

	return;

}

void NIDAQmx::requestShutdown()
{
	if (analogOutBuffer)
		analogOutBuffer->requestShutdown();
}

void NIDAQmx::clearTasks()
{

	NIDAQ::int32	error = 0;
	char			errBuff[ERR_BUFF_SIZE] = { '\0' };

	clearAnalogTask();
	clearDigitalFiniteTask();

	if (taskHandlesDO.size() > 0)
	{
		for (auto& taskHandle : taskHandlesDO)
		{
			NIDAQ::DAQmxStopTask(taskHandle);
			NIDAQ::DAQmxClearTask(taskHandle);
		}
		taskHandlesDO.clear();
	}

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);

	return;
}

void NIDAQmx::clearAnalogTask()
{

	NIDAQ::int32	error = 0;
	char			errBuff[ERR_BUFF_SIZE] = { '\0' };

	if (taskHandleAO > 0)
	{
		NIDAQ::DAQmxStopTask(taskHandleAO);
		NIDAQ::DAQmxClearTask(taskHandleAO);
		taskHandleAO = 0;
	}

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);

	return;
}

void NIDAQmx::clearContinuousDOTasks()
{
	NIDAQ::int32 error = 0;
	char errBuff[ERR_BUFF_SIZE] = { '\0' };

	for (auto& taskHandle : taskHandlesDO)
	{
		NIDAQ::DAQmxStopTask(taskHandle);
		NIDAQ::DAQmxClearTask(taskHandle);
	}
	taskHandlesDO.clear();

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);
}

void NIDAQmx::clearDigitalFiniteTask()
{
	NIDAQ::int32 error = 0;
	char errBuff[ERR_BUFF_SIZE] = { '\0' };

	if (taskHandleDOFinite > 0)
	{
		NIDAQ::DAQmxStopTask(taskHandleDOFinite);
		NIDAQ::DAQmxClearTask(taskHandleDOFinite);
		taskHandleDOFinite = 0;
	}

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);
}

String NIDAQmx::getFirstSelectedDigitalLine() const
{
	if (device->getName() == "Simulated" || device->numDOChannels <= 0)
		return {};

	for (int portIdx = 0; portIdx < device->digitalPortStates.size(); ++portIdx)
	{
		if (!device->digitalPortStates[portIdx])
			continue;

		if (portIdx >= device->digitalPortNames.size())
			break;

		const String portPrefix = String(device->digitalPortNames[portIdx].c_str()) + "/";

		for (int i = 0; i < dout.size(); ++i)
		{
			if (!dout[i]->isEnabled())
				continue;

			if (dout[i]->getName().startsWith(portPrefix))
				return dout[i]->getName();
		}
	}

	return {};
}

void NIDAQmx::analogWrite(AudioBuffer<float>& buffer, int numSamples)
{
	NIDAQ::int32 error = 0;
    NIDAQ::int32 numSamplesWritten;
    NIDAQ::float64 timeout = 10.0;
    char errBuff[2048] = { '\0' };

	if (audioSampleRate <= 0 || numSamples <= 0 || buffer.getNumChannels() < 1)
		return;

	const int numChannels = 1;

	samplesPerChannel = (NIDAQ::uInt64)(numSamples / (audioSampleRate / 10000.0 / 2.0));
	int writeSamples = (int)(samplesPerChannel * 2);
	if (writeSamples > numSamples)
	{
		writeSamples = numSamples;
		samplesPerChannel = (NIDAQ::uInt64)(writeSamples / 2);
	}
	if (writeSamples <= 0 || samplesPerChannel == 0)
		return;

	HeapBlock<NIDAQ::float64> outputData(numChannels * writeSamples);

	const float* channelData = buffer.getReadPointer(0);
	for (int sample = 0; sample < writeSamples; ++sample)
		outputData[sample] = static_cast<NIDAQ::float64>(channelData[sample]);

	analogOutBuffer->write(outputData, numChannels * writeSamples);

	writeCount++;

	if (!isThreadRunning() && writeCount > 2) startThread();

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);

	return;

}

void NIDAQmx::writeAnalogWaveform(const AudioBuffer<float>& buffer, int analogOutputChannel)
{
	NIDAQ::int32 error = 0;
	NIDAQ::int32 numSamplesWritten = 0;
	NIDAQ::float64 timeout = 10.0;
	char errBuff[2048] = { '\0' };

	if (!buffer.getNumChannels() || !buffer.getNumSamples())
		return;

	if (isThreadRunning())
	{
		requestShutdown();
		stopThread(2000);
		analogOutBuffer = std::make_unique<CircularBuffer<double>>(200000);
	}

	clearAnalogTask();
	clearDigitalFiniteTask();

	const int maxAo = juce::jmax(1, (int) device->numAOChannels);
	const int firstChannel = juce::jlimit(0, maxAo - 1, analogOutputChannel);
	const int numChannels = juce::jmin(buffer.getNumChannels(), maxAo - firstChannel);
	const int numSamples = buffer.getNumSamples();
	if (numChannels <= 0)
		return;

	const String digitalLine = getFirstSelectedDigitalLine();
	setNumActiveAnalogOutputs(numChannels);
	const int lastChannel = firstChannel + numChannels - 1;
	const String channelString = (numChannels == 1)
		? device->getName() + "/ao" + String(firstChannel)
		: device->getName() + "/ao" + String(firstChannel) + ":" + String(lastChannel);
	LOGD("NIDAQmx::writeAnalogWaveform configuring finite AO task with sampleRate=", getSampleRate(),
		 ", bufferChannels=", buffer.getNumChannels(),
		 ", deviceAOChannels=", device->numAOChannels,
		 ", firstChannel=", firstChannel,
		 ", channelsWritten=", numChannels,
		 ", samplesPerChannel=", numSamples,
		 ", channelString=", channelString,
		 ", digitalLine=", digitalLine);
	HeapBlock<NIDAQ::float64> outputData(numChannels * numSamples);

	// DAQmx_Val_GroupByChannel: all samples for ch0, then all samples for ch1
	for (int ch = 0; ch < numChannels; ++ch)
	{
		const float* channelData = buffer.getReadPointer(ch);
		for (int sample = 0; sample < numSamples; ++sample)
			outputData[ch * numSamples + sample] = static_cast<NIDAQ::float64>(channelData[sample]);
	}

	if (device->isUSBDevice)
		DAQmxErrChk(NIDAQ::DAQmxCreateTask("AOTask_USB_Finite", &taskHandleAO));
	else
		DAQmxErrChk(NIDAQ::DAQmxCreateTask("AOTask_PXI_Finite", &taskHandleAO));

	DAQmxErrChk(NIDAQ::DAQmxCreateAOVoltageChan(
		taskHandleAO,
		STR2CHR(channelString),
		"", -10.0, 10.0,
		DAQmx_Val_Volts,
		nullptr)
	);

	DAQmxErrChk(NIDAQ::DAQmxCfgSampClkTiming(
		taskHandleAO,
		"",
		getSampleRate(),
		DAQmx_Val_Rising,
		DAQmx_Val_FiniteSamps,
		numSamples)
	);


	DAQmxErrChk(NIDAQ::DAQmxWriteAnalogF64(
		taskHandleAO,
		numSamples,
		0,
		timeout,
		DAQmx_Val_GroupByChannel,
		outputData,
		&numSamplesWritten,
		NULL
	));
	LOGD("NIDAQmx::writeAnalogWaveform wrote samplesPerChannel=", numSamplesWritten,
		 ", configuredSampleRate=", getSampleRate());

	bool digitalTaskReady = false;
	if (digitalLine.isNotEmpty())
	{
		NIDAQ::int32 doError = 0;
		char doErrBuff[ERR_BUFF_SIZE] = { '\0' };
		char sampleClockSource[256] = { '\0' };

		clearContinuousDOTasks();

		doError = GetTerminalNameWithDevPrefix(taskHandleAO, "ao/SampleClock", sampleClockSource);
		if (!DAQmxFailed(doError))
		{
			if (device->isUSBDevice)
				doError = NIDAQ::DAQmxCreateTask("DOTask_USB_Finite", &taskHandleDOFinite);
			else
				doError = NIDAQ::DAQmxCreateTask(STR2CHR("DOTask_PXI_Finite" + getSerialNumber()), &taskHandleDOFinite);
		}

		if (!DAQmxFailed(doError))
		{
			doError = NIDAQ::DAQmxCreateDOChan(
				taskHandleDOFinite,
				STR2CHR(digitalLine),
				"",
				DAQmx_Val_ChanPerLine);
		}

		if (!DAQmxFailed(doError))
		{
			doError = NIDAQ::DAQmxCfgSampClkTiming(
				taskHandleDOFinite,
				sampleClockSource,
				getSampleRate(),
				DAQmx_Val_Rising,
				DAQmx_Val_FiniteSamps,
				numSamples);
		}

		if (!DAQmxFailed(doError))
		{
			HeapBlock<NIDAQ::uInt8> digitalData(numSamples);
			int highSamples = 0;

			for (int sample = 0; sample < numSamples; ++sample)
			{
				float maxAbs = 0.0f;
				for (int ch = 0; ch < numChannels; ++ch)
					maxAbs = jmax(maxAbs, std::abs(buffer.getSample(ch, sample)));

				const bool high = maxAbs > 1.0e-6f;
				digitalData[sample] = high ? 1 : 0;
				if (high)
					++highSamples;
			}

			NIDAQ::int32 numDigitalSamplesWritten = 0;
			doError = NIDAQ::DAQmxWriteDigitalLines(
				taskHandleDOFinite,
				numSamples,
				0,
				timeout,
				DAQmx_Val_GroupByChannel,
				digitalData,
				&numDigitalSamplesWritten,
				NULL);

			if (!DAQmxFailed(doError))
			{
				digitalTaskReady = true;
				LOGD("NIDAQmx::writeAnalogWaveform wrote digitalLine=", digitalLine,
					 ", highSamples=", highSamples,
					 ", totalSamples=", numDigitalSamplesWritten);
			}
		}

		if (!digitalTaskReady)
		{
			if (DAQmxFailed(doError))
				NIDAQ::DAQmxGetExtendedErrorInfo(doErrBuff, ERR_BUFF_SIZE);
			LOGE("Failed to configure digital stimulus output, continuing with analog only: ", doErrBuff);
			clearDigitalFiniteTask();
		}
	}

	if (digitalTaskReady)
		DAQmxErrChk(NIDAQ::DAQmxStartTask(taskHandleDOFinite));

	DAQmxErrChk(NIDAQ::DAQmxStartTask(taskHandleAO));

	LOGC("Started finite analog waveform: channels=", numChannels,
		 ", samples=", numSamples,
		 ", sampleRate=", getSampleRate(),
		 ", duration=", numSamples / getSampleRate(), " seconds",
		 digitalTaskReady ? ", digitalLine=" + digitalLine : "");

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);

	return;
}
void NIDAQmx::addEvent(int64 sampleNumber, uint8 ttlLine, bool state)
{
	//TODO: Buffer events for synchronization
}

void NIDAQmx::run() 
{

	NIDAQ::int32 error = 0;
    char errBuff[2048] = { '\0' };

	int totalWrittenSamples = 0;
	float timeout = 10.0;

	NIDAQ::int32 writtenAnalogSamples = 0;
	NIDAQ::int32 writtenDigitalSamples = 0;

	int loopCount = 0;

	while (!threadShouldExit())
	{
		const int numChannels = numActiveAnalogOutputs;

		if (samplesPerChannel == 0)
		{
			Thread::sleep(1);
			continue;
		}

		// analogWrite produces 2*samplesPerChannel; consume half-chunks for headroom
		HeapBlock<NIDAQ::float64> analogData(numChannels * samplesPerChannel);
		analogOutBuffer->read(analogData, numChannels * samplesPerChannel);

		DAQmxErrChk(NIDAQ::DAQmxWriteAnalogF64(taskHandleAO,
			samplesPerChannel,
			0,
			timeout,
			DAQmx_Val_GroupByChannel,
			analogData,
			&writtenAnalogSamples,
			NULL
		));

		totalWrittenSamples += writtenAnalogSamples;

		loopCount++;

	}

	clearTasks();

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);

	return;
	
}

void NIDAQmx::digitalWrite(int channelIdx, bool state)
{

	NIDAQ::int32	error = 0;
	char			errBuff[ERR_BUFF_SIZE] = { '\0' };
	NIDAQ::int32 	write;
	NIDAQ::uInt8	eventData[1] = {0};

	if (channelIdx >= dout.size())
		return;

	if (dout[channelIdx]->isEnabled() && taskHandlesDO.size())
	{

		uint8 mask = 1 << channelIdx;
		if (state)
			eventData[0] = eventCode | mask;
		else
			eventData[0] = eventCode & ~mask;

		DAQmxErrChk(NIDAQ::DAQmxWriteDigitalU8(
			taskHandlesDO[0],
			1,
			1,
			10.0,
			DAQmx_Val_GroupByChannel,
			eventData,
			&write,
			nullptr
		));

		eventCode = eventData[0];

	}

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (DAQmxFailed(error))
		LOGE("DAQmx Error: ", errBuff);
	fflush(stdout);

	return;

}

uint32 NIDAQmx::getActiveDigitalLines()
{
	if (!getNumActiveDigitalOutputs())
		return 0;

	uint32 linesEnabled = 0;
	for (int i = 0; i < dout.size(); i++)
	{
		if (dout[i]->isEnabled())
			linesEnabled += pow(2, i);
	}
	return linesEnabled;
}
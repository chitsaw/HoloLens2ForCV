//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
// Portions of this code adapted from:
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/SensorScenario.h

#pragma once

#include "researchmode\ResearchModeApi.h"
#include "RMCameraReader.h"

class SensorScenario
{
public:
	SensorScenario(const std::vector<ResearchModeSensorType>& kEnabledSensorTypes);
	virtual ~SensorScenario();

	void InitializeSensorDevice();
	void InitializeSensors();
	winrt::Windows::Foundation::IAsyncAction StartRecordingAsync(const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& worldCoordSystem);
	void StopRecording();
	void GetRigNodeId(GUID& outGuid) const;
	static void CamAccessOnComplete(ResearchModeSensorConsent consent);

private:

	const std::vector<ResearchModeSensorType>& m_kEnabledSensorTypes;
	std::vector<std::shared_ptr<RMCameraReader>> m_cameraReaders;

	IResearchModeSensorDevice* m_pSensorDevice = nullptr;
	IResearchModeSensorDeviceConsent* m_pSensorDeviceConsent = nullptr;

	std::vector<ResearchModeSensorDescriptor> m_sensorDescriptors;
};

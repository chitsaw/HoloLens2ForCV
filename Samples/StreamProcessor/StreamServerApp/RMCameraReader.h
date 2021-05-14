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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/RMCameraReader.h

#pragma once

#include "researchmode\ResearchModeApi.h"
#include "SensorStreamServer.h"
#include "..\Utils\TimeConverter.h"

#include <mutex>
#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Preview.h>

class RMCameraReader
{
public:
	RMCameraReader(IResearchModeSensor* pLLSensor, const GUID& guid);
	virtual ~RMCameraReader();

	winrt::Windows::Foundation::IAsyncAction StartRecordingAsync(
		const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& coordSystem,
		const HANDLE camConsentGiven,
		const ResearchModeSensorConsent& camAccessConsent);

	void StopRecording();

private:
	// Thread for retrieving frames
	void CameraUpdateThread(const HANDLE camConsentGiven, const ResearchModeSensorConsent& camAccessConsent);

	// Thread for writing frames to disk
	void CameraWriteThread();

	bool IsNewTimestamp(IResearchModeSensorFrame* pSensorFrame);

	winrt::Windows::Foundation::IAsyncAction SendFrameAsync(IResearchModeSensorFrame* pSensorFrame);
	winrt::Windows::Foundation::IAsyncAction SendVLCFrameAsync(IResearchModeSensorFrame* pSensorFrame, IResearchModeSensorVLCFrame* pVLCFrame);
	winrt::Windows::Foundation::IAsyncAction SendDepthFrameAsync(
		IResearchModeSensorDepthFrame* pDepthFrame,
		const ResearchModeSensorResolution& resolution,
		const winrt::Windows::Foundation::Numerics::float4x4& cameraLocation,
		long long timestamp);

	void GetCalibrationData(int width, int height, DirectX::XMFLOAT4X4* pExtrinsics, std::vector<float>& imageToCameraMap);
	winrt::Windows::Foundation::IAsyncAction SendCalibrationDataAsync();

	void SetLocator(const GUID& guid);
	bool GetFrameLocation(winrt::Windows::Foundation::Numerics::float4x4* pLocation);

	// Mutex to access sensor frame
	std::mutex m_sensorFrameMutex;
	IResearchModeSensor* m_pRMSensor = nullptr;
	IResearchModeSensorFrame* m_pSensorFrame = nullptr;

	bool m_fExit = false;
	std::thread* m_pCameraUpdateThread = nullptr;
	std::thread* m_pWriteThread = nullptr;
	
	TimeConverter m_converter;
	UINT64 m_prevTimestamp = 0;

	winrt::Windows::Perception::Spatial::SpatialLocator m_locator = nullptr;
	winrt::Windows::Perception::Spatial::SpatialCoordinateSystem m_worldCoordSystem = nullptr;
	std::unique_ptr<SensorStreamServer> m_server;
	std::unique_ptr<SensorStreamServer> m_calibServer;
	ResearchModeSensorType m_sensorType;
};

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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/SensorScenario.cpp

#include "SensorScenario.h"

extern "C"
HMODULE LoadLibraryA(
	LPCSTR lpLibFileName
);

static ResearchModeSensorConsent camAccessCheck;
static HANDLE camConsentGiven;

SensorScenario::SensorScenario(const std::vector<ResearchModeSensorType>& kEnabledSensorTypes):
	m_kEnabledSensorTypes(kEnabledSensorTypes)
{
}

SensorScenario::~SensorScenario()
{
	if (m_pSensorDevice)
	{
		m_pSensorDevice->EnableEyeSelection();
		m_pSensorDevice->Release();
	}

	if (m_pSensorDeviceConsent)
	{
		m_pSensorDeviceConsent->Release();
	}
}

void SensorScenario::GetRigNodeId(GUID& outGuid) const
{
	IResearchModeSensorDevicePerception* pSensorDevicePerception;
	winrt::check_hresult(m_pSensorDevice->QueryInterface(IID_PPV_ARGS(&pSensorDevicePerception)));
	winrt::check_hresult(pSensorDevicePerception->GetRigNodeId(&outGuid));
	pSensorDevicePerception->Release();
}

void SensorScenario::InitializeSensorDevice()
{
	size_t sensorCount = 0;
	camConsentGiven = CreateEvent(nullptr, true, false, nullptr);

	// Load Research Mode library
	HMODULE hrResearchMode = LoadLibraryA("ResearchModeAPI");
	if (hrResearchMode)
	{
		typedef HRESULT(__cdecl* PFN_CREATEPROVIDER) (IResearchModeSensorDevice** ppSensorDevice);
		PFN_CREATEPROVIDER pfnCreate = reinterpret_cast<PFN_CREATEPROVIDER>(GetProcAddress(hrResearchMode, "CreateResearchModeSensorDevice"));
		if (pfnCreate)
		{
			winrt::check_hresult(pfnCreate(&m_pSensorDevice));
		}
	}

	// Request Sensor Consent
	winrt::check_hresult(m_pSensorDevice->QueryInterface(IID_PPV_ARGS(&m_pSensorDeviceConsent)));
	winrt::check_hresult(m_pSensorDeviceConsent->RequestCamAccessAsync(SensorScenario::CamAccessOnComplete));

	m_pSensorDevice->DisableEyeSelection();

	m_pSensorDevice->GetSensorCount(&sensorCount);
	m_sensorDescriptors.resize(sensorCount);

	m_pSensorDevice->GetSensorDescriptors(m_sensorDescriptors.data(), m_sensorDescriptors.size(), &sensorCount);
}

void SensorScenario::InitializeSensors()
{
	// Get RigNode id which will be used to initialize
	// the spatial locators for camera readers objects
	GUID guid;
	GetRigNodeId(guid);

	for (auto& sensorDescriptor : m_sensorDescriptors)
	{
		if (sensorDescriptor.sensorType == LEFT_FRONT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), LEFT_FRONT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}

			winrt::com_ptr<IResearchModeSensor> lfCameraSensor;
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, lfCameraSensor.put()));
			auto cameraReader = std::make_shared<RMCameraReader>(lfCameraSensor.get(), guid);
			m_cameraReaders.push_back(cameraReader);
		}

		if (sensorDescriptor.sensorType == RIGHT_FRONT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), RIGHT_FRONT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}

			winrt::com_ptr<IResearchModeSensor> rfCameraSensor;
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, rfCameraSensor.put()));
			auto cameraReader = std::make_shared<RMCameraReader>(rfCameraSensor.get(), guid);
			m_cameraReaders.push_back(cameraReader);
		}

		if (sensorDescriptor.sensorType == LEFT_LEFT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), LEFT_LEFT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}

			winrt::com_ptr<IResearchModeSensor> llCameraSensor;
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, llCameraSensor.put()));
			auto cameraReader = std::make_shared<RMCameraReader>(llCameraSensor.get(), guid);
			m_cameraReaders.push_back(cameraReader);
		}

		if (sensorDescriptor.sensorType == RIGHT_RIGHT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), RIGHT_RIGHT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}

			winrt::com_ptr<IResearchModeSensor> rrCameraSensor;
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, rrCameraSensor.put()));
			auto cameraReader = std::make_shared<RMCameraReader>(rrCameraSensor.get(), guid);
			m_cameraReaders.push_back(cameraReader);
		}

		if (sensorDescriptor.sensorType == DEPTH_LONG_THROW)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), DEPTH_LONG_THROW) == m_kEnabledSensorTypes.end())
			{
				continue;
			}

			winrt::com_ptr<IResearchModeSensor> ltSensor;
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, ltSensor.put()));
			auto cameraReader = std::make_shared<RMCameraReader>(ltSensor.get(), guid);
			m_cameraReaders.push_back(cameraReader);
		}

		if (sensorDescriptor.sensorType == DEPTH_AHAT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), DEPTH_AHAT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}

			winrt::com_ptr<IResearchModeSensor> ahatSensor;
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, ahatSensor.put()));
			auto cameraReader = std::make_shared<RMCameraReader>(ahatSensor.get(), guid);
			m_cameraReaders.push_back(cameraReader);
		}
	}	
}

void SensorScenario::CamAccessOnComplete(ResearchModeSensorConsent consent)
{
	camAccessCheck = consent;
	SetEvent(camConsentGiven);
}

winrt::Windows::Foundation::IAsyncAction SensorScenario::StartRecordingAsync(const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& worldCoordSystem)
{
	InitializeSensors();
	for (int i = 0; i < m_cameraReaders.size(); ++i)
	{
		co_await m_cameraReaders[i]->StartRecordingAsync(worldCoordSystem, camConsentGiven, camAccessCheck);
	}
}

void SensorScenario::StopRecording()
{
	m_cameraReaders.clear();
}

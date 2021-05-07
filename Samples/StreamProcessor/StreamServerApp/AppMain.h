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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/AppMain.h

#pragma once

#include "../Cannon/Common/Timer.h"
#include "../Cannon/DrawCall.h"
#include "../Cannon/FloatingSlate.h"
#include "../Cannon/FloatingText.h"
#include "../Cannon/MixedReality.h"
#include "../Cannon/TrackedHands.h"

#include "SensorScenario.h"
#include "SpatialMapper.h"
#include "VideoFrameProcessor.h"

class AppMain : public IFloatingSlateButtonCallback
{
public:

	AppMain();

	void Update();
	virtual void OnButtonPressed(FloatingSlateButton* pButton);
	
	void DrawObjects();
	void Render();

	winrt::Windows::Foundation::IAsyncAction StartRecordingAsync();
	winrt::Windows::Foundation::IAsyncAction StopRecordingAsync();

	static std::vector<ResearchModeSensorType> kEnabledRMStreamTypes;

private:
	winrt::Windows::Foundation::IAsyncAction InitializeVideoFrameProcessorAsync();
	bool IsVideoFrameProcessorWantedAndReady() const;
	void ReceiverThreadFunction();
	void ReceiveLabelsThreadFunction();

	MixedReality m_mixedReality;
	TrackedHands m_hands;

	FloatingSlate m_menu;
	FloatingText m_debugText;
	FloatingSlate m_poster;
	std::vector<std::unique_ptr<FloatingSlate>> m_objectLabels;
	size_t m_posterAnchor = 0;
	std::string m_debugString;
	std::vector<std::tuple<std::string, XMMATRIX>> m_labelData;
	std::mutex m_debugStringMutex;
	std::mutex m_labelDataMutex;

	std::unique_ptr<SensorScenario> m_scenario = nullptr;;

	std::unique_ptr<VideoFrameProcessor> m_videoFrameProcessor = nullptr;
	winrt::Windows::Foundation::IAsyncAction m_videoFrameProcessorOperation = nullptr;
	winrt::Windows::Foundation::IAsyncAction m_sendPositionOperation = nullptr;
	winrt::Windows::Foundation::IAsyncAction m_sendSpatialMapOperation = nullptr;

	Timer m_frameDeltaTimer;
	bool m_recording;

	std::thread* m_pReceiverThread = nullptr;
	std::thread* m_pLabelsReceiverThread = nullptr;
	std::unique_ptr<SensorStreamServer> m_headPoseServer;
	std::unique_ptr<SensorStreamServer> m_debugTextReceiver;
	std::unique_ptr<SensorStreamServer> m_objectLabelsReceiver;
	std::unique_ptr<SpatialMapper> m_spatialMapper;
	long long m_lastPositionTimestamp = 0;
	long long m_lastSpatialMapTimestamp = 0;
};

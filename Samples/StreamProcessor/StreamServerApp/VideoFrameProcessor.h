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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/VideoFrameProcessor.h

#pragma once

#include <MemoryBuffer.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Devices.Core.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Media.MediaProperties.h>
#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Preview.h>
#include "SensorStreamServer.h"
#include "..\Utils\TimeConverter.h"
#include <shared_mutex>
#include <thread>
#include <ppltasks.h>

class VideoFrameProcessor
{
public:
    VideoFrameProcessor();
    virtual ~VideoFrameProcessor();

    void SetLocator(const GUID& guid);

    winrt::Windows::Foundation::IAsyncAction InitializeAsync();

    winrt::Windows::Foundation::IAsyncAction StartRecordingAsync(
        const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& worldCoordSystem);

    winrt::Windows::Foundation::IAsyncAction StopRecordingAsync();

protected:
    void OnFrameArrived(
        const winrt::Windows::Media::Capture::Frames::MediaFrameReader& sender,
        const winrt::Windows::Media::Capture::Frames::MediaFrameArrivedEventArgs& args);

    void OnMixedRealityFrameArrived(
        const winrt::Windows::Media::Capture::Frames::MediaFrameReader& sender,
        const winrt::Windows::Media::Capture::Frames::MediaFrameArrivedEventArgs& args);

private:
    // writing thread
    void CameraWriteThread();
    void MixedRealityWriteThread();

    winrt::Windows::Foundation::IAsyncAction SendFrameAsync(
        const winrt::Windows::Graphics::Imaging::SoftwareBitmap& softwareBitmap,
        const winrt::Windows::Foundation::Numerics::float4x4& cameraLocation,
        long long timestamp);

    winrt::Windows::Foundation::IAsyncAction SendMixedRealityFrameAsync(
        const winrt::Windows::Graphics::Imaging::SoftwareBitmap& softwareBitmap,
        const winrt::Windows::Foundation::Numerics::float4x4& cameraLocation,
        long long timestamp);

    winrt::Windows::Foundation::IAsyncAction SendCalibrationDataAsync();

    winrt::Windows::Media::Capture::MediaCapture m_mediaCapture = nullptr;
    winrt::Windows::Media::Capture::Frames::MediaFrameReader m_mediaFrameReader = nullptr;
    winrt::Windows::Media::Capture::Frames::MediaFrameReader m_mixedRealityFrameReader = nullptr;
    winrt::event_token m_OnFrameArrivedRegistration;

    std::shared_mutex m_frameMutex;
    std::shared_mutex m_mixedRealityFrameMutex;
    long long m_previousCameraImageTimestamp = 0;
    long long m_previousMixedRealityImageTimestamp = 0;
    winrt::Windows::Media::Capture::Frames::MediaFrameReference m_latestFrame = nullptr;
    winrt::Windows::Media::Capture::Frames::MediaFrameReference m_latestMixedRealityFrame = nullptr;
    TimeConverter m_converter;
    winrt::Windows::Perception::Spatial::SpatialLocator m_locator = nullptr;
    winrt::Windows::Perception::Spatial::SpatialCoordinateSystem m_worldCoordSystem = nullptr;
    std::unique_ptr<SensorStreamServer> m_server;
    std::unique_ptr<SensorStreamServer> m_calibServer;
    std::unique_ptr<SensorStreamServer> m_mixedRealityServer;

    std::thread* m_pWriteThread = nullptr;
    std::thread* m_pMixedRealityWriteThread = nullptr;
    bool m_fExit = false;

    static const int kImageWidth;
    static const int kFrameRate;
};

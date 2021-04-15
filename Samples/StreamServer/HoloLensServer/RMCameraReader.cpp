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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/RMCameraReader.cpp

#include "RMCameraReader.h"
#include <sstream>

using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Perception;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception::Spatial::Preview;

namespace Depth
{
    enum InvalidationMasks
    {
        Invalid = 0x80,
    };
    static constexpr UINT16 AHAT_INVALID_VALUE = 4090;
}

RMCameraReader::RMCameraReader(IResearchModeSensor* pSensor, const GUID& guid)
{
    m_pRMSensor = pSensor;
    m_pRMSensor->AddRef();
    m_sensorType = m_pRMSensor->GetSensorType();
    m_pSensorFrame = nullptr;
    m_server = std::make_unique<SensorStreamServer>();
    m_server->ClientConnected([this] { m_pWriteThread = new std::thread(&RMCameraReader::CameraWriteThread, this); });
    m_calibServer = std::make_unique<SensorStreamServer>();
    m_calibServer->ClientConnected({ this, &RMCameraReader::SendCalibrationDataAsync });

    // Initialize the SpatialLocator using the GUID for the rigNode
    SetLocator(guid);
}

RMCameraReader::~RMCameraReader()
{
    StopRecording();
    m_pRMSensor->Release();
}

winrt::Windows::Foundation::IAsyncAction RMCameraReader::StartRecordingAsync(
    const SpatialCoordinateSystem& coordSystem,
    const HANDLE camConsentGiven,
    const ResearchModeSensorConsent& camAccessConsent)
{
    m_worldCoordSystem = coordSystem;

    m_fExit = false;
    m_pCameraUpdateThread = new std::thread(&RMCameraReader::CameraUpdateThread, this, camConsentGiven, camAccessConsent);

    // TODO: HARDCODED PORT NUMBERS
    co_await m_server->StartListeningAsync(30002);
    co_await m_calibServer->StartListeningAsync(30003);
}

void RMCameraReader::StopRecording()
{
    m_calibServer->StopListening();
    m_server->StopListening();

    m_fExit = true;
    m_pCameraUpdateThread->join();

    if (m_pWriteThread)
    {
        m_pWriteThread->join();
        m_pWriteThread = nullptr;
    }
}

void RMCameraReader::CameraUpdateThread(const HANDLE camConsentGiven, const ResearchModeSensorConsent& camAccessConsent)
{
	HRESULT hr = S_OK;

    DWORD waitResult = WaitForSingleObject(camConsentGiven, INFINITE);

    if (waitResult == WAIT_OBJECT_0)
    {
        switch (camAccessConsent)
        {
        case ResearchModeSensorConsent::Allowed:
            OutputDebugString(L"Access is granted");
            break;
        case ResearchModeSensorConsent::DeniedBySystem:
            OutputDebugString(L"Access is denied by the system");
            hr = E_ACCESSDENIED;
            break;
        case ResearchModeSensorConsent::DeniedByUser:
            OutputDebugString(L"Access is denied by the user");
            hr = E_ACCESSDENIED;
            break;
        case ResearchModeSensorConsent::NotDeclaredByApp:
            OutputDebugString(L"Capability is not declared in the app manifest");
            hr = E_ACCESSDENIED;
            break;
        case ResearchModeSensorConsent::UserPromptRequired:
            OutputDebugString(L"Capability user prompt required");
            hr = E_ACCESSDENIED;
            break;
        default:
            OutputDebugString(L"Access is denied by the system");
            hr = E_ACCESSDENIED;
            break;
        }
    }
    else
    {
        hr = E_UNEXPECTED;
    }

    if (SUCCEEDED(hr))
    {
        hr = m_pRMSensor->OpenStream();

        if (FAILED(hr))
        {
            m_pRMSensor->Release();
            m_pRMSensor = nullptr;
        }

        while (!m_fExit && m_pRMSensor)
        {
            HRESULT hr = S_OK;
            IResearchModeSensorFrame* pSensorFrame;

            hr = m_pRMSensor->GetNextBuffer(&pSensorFrame);

            if (SUCCEEDED(hr))
            {
                m_sensorFrameMutex.lock();
                if (m_pSensorFrame)
                {
                    m_pSensorFrame->Release();
                }
                m_pSensorFrame = pSensorFrame;
                m_sensorFrameMutex.unlock();
            }
        }

        if (m_pRMSensor)
        {
            m_pRMSensor->CloseStream();
        }
    }
}

void RMCameraReader::CameraWriteThread()
{
    std::vector<byte> depthPgmData;
    bool isLongThrow = (m_sensorType == ResearchModeSensorType::DEPTH_LONG_THROW);

    while (!m_fExit && m_server->IsClientConnected())
    {
        depthPgmData.clear();

        m_sensorFrameMutex.lock();
        if (m_pSensorFrame)
        {
            ResearchModeSensorTimestamp timestamp;
            winrt::check_hresult(m_pSensorFrame->GetTimeStamp(&timestamp));

            if (timestamp.HostTicks != m_prevTimestamp)
            {
                m_prevTimestamp = timestamp.HostTicks;

                // Get resolution
                ResearchModeSensorResolution resolution;
                winrt::check_hresult(m_pSensorFrame->GetResolution(&resolution));

                IResearchModeSensorDepthFrame* pDepthFrame = nullptr;
                winrt::check_hresult(m_pSensorFrame->QueryInterface(IID_PPV_ARGS(&pDepthFrame)));
                if (pDepthFrame)
                {
                    // Get location
                    float4x4 location;
                    if (GetFrameLocation(&location))
                    {
                        const UINT16* pDepth = nullptr;
                        size_t outDepthBufferCount = 0;

                        const BYTE* pSigma = nullptr;
                        size_t outSigmaBufferCount = 0;

                        if (isLongThrow)
                        {
                            winrt::check_hresult(pDepthFrame->GetSigmaBuffer(&pSigma, &outSigmaBufferCount));
                        }

                        winrt::check_hresult(pDepthFrame->GetBuffer(&pDepth, &outDepthBufferCount));

                        // Prepare the data to save for Depth
                        depthPgmData.reserve(sizeof(resolution) + sizeof(location) + outDepthBufferCount * sizeof(UINT16));
                        depthPgmData.insert(depthPgmData.end(), (byte*)&resolution, (byte*)&resolution + sizeof(resolution));
                        depthPgmData.insert(depthPgmData.end(), (byte*)&location, (byte*)&location + sizeof(location));

                        if (isLongThrow)
                        {
                            assert(outDepthBufferCount == outSigmaBufferCount);
                        }

                        // Validate depth
                        for (size_t i = 0; i < outDepthBufferCount; ++i)
                        {
                            UINT16 d;
                            const bool invalid = isLongThrow ?
                                ((pSigma[i] & Depth::InvalidationMasks::Invalid) > 0) :
                                (pDepth[i] >= Depth::AHAT_INVALID_VALUE);

                            if (invalid)
                            {
                                d = 0;
                            }
                            else
                            {
                                d = pDepth[i];
                            }

                            depthPgmData.push_back((BYTE)d);
                            depthPgmData.push_back((BYTE)(d >> 8));
                        }
                    }

                    pDepthFrame->Release();
                }
            }
        }
        m_sensorFrameMutex.unlock();

        if (depthPgmData.size() > 0)
        {
            // Send the depth frame data
            long long ticks = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds((long long)m_prevTimestamp)).count();
            m_server->SendDataFrameAsync(&depthPgmData[0], depthPgmData.size(), ticks).get();
        }
    }
}

void RMCameraReader::GetCalibrationData(int width, int height, DirectX::XMFLOAT4X4* pExtrinsics, std::vector<float>& imageToCameraMap)
{   
    // Get camera sensor object
    IResearchModeCameraSensor* pCameraSensor = nullptr;    
    HRESULT hr = m_pRMSensor->QueryInterface(IID_PPV_ARGS(&pCameraSensor));
    winrt::check_hresult(hr);

    // Get extrinsics (rotation and translation) with respect to the rigNode
    pCameraSensor->GetCameraExtrinsicsMatrix(pExtrinsics);
    
    // Compute LUT
    float uv[2];
    float xy[2];
    imageToCameraMap.clear();
    imageToCameraMap.resize(size_t(width) * size_t(height) * 3);
    auto pLutTable = imageToCameraMap.data();

    for (size_t y = 0; y < height; y++)
    {
        uv[1] = (y + 0.5f);
        for (size_t x = 0; x < width; x++)
        {
            uv[0] = (x + 0.5f);
            hr = pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
            if (FAILED(hr))
            {
				*pLutTable++ = xy[0];
				*pLutTable++ = xy[1];
				*pLutTable++ = 0.f;
                continue;
            }
            float z = 1.0f;
            const float norm = sqrtf(xy[0] * xy[0] + xy[1] * xy[1] + z * z);
            const float invNorm = 1.0f / norm;
            xy[0] *= invNorm;
            xy[1] *= invNorm;
            z *= invNorm;

            // Dump LUT row
            *pLutTable++ = xy[0];
            *pLutTable++ = xy[1];
            *pLutTable++ = z;
        }
    }

    pCameraSensor->Release();
}

winrt::Windows::Foundation::IAsyncAction RMCameraReader::SendCalibrationDataAsync()
{
    ResearchModeSensorResolution resolution;
    ResearchModeSensorTimestamp timestamp;
    {
        std::lock_guard<std::mutex> guard(m_sensorFrameMutex);
        assert(m_pSensorFrame != nullptr);
        winrt::check_hresult(m_pSensorFrame->GetResolution(&resolution));
        winrt::check_hresult(m_pSensorFrame->GetTimeStamp(&timestamp));
    }

    // Get extrinsics
    DirectX::XMFLOAT4X4 extrinsics;
    std::vector<float> imageToCameraMap;
    GetCalibrationData(resolution.Width, resolution.Height, &extrinsics, imageToCameraMap);
    std::vector<byte> calibrationData;

    calibrationData.reserve(sizeof(resolution.Width) + sizeof(resolution.Height) + imageToCameraMap.size() * sizeof(float) + sizeof(extrinsics));
    calibrationData.insert(calibrationData.end(), (byte*)&resolution.Width, (byte*)&resolution.Width + sizeof(resolution.Width));
    calibrationData.insert(calibrationData.end(), (byte*)&resolution.Height, (byte*)&resolution.Height + sizeof(resolution.Height));
    calibrationData.insert(calibrationData.end(), (byte*)&imageToCameraMap[0], (byte*)&imageToCameraMap[0] + imageToCameraMap.size() * sizeof(float));
    calibrationData.insert(calibrationData.end(), (byte*)&extrinsics, (byte*)&extrinsics + sizeof(extrinsics));

    auto ticks = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds(timestamp.HostTicks));

    // Send the calibration frame
    co_await m_calibServer->SendDataFrameAsync(&calibrationData[0], calibrationData.size(), ticks.count());
}

void RMCameraReader::SetLocator(const GUID& guid)
{
    m_locator = SpatialGraphInteropPreview::CreateLocatorForNode(guid);
}

bool RMCameraReader::IsNewTimestamp(IResearchModeSensorFrame* pSensorFrame)
{
    ResearchModeSensorTimestamp timestamp;
    winrt::check_hresult(pSensorFrame->GetTimeStamp(&timestamp));

    if (m_prevTimestamp == timestamp.HostTicks)
    {
        return false;
    }

    m_prevTimestamp = timestamp.HostTicks;

    return true;
}

winrt::Windows::Foundation::IAsyncAction RMCameraReader::SendDepthFrameAsync(
    IResearchModeSensorDepthFrame* pDepthFrame,
    const ResearchModeSensorResolution& resolution,
    const float4x4& cameraLocation,
    long long timestamp)
{
    bool isLongThrow = (m_pRMSensor->GetSensorType() == DEPTH_LONG_THROW);

    const UINT16* pDepth = nullptr;
    size_t outDepthBufferCount = 0;

    const BYTE* pSigma = nullptr;
    size_t outSigmaBufferCount = 0;

    if (isLongThrow)
    {
        winrt::check_hresult(pDepthFrame->GetSigmaBuffer(&pSigma, &outSigmaBufferCount));
    }

    winrt::check_hresult(pDepthFrame->GetBuffer(&pDepth, &outDepthBufferCount));

    // Prepare the data to save for Depth
    std::vector<byte> depthPgmData;
    depthPgmData.reserve(sizeof(resolution) + sizeof(cameraLocation) + outDepthBufferCount * sizeof(UINT16));
    depthPgmData.insert(depthPgmData.end(), (byte*)&resolution, (byte*)&resolution + sizeof(resolution));
    depthPgmData.insert(depthPgmData.end(), (byte*)&cameraLocation, (byte*)&cameraLocation + sizeof(cameraLocation));

    if (isLongThrow)
    {
        assert(outDepthBufferCount == outSigmaBufferCount);
    }

    // Validate depth
    for (size_t i = 0; i < outDepthBufferCount; ++i)
    {
        UINT16 d;
        const bool invalid = isLongThrow ?
            ((pSigma[i] & Depth::InvalidationMasks::Invalid) > 0) :
            (pDepth[i] >= Depth::AHAT_INVALID_VALUE);

        if (invalid)
        {
            d = 0;
        }
        else
        {
            d = pDepth[i];
        }

        depthPgmData.push_back((BYTE)d);
        depthPgmData.push_back((BYTE)(d >> 8));
    }

    // Send the depth frame
    co_await m_server->SendDataFrameAsync(&depthPgmData[0], depthPgmData.size(), timestamp);
}

winrt::Windows::Foundation::IAsyncAction RMCameraReader::SendVLCFrameAsync(IResearchModeSensorFrame* pSensorFrame, IResearchModeSensorVLCFrame* pVLCFrame)
{        
    // Get resolution
    ResearchModeSensorResolution resolution;
    winrt::check_hresult(pSensorFrame->GetResolution(&resolution));

    HundredsOfNanoseconds timestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds((long long)m_prevTimestamp));

    // Convert the software bitmap to raw bytes    
    std::vector<BYTE> pgmData;
    size_t outBufferCount = 0;
    const BYTE* pImage = nullptr;

    winrt::check_hresult(pVLCFrame->GetBuffer(&pImage, &outBufferCount));

    pgmData.reserve(sizeof(resolution) + outBufferCount);
    pgmData.insert(pgmData.end(), (byte*)&resolution, (byte*)&resolution + sizeof(resolution));
    pgmData.insert(pgmData.end(), pImage, pImage + outBufferCount);

    // Send the VLC frame
    co_await m_server->SendDataFrameAsync(&pgmData[0], pgmData.size(), timestamp.count());
}

winrt::Windows::Foundation::IAsyncAction RMCameraReader::SendFrameAsync(IResearchModeSensorFrame* pSensorFrame)
{
    HundredsOfNanoseconds timestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds((long long)m_prevTimestamp));

    // Get resolution
    ResearchModeSensorResolution resolution;
    winrt::check_hresult(pSensorFrame->GetResolution(&resolution));

    IResearchModeSensorVLCFrame* pVLCFrame = nullptr;
    IResearchModeSensorDepthFrame* pDepthFrame = nullptr;

    HRESULT hr = pSensorFrame->QueryInterface(IID_PPV_ARGS(&pVLCFrame));

    if (FAILED(hr))
    {
        hr = pSensorFrame->QueryInterface(IID_PPV_ARGS(&pDepthFrame));
    }

    if (pVLCFrame)
    {
        co_await SendVLCFrameAsync(pSensorFrame, pVLCFrame);
        pVLCFrame->Release();
    }

    if (pDepthFrame)
    {
        // Get location
        float4x4 location;
        if (GetFrameLocation(&location))
        {
            co_await SendDepthFrameAsync(
                pDepthFrame,
                resolution,
                location,
                timestamp.count());
            pDepthFrame->Release();
        }
    }
}

bool RMCameraReader::GetFrameLocation(float4x4* pLocation)
{         
    auto timestamp = PerceptionTimestampHelper::FromSystemRelativeTargetTime(HundredsOfNanoseconds(checkAndConvertUnsigned(m_prevTimestamp)));
    auto location = m_locator.TryLocateAtTimestamp(timestamp, m_worldCoordSystem);
    if (!location)
    {
        return false;
    }

    *pLocation = make_float4x4_from_quaternion(location.Orientation()) * make_float4x4_translation(location.Position());
    return true;
}

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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/VideoFrameProcessor.cpp

#include "VideoFrameProcessor.h"
#include "MrcVideoEffectDefinition.h"
#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Capture;
using namespace winrt::Windows::Media::Capture::Frames;
using namespace winrt::Windows::Media::Devices::Core;
using namespace winrt::Windows::Networking::Sockets;
using namespace winrt::Windows::Perception;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception::Spatial::Preview;

const int VideoFrameProcessor::kImageWidth = 1280;
const int VideoFrameProcessor::kFrameRate = 15;

VideoFrameProcessor::VideoFrameProcessor()
{
    m_server = std::make_unique<SensorStreamServer>();
    m_server->ClientConnected([this] { m_pWriteThread = new std::thread(&VideoFrameProcessor::CameraWriteThread, this); });

    m_calibServer = std::make_unique<SensorStreamServer>();
    m_calibServer->ClientConnected({ this, &VideoFrameProcessor::SendCalibrationDataAsync });

    m_mixedRealityServer = std::make_unique<SensorStreamServer>();
    m_mixedRealityServer->ClientConnected([this] { m_pMixedRealityWriteThread = new std::thread(&VideoFrameProcessor::MixedRealityWriteThread, this); });
}

VideoFrameProcessor::~VideoFrameProcessor()
{
    StopRecordingAsync();
}

void VideoFrameProcessor::SetLocator(const GUID& guid)
{
    m_locator = SpatialGraphInteropPreview::CreateLocatorForNode(guid);
}

winrt::Windows::Foundation::IAsyncAction VideoFrameProcessor::InitializeAsync()
{
    auto mediaFrameSourceGroups{ co_await MediaFrameSourceGroup::FindAllAsync() };

    MediaFrameSourceGroup selectedSourceGroup = nullptr;
    MediaCaptureVideoProfile profile = nullptr;
    MediaCaptureVideoProfileMediaDescription desc = nullptr;
    std::vector<MediaFrameSourceInfo> selectedSourceInfos;

    // Find MediaFrameSourceGroup
    for (const MediaFrameSourceGroup& mediaFrameSourceGroup : mediaFrameSourceGroups)
    {
        auto knownProfiles = MediaCapture::FindKnownVideoProfiles(mediaFrameSourceGroup.Id(), KnownVideoProfile::VideoConferencing);
        for (const auto& knownProfile : knownProfiles)
        {
            for (auto knownDesc : knownProfile.SupportedRecordMediaDescription())
            {
                if ((knownDesc.Width() == kImageWidth) && (std::round(knownDesc.FrameRate()) == kFrameRate))
                {
                    profile = knownProfile;
                    desc = knownDesc;
                    selectedSourceGroup = mediaFrameSourceGroup;
                    break;
                }
            }
        }
    }

    winrt::check_bool(selectedSourceGroup != nullptr);

    for (auto sourceInfo : selectedSourceGroup.SourceInfos())
    {
        // Workaround since multiple Color sources can be found,
        // and not all of them are necessarily compatible with the selected video profile
        if (sourceInfo.SourceKind() == MediaFrameSourceKind::Color)
        {
            selectedSourceInfos.push_back(sourceInfo);
        }
    }
    winrt::check_bool(!selectedSourceInfos.empty());
    
    // Initialize a MediaCapture object
    MediaCaptureInitializationSettings settings;
    settings.VideoProfile(profile);
    settings.RecordMediaDescription(desc);
    settings.VideoDeviceId(selectedSourceGroup.Id());
    settings.StreamingCaptureMode(StreamingCaptureMode::Video);
    settings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);
    settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
    settings.SourceGroup(selectedSourceGroup);

    m_mediaCapture = MediaCapture();
    co_await m_mediaCapture.InitializeAsync(settings);

    MediaFrameSource selectedSource = nullptr;
    MediaFrameFormat preferredFormat = nullptr;

    for (MediaFrameSourceInfo sourceInfo : selectedSourceInfos)
    {
        if (sourceInfo.MediaStreamType() == winrt::Windows::Media::Capture::MediaStreamType::VideoRecord)
        {
            auto tmpSource = m_mediaCapture.FrameSources().Lookup(sourceInfo.Id());
            for (MediaFrameFormat format : tmpSource.SupportedFormats())
            {
                auto frameRate = (double)format.FrameRate().Numerator() / (double)format.FrameRate().Denominator();
                if (format.VideoFormat().Width() == kImageWidth && std::round(frameRate) == kFrameRate)
                {
                    selectedSource = tmpSource;
                    preferredFormat = format;
                    break;
                }
            }
        }
    }

    winrt::check_bool(preferredFormat != nullptr);

    co_await selectedSource.SetFormatAsync(preferredFormat);

    m_mediaFrameReader = co_await m_mediaCapture.CreateFrameReaderAsync(selectedSource);
    m_OnFrameArrivedRegistration = m_mediaFrameReader.FrameArrived({ this, &VideoFrameProcessor::OnFrameArrived });

    // Now add the mixed-reality effect to the VideoPreview streams so we can capture the video with holograms
    auto mrcVideoEffectDefinition = winrt::make<MrcVideoEffectDefinition>();
    co_await m_mediaCapture.AddVideoEffectAsync(mrcVideoEffectDefinition, MediaStreamType::VideoPreview);

    MediaFrameSource selectedSource2 = nullptr;
    MediaFrameFormat preferredFormat2 = nullptr;
    for (MediaFrameSourceInfo sourceInfo : selectedSourceInfos)
    {
        if (sourceInfo.MediaStreamType() == winrt::Windows::Media::Capture::MediaStreamType::VideoPreview)
        {
            auto tmpSource = m_mediaCapture.FrameSources().Lookup(sourceInfo.Id());
            for (MediaFrameFormat format : tmpSource.SupportedFormats())
            {
                auto frameRate = (double)format.FrameRate().Numerator() / (double)format.FrameRate().Denominator();
                if (format.VideoFormat().Width() == kImageWidth && std::round(frameRate) == kFrameRate)
                {
                    selectedSource2 = tmpSource;
                    preferredFormat2 = format;
                    break;
                }
            }
        }
    }

    winrt::check_bool(preferredFormat2 != nullptr);
    co_await selectedSource2.SetFormatAsync(preferredFormat2);

    m_mixedRealityFrameReader = co_await m_mediaCapture.CreateFrameReaderAsync(selectedSource2);
    m_mixedRealityFrameReader.FrameArrived({ this, &VideoFrameProcessor::OnMixedRealityFrameArrived });
}

winrt::Windows::Foundation::IAsyncAction VideoFrameProcessor::StartRecordingAsync(const SpatialCoordinateSystem& worldCoordSystem)
{
    auto status = co_await m_mediaFrameReader.StartAsync();
    winrt::check_bool(status == MediaFrameReaderStartStatus::Success);

    status = co_await m_mixedRealityFrameReader.StartAsync();
    winrt::check_bool(status == MediaFrameReaderStartStatus::Success);

    m_worldCoordSystem = worldCoordSystem;
    m_fExit = false;
    co_await m_server->StartListeningAsync(30000);
    co_await m_calibServer->StartListeningAsync(30001);
    co_await m_mixedRealityServer->StartListeningAsync(30006);
}

winrt::Windows::Foundation::IAsyncAction VideoFrameProcessor::StopRecordingAsync()
{
    m_mixedRealityServer->StopListening();
    m_calibServer->StopListening();
    m_server->StopListening();
    m_fExit = true;
    if (m_pMixedRealityWriteThread)
    {
        m_pMixedRealityWriteThread->join();
        m_pMixedRealityWriteThread = nullptr;
    }
    if (m_pWriteThread)
    {
        m_pWriteThread->join();
        m_pWriteThread = nullptr;
    }

    co_await m_mixedRealityFrameReader.StopAsync();
    co_await m_mediaFrameReader.StopAsync();
}

void VideoFrameProcessor::OnFrameArrived(const MediaFrameReader& sender, const MediaFrameArrivedEventArgs& args)
{
    if (MediaFrameReference frame = sender.TryAcquireLatestFrame())
    {
        std::lock_guard<std::shared_mutex> lock(m_frameMutex);
        m_latestFrame = frame;
    }
}

void VideoFrameProcessor::OnMixedRealityFrameArrived(const MediaFrameReader& sender, const MediaFrameArrivedEventArgs& args)
{
    if (MediaFrameReference frame = sender.TryAcquireLatestFrame())
    {
        std::lock_guard<std::shared_mutex> lock(m_mixedRealityFrameMutex);
        m_latestMixedRealityFrame = frame;
    }
}

winrt::Windows::Foundation::IAsyncAction VideoFrameProcessor::SendFrameAsync(
    const SoftwareBitmap& softwareBitmap,
    const float4x4& cameraLocation,
    long long timestamp)
{
    int imageWidth = softwareBitmap.PixelWidth();
    int imageHeight = softwareBitmap.PixelHeight();

    // Compress the bitmap to JPEG
    winrt::Windows::Storage::Streams::InMemoryRandomAccessStream jpegStream;
    auto encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::JpegEncoderId(), jpegStream);
    encoder.SetSoftwareBitmap(softwareBitmap);
    co_await encoder.FlushAsync();

    // Get the JPEG data
    uint32_t jpegDataLength = (uint32_t)jpegStream.Size();
    winrt::Windows::Storage::Streams::Buffer buffer(jpegDataLength);
    co_await jpegStream.ReadAsync(buffer, jpegDataLength, winrt::Windows::Storage::Streams::InputStreamOptions::None);
    uint8_t* jpegData = buffer.data();

    // Send the compressed JPEG image frame
    m_server->NewDataFrame();
    m_server->AppendDataFrame((byte*)&imageWidth, sizeof(imageWidth));
    m_server->AppendDataFrame((byte*)&imageHeight, sizeof(imageHeight));
    m_server->AppendDataFrame((byte*)&cameraLocation, sizeof(cameraLocation));
    m_server->AppendDataFrame(jpegData, jpegDataLength);
    co_await m_server->SendDataFrameAsync(timestamp);
}

winrt::Windows::Foundation::IAsyncAction VideoFrameProcessor::SendMixedRealityFrameAsync(
    const SoftwareBitmap& softwareBitmap,
    const float4x4& cameraLocation,
    long long timestamp)
{
    int imageWidth = softwareBitmap.PixelWidth();
    int imageHeight = softwareBitmap.PixelHeight();

    // Compress the bitmap to JPEG
    winrt::Windows::Storage::Streams::InMemoryRandomAccessStream jpegStream;
    auto encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::JpegEncoderId(), jpegStream);
    encoder.SetSoftwareBitmap(softwareBitmap);
    co_await encoder.FlushAsync();

    // Get the JPEG data
    uint32_t jpegDataLength = (uint32_t)jpegStream.Size();
    winrt::Windows::Storage::Streams::Buffer buffer(jpegDataLength);
    co_await jpegStream.ReadAsync(buffer, jpegDataLength, winrt::Windows::Storage::Streams::InputStreamOptions::None);
    uint8_t* jpegData = buffer.data();

    // Send the compressed JPEG image frame
    m_mixedRealityServer->NewDataFrame();
    m_mixedRealityServer->AppendDataFrame((byte*)&imageWidth, sizeof(imageWidth));
    m_mixedRealityServer->AppendDataFrame((byte*)&imageHeight, sizeof(imageHeight));
    m_mixedRealityServer->AppendDataFrame((byte*)&cameraLocation, sizeof(cameraLocation));
    m_mixedRealityServer->AppendDataFrame(jpegData, jpegDataLength);
    co_await m_mixedRealityServer->SendDataFrameAsync(timestamp);
}

winrt::Windows::Foundation::IAsyncAction VideoFrameProcessor::SendCalibrationDataAsync()
{
    m_frameMutex.lock();
    assert(m_latestFrame != nullptr);
    long long timestamp = m_latestFrame.SystemRelativeTime().Value().count();

    auto focalLength = m_latestFrame.VideoMediaFrame().CameraIntrinsics().FocalLength();
    auto principalPoint = m_latestFrame.VideoMediaFrame().CameraIntrinsics().PrincipalPoint();
    auto radialDistortion = m_latestFrame.VideoMediaFrame().CameraIntrinsics().RadialDistortion();
    auto tangentialDistortion = m_latestFrame.VideoMediaFrame().CameraIntrinsics().TangentialDistortion();
    auto imageHeight = m_latestFrame.VideoMediaFrame().CameraIntrinsics().ImageHeight();
    auto imageWidth = m_latestFrame.VideoMediaFrame().CameraIntrinsics().ImageWidth();

    float4x4 cameraExtrinsics = float4x4::identity();
    if (m_locator)
    {
        // If ResearchMode is enabled, get the camera extrinsics relative to the rig node
        auto rig2cam = m_locator.TryLocateAtTimestamp(
            PerceptionTimestampHelper::FromSystemRelativeTargetTime(HundredsOfNanoseconds(timestamp)),
            m_latestFrame.CoordinateSystem());
        cameraExtrinsics = make_float4x4_from_quaternion(rig2cam.Orientation()) * make_float4x4_translation(rig2cam.Position());
    }

    m_frameMutex.unlock();

    std::vector<byte> calibrationData;
    calibrationData.reserve(
        sizeof(imageWidth) +
        sizeof(imageHeight) +
        sizeof(focalLength) +
        sizeof(principalPoint) +
        sizeof(radialDistortion) +
        sizeof(tangentialDistortion) +
        sizeof(cameraExtrinsics));

    calibrationData.insert(calibrationData.end(), (byte*)&imageWidth, (byte*)&imageWidth + sizeof(imageWidth));
    calibrationData.insert(calibrationData.end(), (byte*)&imageHeight, (byte*)&imageHeight + sizeof(imageHeight));
    calibrationData.insert(calibrationData.end(), (byte*)&focalLength, (byte*)&focalLength + sizeof(focalLength));
    calibrationData.insert(calibrationData.end(), (byte*)&principalPoint, (byte*)&principalPoint + sizeof(principalPoint));
    calibrationData.insert(calibrationData.end(), (byte*)&radialDistortion, (byte*)&radialDistortion + sizeof(radialDistortion));
    calibrationData.insert(calibrationData.end(), (byte*)&tangentialDistortion, (byte*)&tangentialDistortion + sizeof(tangentialDistortion));
    calibrationData.insert(calibrationData.end(), (byte*)&cameraExtrinsics, (byte*)&cameraExtrinsics + sizeof(cameraExtrinsics));

    auto ticks = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds(timestamp));

    // Send the calibration frame
    co_await m_calibServer->SendDataFrameAsync(&calibrationData[0], calibrationData.size(), ticks.count());
}

void VideoFrameProcessor::CameraWriteThread()
{
    while (!m_fExit && m_server->IsClientConnected())
    {
        winrt::Windows::Foundation::Numerics::float4x4 cameraLocation{};
        SoftwareBitmap softwareBitmap = nullptr;
        long long timestamp = 0;
 
        m_frameMutex.lock();
        if (m_latestFrame != nullptr)
        {
            timestamp = m_latestFrame.SystemRelativeTime().Value().count();
            if (timestamp != m_previousCameraImageTimestamp)
            {
                softwareBitmap = SoftwareBitmap::Convert(m_latestFrame.VideoMediaFrame().SoftwareBitmap(), BitmapPixelFormat::Bgra8);
                m_previousCameraImageTimestamp = timestamp;

                auto cameraFrame = m_latestFrame.CoordinateSystem();

                // get the camera location in the world
                if (auto pvToWorld = cameraFrame.TryGetTransformTo(m_worldCoordSystem))
                {
                    cameraLocation = pvToWorld.Value();
                }
            }
        }
        m_frameMutex.unlock();
        
        // write the bitmap
        if (softwareBitmap != nullptr)
        {
            timestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds(timestamp)).count();
            SendFrameAsync(
                softwareBitmap,
                cameraLocation,
                timestamp).get();
        }
    }
}

void VideoFrameProcessor::MixedRealityWriteThread()
{
    while (!m_fExit && m_mixedRealityServer->IsClientConnected())
    {
        winrt::Windows::Foundation::Numerics::float4x4 cameraLocation{};
        SoftwareBitmap softwareBitmap = nullptr;
        long long timestamp = 0;

        m_mixedRealityFrameMutex.lock();
        if (m_latestMixedRealityFrame != nullptr)
        {
            timestamp = m_latestMixedRealityFrame.SystemRelativeTime().Value().count();
            if (timestamp != m_previousMixedRealityImageTimestamp)
            {
                softwareBitmap = SoftwareBitmap::Convert(m_latestMixedRealityFrame.VideoMediaFrame().SoftwareBitmap(), BitmapPixelFormat::Bgra8);
                m_previousMixedRealityImageTimestamp = timestamp;

                auto cameraFrame = m_latestMixedRealityFrame.CoordinateSystem();

                // get the camera location in the world
                if (auto pvToWorld = cameraFrame.TryGetTransformTo(m_worldCoordSystem))
                {
                    cameraLocation = pvToWorld.Value();
                }
            }
        }
        m_mixedRealityFrameMutex.unlock();

        // write the bitmap
        if (softwareBitmap != nullptr)
        {
            timestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds(timestamp)).count();
            SendMixedRealityFrameAsync(
                softwareBitmap,
                cameraLocation,
                timestamp).get();
        }
    }
}

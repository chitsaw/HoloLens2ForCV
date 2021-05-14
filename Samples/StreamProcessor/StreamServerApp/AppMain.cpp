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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/AppMain.cpp

#include "AppMain.h"
#include <winrt/Windows.Foundation.h>
#include <ctime>

using namespace std;
using namespace DirectX;
using namespace winrt::Windows::Storage;

enum class ButtonID
{
    Start,
    Stop
};

// Set streams to capture
// Supported ResearchMode streams are:
//   LEFT_FRONT,
//	 LEFT_LEFT,
//	 RIGHT_FRONT,
//   RIGHT_RIGHT,
//   DEPTH_AHAT,
//   DEPTH_LONG_THROW
// Note that concurrent access to AHAT and Long Throw is currently not supported
std::vector<ResearchModeSensorType> AppMain::kEnabledRMStreamTypes = { DEPTH_LONG_THROW };

AppMain::AppMain() :
    m_recording(false)
{
    DrawCall::vAmbient = XMVectorSet(.25f, .25f, .25f, 1.f);
    DrawCall::vLights[0].vLightPosW = XMVectorSet(0.0f, 1.0f, 0.0f, 0.f);
    DrawCall::PushBackfaceCullingState(false);

    m_mixedReality.EnableMixedReality();
    m_mixedReality.EnableEyeTracking();

    const float rootMenuHeight = 0.03f;
    XMVECTOR mainButtonSize = XMVectorSet(0.04f, rootMenuHeight, 0.01f, 0.0f);

    m_menu.HideTitleBar();
    m_menu.SetColor(XMVectorZero());
    m_menu.SetSize(XMVectorZero());

    m_menu.AddButton(make_shared<FloatingSlateButton>(XMVectorSet(-0.0225f, 0.0f, 0.0f, 1.0f), mainButtonSize, XMVectorSet(0.0f, 0.5f, 0.0f, 1.0f), (unsigned)ButtonID::Start, this, "Start"));
    m_menu.AddButton(make_shared<FloatingSlateButton>(XMVectorSet(0.0225f, 0.0f, 0.0f, 1.0f), mainButtonSize, XMVectorSet(0.5f, 0.0f, 0.0f, 1.0f), (unsigned)ButtonID::Stop, this, "Stop"));

    // Initialize debug text size
    m_debugText.SetSize(XMVectorSet(0.40f, 0.60f, 1.0f, 1.0f));
    m_debugText.SetFontSize(128.0f);

    if (AppMain::kEnabledRMStreamTypes.size() > 0)
    {
        // Enable SensorScenario for ResearchMode streams
        m_scenario = std::make_unique<SensorScenario>(kEnabledRMStreamTypes);
        m_scenario->InitializeSensorDevice();
    }	

    m_headPoseServer = make_unique<SensorStreamServer>();
    m_debugTextReceiver = make_unique<SensorStreamServer>();
    m_debugTextReceiver->ClientConnected([this] { m_pReceiverThread = new std::thread(&AppMain::ReceiverThreadFunction, this); });
    m_objectLabelsReceiver = make_unique<SensorStreamServer>();
    m_objectLabelsReceiver->ClientConnected([this] { m_pLabelsReceiverThread = new std::thread(&AppMain::ReceiveLabelsThreadFunction, this); });

    m_videoFrameProcessorOperation = InitializeVideoFrameProcessorAsync();
}

void AppMain::Update()
{
    float frameDelta = m_frameDeltaTimer.GetTime();
    m_frameDeltaTimer.Reset();

    m_mixedReality.Update();
    m_hands.UpdateFromMixedReality(m_mixedReality);

    auto startButton = m_menu.GetButton((unsigned)ButtonID::Start);
    auto stopButton = m_menu.GetButton((unsigned)ButtonID::Stop);
    startButton->SetDisabled(m_recording || (!IsVideoFrameProcessorWantedAndReady()));
    stopButton->SetDisabled(!m_recording);

    // Get the head and eye gaze vectors
    const XMVECTOR headPosition = m_mixedReality.GetHeadPosition();
    const XMVECTOR headForward = m_mixedReality.GetHeadForwardDirection();
    const XMVECTOR headUp = m_mixedReality.GetHeadUpDirection();
    const XMVECTOR headRight = XMVector3Cross(headUp, -headForward);
    const XMVECTOR eyeGazeDirection = m_mixedReality.GetEyeGazeDirection();
    const XMVECTOR eyeGazeOrigin = m_mixedReality.GetEyeGazeOrigin();

    m_menu.Update(frameDelta, m_hands);

    if (m_recording)
    {		
        // Get hand joints transforms
        std::array<DirectX::XMMATRIX, (size_t)HandJointIndex::Count> leftHandTransform{};
        std::array<DirectX::XMMATRIX, (size_t)HandJointIndex::Count> rightHandTransform{};

        if (m_hands.IsHandTracked(0)) // left hand
        {
            for (int j = 0; j < (int)HandJointIndex::Count; ++j)
            {
                leftHandTransform[j] = m_hands.GetOrientedJoint(0, HandJointIndex(j));
            }
        }

        if (m_hands.IsHandTracked(1)) // right hand
        {
            for (int j = 0; j < (int)HandJointIndex::Count; ++j)
            {
                rightHandTransform[j] = m_hands.GetOrientedJoint(1, HandJointIndex(j));
            }
        }

        // Get timestamp
        long long timestamp = m_mixedReality.GetPredictedDisplayTime();

        if (m_headPoseServer && m_headPoseServer->IsClientConnected() &&
            (m_sendPositionOperation == nullptr || m_sendPositionOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed) &&
            timestamp > m_lastPositionTimestamp)
        {
            std::vector<XMVECTOR> headAndEyes;
            headAndEyes.reserve(sizeof(XMVECTOR) * 6);
            headAndEyes.push_back(headPosition);
            headAndEyes.push_back(headForward);
            headAndEyes.push_back(headUp);
            headAndEyes.push_back(headRight);
            headAndEyes.push_back(eyeGazeOrigin);
            headAndEyes.push_back(eyeGazeDirection);
            m_lastPositionTimestamp = timestamp;

            // Send the head, eye and hand data
            m_headPoseServer->NewDataFrame();
            m_headPoseServer->AppendDataFrame((uint8_t*)&headAndEyes[0], sizeof(XMVECTOR) * headAndEyes.size());
            m_headPoseServer->AppendDataFrame((uint8_t*)&leftHandTransform[0], sizeof(XMMATRIX) * leftHandTransform.size());
            m_headPoseServer->AppendDataFrame((uint8_t*)&rightHandTransform[0], sizeof(XMMATRIX) * rightHandTransform.size());
            m_sendPositionOperation = m_headPoseServer->SendDataFrameAsync(m_lastPositionTimestamp);
        }

        // Update debug data from the host
        {
            std::lock_guard<std::mutex> lock(m_debugStringMutex);
            m_debugText.SetText(m_debugString);
        }

        // Update detected object labels from the host
        int i = 0;
        {
            std::lock_guard<std::mutex> lock(m_labelDataMutex);

            for (auto const& label : m_labelData)
            {
                auto const& labelText = std::get<0>(label);
                auto const& labelPose = std::get<1>(label);

                if (i == m_objectLabels.size())
                {
                    // Create a new object label if we have run out of existing ones
                    auto labelSize = XMVectorSet(0.127f, 0.0762f, 0.0000762f, 1.0f);
                    auto labelColor = XMVectorSet(0.5f, 0.5f, 0.3f, 1.0f);
                    auto labelTextColor = TextColor::Black;
                    auto newObjectLabel = make_unique<FloatingSlate>(XMVectorZero());
                    auto newObjectLabelText = make_shared<FloatingSlateButton>(XMVectorZero(), labelSize, labelColor, 0, this);
                    newObjectLabelText->SetShape(Mesh::MT_BOX);
                    newObjectLabelText->SetFontSize(200.0f);
                    newObjectLabelText->SetTextColor(TextColor::Black);
                    newObjectLabel->AddButton(newObjectLabelText);
                    newObjectLabel->HideTitleBar();
                    m_objectLabels.push_back(std::move(newObjectLabel));
                }

                // Try to re-use existing FloatingText object labels to avoid the creation overhead
                auto const& objectLabel = m_objectLabels[i++];
                auto const& objectLabelText = objectLabel->GetButton(0);

                objectLabelText->SetText(labelText);

                XMVECTOR translation, rotation, scale;
                if (XMMatrixDecompose(&scale, &rotation, &translation, labelPose))
                {
                    objectLabel->SetPosition(translation);
                    objectLabel->SetRotation(rotation);
                    objectLabel->Update(frameDelta, m_hands);
                }
            }
        }

        // Release any extra existing labels which we no longer need
        m_objectLabels.erase(m_objectLabels.begin() + i, m_objectLabels.end());
    }
    else
    {
        m_menu.SetRotation(-headForward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        m_menu.SetPosition(headPosition + headForward * 0.5f + headUp * 0.05f);
    }

    const XMVECTOR textPosition = headPosition + headForward * 2.0f + headUp * 0.25f + headRight * 0.3f;
    m_debugText.SetPosition(textPosition);
    m_debugText.SetForwardDirection(-headForward);
}

winrt::Windows::Foundation::IAsyncAction AppMain::StartRecordingAsync()
{
    if (m_scenario)
    {
        co_await m_scenario->StartRecordingAsync(m_mixedReality.GetWorldCoordinateSystem());
    }
    if (m_videoFrameProcessor)
    {
        co_await m_videoFrameProcessor->StartRecordingAsync(m_mixedReality.GetWorldCoordinateSystem());
    }
    if (m_headPoseServer)
    {
        co_await m_headPoseServer->StartListeningAsync(30004);
    }
    if (m_debugTextReceiver)
    {
        co_await m_debugTextReceiver->StartListeningAsync(40000);
    }
    if (m_objectLabelsReceiver)
    {
        co_await m_objectLabelsReceiver->StartListeningAsync(40001);
    }

    m_recording = true;
}

winrt::Windows::Foundation::IAsyncAction AppMain::StopRecordingAsync()
{
    if (m_videoFrameProcessor)
    {
        co_await m_videoFrameProcessor->StopRecordingAsync();
    }
    if (m_scenario)
    {
        m_scenario->StopRecording();
    }
    if (m_headPoseServer)
    {
        m_headPoseServer->StopListening();
    }
    if (m_debugTextReceiver)
    {
        m_headPoseServer->StopListening();
    }
    if (m_objectLabelsReceiver)
    {
        m_objectLabelsReceiver->StopListening();
    }

    m_recording = false;

    if (m_pReceiverThread)
    {
        m_pReceiverThread->join();
        m_pReceiverThread = nullptr;
    }
    if (m_pLabelsReceiverThread)
    {
        m_pLabelsReceiverThread->join();
        m_pLabelsReceiverThread = nullptr;
    }
}

winrt::Windows::Foundation::IAsyncAction AppMain::InitializeVideoFrameProcessorAsync()
{
    if (m_videoFrameProcessorOperation &&
        m_videoFrameProcessorOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
    {
        return;
    }

    m_videoFrameProcessor = make_unique<VideoFrameProcessor>();

    // If using ResearchMode, compute PV camera extrinsics relative to the RigNode
    if (m_scenario)
    {
        GUID rigNodeGuid;
        m_scenario->GetRigNodeId(rigNodeGuid);
        m_videoFrameProcessor->SetLocator(rigNodeGuid);
    }

    co_await m_videoFrameProcessor->InitializeAsync();
}

bool AppMain::IsVideoFrameProcessorWantedAndReady() const
{
    return (m_videoFrameProcessorOperation == nullptr ||
        m_videoFrameProcessorOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed);
}

void AppMain::OnButtonPressed(FloatingSlateButton* pButton)
{
    if (pButton->GetID() == (unsigned)ButtonID::Start)
    {
        StartRecordingAsync();
    }
    else if (pButton->GetID() == (unsigned)ButtonID::Stop)
    {		
        StopRecordingAsync();		
    }
}

void AppMain::DrawObjects()
{
    m_menu.Draw();
    m_debugText.Render();

    for (const auto& label : m_objectLabels)
    {
        label->Draw();
    }
}

void AppMain::Render()
{
    if (m_mixedReality.IsEnabled())
    {
        DrawCall::vLights[0].vLightPosW = m_mixedReality.GetHeadPosition() + XMVectorSet(0.0f, 1.0f, 0.0f, 0.f);

        for (size_t cameraPoseIndex = 0; cameraPoseIndex < m_mixedReality.GetCameraPoseCount(); ++cameraPoseIndex)
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> d3dBackBuffer = m_mixedReality.GetBackBuffer(cameraPoseIndex);
            D3D11_VIEWPORT d3dViewport = m_mixedReality.GetViewport(cameraPoseIndex);
            DrawCall::SetBackBuffer(d3dBackBuffer.Get(), d3dViewport);

            // Default clip planes are 0.1 and 20
            static XMMATRIX leftView, rightView, leftProj, rightProj;
            m_mixedReality.GetViewMatrices(cameraPoseIndex, leftView, rightView);
            m_mixedReality.GetProjMatrices(cameraPoseIndex, leftProj, rightProj);
            DrawCall::PushView(leftView, rightView);
            DrawCall::PushProj(leftProj, rightProj);

            DrawCall::GetBackBuffer()->Clear(0.f, 0.f, 0.f, 0.f);

            DrawCall::PushRenderPass(0, DrawCall::GetBackBuffer());
            DrawObjects();
            DrawCall::PopRenderPass();

            if (!DrawCall::IsSinglePassSteroEnabled())
            {
                DrawCall::PushRightEyePass(0, DrawCall::GetBackBuffer());
                DrawObjects();
                DrawCall::PopRightEyePass();
            }

            DrawCall::PopView();
            DrawCall::PopProj();

            m_mixedReality.CommitDepthBuffer(cameraPoseIndex, DrawCall::GetBackBuffer()->GetD3DDepthStencilTexture());
        }

        m_mixedReality.PresentAndWait();
    }
    else
    {
        DrawCall::PushView(XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f), XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        DrawCall::PushProj(XM_PIDIV4, DrawCall::GetCurrentRenderTarget()->GetWidth() / (float)DrawCall::GetCurrentRenderTarget()->GetHeight(), 0.01f, 20.0f);

        DrawCall::PushRenderPass(0, DrawCall::GetBackBuffer());

        DrawCall::GetBackBuffer()->Clear(0.f, 0.f, 0.f, 0.f);
        DrawObjects();

        DrawCall::PopView();
        DrawCall::PopProj();
        DrawCall::PopRenderPass();

        DrawCall::GetD3DSwapChain()->Present(1, 0);
    }
}

/// <summary>
/// This just spins up a server listening on port 40000 for float(x, y, z) values and renders them.
/// </summary>
void AppMain::ReceiverThreadFunction()
{
    std::vector<uint8_t> buffer;
    while (m_debugTextReceiver->IsClientConnected())
    {
        size_t length = 0;
        int64_t timestamp = 0;
        std::string displayString;
        m_debugTextReceiver->ReceiveDataFrameAsync(buffer, length, timestamp).get();
        if (buffer.size() == 4)
        {
            int i = *(int*)&buffer[0];

            {
                std::lock_guard lock(m_debugStringMutex);
                m_debugString = std::to_string(i);
            }
        }
        else if (buffer.size() == 12)
        {
            float x = *(float*)&buffer[0];
            float y = *(float*)&buffer[4];
            float z = *(float*)&buffer[8];

            // Show some debug info
            {
                std::lock_guard lock(m_debugStringMutex);
                m_debugString = "X:" + to_string(x) + " Y:" + to_string(y) + " Z:" + to_string(z);
            }
        }
    }
}

/// <summary>
/// This listens on port 40001 for a list of strings with positions for rendering
/// </summary>
void AppMain::ReceiveLabelsThreadFunction()
{
    std::vector<std::uint8_t> buffer;
    while (m_objectLabelsReceiver->IsClientConnected())
    {
        size_t length = 0;
        int64_t timestamp = 0;
        m_objectLabelsReceiver->ReceiveDataFrameAsync(buffer, length, timestamp).get();
        if (buffer.size() > 0)
        {
            uint8_t* p = &buffer[0];
            if (*p != 0)
            {
                std::lock_guard<std::mutex> lock(m_labelDataMutex);

                m_labelData.clear();
                while (*p != 0)
                {
                    std::string text = reinterpret_cast<char*>(p);
                    p += text.length() + 1; // null-terminated string
                    XMMATRIX* pm = reinterpret_cast<XMMATRIX*>(p);
                    XMMATRIX location = *pm++;
                    p = reinterpret_cast<uint8_t*>(pm);
                    m_labelData.push_back({ text, location });
                }
            }
        }
    }
}

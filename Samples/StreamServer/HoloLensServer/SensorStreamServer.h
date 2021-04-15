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

#pragma once

#include <Windows.h>
#include <debugapi.h>
#include <ppltasks.h>
#include <fstream>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>

class SensorStreamServer
{
public:
    SensorStreamServer();
    virtual ~SensorStreamServer();
    winrt::Windows::Foundation::IAsyncAction StartListeningAsync(int port);
    void NewDataFrame();
    void AppendDataFrame(const uint8_t* buffer, size_t length);
    winrt::Windows::Foundation::IAsyncAction SendDataFrameAsync(int64_t timestamp);
    winrt::Windows::Foundation::IAsyncAction SendDataFrameAsync(const uint8_t* buffer, size_t length, int64_t timestamp);
    winrt::Windows::Foundation::IAsyncAction ReceiveDataFrameAsync(std::vector<uint8_t>& buffer, size_t& length, int64_t& timestamp);
    void StopListening();
    bool IsClientConnected();
    winrt::event_token ClientConnected(winrt::delegate<> const& handler);
    void ClientConnected(winrt::event_token const& token);
    winrt::event_token ClientDisconnected(winrt::delegate<> const& handler);
    void ClientDisconnected(winrt::event_token const& token);

protected:
    virtual void OnConnectionReceived(
        const winrt::Windows::Networking::Sockets::StreamSocketListener& sender,
        const winrt::Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs& args);

    virtual void OnClientConnectionTerminated();

private:
    winrt::Windows::Networking::Sockets::StreamSocketListener m_listener = nullptr;
    winrt::Windows::Networking::Sockets::StreamSocket m_socket = nullptr;
    winrt::Windows::Storage::Streams::DataWriter m_writer = nullptr;
    winrt::Windows::Storage::Streams::DataReader m_reader = nullptr;
    winrt::event<winrt::delegate<>> m_clientConnectedEvent;
    winrt::event<winrt::delegate<>> m_clientDisconnectedEvent;
    winrt::event_token m_onConnectionReceivedEventToken;
    std::vector<uint8_t> m_frameBuffer;
};

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

#include "SensorStreamServer.h"

SensorStreamServer::SensorStreamServer()
{
}

SensorStreamServer::~SensorStreamServer()
{
    this->StopListening();
}

winrt::Windows::Foundation::IAsyncAction SensorStreamServer::StartListeningAsync(int port)
{
    m_listener = winrt::Windows::Networking::Sockets::StreamSocketListener();
    m_onConnectionReceivedEventToken = m_listener.ConnectionReceived({ this, &SensorStreamServer::OnConnectionReceived });
    m_listener.Control().KeepAlive(true);
    co_await m_listener.BindServiceNameAsync(std::to_wstring(port));
}

void SensorStreamServer::StopListening()
{
    m_listener.Close();
}

bool SensorStreamServer::IsClientConnected()
{
    return m_socket != nullptr;
}

winrt::event_token SensorStreamServer::ClientConnected(winrt::delegate<> const& handler)
{
    return m_clientConnectedEvent.add(handler);
}

void SensorStreamServer::ClientConnected(winrt::event_token const& token)
{
    m_clientConnectedEvent.remove(token);
}

winrt::event_token SensorStreamServer::ClientDisconnected(winrt::delegate<> const& handler)
{
    return m_clientDisconnectedEvent.add(handler);
}

void SensorStreamServer::ClientDisconnected(winrt::event_token const& token)
{
    m_clientDisconnectedEvent.remove(token);
}

void SensorStreamServer::NewDataFrame()
{
    m_frameBuffer.clear();
}

void SensorStreamServer::AppendDataFrame(const uint8_t* buffer, size_t length)
{
    m_frameBuffer.insert(m_frameBuffer.end(), buffer, buffer + length);
}

winrt::Windows::Foundation::IAsyncAction SensorStreamServer::SendDataFrameAsync(int64_t timestamp)
{
    if (m_writer != nullptr)
    {
        try
        {
            m_writer.WriteInt32((int)(m_frameBuffer.size() + sizeof(int64_t))); // buffer + timestamp
            m_writer.WriteInt64(timestamp);
            m_writer.WriteBytes(m_frameBuffer);
            co_await m_writer.StoreAsync();
        }
        catch (...)
        {
            OnClientConnectionTerminated();
        }
    }
}

winrt::Windows::Foundation::IAsyncAction SensorStreamServer::SendDataFrameAsync(const uint8_t* buffer, size_t length, int64_t timestamp)
{
    NewDataFrame();
    AppendDataFrame(buffer, length);
    return SendDataFrameAsync(timestamp);
}

winrt::Windows::Foundation::IAsyncAction SensorStreamServer::ReceiveDataFrameAsync(std::vector<uint8_t>& buffer, size_t& length, int64_t& timestamp)
{
    if (m_reader != nullptr)
    {
        try
        {
            if (m_reader.UnconsumedBufferLength() < sizeof(int))
            {
                co_await m_reader.LoadAsync(sizeof(int));
            }

            uint32_t totalLength = m_reader.ReadInt32();
            if (m_reader.UnconsumedBufferLength() < totalLength)
            {
                co_await m_reader.LoadAsync(totalLength);
            }

            timestamp = m_reader.ReadInt64();
            length = totalLength - sizeof(timestamp);
            buffer.resize(length);
            m_reader.ReadBytes(winrt::array_view<uint8_t>(buffer.data(), buffer.data() + length));
        }
        catch (winrt::hresult_error const&)
        {
            OnClientConnectionTerminated();
        }
    }
}

void SensorStreamServer::OnConnectionReceived(const winrt::Windows::Networking::Sockets::StreamSocketListener& sender, const winrt::Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs& args)
{
    m_socket = args.Socket();
    m_writer = winrt::Windows::Storage::Streams::DataWriter(m_socket.OutputStream());
    m_writer.UnicodeEncoding(winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8);
    m_writer.ByteOrder(winrt::Windows::Storage::Streams::ByteOrder::LittleEndian);
    m_reader = winrt::Windows::Storage::Streams::DataReader(m_socket.InputStream());
    m_reader.UnicodeEncoding(winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8);
    m_reader.ByteOrder(winrt::Windows::Storage::Streams::ByteOrder::LittleEndian);
    m_clientConnectedEvent();
}

void SensorStreamServer::OnClientConnectionTerminated()
{
    m_writer = nullptr;
    m_reader = nullptr;
    if (m_socket != nullptr)
    {
        m_socket.Close();
    }

    m_socket = nullptr;
    m_clientDisconnectedEvent();
}

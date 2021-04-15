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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/Cannon/MixedReality.cpp


#include "SpatialMapper.h"
#include "Cannon/Common/Timer.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <robuffer.h>

using namespace DirectX;
using namespace std;
using namespace winrt::Windows::Perception::Spatial;

SpatialMapper::SpatialMapper() 
{
    m_headPosition = XMVectorZero();
    m_server = std::make_unique<SensorStreamServer>();
    m_server->ClientConnected([this] { m_pWriteThread = new std::thread(&SpatialMapper::SpatialMapUpdateThreadFunction, this); });
}

winrt::Windows::Foundation::IAsyncAction SpatialMapper::StartRecordingAsync(const SpatialCoordinateSystem& worldCoordSystem)
{
    m_worldCoordSystem = worldCoordSystem;
    m_fExit = false;
    m_pSurfaceObservationThread = new std::thread(&SpatialMapper::SurfaceObservationThreadFunction, this);
    co_await m_server->StartListeningAsync(30005);
}

void SpatialMapper::StopRecording()
{
    m_server->StopListening();
    m_fExit = true;
    m_pSurfaceObservationThread->join();
    if (m_pWriteThread)
    {
        m_pWriteThread->join();
        m_pWriteThread = nullptr;
    }
}

void SpatialMapper::CreaterObserverIfNeeded()
{
    if (m_surfaceObserver)
        return;

    auto status = winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver::RequestAccessAsync().get();
    if (status == winrt::Windows::Perception::Spatial::SpatialPerceptionAccessStatus::Allowed)
    {
        m_surfaceObserver = winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver();
    }
}

// Returns the list of observed surfaces that are new or in need of an update.
// The list is sorted newest to oldest with brand new meshes appearing after the oldest.
// Code processing this list should work from back to front, so then new meshes get processed first,
// followed by meshes that have gone the longest without an update.

void SpatialMapper::GetLatestSurfacesToProcess(std::vector<TimestampSurfacePair>& surfacesToProcess)
{
    auto observedSurfaces = m_surfaceObserver.GetObservedSurfaces();

    m_meshRecordsMutex.lock();

    for (auto const& observedSurfacePair : observedSurfaces)
    {
        auto surfaceInfo = observedSurfacePair.Value();

        auto meshRecordIterator = m_meshRecords.find(surfaceInfo.Id());
        if (meshRecordIterator == m_meshRecords.end())
        {
            surfacesToProcess.push_back(pair<long long, winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo>(0, surfaceInfo));
        }
        else if (surfaceInfo.UpdateTime().time_since_epoch().count() - meshRecordIterator->second.lastSurfaceUpdateTime > 5 * 10000000 || !meshRecordIterator->second.mesh)
        {
            surfacesToProcess.push_back(pair<long long, winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo>(meshRecordIterator->second.lastMeshUpdateTime, surfaceInfo));
        }
    }

    for (auto& meshRecordPair : m_meshRecords)
    {
        if (!observedSurfaces.HasKey(meshRecordPair.first))
            m_meshRecordIDsToErase.push_back(meshRecordPair.first);
    }

    m_meshRecordsMutex.unlock();

    sort(surfacesToProcess.begin(), surfacesToProcess.end(), [](const pair<long long, winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo>& a, const pair<long long, winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo>& b)
    {
        return a.first > b.first;
    });
}

void SpatialMapper::SurfaceObservationThreadFunction()
{
    vector<TimestampSurfacePair> surfacesToProcess;
    vector<XMVECTOR> meshVertices;

    while (!m_fExit)
    {
        CreaterObserverIfNeeded();
        if (!m_surfaceObserver || !m_worldCoordSystem)
        {
            Sleep(50);
            continue;
        }

        m_headPositionMutex.lock();
        winrt::Windows::Perception::Spatial::SpatialBoundingBox box = { { XMVectorGetX(m_headPosition), XMVectorGetY(m_headPosition), XMVectorGetZ(m_headPosition) }, { 10.f, 10.f, 5.f } };
        winrt::Windows::Perception::Spatial::SpatialBoundingVolume bounds = winrt::Windows::Perception::Spatial::SpatialBoundingVolume::FromBox(m_worldCoordSystem, box);
        m_surfaceObserver.SetBoundingVolume(bounds);
        m_headPositionMutex.unlock();

        if (surfacesToProcess.empty())
        {
            Sleep(50);
            GetLatestSurfacesToProcess(surfacesToProcess);
        }

        while (!surfacesToProcess.empty())
        {
            Sleep(50);

            auto surfaceInfo = surfacesToProcess.back().second;
            surfacesToProcess.pop_back();

            auto options = winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceMeshOptions();
            options.IncludeVertexNormals(true);
            auto sourceMesh = surfaceInfo.TryComputeLatestMeshAsync(1000.0, options).get();
            if (sourceMesh)
            {
                MeshRecord newMeshRecord;
                newMeshRecord.id = sourceMesh.SurfaceInfo().Id();
                newMeshRecord.sourceMesh = sourceMesh;
                newMeshRecord.lastMeshUpdateTime = Timer::GetSystemRelativeTime();
                newMeshRecord.lastSurfaceUpdateTime = sourceMesh.SurfaceInfo().UpdateTime().time_since_epoch().count();

                auto tryTransform = sourceMesh.CoordinateSystem().TryGetTransformTo(m_worldCoordSystem);
                if (tryTransform)
                    newMeshRecord.worldTransform = tryTransform.Value();

                newMeshRecord.mesh = make_shared<Mesh>(nullptr, 0);
                ConvertMesh(newMeshRecord.sourceMesh, newMeshRecord.mesh);
                newMeshRecord.sourceMesh = nullptr;
                newMeshRecord.mesh->UpdateBoundingBox();

                m_newMeshRecordsMutex.lock();
                m_newMeshRecords.push_back(newMeshRecord);
                m_newMeshRecordsMutex.unlock();
            }
        }

        m_meshRecordsMutex.lock();

        for (auto& guid : m_meshRecordIDsToErase)
        {
            m_meshRecords.erase(guid);
        }
        m_meshRecordIDsToErase.clear();

        for (auto& meshRecord : m_newMeshRecords)
        {
            m_meshRecords[meshRecord.id] = meshRecord;
            m_lastUpdateTime = meshRecord.lastSurfaceUpdateTime;
        }
        m_newMeshRecords.clear();

        m_meshRecordsMutex.unlock();
    }
}

void SpatialMapper::SpatialMapUpdateThreadFunction()
{
    winrt::Windows::Foundation::IAsyncAction lastSendOperation = nullptr;
    while (!m_fExit && m_server->IsClientConnected())
    {
        if (m_lastUpdateTime > m_lastSendTime && (!lastSendOperation || lastSendOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed))
        {
            m_server->NewDataFrame();

            m_meshRecordsMutex.lock();

            for (auto& meshRecord : m_meshRecords)
            {
                auto& vertices = meshRecord.second.mesh->GetVertices();
                XMMATRIX worldTransform = XMLoadFloat4x4(&meshRecord.second.worldTransform);
                for (auto& vertex : vertices)
                {
                    XMVECTOR v = XMVector3Transform(vertex.position, worldTransform);
                    m_server->AppendDataFrame((uint8_t*)&v, sizeof(XMVECTOR));
                }
            }

            m_lastSendTime = m_lastUpdateTime;
            m_meshRecordsMutex.unlock();

            lastSendOperation = m_server->SendDataFrameAsync(m_lastSendTime);
        }
        else
        {
            // No update or last update still in progress
            Sleep(50);
        }
    }
}

void SpatialMapper::UpdateHeadPosition(const XMVECTOR& headPosition)
{
    m_headPositionMutex.lock();
    m_headPosition = headPosition;
    m_headPositionMutex.unlock();
}

void SpatialMapper::ConvertMesh(winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh sourceMesh, shared_ptr<Mesh> destinationMesh)
{
    if (!sourceMesh || !destinationMesh)
        return;

    auto spatialIndexFormat = sourceMesh.TriangleIndices().Format();
    assert((DXGI_FORMAT)spatialIndexFormat == DXGI_FORMAT_R16_UINT);

    auto spatialPositionsFormat = sourceMesh.VertexPositions().Format();
    assert((DXGI_FORMAT)spatialPositionsFormat == DXGI_FORMAT_R16G16B16A16_SNORM);

    auto spatialNormalsFormat = sourceMesh.VertexNormals().Format();
    assert((DXGI_FORMAT)spatialNormalsFormat == DXGI_FORMAT_R8G8B8A8_SNORM);

    Microsoft::WRL::ComPtr<IUnknown> unknown;
    Microsoft::WRL::ComPtr<Windows::Storage::Streams::IBufferByteAccess> bufferByteAccess;

    unknown = (IUnknown*)winrt::get_abi(sourceMesh.TriangleIndices().Data());
    unknown.As(&bufferByteAccess);
    unsigned short* pSourceIndexBuffer = nullptr;
    bufferByteAccess->Buffer((unsigned char**)&pSourceIndexBuffer);

    unknown = (IUnknown*)winrt::get_abi(sourceMesh.VertexPositions().Data());
    unknown.As(&bufferByteAccess);
    short* pSourcePositionsBuffer = nullptr;
    bufferByteAccess->Buffer((unsigned char**)&pSourcePositionsBuffer);

    unknown = (IUnknown*)winrt::get_abi(sourceMesh.VertexNormals().Data());
    unknown.As(&bufferByteAccess);
    char* pSourceNormalsBuffer = nullptr;
    bufferByteAccess->Buffer((unsigned char**)&pSourceNormalsBuffer);

    auto vertexScaleFactor = sourceMesh.VertexPositionScale();
    float short_max = pow(2.0f, 15.0f);
    float char_max = pow(2.0f, 7.0f);

    assert(sourceMesh.VertexPositions().ElementCount() == sourceMesh.VertexNormals().ElementCount());

    auto& vertexBuffer = destinationMesh->GetVertices();
    vertexBuffer.resize(sourceMesh.VertexPositions().ElementCount());
    auto& indexBuffer = destinationMesh->GetIndices();
    indexBuffer.resize(sourceMesh.TriangleIndices().ElementCount());

    for (unsigned i = 0; i < indexBuffer.size(); ++i)
    {
        indexBuffer[i] = pSourceIndexBuffer[i];
    }

    for (unsigned i = 0; i < vertexBuffer.size(); ++i)
    {
        unsigned sourceIndex = i * 4;

        vertexBuffer[i].position = XMVectorSet(pSourcePositionsBuffer[sourceIndex + 0] / short_max * vertexScaleFactor.x,
            pSourcePositionsBuffer[sourceIndex + 1] / short_max * vertexScaleFactor.y,
            pSourcePositionsBuffer[sourceIndex + 2] / short_max * vertexScaleFactor.z,
            1.0f);

        vertexBuffer[i].normal = XMVectorSet(pSourceNormalsBuffer[sourceIndex + 0] / char_max,
            pSourceNormalsBuffer[sourceIndex + 1] / char_max,
            pSourceNormalsBuffer[sourceIndex + 2] / char_max,
            0.0f);

        vertexBuffer[i].texcoord.x = 0;
        vertexBuffer[i].texcoord.y = 0;
    }
}

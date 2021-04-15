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
// https://github.com/microsoft/HoloLens2ForCV/blob/main/Samples/StreamRecorder/StreamRecorderApp/Cannon/MixedReality.h

#pragma once

#include "Cannon/DrawCall.h"
#include "SensorStreamServer.h"

#include <winrt/Windows.Perception.Spatial.Surfaces.h>
#include <mutex>

class SpatialMapper
{
public:

    SpatialMapper();
    void UpdateHeadPosition(const XMVECTOR& headPosition);
    winrt::Windows::Foundation::IAsyncAction StartRecordingAsync(const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& worldCoordSystem);
    void StopRecording();

private:

    struct MeshRecord
    {
        winrt::guid id;

        std::shared_ptr<Mesh> mesh;
        winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh sourceMesh{ nullptr };	// This will be nullptr unless update is in progresss

        long long lastMeshUpdateTime;		// The time when this mesh was last updated with the last surface
        long long lastSurfaceUpdateTime;	// The time when the last surface was last updated by the system

        winrt::Windows::Foundation::Numerics::float4x4 worldTransform;

        MeshRecord()
        {
            memset(&id, 0, sizeof(id));

            lastMeshUpdateTime = 0;
            lastSurfaceUpdateTime = 0;

            XMStoreFloat4x4(&worldTransform, XMMatrixIdentity());
        }
    };
    typedef std::pair<winrt::guid, MeshRecord> MeshRecordPair;
    typedef std::pair<long long, winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo> TimestampSurfacePair;

    void CreaterObserverIfNeeded();
    void GetLatestSurfacesToProcess(std::vector<TimestampSurfacePair>& surfacesToProcess);
    void SurfaceObservationThreadFunction();
    void SpatialMapUpdateThreadFunction();
    void ConvertMesh(winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh sourceMesh, std::shared_ptr<Mesh> destinationMesh);

    winrt::Windows::Perception::Spatial::SpatialCoordinateSystem m_worldCoordSystem{ nullptr };
    winrt::Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver m_surfaceObserver{ nullptr };

    XMVECTOR m_headPosition;
    std::mutex m_headPositionMutex;

    std::map<winrt::guid, MeshRecord> m_meshRecords;
    std::vector<winrt::guid> m_meshRecordIDsToErase;
    std::mutex m_meshRecordsMutex;

    std::vector<MeshRecord> m_newMeshRecords;
    std::mutex m_newMeshRecordsMutex;

    std::thread* m_pSurfaceObservationThread;
    std::thread* m_pWriteThread;
    bool m_fExit = false;
    long long m_lastUpdateTime = 0;
    long long m_lastSendTime = 0;

    std::unique_ptr<SensorStreamServer> m_server;
};

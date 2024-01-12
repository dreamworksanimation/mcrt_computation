// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mcrt_dataio/engine/merger/GlobalNodeInfo.h>
#include <mcrt_dataio/share/util/ClockDelta.h>

#include <mcrt_dataio/share/sock/SockServer.h>
#include <mcrt_dataio/share/sock/SockServerConnection.h>

#include <tbb/atomic.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace mcrt_computation {

class ProgMcrtMergeClockDeltaDriver
//
// This class is executing server side functionality of clock delta measurement.
// Basically merge computation runs server side operation and all other computations
// and client do client side clock delta operation.
// This class boots independent 4 threads internally to process incoming clockDelta measurement
// requests from other computations. During AzureDeepTest (Jun/2020), this logic was tested up
// to 128 nodes and 4 server threads were working without trouble. 4 threads are almost asleep
// and almost no CPU load when there are no incoming clock delta measurement requests.
// This has almost no impact on the main merge task operation.
//
{
public:
    using GlobalNodeInfo = mcrt_dataio::GlobalNodeInfo;
    using ProgMcrtMergeClockDeltaDriverShPtr = std::shared_ptr<ProgMcrtMergeClockDeltaDriver>;

    enum class ThreadState : int { INIT, RUN };

    static void init(GlobalNodeInfo &globalNodeInfo);
    static ProgMcrtMergeClockDeltaDriverShPtr get();

    // Non-copyable
    ProgMcrtMergeClockDeltaDriver &operator =(const ProgMcrtMergeClockDeltaDriver) = delete;
    ProgMcrtMergeClockDeltaDriver(const ProgMcrtMergeClockDeltaDriver &) = delete;

    ProgMcrtMergeClockDeltaDriver(GlobalNodeInfo &globalNodeInfo);
    ~ProgMcrtMergeClockDeltaDriver();

private:
    using ConnectionShPtr = mcrt_dataio::SockServerConnectionQueue::ConnectionShPtr;

    GlobalNodeInfo &mGlobalNodeInfo;

    std::thread mServerThread;
    std::thread mWorkerThread;  // single worker
    tbb::atomic<ThreadState> mServerThreadState;
    tbb::atomic<ThreadState> mWorkerThreadState;
    bool mThreadShutdown;

    mutable std::mutex mMutex;
    std::condition_variable mCv; // using at boot threadMain sequence

    int mClockDeltaSvrPort;
    std::string mClockDeltaSvrPath; // Unix-domain ipc path

    mcrt_dataio::SockServerConnectionQueue mConnectionQueue;

    static void serverMain(ProgMcrtMergeClockDeltaDriver *driver);
    static void workerMain(ProgMcrtMergeClockDeltaDriver *driver);
    static void workerConnection(ProgMcrtMergeClockDeltaDriver *driver, ConnectionShPtr connection);
};

} // namespace mcrt_computation

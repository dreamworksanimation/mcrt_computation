// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ProgMcrtMergeClockDeltaDriver.h"

#include <tbb/task_group.h>

#include <iostream>

#define DEBUG_MSG

namespace mcrt_computation {

ProgMcrtMergeClockDeltaDriver::ProgMcrtMergeClockDeltaDriver(GlobalNodeInfo &globalNodeInfo) :
    mGlobalNodeInfo(globalNodeInfo),
    mServerThreadState(ThreadState::INIT),
    mWorkerThreadState(ThreadState::INIT),
    mThreadShutdown(false),
    mClockDeltaSvrPort(0)
{
    mClockDeltaSvrPort = mGlobalNodeInfo.getMergeClockDeltaSvrPort();
    mClockDeltaSvrPath = mGlobalNodeInfo.getMergeClockDeltaSvrPath();

    // We have to build threads after finish mMutex and mCv initialization completed
    mServerThread = std::move(std::thread(serverMain, this));
    mWorkerThread = std::move(std::thread(workerMain, this));

    // Wait until thread is booted
    std::unique_lock<std::mutex> uqLock(mMutex);
    mCv.wait(uqLock, [&]{
            // Not wait if already non INIT condition
            return (mServerThreadState != ThreadState::INIT && mWorkerThreadState != ThreadState::INIT);
        });
}

ProgMcrtMergeClockDeltaDriver::~ProgMcrtMergeClockDeltaDriver()
{
    mThreadShutdown = true; // This is the only place mThreadShutdown is set to true

    if (mServerThread.joinable()) {
        mServerThread.join();
    }
    if (mWorkerThread.joinable()) {
        mWorkerThread.join();
    }
}

//------------------------------------------------------------------------------------------

// static
void
ProgMcrtMergeClockDeltaDriver::serverMain(ProgMcrtMergeClockDeltaDriver *driver)
{
    // First of all change driver's threadState condition and do notify_one to caller.
    driver->mServerThreadState = ThreadState::RUN;
    driver->mCv.notify_one(); // notify to ProgMcrtMergeClockDeltaDriver's constructor

#   ifdef DEBUG_MSG
    std::cerr << ">> ProgMcrtMergeClockDeltaDriver.cc serverMain() booted\n";
#   endif // end DEBUG_MSG

    mcrt_dataio::SockServer svr(&driver->mThreadShutdown);
    svr.mainLoop(driver->mClockDeltaSvrPort, driver->mClockDeltaSvrPath, driver->mConnectionQueue);

#   ifdef DEBUG_MSG
    std::cerr << ">> ProgMcrtMergeClockDeltaDriver.cc serverMain() shutdown\n";
#   endif // end DEBUG_MSG
}

// static
void
ProgMcrtMergeClockDeltaDriver::workerMain(ProgMcrtMergeClockDeltaDriver *driver)
{
    // First of all change driver's threadState condition and do notify_one to caller.
    driver->mWorkerThreadState = ThreadState::RUN;
    driver->mCv.notify_one(); // notify to ProgMcrtMergeClockDeltaDriver's constructor

#   ifdef DEBUG_MSG
    std::cerr << ">> ProgMcrtMergeClockDeltaDriver.cc workerMain() booted\n";
#   endif // end DEBUG_MSG

    tbb::task_group taskGroup;
    constexpr unsigned threadCount = 4; // very heuristic number. It confirmed working up to 128 nodes.
        
    for (unsigned iThread = 0; iThread < threadCount; ++iThread) {
        taskGroup.run([&]() {
                while (1) {
                    if (driver->mThreadShutdown) break;

                    ConnectionShPtr connection = driver->mConnectionQueue.deq();
                    if (!connection) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }

                    workerConnection(driver, connection);
                }
            }); // taskGroup.run()
    } // loop iThread
    taskGroup.wait();

#   ifdef DEBUG_MSG
    std::cerr << ">> ProgMcrtMergeClockDeltaDriver.cc workerMain() shutdown\n";
#   endif // end DEBUG_MSG
}

// static
void
ProgMcrtMergeClockDeltaDriver::workerConnection(ProgMcrtMergeClockDeltaDriver *driver,
                                                ConnectionShPtr connection)
{
    std::string hostName;
    float clockDelta, roundTripAve; // millisec
    mcrt_dataio::ClockDelta::NodeType nodeType;
    if (!mcrt_dataio::ClockDelta::serverMain(connection,
                                             128, // test iteration count
                                             hostName,
                                             clockDelta, // millisec
                                             roundTripAve, // millisec
                                             nodeType)) {
        std::cerr << "error" << std::endl;
        return;
    }
        
    driver->mGlobalNodeInfo.setClockDeltaTimeShift(nodeType,
                                                   hostName,
                                                   clockDelta, // millisec
                                                   roundTripAve); // millisec

#   ifdef DEBUG_MSG
    std::cerr << "hostName:" << hostName
              << " clockDelta:" << clockDelta << " ms"
              << " roundTrip:" << roundTripAve << " ms\n";
#   endif // end DEBUG_MSG
}

//==========================================================================================

ProgMcrtMergeClockDeltaDriver::ProgMcrtMergeClockDeltaDriverShPtr gProgMcrtMergeClockDeltaDriver;

// static function
void
ProgMcrtMergeClockDeltaDriver::init(GlobalNodeInfo &globalNodeInfo)
{
    gProgMcrtMergeClockDeltaDriver.reset(new ProgMcrtMergeClockDeltaDriver(globalNodeInfo));
}

// static
ProgMcrtMergeClockDeltaDriver::ProgMcrtMergeClockDeltaDriverShPtr
ProgMcrtMergeClockDeltaDriver::get()
{
    return gProgMcrtMergeClockDeltaDriver;
}

} // namespace mcrt_computation

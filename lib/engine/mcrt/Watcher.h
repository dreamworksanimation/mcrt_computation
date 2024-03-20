// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <tbb/atomic.h>
#include <thread>

namespace mcrt_computation {

class Watcher
//
// This class executes user defined call-back function as an independent thread.
//
{
public:        
    using CallBackFunc = std::function<void()>;

    // 
    // RunMode : STOP_AND_GO
    // Initially, the callback function execution thread is suspended and waiting resume() API call.
    // In order to start the callback function execution, you have to call resume() API. After finishing
    // the callback function, runState is back to WAIT and the thread is suspended until the next resume() API.
    //
    // RunMODE::NON_STOP
    // The callback function execution thread immediately executes the callback function after boot.
    // The callback is executed again immediately when the callback function is finished.
    //
    enum class RunMode : int { STOP_AND_GO, NON_STOP };

    Watcher() = default;
    ~Watcher();

    void boot(RunMode runMode, Watcher* watcher, const CallBackFunc& callBack);

    void resume(); // set run state to START if state is WAIT. Only used under RunMode = STOP_AND_GO.

    bool isRunStateWait() const;

    void shutDown();

    std::string show() const;

private:
    RunMode mRunMode {RunMode::STOP_AND_GO};

    enum class RunState : int { WAIT, START };
    enum class ThreadState : int { INIT, IDLE, BUSY };

    static void threadMain(Watcher* watcher, const CallBackFunc& callBack);

    static std::string showThreadState(const ThreadState& state);
    static std::string showRunState(const RunState& state);

    //------------------------------

    std::thread mThread;

    tbb::atomic<ThreadState> mThreadState {ThreadState::INIT};
    tbb::atomic<RunState> mRunState {RunState::WAIT};
    bool mThreadShutdown {false}; 

    mutable std::mutex mMutexBoot;
    std::condition_variable mCvBoot; // using by threadMain boot sequence

    mutable std::mutex mMutexRun;
    std::condition_variable mCvRun; // using by threadMain run phase
};

} // namespace mcrt_computation

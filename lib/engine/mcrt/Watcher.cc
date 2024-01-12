// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "Watcher.h"

#include <scene_rdl2/render/util/StrUtil.h>

#include <sstream>

namespace mcrt_computation {

Watcher::~Watcher()
{
    mThreadShutdown = true;

    mRunState = RunState::START;
    mCvRun.notify_one(); // notify to threadMain main loop
    
    if (mThread.joinable()) {
        mThread.join();
    }
}

void
Watcher::boot(RunMode runMode, Watcher* watcher, const CallBackFunc& callBack)
{
    mRunMode = runMode;

    mThread = std::move(std::thread(threadMain, this, callBack));

    // Wait until threadMain is booted
    std::unique_lock<std::mutex> uqLock(mMutexBoot);
    mCvBoot.wait(uqLock, [&]{
            return (mThreadState != ThreadState::INIT); // Only wait if state is INIT
        });
}

void
Watcher::resume()
{
    if (mRunState == RunState::WAIT) {
        mRunState = RunState::START;
        mCvRun.notify_one(); // notify to RenderContextDriver threadMain loop
    }
}

bool
Watcher::isRunStateWait() const
{
    return (mRunState == RunState::WAIT);
}

void
Watcher::shutDown()
{
    mThreadShutdown = true;

    mRunState = RunState::START;
    mCvRun.notify_one(); // notify to RenderContextDriver threadMain loop
    
    if (mThread.joinable()) {
        mThread.join();
    }
}

std::string
Watcher::show() const
{
    std::ostringstream ostr;
    ostr << "Watcher {\n"
         << "  mThreadState:" << showThreadState(mThreadState) << '\n'
         << "  mRunState:" << showRunState(mRunState) << '\n'
         << "  mThreadShutdown:" << scene_rdl2::str_util::boolStr(mThreadShutdown) << '\n'
         << "}";
    return ostr.str();
}

//------------------------------------------------------------------------------------------

// static function
void
Watcher::threadMain(Watcher* watcher, const CallBackFunc& callBack)
{
    // First of all change thread's threadState condition and do notify_one to caller
    watcher->mThreadState = ThreadState::IDLE;
    watcher->mCvBoot.notify_one(); // notify to Watcher's constructor

    if (watcher->mRunMode == RunMode::NON_STOP) {
        watcher->mRunState = RunState::START;
    }

    while (true) {
        {
            std::unique_lock<std::mutex> uqLock(watcher->mMutexRun);
            watcher->mCvRun.wait(uqLock, [&]{
                    return (watcher->mRunState == RunState::START); // Not wait if state is START
                });
        }

        if (watcher->mThreadShutdown) { // before exit test
            break;
        }

        watcher->mThreadState = ThreadState::BUSY;
        callBack();
        watcher->mThreadState = ThreadState::IDLE;

        if (watcher->mRunMode == RunMode::STOP_AND_GO) {
            watcher->mRunState = RunState::WAIT;
        }

        if (watcher->mThreadShutdown) { // after exit test
            break;
        }
    }
}

// static function
std::string
Watcher::showThreadState(const ThreadState &state)
{
    switch (state) {
    case ThreadState::INIT : return "INIT";
    case ThreadState::IDLE : return "IDLE";
    case ThreadState::BUSY : return "BUSY";
    default : break;
    }
    return "?";
}

// static function    
std::string
Watcher::showRunState(const RunState &state)
{
    switch (state) {
    case RunState::WAIT : return "WAIT";
    case RunState::START : return "START";
    default : break;
    }
    return "?";
}

} // namespace mcrt_computation

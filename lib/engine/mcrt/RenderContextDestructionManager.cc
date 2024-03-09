// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "RenderContextDestructionManager.h"

#include <mcrt_computation/common/mcrt_logging/TimeStampDebugMsg.h>
#include <moonray/rendering/rndr/RenderContext.h>
#include <scene_rdl2/common/rec_time/RecTime.h>

namespace mcrt_computation {

RenderContextDestructionManager::RenderContextDestructionManager()
{
    mOldRenderContextWatcher.boot(Watcher::RunMode::NON_STOP,
                                  &mOldRenderContextWatcher,
                                  [&](const bool* threadShutdownFlag) { // main function of booted thread
                                      oldRenderContextCleanupMain(threadShutdownFlag);
                                  });
}

RenderContextDestructionManager::~RenderContextDestructionManager()
{
    mOldRenderContextWatcher.shutDown();    
}

void
RenderContextDestructionManager::push(RenderContext* oldRenderContextPtr)
{
    if (!oldRenderContextPtr) return;           // skip nullptr, just in case

    McrtTimeStamp("save oldRenderContext {", "start save oldRenderCotext");
    {
        std::lock_guard<std::mutex> lock(mMutexOldRenderContextTbl);
        mOldRenderContextTbl.push_back(oldRenderContextPtr);
    }
    mCvOldRenderContextTbl.notify_one();
    McrtTimeStamp("save oldRenderContext }", "finish save oldRenderContext");
}

void
RenderContextDestructionManager::oldRenderContextCleanupMain(const bool* threadShutdownFlag)
{
    while (true) {
        RenderContext* currRenderContext = nullptr;
        {
            // retrieve single old RenderContext object
            std::unique_lock<std::mutex> uqLock(mMutexOldRenderContextTbl);
            mCvOldRenderContextTbl.wait(uqLock, [&] {
                    // Wait until oldRenderContext is not empty or shutdownFlag is ON
                    return !mOldRenderContextTbl.empty() || (*threadShutdownFlag);
                });
            if (mOldRenderContextTbl.empty() && (*threadShutdownFlag)) {
                // This function only exits when all the old RenderContext are removed and shutdownFlag is on.
                break;
            }

            currRenderContext = mOldRenderContextTbl.back();
            mOldRenderContextTbl.pop_back();
        }

        if (currRenderContext) {
            std::ostringstream ostr;
            ostr << "finish cleanup old:0x" << std::hex << currRenderContext << std::dec;

            scene_rdl2::rec_time::RecTime recTime;
            recTime.start();
            delete currRenderContext;

            ostr << " " << recTime.end() << " sec";
            McrtTimeStamp("oldRenderContextCleanupMain ", ostr.str());
        }
    }
}

} // namespace mcrt_computation

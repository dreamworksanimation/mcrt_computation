// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Watcher.h"

#include <condition_variable>

namespace moonray {
namespace rndr {
    class RenderContext;
} // namespace rndr
} // namespace moonray

namespace mcrt_computation {

class RenderContextDestructionManager
//
// This class manages the destruction of unnecessary old RenderContext objects.
// Sometimes RenderContext object is big and needs a long time to delete. If we want to restart rendering
// from scratch, a naive implementation would be that we need to destroy previously used RenderContext
// first and sometimes this delete action cost is big then you need to wait long time to start next frame.
// Instead, this class deletes previously allocated unnecessary old RenderContext by background thread
// simultaneously with current rendering execution. As a result, we can start current render as quickly as
// possible without waiting deletion of the previous renderContext.
//
{
public:
    using RenderContext = moonray::rndr::RenderContext;

    RenderContextDestructionManager();
    ~RenderContextDestructionManager();

    void push(RenderContext* oldRenderContextPtr);

private:
    void oldRenderContextCleanupMain(const bool* threadShutdownFlag);

    //------------------------------

    // Using mcrt_computation::Watcher framework to boot background thread for RenderContext deletion
    Watcher mOldRenderContextWatcher;

    std::mutex mMutexOldRenderContextTbl; // mutex for oldRenderContextTbl
    std::condition_variable mCvOldRenderContextTbl;

    // We want to control RenderContext pointer by ourselvess (i.e. do not use unique_ptr, shared_ptr)
    std::vector<RenderContext*> mOldRenderContextTbl;
};

} // namespace mcrt_computation

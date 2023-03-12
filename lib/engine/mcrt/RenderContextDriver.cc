// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#include "RenderContextDriver.h"

#include <mcrt_computation/common/mcrt_logging/McrtLogging.h>
#include <mcrt_dataio/share/util/MiscUtil.h>
#include <mcrt_dataio/share/util/SysUsage.h>
#include <mcrt_messages/RDLMessage.h>
#include <scene_rdl2/common/rec_time/RecTime.h>

#include <iostream>
#include <sstream>

namespace mcrt_computation {

RenderContextDriver::RenderContextDriver(const int driverId,
                                         const moonray::rndr::RenderOptions *renderOptions,
                                         int numMachineOverride,
                                         int machineIdOverride,
                                         McrtLogging *mcrtLogging,
                                         bool* mcrtDebugLogCreditUpdateMessage,
                                         PackTilePrecisionMode precisionMode,
                                         std::atomic<bool> *renderPrepCancel,
                                         const PostMainCallBack &postMainCallBack,
                                         const StartFrameCallBack &startFrameCallBack,
                                         const StopFrameCallBack &stopFrameCallBack)
    : mDriverId(driverId)
    , mThreadState(ThreadState::INIT)
    , mRunState(RunState::WAIT)
    , mThreadShutdown(false)
    , mNumMachinesOverride(numMachineOverride)
    , mMachineIdOverride(machineIdOverride)
    , mMcrtLogging(mcrtLogging)
    , mMcrtDebugLogCreditUpdateMessage(mcrtDebugLogCreditUpdateMessage)
    , mRenderPrepCancel(renderPrepCancel)
    , mPostMainCallBack(postMainCallBack)
    , mStartFrameCallBack(startFrameCallBack)
    , mStopFrameCallBack(stopFrameCallBack)
    , mPackTilePrecisionMode(precisionMode)
    , mRenderContext(nullptr)
    , mSceneContextBackup(nullptr)
    , mGeometryUpdate(nullptr)
    , mMultiBankTotal(1)
    , mReceivedSnapshotRequest(false)
    , mOutputRatesFrameCount(0)
    , mSyncId(0)
    , mInitFrameNonDelayedSnapshotMaxNode(0)
    , mInitFrameDelayedSnapshotStepMS(6.25f) // ms
    , mInitFrameDelayedSnapshotSec(-1.0)
    , mLastTimeRenderPrepResult(RP_RESULT::FINISHED)
    , mLastTimeOfEnoughIntervalForSend(0.0)
    , mLastSnapshotFilmActivity(0)
    , mSnapshotId(0)
    , mReloadingScene(true)
    , mRenderCounter(0)
    , mRenderCounterLastSnapshot(0)
    , mInitialSnapshotReadyTime(0.0)
    , mBeforeInitialSnapshot(false)
    , mSentCompleteFrame(true)
    , mSentFinalPixelInfoBuffer(true)
    , mSnapshotToSendTimeLog(std::make_shared<scene_rdl2::rec_time::RecTimeLog>())
    , mMcrtLoggingInfo((mcrtLogging) ? mcrtLogging->isEnableInfo() : false)
    , mMcrtLoggingDebug((mcrtLogging) ? mcrtLogging->isEnableDebug() : false)
{
    if (renderOptions) {
        mRenderOptions = *renderOptions; // save specified RenderOptions for delay construction and others
    }

    resetRenderContext();

    mFbSender.setMachineId(machineIdOverride);

    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setHostName(mcrt_dataio::MiscUtil::getHostName());
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setCpuTotal(mcrt_dataio::SysUsage::cpuTotal());
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setMemTotal(mcrt_dataio::SysUsage::memTotal());
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setMachineId(mMachineIdOverride);

    debugCommandParserConfigure();

    //------------------------------

    mThread = std::move(std::thread(threadMain, this));

    // Wait until thread is booted
    std::unique_lock<std::mutex> uqLock(mMutexBoot);
    mCvBoot.wait(uqLock, [&]{
            return (mThreadState == ThreadState::IDLE); // Not wait if state is IDLE condition
        });
}

RenderContextDriver::~RenderContextDriver()
{
    mThreadShutdown = true; // This is a only place set true to mThreadShutdown

    mRunState = RunState::START;
    mCvRun.notify_one(); // notify to RenderContextDriver threadMain loop
    
    if (mThread.joinable()) {
        mThread.join();
    }
}

void
RenderContextDriver::start()
//
// This function is calling from arras main thread
//
{
    // need to stop renderDriver here.
    // we have to stop MCRT stage or RenderPrep here if it is running

    mRunState = RunState::START;
    mCvRun.notify_one(); // notify to RenderContextDriver threadMain loop
}

//------------------------------------------------------------------------------------------

std::string
RenderContextDriver::show() const
{
    std::ostringstream ostr;
    ostr << "RenderContextDriver {\n"
         << "  mDriverId:" << mDriverId << '\n'
         << "  mThreadState:" << showThreadState(mThreadState) << '\n'
         << "  mRunState:" << showRunState(mRunState) << '\n'
         << "  mThreadShutdown:" << ((mThreadShutdown) ? "true" : "false") << '\n'
         << "}";
    return ostr.str();
}

//------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------

moonray::rndr::RenderContext *
RenderContextDriver::getRenderContext()
{
    if (!mRenderContext) {
        return resetRenderContext();
    }
    return mRenderContext.get();
}

moonray::rndr::RenderContext *
RenderContextDriver::resetRenderContext()
{
    mRenderContext.reset(new moonray::rndr::RenderContext(mRenderOptions));
    mReloadingScene = true;
    return mRenderContext.get();
}

scene_rdl2::rdl2::SceneContext *
RenderContextDriver::getSceneContextBackup()
{
    if (!mSceneContextBackup) {
        return resetSceneContextBackup();
    }
    return mSceneContextBackup.get();
}

scene_rdl2::rdl2::SceneContext *    
RenderContextDriver::resetSceneContextBackup()
{
    mSceneContextBackup.reset(new scene_rdl2::rdl2::SceneContext());
    return mSceneContextBackup.get();
}

// static function
void
RenderContextDriver::updateSceneContextBackup(scene_rdl2::rdl2::SceneContext *sceneContext,
                                              const std::string &manifest,
                                              const std::string &payload)
{
    // Apply the binary update.
    scene_rdl2::rdl2::BinaryReader reader(*sceneContext);
    reader.fromBytes(manifest, payload);
}
    
//------------------------------------------------------------------------------------------    

// static function
void
RenderContextDriver::threadMain(RenderContextDriver *driver)
{
    // First of all change thread's threadState condition and do notify_one to caller
    driver->mThreadState = ThreadState::IDLE;
    driver->mCvBoot.notify_one(); // notify to RenderContextDriver's constructor

    while (true) {
        {
            std::unique_lock<std::mutex> uqLock(driver->mMutexRun);
            driver->mCvRun.wait(uqLock, [&]{
                    return (driver->mRunState == RunState::START); // Not wait if state is START
                });
        }

        if (driver->mThreadShutdown) { // before exit test
            break;
        }

        driver->mThreadState = ThreadState::BUSY;
        if (driver->main()) { // main function of renderContextDriver thread
            if (driver->mPostMainCallBack) {
                driver->mPostMainCallBack();
            }                
        }
        driver->mThreadState = ThreadState::IDLE;
        driver->mRunState = RunState::WAIT;

        if (driver->mThreadShutdown) { // after exit test
            break;
        }
    }
}

bool
RenderContextDriver::main()
{
    MNRY_ASSERT(mRenderContext && "RenderContextDriver::main() can not run without renderContext");

    auto propagateRenderPrepCancelInfoToDownStream = [&]() {
        // propagate renderPrep cancel condition to the downstream computation
        std::lock_guard<std::mutex> lock(mMutexMcrtNodeInfoMapItem);
        mMcrtNodeInfoMapItem.getMcrtNodeInfo().setRenderPrepCancel((*mRenderPrepCancel));
    };

    if (mRenderPrepCancel) {
        if ((*mRenderPrepCancel)) {
            if (mRenderContext->isFrameRendering()) {
                // Mcrt phase already started. We should disable renderPrep cancel flag.
                (*mRenderPrepCancel) = false;
                propagateRenderPrepCancelInfoToDownStream();
            }
        }
    }

    moonray::rndr::RenderContext::RP_RESULT flag = mRenderContext->startFrame();
    mLastTimeRenderPrepResult = flag; // update last renderPrep result 

    if (mTimingRecFrame) mTimingRecFrame->setRenderPrepEndTiming();

    if (mRenderPrepCancel) {
        // We have to reset renderPrep cancel condition for the next iteration
        (*mRenderPrepCancel) = false;
        propagateRenderPrepCancelInfoToDownStream();
    }

    // This should be called after mRenderContext->startFrame(), before
    // any snapshots are taken.  It is responsible for setting up
    // the active render output buffers (including roi)- which can
    // only be known after startFrame() has been called.  This is
    // because mRenderConstext->startFrame() actually creates the
    // render output driver along with the list of active render outputs.

    // in addition to the render output buffers, we might require
    // a pixel info and heat map buffer for scratch use.  if our options
    // are set to require a pixel info buffer, we should already have it,
    // but if not, we'll need to initialize it if our rod says we need it

    const auto *rod = mRenderContext->getRenderOutputDriver(); 
    mFbSender.initRenderOutput((rod->getNumberOfRenderOutputs()) ? rod : nullptr);
    mFbSender.setPrecisionControl(mPackTilePrecisionMode);

    if (flag == moonray::rndr::RenderContext::RP_RESULT::CANCELED) {
        std::lock_guard<std::mutex> lock(mMutexMcrtNodeInfoMapItem);
        mMcrtNodeInfoMapItem.getMcrtNodeInfo().setRenderActive(false); // set renderActive = off
        return false;           // cancel
    }

    mLastSnapshotFilmActivity = 0;

    mRenderCounter++; // Update the render counter for start new render
    mInitialSnapshotReadyTime = 0.0; // reset initial snapshot ready time for new frame
    mBeforeInitialSnapshot = true; // In order to track the very first snapshot data of the new frame.
    mSentCompleteFrame = false; // Condition flag about sending a snapshot of the completed condition.
    mSentFinalPixelInfoBuffer = false;

    return true;
}

void
RenderContextDriver::showMsg(const std::string &msg, bool cerrOut)
{
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().enqGenericComment(msg);
    if (cerrOut) {
        std::cerr << msg;
    }
}

// static function
std::string
RenderContextDriver::showThreadState(const ThreadState &state)
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
RenderContextDriver::showRunState(const RunState &state)
{
    switch (state) {
    case RunState::WAIT : return "WAIT";
    case RunState::START : return "START";
    default : break;
    }
    return "?";
}

std::string
RenderContextDriver::showInitFrameControlStat() const
{
    std::ostringstream ostr;
    ostr << "initFrameControl {\n"
         << "  mInitFrameNonDelayedSnapshotMaxNode:" << mInitFrameNonDelayedSnapshotMaxNode << '\n'
         << "  mInitFrameDelayedSnapshotStepMS:" << mInitFrameDelayedSnapshotStepMS << " ms\n"
         << "  mInitFrameDelayedSnapshotSec:" << mInitFrameDelayedSnapshotSec << " sec\n"
         << "}";
    return ostr.str();
}

std::string
RenderContextDriver::showMultiBankControlStat() const
{
    std::ostringstream ostr;
    ostr << "multiBankControl {\n"
         << "  mMultiBankTotal:" << mMultiBankTotal << '\n'
         << "}";
    return ostr.str();
}

} // namespace mcrt_computation


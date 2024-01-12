// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "RenderContextDriver.h"

#include <mcrt_computation/common/mcrt_logging/McrtLogging.h>
#include <mcrt_dataio/share/util/MiscUtil.h>
#include <mcrt_dataio/share/util/SysUsage.h>
#include <mcrt_messages/RDLMessage.h>
#include <scene_rdl2/common/rec_time/RecTime.h>

#include <iostream>
#include <sstream>

// This directive adds debugFeedback module in order to verify feedback logic.
// There is a runtime debug on/off debugConsole command and it is no impact on the
// performance as long as the runtime debug sets to off even if this directive is active
#define DEBUG_FEEDBACK

namespace mcrt_computation {

RenderContextDriver::RenderContextDriver(const RenderContextDriverOptions& options)
    : mDriverId(options.driverId)
    , mNumMachinesOverride(options.numMachineOverride)
    , mMachineIdOverride(options.machineIdOverride)
    , mFps {options.fps}
    , mMcrtLogging(options.mcrtLogging)
    , mMcrtDebugLogCreditUpdateMessage(options.mcrtDebugLogCreditUpdateMessage)
    , mRenderPrepCancel(options.renderPrepCancel)
    , mPostRenderPrepMainCallBack {options.postRenderPrepMainCallBack}
    , mStartFrameCallBack {options.startFrameCallBack}
    , mStopFrameCallBack {options.stopFrameCallBack}
    , mSendInfoOnlyCallBack {options.sendInfoOnlyCallBack}
    , mSysUsage {options.sysUsage}
    , mPackTilePrecisionMode(options.precisionMode)
    , mSendBandwidthTracker {options.sendBandwidthTracker}
    , mRecvFeedbackFpsTracker(options.recvFeedbackFpsTracker)
    , mRecvFeedbackBandwidthTracker(options.recvFeedbackBandwidthTracker)
    , mMcrtLoggingInfo((options.mcrtLogging) ? options.mcrtLogging->isEnableInfo() : false)
    , mMcrtLoggingDebug((options.mcrtLogging) ? options.mcrtLogging->isEnableDebug() : false)
{
    if (options.renderOptions) {
        mRenderOptions = *(options.renderOptions); // save specified RenderOptions for delay construction and others
    }

    resetRenderContext();

    mFbSender.setMachineId(mMachineIdOverride);

    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setHostName(mcrt_dataio::MiscUtil::getHostName());
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setCpuTotal(mcrt_dataio::SysUsage::getCpuTotal());
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setAssignedCpuTotal(mRenderOptions.getThreads());
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setMemTotal(mcrt_dataio::SysUsage::getMemTotal());
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().setMachineId(mMachineIdOverride);

#   ifdef DEBUG_FEEDBACK
    mMcrtDebugFeedback = std::make_unique<McrtDebugFeedback>(5, // keep max frame count
                                                             static_cast<unsigned>(mMachineIdOverride));
#   endif // end DEBUG_FEEDBACK    

    debugCommandParserConfigure();

    //------------------------------

    mRenderPrepWatcher.boot(Watcher::RunMode::STOP_AND_GO, &mRenderPrepWatcher,
                            [&]() {
                                if (renderPrepMain()) {
                                    if (mPostRenderPrepMainCallBack) {
                                        mPostRenderPrepMainCallBack();
                                    }
                                }
                            });

    mHeartBeatWatcher.boot(Watcher::RunMode::NON_STOP, &mHeartBeatWatcher,
                           [&]() {
                               heartBeatMain();
                           });
}

RenderContextDriver::~RenderContextDriver()
{
    // In order to avoid unexpected sendInfoOnly actions, it would be better
    // if we shut down heartBeatWatcher first.
    mHeartBeatWatcher.shutDown();
    mRenderPrepWatcher.shutDown();
}

void
RenderContextDriver::start()
//
// This function is calling from arras main thread
//
{
    // need to stop renderDriver here.
    // we have to stop MCRT stage or RenderPrep here if it is running

    mRenderPrepWatcher.resume();
}

//------------------------------------------------------------------------------------------

std::string
RenderContextDriver::show() const
{
    std::ostringstream ostr;
    ostr << "RenderContextDriver {\n"
         << "  mDriverId:" << mDriverId << '\n'
         << scene_rdl2::str_util::addIndent("mRenderPrepWatcher " + mRenderPrepWatcher.show()) << '\n'
         << scene_rdl2::str_util::addIndent("mHeartBeatWatcher " + mHeartBeatWatcher.show()) << '\n'
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

bool
RenderContextDriver::renderPrepMain()
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
RenderContextDriver::heartBeatMain()
{
    float checkFps = (*mFps) * 0.5f; // try to check by half fps interval. i.e. 2x longer interval
    if (isEnoughSendIntervalHeartBeat(checkFps)) {
        std::cerr << ">> RenderContextDriver.cc heartBeatMain() need to send info\n";

        // First of all, encode McrtNodeInfo data for statistical info update by infoCodec
        std::vector<std::string> infoDataArray;
        updateInfoData(infoDataArray);

        if (infoDataArray.size()) {
            // We have to send info only progressiveFrame message here
            sendProgressiveFrameMessageInfoOnly(infoDataArray);
        }
    }

    float sleepIntervalMicroSec = (1.0f / checkFps) * 1000000.0f;
    usleep(static_cast<useconds_t>(sleepIntervalMicroSec));

    // mHeartBeatWatcher immediately executes this function again after finishing this function.
}

void
RenderContextDriver::showMsg(const std::string &msg, bool cerrOut)
{
    mMcrtNodeInfoMapItem.getMcrtNodeInfo().enqGenericComment(msg);
    if (cerrOut) {
        std::cerr << msg;
    }
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

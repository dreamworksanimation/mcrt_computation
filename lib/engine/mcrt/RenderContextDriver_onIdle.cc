// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "RenderContextDriver.h"

#include <arras4_log/Logger.h> // ARRAS_LOG_INFO
#include <mcrt_dataio/share/util/BandwidthTracker.h>
#include <mcrt_dataio/share/util/FpsTracker.h>
#include <mcrt_dataio/share/util/SysUsage.h>
#include <mcrt_messages/BaseFrame.h>
#include <moonray/rendering/mcrt_common/ExecutionMode.h>
#include <moonray/rendering/rndr/RenderContext.h>
#include <moonray/rendering/rndr/TileScheduler.h>
#include <scene_rdl2/common/rec_time/RecTime.h>

#include <random>

//#define DEBUG_MSG_START_NEWFRAME

namespace mcrt_computation {

bool
RenderContextDriver::isEnoughSendInterval(const float fps,
                                          const bool dispatchGatesFrame)
//
// This function is executed by arras thread
//
// This function only considers intervals and does not account for frame-buffer internal
// and other conditions.
// This function returns true if we are ready to send action from an interval standpoint.
// This function returns false if we have to wait more time to execute send action.
//    
{
    if (haveUpdates()) {
        // We already know some updates are backlogged and we want to stop current render
        // as soon as possible. So we set requestStopAtFrameReadyForDisplay() here.
        // There is some chance render threads can stop just after the initial render pass.
        // If so this is the earliest timing of all the MCRT threads to stop spontaneously.
        // This procedure makes a good response if renderPrep is pretty light and relatively
        // MCRT is heavy case (like in case of only camera update).
        mRenderContext->requestStopAtFrameReadyForDisplay();
        return true;
    }

    if (isMultiMachine() && dispatchGatesFrame) {
        // In a multi-machine setup, frame gating is handled upstream and
        // receiving an update indicates it's time to render the next frame;
        // Receiving a snapshot request indicates it's time to make another 
        // snapshot.
        if (mReceivedSnapshotRequest) {
            return true;
        }

    } else {
        // In a single-machine or multi-machine without dispatchGatesFrame condition,
        // frame gating is handled here.
        double currentTime = scene_rdl2::util::getSeconds();
        if ((currentTime - mLastTimeOfEnoughIntervalForSend) >= (1.0f / fps)) {
            mLastTimeOfEnoughIntervalForSend = currentTime;
            mLastTimeOfEnoughIntervalForHeartBeat = currentTime;
            return true;
        }
    }

    return false; // Interval-wise, we are not ready to send data
}

bool
RenderContextDriver::isEnoughSendIntervalHeartBeat(const float checkFps)
//
// This function is executed by heart-beat thread
//
// checkFps indicates the interval of each heart-beat action interval.
// For example, the interval is 41.666ms if checkFps is 24. (i.e. 41.666ms = 1.0sec / 24).
//
{
    if (mLastTimeOfEnoughIntervalForHeartBeat == 0.0) {
        // not set any data yet. In this case, we don't do anything.
        return false;
    }

    double currentTime = scene_rdl2::util::getSeconds();
    if ((currentTime - mLastTimeOfEnoughIntervalForHeartBeat) >= (1.0f / checkFps)) {    
        mLastTimeOfEnoughIntervalForHeartBeat = currentTime;
        return true;
    }
    return false; // Interval-wise, we are not ready to send data
}

void
RenderContextDriver::sendDelta(ProgressiveFrameSendCallBack callBackSend)
//
// This function is executed by arras thread
//    
// This function basically does snapshot and sends it to the downstream.
// This function does not send image data if it is not necessary like renderContext is not ready
// to snapshot or frame buffer information is not updated.
// Usually, statistical info codec data (= infoDataArray) is sent with delta image data by progressiveFrame.
// However, this function sends a super lightweight progressiveFrame message which only stores
// info data without image delta in some circumstances.
//    
{
    // First of all, encode McrtNodeInfo data for statistical info update by infoCodec
    std::vector<std::string> infoDataArray;
    updateInfoData(infoDataArray);

    //------------------------------

    // Make sure we are able to do a snapshot of the frame buffer.
    bool isReadyToSnapshot =
        mRenderPrepWatcher.isRunStateWait() && // renderPrep completed
        mRenderContext && 
        mRenderContext->isFrameReadyForDisplay() &&
        (mRenderContext->isFrameRendering() || mRenderContext->isFrameComplete() ||
         mRenderContext->isFrameCompleteAtPassBoundary());

    bool dataSendFlag = false;
    if (isReadyToSnapshot) {

        const int snapshotFilmActivity = mRenderContext->getFilmActivity();
        bool renderUpdate = (snapshotFilmActivity != mLastSnapshotFilmActivity);
        bool renderComplete =
            mRenderContext->isFrameRendering() &&
            (mRenderContext->isFrameComplete() || mRenderContext->isFrameCompleteAtPassBoundary());
        if (!renderUpdate && renderComplete && !mSentCompleteFrame) {
            // This is a very first time to pass through here after rendering completion under single
            // and multi mcrt configuration
            //
            // Under adaptive sampling, Sometimes rendering might be completed without any update of
            // the frame buffer at the last moment of rendering when the renderer finds frame buffer
            // condition has already achieved target adaptive threshold.
            // This situation can not be cought by tracking renderContext's filmActivity.
            // We use mSentCompleteFrame flag to track down whether the final
            // snapshot is properly sent or not.
            renderUpdate = true;
        }

        if (renderUpdate) {
            if (haveUpdates()) {
                // We already know some updates are backlogged and we want to stop current render
                // as soon as possible. However stop render involves blocking operation like busy
                // wait loop for finish all MCRT threads and this is timing wise costly.
                // So first of all, we set requestStop() call here (this is not a blocking call)
                // and start send operation.
                // There is some chance that all MCRT threads can be stopped during send.
                mRenderContext->requestStop();
            }

            dataSendFlag = false;
            if (!mRenderContext->getTiles()->empty()) { // check empty schedule or not
                double now = scene_rdl2::util::getSeconds();
                if (mInitialSnapshotReadyTime == 0.0) {
                    mInitialSnapshotReadyTime = now;
                }

                if (isReadyToSend(now)) {
                    mLastSnapshotFilmActivity = snapshotFilmActivity;

                    // Do snapshot and send to downstream by progressiveFrame message
                    if (sendProgressiveFrameMessage(!isMultiMachine(), // directoToClient
                                                    infoDataArray,
                                                    callBackSend)) {
                        // sent FINISH condition last message.
                        // We are going to call stopFrame() in order to execute the post-rendering
                        // procedure (like rendering log dump etc).
                        mRenderContext->stopFrame();
                    }
                    dataSendFlag = true;
                }
            }

            // we only want to perform snapshots once when requested; always setting this to false
            // doesn't cost anything significant and avoids unnecessary additional logic
            mReceivedSnapshotRequest = false;
        }
    }

    if (!dataSendFlag && infoDataArray.size()) {
        // We have to send info only progressiveFrame message here
        sendProgressiveFrameMessageInfoOnly(infoDataArray);
    }
}

void
RenderContextDriver::applyUpdatesAndRestartRender(const GetTimingRecFrameCallBack& getTimingRecFrameCallBack)
{
    if (!mRenderPrepWatcher.isRunStateWait()) {
        // We have to wait for the previous renderPrep should be completed.
        return;
    }

    moonray::rndr::RenderContext *renderContext = getRenderContext();
    if (!renderContext) {
        return;                 // early exit
    }
    if (mMcrtUpdates.empty() && !mGeometryUpdate) {
        return;                 // empty updates -> exit
    }

    // Apply updates if needed but not unless we've sent at least one snapshot
    if (mRenderCounterLastSnapshot < mRenderCounter) {
        return;
    }

    //------------------------------

#   ifdef DEBUG_MSG_START_NEWFRAME
    std::cerr << ">> RenderContextDriver_onIdle.cc ===>>> applyUpdatesAndRestartRender() <<<===\n";
#   endif // end DEBUG_MSG_START_NEWFRAME

    // Stop rendering. The frame may not have started yet, so only stop it if it has
    stopFrame();

    mTimingRecFrame = (getTimingRecFrameCallBack) ? getTimingRecFrameCallBack() : nullptr;

    if (!mMcrtUpdates.empty()) {
        // clean up redundant unnecessary messages
        if (!preprocessQueuedMessage()) { 
            // We don't have forceReload message, so we have to consider previous renderPrep cancel codition
            if (mLastTimeRenderPrepResult == RP_RESULT::CANCELED) {
                // previous renderPrep was canceled, so we need to reconstruct sceneContext from backup
                reconstructSceneFromBackup();
                showMsg("reconstruct sceneContext by backup\n");
            }
        }

        // Apply all the updates here
        float oldestRecvTime = 0.0f;
        float newestRecvTime = 0.0f;
        unsigned totalMsg = 0;
        for (auto itr = mMcrtUpdates.begin(); itr != mMcrtUpdates.end(); ++itr) {
            // message is already sorted by oldest to newest order
            float currRecvTime = (*itr)->getMsgRecvTiming();
            if (oldestRecvTime == 0.0f) {
                oldestRecvTime = currRecvTime;
                newestRecvTime = currRecvTime;
            } else {
                newestRecvTime = currRecvTime;
            }
            totalMsg++;
            
            (**itr)();               // process update data
        }

        if (mTimingRecFrame) {
            mTimingRecFrame->setMsgTimingRange(totalMsg, oldestRecvTime, newestRecvTime);
        }

        // Make sure our updates didn't trample our overrides
        applyConfigOverrides(); // need applyConfigOverride before start render
        mMcrtUpdates.clear();

        // There is some possibility to be updated renderContext address by rdlMessage forceReload condition.
        // We update renderContext address just in case.
        renderContext = getRenderContext();
    }

    if (renderContext->isInitialized()) {
        // Apply geometry updates.
        if (mGeometryUpdate) {
            uint32_t currSyncId = mGeometryUpdate->mFrame;
            if (currSyncId > mSyncId) mSyncId = currSyncId;

            renderContext->updateGeometry(mGeometryUpdate->mObjectData);
            mGeometryUpdate.reset();
        }

        // Start rendering.
        {
            std::ostringstream ostr;
            ostr << "Starting Rendering (syncId:" << mSyncId << ")";
#           ifdef DEBUG_MSG_START_NEWFRAME
            std::cerr << ">> RenderContextDriver_onIdle.cc ===>>> applyUpdatesAndRestartRender() " << ostr.str() << '\n';
#           endif // end DEBUG_MSG_START_NEWFRAME
            ARRAS_LOG_INFO(ostr.str());
        }

#       ifdef DEBUG_MSG_START_NEWFRAME
        std::cerr << ">> RenderContextDriver_onIdle.cc ===>>> applyUpdatesAndRestartRender() startFrame() <<<===\n";
#       endif // end DEBUG_MSG_START_NEWFRAME
        startFrame();

        mSnapshotId = 0;        // initialize snapshot id

        mMessageHistory.newFrame();
    }
}

//------------------------------------------------------------------------------------------

bool
RenderContextDriver::preprocessQueuedMessage()
//
// This function removes unnecessary messages from the unified message queue.
// For example, there is some situation like that if we have lots of rdlMessages inside a unified message
// queue but the last one (i.e. newest rdlMessage) has forceReload condition.
// In this case, we don't need to evaluate rdlMessages except the last one.
// It is enough to evaluate the last one and there is no reason to waste CPU resources.
// A similar situation might happen about some other type of messages as well.
// This function tries to set disable condition to the queued message if the message does not need to
// evaluate.
// returned forceReload condition will be used by recover from renderPrep cancel procedure.
//    
{
    bool forceReload = false;

    auto preprocessMsgReverse = [&](int startId) {
        using MsgType = McrtUpdate::MsgType;
        using MsgTypeTbl = std::vector<MsgType>;
        
        auto disableMsgReverse = [&](int startId, const MsgTypeTbl &msgTypeTbl) {
            auto checkMsgType = [&](const MsgType &msgType, const MsgTypeTbl &msgTypeTbl) -> bool {
                for (auto &itr: msgTypeTbl) { if (itr == msgType) return true; }
                return false;
            };

            for (int i = startId; i >= 0; --i) {
                McrtUpdateShPtr currMsg = mMcrtUpdates[i];
                if (currMsg->isActive() && checkMsgType(currMsg->msgType(), msgTypeTbl)) {
                    currMsg->disable();
                }
            }
        };
        
        McrtUpdateShPtr currMsg = mMcrtUpdates[startId];
        if (!currMsg->isActive()) return;

        MsgType msgType = currMsg->msgType();
        if (msgType == MsgType::RDL_FORCE_RELOAD) {
            disableMsgReverse(startId - 1, MsgTypeTbl{MsgType::RDL, MsgType::RDL_FORCE_RELOAD});
            forceReload = true;
        } else if (msgType == MsgType::RENDER_START) {
            disableMsgReverse(startId - 1, MsgTypeTbl{MsgType::RENDER_START});
        } else if (msgType == MsgType::ROI_SET ||
                   msgType == MsgType::ROI_DISABLE) {
            disableMsgReverse(startId - 1, MsgTypeTbl{MsgType::ROI_SET, MsgType::ROI_DISABLE});
        } else if (msgType == MsgType::VIEWPORT) {
            disableMsgReverse(startId - 1, MsgTypeTbl{MsgType::VIEWPORT});
        }
    };

    for (int i = static_cast<int>(mMcrtUpdates.size()) - 1; i >= 0; --i) {
        preprocessMsgReverse(i);
    }
    return forceReload;
}

bool
RenderContextDriver::isReadyToSend(const double now)
{
    //
    // We only send initial frame data by some amount of backend mcrt_computations and all the backend
    // computations don't send the initial image immediately. This is pretty important to keep good
    // interactivity under many machine contexts. If all mcrt_computations send the initial frame to merge,
    // merge computation is too busy to handle them and the client receives lots of early stage updates.
    // This causes interactivity down a lot. To keep data bandwidth in a moderate range at the early stage
    // of rendering is significant impact to the interactivity and important.
    //        
    if (mNumMachinesOverride == 1 || mNumMachinesOverride <= mInitFrameNonDelayedSnapshotMaxNode) {
        // This is single backend computation case or mcrt_computations total is less than the limit.
        // We simply send data without any special delay.
        return true;
    }

    if (mInitFrameDelayedSnapshotSec < 0.0f) {
        double nonDelayRatio =
            static_cast<double>(mInitFrameNonDelayedSnapshotMaxNode) /
            static_cast<double>(mNumMachinesOverride);

        std::random_device rnd;
        std::mt19937 mt(rnd());
        std::uniform_real_distribution<> rand(0.0, 1.0);
        double v = rand(mt);

        if (v <= nonDelayRatio) {
            mInitFrameDelayedSnapshotSec = 0.0;
        } else {
            double maxSec =
                mInitFrameDelayedSnapshotStepMS *
                static_cast<double>(mNumMachinesOverride) / 1000.0;
            double delayRatio = (v - nonDelayRatio) / (1.0 - nonDelayRatio);
            mInitFrameDelayedSnapshotSec = delayRatio * maxSec;
        }
    }

    double intervalSec = now - mInitialSnapshotReadyTime;
    return mInitFrameDelayedSnapshotSec <= intervalSec;
}

bool
RenderContextDriver::sendProgressiveFrameMessage(const bool directToClient,
                                                 std::vector<std::string>& infoDataArray,
                                                 ProgressiveFrameSendCallBack callBackSend)
//
// This function is executed by arras thread
//    
// Send frame buffer data by delta coding as progressiveFrame.
// ProgressiveFrame message is sent with a properly updated header even if the frame buffer condition is
// exactly the same as the previous send action,  In this case, the progressiveFrame message does not
// include any frame buffer pixel information and is extremely small.
//
{
    std::lock_guard<std::mutex> lock(mMutexSendAction);

    if (mTimingRecFrame) mTimingRecFrame->newSnapshotSendAction();

    scene_rdl2::rec_time::RecTime recTimeSnapshotToSend;
    recTimeSnapshotToSend.start(); // Measurement of elapsed time from snapshot to send.

    piggyBackStatsInfo(infoDataArray);

    //------------------------------
    //
    // Compute status of snapshot delta info
    //
    bool sentLastData = false;
    mcrt::BaseFrame::Status status = mcrt::BaseFrame::RENDERING;
    if (mBeforeInitialSnapshot) {
        mFbSender.fbReset();
        status = mcrt::BaseFrame::STARTED;
        mBeforeInitialSnapshot = false;
    }
    if (mRenderContext->isFrameRendering() &&
        (mRenderContext->isFrameComplete() || mRenderContext->isFrameCompleteAtPassBoundary())) {
        status = mcrt::BaseFrame::FINISHED;
        mSentCompleteFrame = true;
        sentLastData = true;
        
        std::lock_guard<std::mutex> lock(mMutexMcrtNodeInfoMapItem);
        mMcrtNodeInfoMapItem.getMcrtNodeInfo().setRenderActive(false); // set renderActive = off
    }

    mRenderCounterLastSnapshot = mRenderCounter;

    //------------------------------

    const bool doParallel = (mRenderContext->getRenderMode() == moonray::rndr::RenderMode::REALTIME);
    const bool doPixelInfo = (mRenderContext->hasPixelInfoBuffer() && !mSentFinalPixelInfoBuffer);
    const bool coarsePass = !mRenderContext->areCoarsePassesComplete();

    if (mTimingRecFrame) mTimingRecFrame->setSnapshotStartTiming();
    mFbSender.snapshotDelta(*mRenderContext, doPixelInfo, doParallel, mSnapshotId,
                            [&](const std::string &bufferName) -> bool {
                                return checkOutputRatesInterval(bufferName);
                            },
                            coarsePass);
    if (mTimingRecFrame) mTimingRecFrame->setSnapshotEndTiming();

    //------------------------------

    mcrt::ProgressiveFrame::Ptr frameMsg(new mcrt::ProgressiveFrame);

    frameMsg->mMachineId = mMachineIdOverride;
    frameMsg->mSnapshotId = mSnapshotId; // set snapshotId (this should same as latencyLog snapshotId)
    frameMsg->mSendImageActionId = mSendImageActionId; // set sendImageActionId (never reset to 0)
    frameMsg->mSnapshotStartTime = mFbSender.getSnapshotStartTime();
    frameMsg->mCoarsePassStatus = (coarsePass)? 0: 1; // 0:coarsePass 1:FinePass
    if (status == mcrt::BaseFrame::STARTED) {
        // We only update denoiserInputName once at beginning of the frame.
        if (mFbSender.getDenoiserAlbedoInputNamePtr()) {
            frameMsg->mDenoiserAlbedoInputName = *(mFbSender.getDenoiserAlbedoInputNamePtr());
        }
        if (mFbSender.getDenoiserNormalInputNamePtr()) {
            frameMsg->mDenoiserNormalInputName = *(mFbSender.getDenoiserNormalInputNamePtr());
        }
    }

    frameMsg->mHeader.mViewId = 0; // progmcrt not support multi view yet. always 0
    { // setup RezedViewport
        scene_rdl2::math::HalfOpenViewport halfOpenViewport = mRenderContext->getRezedRegionWindow();
        scene_rdl2::math::Viewport rezedViewport = scene_rdl2::math::convertToClosedViewport(halfOpenViewport);
        frameMsg->mHeader.setRezedViewport(rezedViewport.mMinX, rezedViewport.mMinY,
                                           rezedViewport.mMaxX, rezedViewport.mMaxY);
    }
    // setup viewport
    if (mFbSender.getRoiViewportStatus()) {
        scene_rdl2::math::Viewport roiViewport =
            scene_rdl2::math::convertToClosedViewport(mFbSender.getRoiViewport());
        frameMsg->mHeader.setViewport(roiViewport.mMinX, roiViewport.mMinY,
                                      roiViewport.mMaxX, roiViewport.mMaxY);
    } else {
        frameMsg->mHeader.mViewport.reset();
    }
    frameMsg->mHeader.mFrameId = mSyncId;
    frameMsg->mHeader.mStatus = status;
    frameMsg->mHeader.mProgress = getRenderProgress();

    mSnapshotId++; // increment snapshotId
    mSendImageActionId++; // this id is never reset and first send data has 0

    //------------------------------

    size_t dataSizeTotal = 0;

    // beauty
    mFbSender.addBeautyToProgressiveFrame
        (directToClient,
         [&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
            frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));
            dataSizeTotal += dataSize;
        });

    // pixelInfo
    if (mFbSender.getPixelInfoStatus() && !mSentFinalPixelInfoBuffer) {
        mFbSender.addPixelInfoToProgressiveFrame
            ([&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
                frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));
                dataSizeTotal += dataSize;
            });
        if (!coarsePass) {
            mSentFinalPixelInfoBuffer = true;
        }
    }

    // heatMap
    if (mFbSender.getHeatMapStatus() && !mFbSender.getHeatMapSkipCondition()) {
        mFbSender.addHeatMapToProgressiveFrame
            (directToClient,
             [&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
                frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));
                dataSizeTotal += dataSize;
            });
    }

    // weight buffer
    if (mFbSender.getWeightBufferStatus() && !mFbSender.getWeightBufferSkipCondition()) {
        mFbSender.addWeightBufferToProgressiveFrame
            ([&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
                frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));
                dataSizeTotal += dataSize;
            });
    }

    // renderBufferOdd
    if (mFbSender.getRenderBufferOddStatus() && !mFbSender.getRenderBufferOddSkipCondition()) {
        mFbSender.addRenderBufferOddToProgressiveFrame
            (directToClient,
             [&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
                frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));                
                dataSizeTotal += dataSize;
            });
    }

    // renderOutput : AOVs
    if (mFbSender.getRenderOutputTotal()) {
        mFbSender.addRenderOutputToProgressiveFrame
            (directToClient,
             [&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
                frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));
                dataSizeTotal += dataSize;
            });
    }
             
    // latencyLog staff
    mFbSender.addLatencyLog
        ([&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
            frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));
            dataSizeTotal += dataSize;
        });

    if (mTimingRecFrame) {
        mTimingRecFrame->setSendSnapshotTiming();
        piggyBackTimingRec(infoDataArray);
    }

    // auxInfo
    if (infoDataArray.size()) {
        mFbSender.addAuxInfoToProgressiveFrame
            (infoDataArray,
             [&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
                frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));
                dataSizeTotal += dataSize;
            });
    }

    //------------------------------

    callBackSend(frameMsg, mSource);
    if (mFeedbackActiveRuntime || mProgressiveFrameRecMode) {
        mSentImageCache.enqueueMessage(frameMsg);
    }

    //------------------------------

    mOutputRatesFrameCount++; // update frame counter for output rates control

    mSendBandwidthTracker.set(dataSizeTotal);
    mSnapshotToSendTimeLog->add(recTimeSnapshotToSend.end());

    return sentLastData;
}

void
RenderContextDriver::sendProgressiveFrameMessageInfoOnly(std::vector<std::string>& infoDataArray)
//
// This function is executed by arras thread
//
// This function sends auxInfo data only without delta image in order to update statistical information for
// downstream.
//
{
    if (!infoDataArray.size()) {
        return;                 // early exit just in case
    }

    std::lock_guard<std::mutex> lock(mMutexSendAction);

    piggyBackStatsInfo(infoDataArray);

    //------------------------------

    mcrt::ProgressiveFrame::Ptr frameMsg(new mcrt::ProgressiveFrame);

    //
    // We are using progress value as special condition flag.
    // mProgress : 0.0 or bigger (positive value) : usually up to 1.0f
    //             Standard progressiveFrame message which contains frame buffer information.
    // mProgress : negative value
    //             No frame buffer info but includes text base information. This is not an image.
    //             All other header information is useless.
    //
    frameMsg->mMachineId = mMachineIdOverride;
    frameMsg->mSnapshotId = 0; // dummy information. This message is not related to snapshot logic
    frameMsg->mHeader.mStatus = mcrt::BaseFrame::CANCELLED; // dummy information for auxInfo only
    frameMsg->mHeader.mProgress = -1.0f; // negative value is text base info data
    frameMsg->mHeader.mFrameId = mSyncId;

    size_t dataSizeTotal = 0;

    // auxInfo
    mFbSender.addAuxInfoToProgressiveFrame
        (infoDataArray,
         [&](const DataPtr &data, const size_t dataSize, const char *aovName, const EncodingType enco) {
            frameMsg->addBuffer(data, dataSize, aovName, encoTypeConvert(enco));
            dataSizeTotal += dataSize;
        });

    //------------------------------

    mSendInfoOnlyCallBack(frameMsg, mSource);

    //------------------------------

    mSendBandwidthTracker.set(dataSizeTotal);
}

float
RenderContextDriver::getRenderProgress()
{
    if (!mRenderContext) return 0.0f; // Nothing to do
    return mRenderContext->getFrameProgressFraction(nullptr, nullptr);
}

float
RenderContextDriver::getGlobalRenderProgress()
{
    if (!mRenderContext) return 0.0f; // Nothing to do
    return mRenderContext->getMultiMachineGlobalProgressFraction();
}

bool
RenderContextDriver::checkOutputRatesInterval(const std::string &name)
{
    // Each render output can be configured to only be sent every N
    // frames. This function returns true if the given output
    // should be sent this frame

    // Check if complete renders sent every frame
    if (mRenderContext->isFrameComplete() && mOutputRates.getSendAllWhenComplete()) {
        return true;
    }

    unsigned interval, offset;
    mOutputRates.getRate(name, interval, offset);

    // output is first sent at frame #offset, and thereafter every #interval frames.
    if (interval == 0 || mOutputRatesFrameCount < offset) {
        return false;
    }
    
    return (mOutputRatesFrameCount -offset) % interval == 0;
}

void
RenderContextDriver::updateInfoData(std::vector<std::string>& infoDataArray)
{
    std::string infoData;
    std::lock_guard<std::mutex> lock(mMutexMcrtNodeInfoMapItem);

    if (mSysUsage.isCpuUsageReady()) {
        //
        // update CPU/Memory usage info
        //
        mcrt_dataio::McrtNodeInfo &mcrtNodeInfo = mMcrtNodeInfoMapItem.getMcrtNodeInfo();
        mcrtNodeInfo.setCpuUsage(mSysUsage.getCpuUsage());
        mcrtNodeInfo.setCoreUsage(mSysUsage.getCoreUsage());
        mcrtNodeInfo.setMemUsage(mSysUsage.getMemUsage());

        updateNetIO();
        updateExecModeMcrtNodeInfo();
    }

    if (mMcrtNodeInfoMapItem.encode(infoData)) {
        infoDataArray.push_back(std::move(infoData));
    }
}

void
RenderContextDriver::updateExecModeMcrtNodeInfo()
{
    auto convertExecMode = [](const moonray::mcrt_common::ExecutionMode& execMode) {
        switch (execMode) {
        case moonray::mcrt_common::ExecutionMode::AUTO : return mcrt_dataio::McrtNodeInfo::ExecMode::AUTO;
        case moonray::mcrt_common::ExecutionMode::VECTORIZED : return mcrt_dataio::McrtNodeInfo::ExecMode::VECTOR;
        case moonray::mcrt_common::ExecutionMode::SCALAR : return mcrt_dataio::McrtNodeInfo::ExecMode::SCALAR;
        case moonray::mcrt_common::ExecutionMode::XPU : return mcrt_dataio::McrtNodeInfo::ExecMode::XPU;
        default : return mcrt_dataio::McrtNodeInfo::ExecMode::UNKNOWN;
        }
    };

    moonray::rndr::RenderContext* renderContext = getRenderContext();
    mcrt_dataio::McrtNodeInfo::ExecMode execModeMcrtNodeInfo =
        convertExecMode(renderContext->getCurrentExecutionMode());

    mcrt_dataio::McrtNodeInfo &mcrtNodeInfo = mMcrtNodeInfoMapItem.getMcrtNodeInfo();
    if (mcrtNodeInfo.getExecMode() != execModeMcrtNodeInfo) {
        mcrtNodeInfo.setExecMode(execModeMcrtNodeInfo);
    }
}

void
RenderContextDriver::updateNetIO()
{
    mSysUsage.updateNetIO(); // update netIO info
    mcrt_dataio::McrtNodeInfo &mcrtNodeInfo = mMcrtNodeInfoMapItem.getMcrtNodeInfo();
    mcrtNodeInfo.setNetRecvBps(mSysUsage.getNetRecv());
    mcrtNodeInfo.setNetSendBps(mSysUsage.getNetSend());
}

void
RenderContextDriver::piggyBackStatsInfo(std::vector<std::string>& infoDataArray)
{
    if (mSnapshotToSendTimeLog->getTotal() > 24) {
        //
        // every 24 send action, we update SnapshotToSend time average info
        //
        float ms = mSnapshotToSendTimeLog->getAverage() * 1000.0f; // millisec
        mSnapshotToSendTimeLog->reset();

        std::lock_guard<std::mutex> lock(mMutexMcrtNodeInfoMapItem);
        mMcrtNodeInfoMapItem.getMcrtNodeInfo().setSnapshotToSend(ms);
    }

    {
        //
        // update sendBandwidth, feedback related info, and renderProgress info
        //
        std::lock_guard<std::mutex> lock(mMutexMcrtNodeInfoMapItem);
        mcrt_dataio::McrtNodeInfo &mcrtNodeInfo = mMcrtNodeInfoMapItem.getMcrtNodeInfo();
        
        float sendBps = mSendBandwidthTracker.getBps(); // Byte/Sec
        mcrtNodeInfo.setSendBps(sendBps); // update outgoing bandwidth

        bool feedbackActive = mFeedbackActiveRuntime;
        mcrtNodeInfo.setFeedbackActive(feedbackActive);
        if (feedbackActive) {
            // We only update feedback related info when feedback is active
            float feedbackInterval = mFeedbackIntervalSec;
            float recvFeedbackBps =
                (mRecvFeedbackBandwidthTracker) ? mRecvFeedbackBandwidthTracker->getBps() : 0.0f; // Byte/Sec
            float recvFeedbackFps =
                (mRecvFeedbackFpsTracker) ? mRecvFeedbackFpsTracker->getFps() : 0.0f; // fps
            float feedbackEvalLogSec = mFeedbackEvalLog.getAvg(); // millisec
            float feedbackLatencySec = mFeedbackLatency.getAvg(); // millisec
            mcrtNodeInfo.setFeedbackInterval(feedbackInterval); // sec
            mcrtNodeInfo.setRecvFeedbackFps(recvFeedbackFps); // update incoming feedback fps
            mcrtNodeInfo.setRecvFeedbackBps(recvFeedbackBps); // update incoming feedback bandwidth
            mcrtNodeInfo.setEvalFeedbackTime(feedbackEvalLogSec);
            mcrtNodeInfo.setFeedbackLatency(feedbackLatencySec);
        }
        
        mcrtNodeInfo.setProgress(getRenderProgress()); // progress fraction update
        mcrtNodeInfo.setGlobalProgress(getGlobalRenderProgress()); // global progress fraction update

        //
        // encode piggy back info
        //
        std::string infoData;
        if (mMcrtNodeInfoMapItem.encode(infoData)) {
            infoDataArray.push_back(std::move(infoData));
        }
    }
}

void
RenderContextDriver::piggyBackTimingRec(std::vector<std::string> &infoDataArray)
{
    if (mTimingRecFrame->is1stFrame()) {
        std::lock_guard<std::mutex> lock(mMutexMcrtNodeInfoMapItem);        
        mcrt_dataio::McrtNodeInfo &mcrtNodeInfo = mMcrtNodeInfoMapItem.getMcrtNodeInfo();

        // mcrtNodeInfo.enqGenericComment(mTimingRecFrame->show());

        mcrtNodeInfo.setGlobalBaseFromEpoch(mTimingRecFrame->getGlobalBaseFromEpoch());
        mcrtNodeInfo.setMsgRecvTotal(mTimingRecFrame->getTotalMsg());
        mcrtNodeInfo.setOldestMsgRecvTiming(mTimingRecFrame->getOldestMsgRecvTiming());
        mcrtNodeInfo.setNewestMsgRecvTiming(mTimingRecFrame->getNewestMsgRecvTiming());
        mcrtNodeInfo.setRenderPrepStartTiming(mTimingRecFrame->getRenderPrepStartTiming());
        mcrtNodeInfo.setRenderPrepEndTiming(mTimingRecFrame->getRenderPrepEndTiming());
        mcrtNodeInfo.set1stSnapshotStartTiming(mTimingRecFrame->getSnapshotStartTiming());
        mcrtNodeInfo.set1stSnapshotEndTiming(mTimingRecFrame->getSnapshotEndTiming());
        mcrtNodeInfo.set1stSendTiming(mTimingRecFrame->getSendTiming());

        std::string infoData;
        if (mMcrtNodeInfoMapItem.encode(infoData)) {
            infoDataArray.push_back(std::move(infoData));
        }
    }
}

mcrt::BaseFrame::ImageEncoding
RenderContextDriver::encoTypeConvert(moonray::engine_tool::ImgEncodingType enco) const
{
    mcrt::BaseFrame::ImageEncoding encoType = mcrt::BaseFrame::ENCODING_UNKNOWN;
    switch (enco) {
    case moonray::engine_tool::ImgEncodingType::ENCODING_UNKNOWN:
        encoType = mcrt::BaseFrame::ENCODING_UNKNOWN;
        break;
    case moonray::engine_tool::ImgEncodingType::ENCODING_RGBA8:
        encoType = mcrt::BaseFrame::ENCODING_RGBA8;
        break;
    case moonray::engine_tool::ImgEncodingType::ENCODING_RGB888:
        encoType = mcrt::BaseFrame::ENCODING_RGB888;
        break;
    case moonray::engine_tool::ImgEncodingType::ENCODING_LINEAR_FLOAT:
        encoType = mcrt::BaseFrame::ENCODING_LINEAR_FLOAT;
        break;
    case moonray::engine_tool::ImgEncodingType::ENCODING_FLOAT:
        encoType = mcrt::BaseFrame::ENCODING_FLOAT;
        break;
    case moonray::engine_tool::ImgEncodingType::ENCODING_FLOAT2:
        encoType = mcrt::BaseFrame::ENCODING_FLOAT2;
        break;
    case moonray::engine_tool::ImgEncodingType::ENCODING_FLOAT3:
        encoType = mcrt::BaseFrame::ENCODING_FLOAT3;
        break;
    }
    return encoType;
}

void
RenderContextDriver::applyConfigOverrides()
{
    // we now honor the setting from the config file and can now support a render mode besides progressive
    mRenderContext->setRenderMode(mRenderOptions.getRenderMode());

    scene_rdl2::rdl2::SceneVariables &sceneVars = mRenderContext->getSceneContext().getSceneVariables();
    {    
        scene_rdl2::rdl2::SceneObject::UpdateGuard guard(&sceneVars);

        if (mNumMachinesOverride >= 0) {
            sceneVars.set(scene_rdl2::rdl2::SceneVariables::sNumMachines, mNumMachinesOverride);
        }
        if (mMachineIdOverride >= 0) {
            sceneVars.set(scene_rdl2::rdl2::SceneVariables::sMachineId, mMachineIdOverride);
        }

        // We found tileSchedulerType has a big impact to the MCRT efficiency if a scene is a texture heavy.
        // MORTON would maximize texture cache access coherency and be much better than other types.
        // We use MORTAON as default for this reason.
        // However, RANDOM might be a pretty interesting choice if texture access is not critial with
        // many multi-machine (like 100 or more) configurations. Need more testing.
        int tileSchedulerType = moonray::rndr::TileScheduler::MORTON;
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sBatchTileOrder, tileSchedulerType);
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sProgressiveTileOrder, tileSchedulerType);
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sCheckpointTileOrder, tileSchedulerType);

        // Under multi-machine configuration, we use time-based checkpoint mode.
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sCheckpointMode, 0); // time-based checkpoint mode

        // And time budget is reasonably short in order to make many pass boundaries which is the
        // timing of stop rendering at multi mcrt computation
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sCheckpointInterval, 3.0f/60.0f); // 3sec

        // We are using checkpoint mode but we don't need a checkpoint file. So we need to set a pretty
        // high number for checkpointStartSPP. Internally SPP is converted to tile based sampling total.
        // In order to avoid overflow, the max number is divided by 64.
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sCheckpointStartSPP,
                      std::numeric_limits<int>::max() / 64);

        // disable snapshot action for interruption by signal
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sCheckpointSnapshotInterval, 0.0f);
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sCheckpointMaxSnapshotOverhead, 0.0f);
    }
}

} // namespace mcrt_computation

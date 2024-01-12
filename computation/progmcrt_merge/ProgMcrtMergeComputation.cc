// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ProgMcrtMergeComputation.h"
#include "ProgMcrtMergeClockDeltaDriver.h"

#include <moonray/rendering/rndr/rndr.h>
#include <moonray/rendering/rndr/TileScheduler.h>

#include <arras4_log/Logger.h>

#include <mcrt_dataio/engine/mcrt/McrtControl.h>
#include <mcrt_dataio/engine/merger/FbMsgSingleFrame.h>
#include <mcrt_messages/CreditUpdate.h>
#include <mcrt_messages/ProgressiveFeedback.h>
#include <mcrt_messages/ProgressiveFrame.h>
#include <mcrt_messages/RenderMessages.h>
#include <mcrt_messages/ViewportMessage.h>

#include <scene_rdl2/common/grid_util/LatencyLog.h>
#include <scene_rdl2/common/platform/Platform.h>
#include <scene_rdl2/render/cache/CacheDequeue.h>
#include <scene_rdl2/render/cache/CacheEnqueue.h>
#include <scene_rdl2/render/util/StrUtil.h>

#include <cmath> // round()
#include <cstdlib>
#include <stdint.h>
#include <string>

// This directive is used only for development purpose.
// This directive shows some debug messages to the stderr in order to make
// shortest latency of message display to the ssh windows.
// However this directive *** SHOULD BE COMMENTED OUT *** for release version.
//#define DEVELOP_VER_MESSAGE

//#define DEBUG_MESSAGE_SEND_PROGRESSIVE

#define USE_RAAS_DEBUG_FILENAME

namespace mcrt_computation {

COMPUTATION_CREATOR(ProgMcrtMergeComputation);

ProgMcrtMergeComputation::ProgMcrtMergeComputation(arras4::api::ComputationEnvironment* env)
    : Computation(env)
    , mMsgSendHandler(std::make_shared<mcrt_dataio::MsgSendHandler>())
    , mGlobalNodeInfo(/* decodeOnly = */ false,
                      /* valueKeepDurationSec = */ 0.0f,
                      mMsgSendHandler)
{
#   ifdef USE_RAAS_DEBUG_FILENAME
    char* delayFilename = getenv("RAAS_DEBUG_FILENAME");
    if (delayFilename) {
        std::cerr << ">> ProgMcrtMergeComputation.cc debug wait loop START"
                  << " delayFilename:" << delayFilename << std::endl;
        while (access(delayFilename, R_OK)) {
            unsigned int const DELTA_TIME = 3;
            sleep(DELTA_TIME);
            std::cerr << ">> ProgMcrtMergeComputation.cc sleep pid:" << (size_t)getpid() << std::endl;
        }
        std::cerr << ">> ProgMcrtMergeComputation.cc debug wait loop END" << std::endl;
    }
#   endif // end USE_RAAS_DEBUG_FILENAME

    // TODO : 
    // Currently mPartialMergeTilesTotal is hard coded as 2048 at this moment.
    // As a result, output bandwidth is adjust around 5~6MByte/sec for Glendale DWA render farm.
    // This number should be dynamically updated based on availability of bandwidth between
    // merger and client. Related tickets are MOONRAY-3139, MOONRAY-3107
}
    
ProgMcrtMergeComputation::~ProgMcrtMergeComputation()
{
}

arras4::api::Result
ProgMcrtMergeComputation::configure(const std::string& op,
                                    arras4::api::ObjectConstRef& aConfig)
{
    mSysUsage.getCpuUsage();            // do initial call for CPU usage monitor

    if (op == "start") {
        onStart();
        return arras4::api::Result::Success;
    } else if (op == "stop") {
        return arras4::api::Result::Success;
    } else if (op != "initialize") {
        return arras4::api::Result::Unknown; 
    }

    if (aConfig["numMachines"].isIntegral()) {
        mNumMachines = aConfig["numMachines"].asInt();
        MNRY_ASSERT_REQUIRE(mNumMachines > 0);

    } else {
        MNRY_ASSERT_REQUIRE(false,
            "numMachines is a required config setting for the progmcrt_merge computation");
    }

    if (aConfig["fps"].isNumeric()) {
        mFps = aConfig["fps"].asFloat();
        mFpsSet = true;
    }

    if (aConfig["partialMergeRefreshInterval"].isNumeric()) {
        mPartialMergeRefreshInterval = aConfig["partialMergeRefreshInterval"].asFloat();
    }

    if (aConfig[arras4::api::ConfigNames::maxThreads].isIntegral()) {
        mNumThreads = aConfig[arras4::api::ConfigNames::maxThreads].asInt();
    } else {
        mNumThreads = tbb::task_scheduler_init::default_num_threads();
    }

    if (aConfig["packTilePrecision"].isString()) {
        if (aConfig["packTilePrecision"].asString() == "auto32") {
            // auto switching UC8 and H16 for coarse pass, F32 for non-coarse pass
            mPackTilePrecisionMode = PackTilePrecisionMode::AUTO32;
            ARRAS_LOG_INFO("PackTile precision auto32 mode");
        } else if (aConfig["packTilePrecision"].asString() == "auto16") {
            // auto switching UC8 and H16 for coarse pass, H16 for non-coarse pass
            mPackTilePrecisionMode = PackTilePrecisionMode::AUTO16;
            ARRAS_LOG_INFO("PackTile precision auto16 mode");
        } else if (aConfig["packTilePrecision"].asString() == "full32") {
            // always use F32
            mPackTilePrecisionMode = PackTilePrecisionMode::FULL32;
            ARRAS_LOG_INFO("PackTile precision full32 mode");
        } else if (aConfig["packTilePrecision"].asString() == "full16") {
            // always use F16
            mPackTilePrecisionMode = PackTilePrecisionMode::FULL16;
            ARRAS_LOG_INFO("PackTile precision full16 mode");
        }
    }

    if (aConfig["initialCredit"].isIntegral()) {
        mInitialCredit = aConfig["initialCredit"].asInt();
    }
    
    if (aConfig["sendCredit"].isBool()) {
        mSendCredit = aConfig["sendCredit"].asBool();
    }

    return arras4::api::Result::Success;
}

void
ProgMcrtMergeComputation::onStart()
{  
    parserConfigureGenericMessage();
    parserConfigureDebugCommand();
    parserConfigureDebugCommandSnapshotDeltaRec();
    parserConfigureDebugCommandInitialFrame();

#   ifdef DEVELOP_VER_MESSAGE
    std::cerr << ">>> hostName: " << mcrt_dataio::MiscUtil::getHostName() << std::endl;
    std::cerr << ">>> ProgMcrtMergeComputation.cc -A-" << std::endl;
#   endif // end DEVELOP_VER_MESSAGE

    // reset current credit to initial credit. If this is >= 0, credit rate control will be enabled
    // otherwise it is inactive
    mCredit = mInitialCredit;
    
    {
        mGlobalNodeInfo.setMergeHostName(mcrt_dataio::MiscUtil::getHostName());
        mGlobalNodeInfo.setMergeClockDeltaSvrPort(20202); // hard coded port number
        mGlobalNodeInfo.setMergeClockDeltaSvrPath("/tmp/progmcrt_merge.ipc");
        mGlobalNodeInfo.setMergeMcrtTotal(mNumMachines);
        mGlobalNodeInfo.setMergeCpuTotal(mcrt_dataio::SysUsage::getCpuTotal());
        mGlobalNodeInfo.setMergeMemTotal(mcrt_dataio::SysUsage::getMemTotal());
    }
    mFbMsgMultiFrames.reset(new mcrt_dataio::FbMsgMultiFrames(&mGlobalNodeInfo, &mFeedbackActive));
    mFbMsgMultiFrames->setTunnelMachineIdInfo(&mTunnelMachineId);

    int totalCacheFrames = 2;
    if (!mFbMsgMultiFrames->initTotalCacheFrames(totalCacheFrames) ||
        !mFbMsgMultiFrames->initNumMachines(mNumMachines)) {
#       ifdef DEVELOP_VER_MESSAGE
        std::cerr << ">>> ProgMcrtMergeComputation.cc fbArray setup failed" << std::endl;
#       endif // end DEVELOP_VER_MESSAGE
    }

    {
#       ifdef DEVELOP_VER_MESSAGE
        std::cerr << ">> ProgMcrtMergeComputation.cc set TBB numThreads:" << mNumThreads << std::endl;
#       endif // end DEVELOP_VER_MESSAGE
        mTaskScheduler = new tbb::task_scheduler_init(mNumThreads);
    }

    //------------------------------

    // mDebugFeedback is a runtime verify functionality for the feedback logic.
    // And there is a runtime activation debugConsole command.
    // It is no impact on the performance as long as the runtime debug sets to off even if mDebugFeedback
    // is allocated.
    mDebugFeedback = std::make_unique<ProgMcrtMergeDebugFeedback>(5, // keep max frame count
                                                                  mNumMachines);

    //------------------------------

    // setup generic message send handler
    mMsgSendHandler->set([&](const std::string& sendMsg) {
            mcrt::GenericMessage::Ptr msg(new mcrt::GenericMessage);
            msg->mValue = sendMsg;
            send(msg);
            if (mCredit > 0) mCredit--;
        });

    ProgMcrtMergeClockDeltaDriver::init(mGlobalNodeInfo);
}

void 
ProgMcrtMergeComputation::setSource(arras4::api::ObjectConstRef source)
{
    if (source.isString()) {
        arras4::api::UUID uuid(source.asString());
        if (!uuid.isNull())
            mSource = uuid.toString();
    }
}

void
ProgMcrtMergeComputation::onIdle()
{
    std::vector<std::string> infoDataArray;
    bool doSendInfo = false;
    {
        // We have to respect user-defined fps intervals for info as well.
        double currInterval = scene_rdl2::util::getSeconds() - mLastInfoPacketSentTime;
        double minInterval = 1.0 / static_cast<double>(mFps);
        if (minInterval <= currInterval) {
            if (mSysUsage.isCpuUsageReady()) {
                //
                // update CPU/Memory usage info
                //
                mGlobalNodeInfo.setMergeCpuUsage(mSysUsage.getCpuUsage());
                mGlobalNodeInfo.setMergeCoreUsage(mSysUsage.getCoreUsage());
                mGlobalNodeInfo.setMergeMemUsage(mSysUsage.getMemUsage());

                updateNetIO();
            }

            std::string tmp;
            doSendInfo = mGlobalNodeInfo.encode(tmp);
            infoDataArray.push_back(tmp);
        }
    }

    if (!mHasProgressiveFrame && !doSendInfo) return;

    bool doSendProgressiveFrame = false;
    if (mHasProgressiveFrame) {
        if (mFbMsgMultiFrames->getDisplayFbMsgSingleFrame()->getReceivedMessagesTotal() &&
            (mCredit != 0)) {
            doSendProgressiveFrame = true;
        }
        mHasProgressiveFrame = false;
    }
    if (!doSendProgressiveFrame && !doSendInfo) return;

    //------------------------------
    //
    // send data to the client
    //
    if (doSendProgressiveFrame) {
        if (!decodeMergeSendProgressiveFrame(infoDataArray)) {
            return;
        }
    } else {
        if (doSendInfo) {
            mFbMsgMultiFrames->getDisplayFbMsgSingleFrame()->resetLastInfoOnlyHistory();
            sendInfoOnlyProgressiveFrame(infoDataArray);
        }
    }

    if (mFirstFrame == true) {
        mcrt::GenericMessage::Ptr firstFrameMsg(new mcrt::GenericMessage);
        firstFrameMsg->mValue = "MCRT Rendered First Frame";
        send(firstFrameMsg, arras4::api::withSource(mSource));
        if (mCredit > 0) mCredit--;
        mFirstFrame = false;
        ARRAS_LOG_INFO("McrtMerge Sent first frame message");
    }
}

arras4::api::Result
ProgMcrtMergeComputation::onMessage(const arras4::api::Message& aMsg)
{
    if (aMsg.classId() == mcrt::ProgressiveFrame::ID) {
        // We call timeLogStart() everytime when message is received however log start logic is only
        // executed by first one.
        mFbSender.timeLogStart();

        if (mPrevRecvMsg != mcrt::ProgressiveFrame::ID) {
#           ifdef DEVELOP_VER_MESSAGE
            std::cerr << ">> ProgMcrtMergeComputation.cc ==>> ProgressiveFrame" << std::endl;
            mPrevRecvMsg = mcrt::ProgressiveFrame::ID;
#           endif // end DEVELOP_VER_MESSAGE
        }

        if (mSendCredit)
            sendCredit(aMsg);

        arras4::api::Object source = aMsg.get(arras4::api::MessageData::sourceId);
        setSource(source);
        
        mcrt::ProgressiveFrame::ConstPtr progressive = aMsg.contentAs<mcrt::ProgressiveFrame>();
        {
            std::vector<uint32_t> logData;
            logData.resize(2);
            logData[0] = (uint32_t)progressive->mMachineId;
            logData[1] = (uint32_t)progressive->mSnapshotId;
            mFbSender.timeLogEnq(scene_rdl2::grid_util::LatencyItem::Key::RECV_PROGRESSIVEFRAME_START, logData);
        }
        recvBpsUpdate(progressive);

        if (progressive->getProgress() >= 0.0f) {
            bool quickUpdate = true;
            bool forceUpdate = false;
            if (mParallelInitialFrameUpdateMode) {
                // parallel initial frame update mode = ON
                if (mFbMsgMultiFrames) {
                    if (mFbMsgMultiFrames->getDisplayFbMsgSingleFrame()) {
                        quickUpdate =
                            mFbMsgMultiFrames->getDisplayFbMsgSingleFrame()->
                            isInitialFrameMessage(*progressive, forceUpdate);
                    }
                }
            } else {
                // parallel initial frame update mode = OFF
                uint32_t currRecvSyncId = progressive->mHeader.mFrameId;
                if (mCurrActiveRecvSyncId == UINT32_MAX) {
                    mCurrActiveRecvSyncId = currRecvSyncId;
                } else {
                    if (mCurrActiveRecvSyncId < currRecvSyncId) {
                        mCurrActiveRecvSyncId = currRecvSyncId;
                    } else {
                        quickUpdate = false;
                    }
                }
            }

            if (quickUpdate) {
                onViewportChanged(*progressive);
                double now = scene_rdl2::util::getSeconds();
                if (forceUpdate || (now - mLastPacketSentTime) > 1.0 / static_cast<double>(mFps)) {
                    //
                    // We received a new syncId message and also it is enough interval from previous send.
                    // This is the 1st packet of the new frame and needs to be sent as soon as possible
                    // for good interactivity. So in this case, we send received data immediately to the
                    // client first. After that we process this data internally. This works quite well and
                    // provides pretty good interactivity even if we are using around 32 mcrt. Interactive
                    // performance drop is pretty small.
                    //
                    size_t dataSize = progressive->serializedLength();
                    m1stSendBandwidthTracker.set(dataSize);
                    /* useful debug info
                    float sendBPS = m1stSendBandwidthTracker.getBps();
                    std::cerr << ">> ProgMcrtMergeComputation.cc 1st packet"
                              << " syncId:" << progressive->mHeader.mFrameId
                              << " machineId:" << progressive->mMachineId
                              << " forceUpdate:" << scene_rdl2::str_util::boolStr(forceUpdate)
                              << " delta:" << (now - mLastPacketSentTime) * 1000.0f << " ms"
                              << " send:" << scene_rdl2::str_util::byteStr((size_t)sendBPS) << " byte/sec"
                              << '\n';
                    */
                    mSendBandwidthTracker.set(dataSize);
                    send(progressive, arras4::api::withSource(mSource));
                    mLastPacketSentTime = scene_rdl2::util::getSeconds();
                }
            }
        }

        if (!mFbMsgMultiFrames->push(*progressive, mFeedbackInitCallBack)) {
#           ifdef DEVELOP_VER_MESSAGE
            std::cerr << ">>> ProgMcrtMergeComputation.cc mFbMsgMultiFrames.push() failed" << std::endl;
#           endif // end DEVELOP_VER_MESSAGE
        }
        if (progressive->getProgress() >= 0.0f) {
            mHasProgressiveFrame = true;
        }

        mFbSender.timeLogEnq(scene_rdl2::grid_util::LatencyItem::Key::RECV_PROGRESSIVEFRAME_END);

    } else if (aMsg.classId() == mcrt::GenericMessage::ID) {
        handleGenericMessage(aMsg.contentAs<mcrt::GenericMessage>());
    } else if (aMsg.classId() == mcrt::JSONMessage::ID) {
        onJSONMessage(aMsg.contentAs<mcrt::JSONMessage>());
    } else if (aMsg.classId() == mcrt::CreditUpdate::ID) {
        onCreditUpdate(aMsg);
    } else {
        return arras4::api::Result::Unknown;
    }
    return arras4::api::Result::Success;
}

void 
ProgMcrtMergeComputation::onCreditUpdate(const arras4::api::Message& msg)
{
    if (mCredit >= 0) {
        mcrt::CreditUpdate::ConstPtr c = msg.contentAs<mcrt::CreditUpdate>();
        if (c) c->applyTo(mCredit,mInitialCredit);  
    }
}
  
void
ProgMcrtMergeComputation::sendCredit(const arras4::api::Message& msg)
{
    // send credit back to an mcrt that has send us a frame
    mcrt::CreditUpdate::Ptr creditMsg = std::make_shared<mcrt::CreditUpdate>();
    creditMsg->value() = 1;
    arras4::api::ObjectConstRef fromAddr = msg.get("from");
    send(creditMsg, arras4::api::sendTo(fromAddr));
}

void
ProgMcrtMergeComputation::onViewportChanged(const mcrt::BaseFrame& msg)
{
    bool shouldReinit = false;
    scene_rdl2::math::Viewport currViewport(msg.getRezedViewport().minX(), msg.getRezedViewport().minY(),
                                       msg.getRezedViewport().maxX(), msg.getRezedViewport().maxY());
    if (currViewport != mRezedViewport) {
        shouldReinit = true;
        mRezedViewport = currViewport;
    }
    if (msg.getViewport().hasViewport()) {
        scene_rdl2::math::Viewport currRoiViewport(msg.getViewport().minX(), msg.getViewport().minY(),
                                              msg.getViewport().maxX(), msg.getViewport().maxY());
        if (!mRoiViewportStatus) {
            shouldReinit = true;
            mRoiViewportStatus = true;
            mRoiViewport = currRoiViewport;
        } else {
            if (currRoiViewport != mRoiViewport) {
                shouldReinit = true;
                mRoiViewport = currRoiViewport;
            }
        }
    } else {
        if (mRoiViewportStatus) {
            mRoiViewportStatus = false;
            shouldReinit = true;
        }
    }
    if (!shouldReinit) return;

    mFbMsgMultiFrames->initFb(mRezedViewport); // update fb data
    mFb.init(mRezedViewport); // for combine all MCRT result into one image
    mFbSender.init(mRezedViewport);

    //
    // This mFeedbackActive condition is not propergated into FbMsgSingleFrame object yet. However, seting
    // mFeedbackActive into FbMsgSingleFrame object would happen just after finishing this function.
    // So we can use mFeedbackActive safely here.
    // Also, mFeedbackActive is only updated by debug command and we don¡Çt need to MTlock logic to access
    // mFeedbackActive here.
    //        
    if (mFeedbackActive && mFeedbackIntervalSec > 0.0f) {
        initFeedbackFbSender();
    }        

    mLastTime = scene_rdl2::util::getSeconds();
}

void
ProgMcrtMergeComputation::handleGenericMessage(mcrt::GenericMessage::ConstPtr msg)
{
    Arg arg(msg->mValue);
    setMessageHandlerToArg(arg); // setup message handler in order to send message to the client
    if (!mParserGenericMessage.main(arg)) {
        arg.msg("parserGenericMessage failed");
    }
}

void
ProgMcrtMergeComputation::onJSONMessage(const mcrt::JSONMessage::ConstPtr& jMsg)
{
    const std::string messageID = jMsg->messageId();
    if (messageID == mcrt::RenderMessages::PICK_DATA_MESSAGE_ID) {
        auto &payload = jMsg->messagePayload();
        int syncId = payload["syncId"].asInt();
        if (syncId > mLastPickDataMessageSyncId) {
            mLastPickDataMessageSyncId = syncId;
            send(jMsg);
        }
    }
}

void
ProgMcrtMergeComputation::sendCompleteToMcrt()
//
// Send image complete condition to MCRT computation by Generic message
//
{
    if (mFbMsgMultiFrames->getDisplayFbMsgSingleFrame()->getTaskType() !=
        mcrt_dataio::FbMsgSingleFrame::TaskType::MULTIPLEX_PIX) {
        return;
    }

    if (!mStopMcrtControl) {
        return; // skip send complete command to mcrt computation sequence
    }

    //
    // We only need to send complete condition to MCRT when using
    // multiplex pixel distribution mode
    //

    float fraction = mFbSender.getProgressFraction();
    if (fraction < 1.0f) {
        return;                 // not completed yet.
    }

    //
    // Total progress fraction is reached more than 100%.
    // So we send completeFrame message to the McrtComputation and try to stop them
    // by using "stop at pass boundary" logic.
    //
    uint32_t syncId = mFbMsgMultiFrames->getDisplaySyncFrameId();
    if (mLastCompleteSyncId == syncId) {
        return;                 // already sent complete message
    }
    mLastCompleteSyncId = syncId;

    mcrt::GenericMessage::Ptr completeFrameMsg(new mcrt::GenericMessage);
    completeFrameMsg->mValue = mcrt_dataio::McrtControl::msgGen_completed(syncId);
    send(completeFrameMsg);
    if (mCredit > 0) mCredit--;

    /* useful debug message
    std::cerr << ">> ProgMcrtMergeComputation.cc sendCompleteToMcrt() fraction:" << fraction
              << " syncId:" << syncId
              << " sendCmd:>" << completeFrameMsg->mValue << "<" << std::endl;
    */
}

void
ProgMcrtMergeComputation::piggyBackInfo(std::vector<std::string>& infoDataArray)
{
    mGlobalNodeInfo.setMergeRecvBps(mRecvBandwidthTracker.getBps());
    mGlobalNodeInfo.setMergeSendBps(mSendBandwidthTracker.getBps());

    mGlobalNodeInfo.setMergeProgress(mFbSender.getProgressFraction());

    {
        bool feedbackActive = false;
        mcrt_dataio::FbMsgSingleFrame* currFbMsgSingleFrame = mFbMsgMultiFrames->getDisplayFbMsgSingleFrame();
        if (currFbMsgSingleFrame) {
            feedbackActive = currFbMsgSingleFrame->getFeedbackActive();
        }
        mGlobalNodeInfo.setMergeFeedbackActive(feedbackActive);
        if (feedbackActive) {
            mGlobalNodeInfo.setMergeFeedbackInterval(mFeedbackIntervalSec);
            mGlobalNodeInfo.setMergeEvalFeedbackTime(mFeedbackEvalLog.getAvg());
            mGlobalNodeInfo.setMergeSendFeedbackFps(mSendFeedbackFpsTracker.getFps());
            mGlobalNodeInfo.setMergeSendFeedbackBps(mSendFeedbackBandwidthTracker.getBps());
        }
    }

    std::string infoData;
    if (mGlobalNodeInfo.encode(infoData)) {
        infoDataArray.push_back(std::move(infoData));
    }
}

bool
ProgMcrtMergeComputation::decodeMergeSendProgressiveFrame(std::vector<std::string>& infoDataArray)
{
    mcrt_dataio::FbMsgSingleFrame* currFbMsgSingleFrame = mFbMsgMultiFrames->getDisplayFbMsgSingleFrame();

    //------------------------------
    //
    // decode
    //
    if (mFbMsgMultiFrames->getDisplayFbMsgSingleFrame()->getStatus() == mcrt::BaseFrame::RENDERING) {
        double currInterval = scene_rdl2::util::getSeconds() - mLastPacketSentTime;
        double minInterval = 1.0 / static_cast<double>(mFps);
        if (currInterval < minInterval) {
            if (infoDataArray.size()) {
                sendInfoOnlyProgressiveFrame(infoDataArray);
            }
            // We need to wait sending progressiveFrame message by user defined interval
            // to control communication bandwidth between merger and client.
            // However this test is only executed under RENDERING condition with same syncId of
            // last send.
            // Because very last image (i.e. render completed image) need to be send to
            // the client regardress of interval from last send immediately.
            // This last image has mcrt::BaseFrame::FINISHED condition.
            // If syncId is different, this means this is a very first image of new syncId
            // and need to send immediately without send interval check as well.
            return false;
        }
    }

    currFbMsgSingleFrame->decodeAll();
    mStats.updateMsgInterval();

    mFbSender.setHeaderInfoAndFbReset(currFbMsgSingleFrame, nullptr); // We should call resetLastHistory()

    //------------------------------
    //
    // merge (partial/all) and update fbSender info
    //
    mFbSender.timeLogEnq(scene_rdl2::grid_util::LatencyItem::Key::MERGE_PROGRESSIVEFRAME_DEQ_START); {
        if (mPartialMergeRefreshInterval > 0.0f) {
            // compute partial merge tiles total based on partial merge refresh interval
            float mergeFraction =
                scene_rdl2::math::clamp(1.0f / mFps / mPartialMergeRefreshInterval, 0.0f, 1.0f);
            if (mergeFraction == 1.0f) {
                mPartialMergeTilesTotal = 0; // special case for all (= non_partial_merge).
            } else {
                mPartialMergeTilesTotal =
                    static_cast<int>(std::round(static_cast<float>(mFb.getTotalTiles()) * mergeFraction));
            }
        }

        // mFb is always clear internally and return fresh combined result
        currFbMsgSingleFrame->merge(mPartialMergeTilesTotal, mFb, mFbSender.getLatencyLog());
        mLastMergeSyncId = currFbMsgSingleFrame->getSyncId();
    }
    mFbSender.timeLogEnq(scene_rdl2::grid_util::LatencyItem::Key::MERGE_PROGRESSIVEFRAME_DEQ_END); {
        // create encoded upstreamLatencyLog data inside fbSender
        mFbSender.encodeUpstreamLatencyLog(currFbMsgSingleFrame);
    }
    mFbSender.timeLogEnq(scene_rdl2::grid_util::LatencyItem::Key::MERGE_UPSTREAM_LATENCYLOG_END); {
        currFbMsgSingleFrame->resetLastHistory();
    }
    mFbSender.timeLogEnq(scene_rdl2::grid_util::LatencyItem::Key::MERGE_RESET_LAST_HISTORY_END);

    //------------------------------
    //
    // snapshotDelta and send to client
    //
    // Do snapshot for progressiveFrame message
    if (!mFb.snapshotDelta(mFbSender.getFb(), mFbSender.getFbActivePixels(),
                           mFbSender.getCoarsePassStatus())) {
        // mFbSender and mFb has different resolution -> error
        mFbSender.timeLogReset();
        return false;             // error : early exit
    }
    mFbSender.timeLogEnq(scene_rdl2::grid_util::LatencyItem::Key::MERGE_SNAPSHOT_END);

    sendCompleteToMcrt();
    sendProgressiveFrame(infoDataArray); // send ProgressiveFrame message to downstream 
    mFbSender.timeLogReset();        

    mLastPacketSentTime = scene_rdl2::util::getSeconds();
    if (infoDataArray.size()) {
        mLastInfoPacketSentTime = mLastPacketSentTime;
    }

    //------------------------------
    //
    // feedback to mcrt
    //
    sendProgressUpdateToMcrt();
    if (currFbMsgSingleFrame->getFeedbackActive() &&
        mFeedbackIntervalSec > 0.0 && mSendFeedbackTime.end() >= mFeedbackIntervalSec) {
        processFeedback();
    }

    return true;
}

void
ProgMcrtMergeComputation::sendProgressiveFrame(std::vector<std::string>& infoDataArray)
{
    piggyBackInfo(infoDataArray);

    //------------------------------

    mcrt::ProgressiveFrame::Ptr frameMsg = nullptr;
    frameMsg.reset(new mcrt::ProgressiveFrame);

    frameMsg->mMachineId = -2; // indicates merge computation node
    frameMsg->mHeader.mRezedViewport.setViewport(mRezedViewport.mMinX, mRezedViewport.mMinY,
                                                 mRezedViewport.mMaxX, mRezedViewport.mMaxY);
    frameMsg->mHeader.mFrameId = mFbMsgMultiFrames->getDisplaySyncFrameId();
    frameMsg->mHeader.mStatus = mFbSender.getFrameStatus();
    frameMsg->mHeader.mProgress = mFbSender.getProgressFraction();
    if (mRoiViewportStatus) {
        frameMsg->mHeader.setViewport(mRoiViewport.mMinX, mRoiViewport.mMinY,
                                      mRoiViewport.mMaxX, mRoiViewport.mMaxY);
    } else {
        frameMsg->mHeader.mViewport.reset();
    }
    frameMsg->mSnapshotStartTime = mFbSender.getSnapshotStartTime();
    frameMsg->mCoarsePassStatus = (mFbSender.getCoarsePassStatus())? 0: 1; // 0:coarsePass
                                                                           // 1:coarsePassDone
                                                                           // 2:unknown
    if (mFbSender.getFrameStatus() == mcrt::BaseFrame::STARTED) {
        // We only update denoiserInputName once at beginning of the frame.
        if (!mFbSender.getDenoiserAlbedoInputName().empty()) {
            frameMsg->mDenoiserAlbedoInputName = mFbSender.getDenoiserAlbedoInputName();
        }
        if (!mFbSender.getDenoiserNormalInputName().empty()) {
            frameMsg->mDenoiserNormalInputName = mFbSender.getDenoiserNormalInputName();
        }
    }

    //------------------------------

    mFbSender.setPrecisionControl(mPackTilePrecisionMode);

    mFbSender.addBeautyBuff(frameMsg);

    if (mFb.getPixelInfoStatus()) {
        mFbSender.addPixelInfo(frameMsg);
    }
    if (mFb.getHeatMapStatus()) {
        mFbSender.addHeatMap(frameMsg);
    }
    if (mFb.getWeightBufferStatus()) {
        mFbSender.addWeightBuffer(frameMsg);
    }
    if (mFb.getRenderBufferOddStatus()) {
        mFbSender.addRenderBufferOdd(frameMsg);
    }

    if (mFb.getRenderOutputStatus()) {
        mFbSender.addRenderOutput(frameMsg);
    }

    mFbSender.addLatencyLog(frameMsg); // latencyLog/upstreamLatencyLog staff        

    if (infoDataArray.size()) {
        mFbSender.addAuxInfo(frameMsg, infoDataArray);
    }

    //------------------------------

    ARRAS_LOG_DEBUG("Sending ProgressiveFrame");
    for (int i = 0; i < mSendDup; ++i) {
        sendBpsUpdate(frameMsg->serializedLength());
        send(frameMsg, arras4::api::withSource(mSource));
        if (mCredit > 0) mCredit--;
    }

#   ifdef DEBUG_MESSAGE_SEND_PROGRESSIVE
    { // statistical information dump
        if (frameMsg->mHeader.mStatus == mcrt::BaseFrame::STARTED) {
            mElapsedSecFromStart.start();
            mLastDisplayTime.start();
        }

        uint64_t msgSize = calcMessageSize(*frameMsg);
        for (int i = 0; i < mSendDup; ++i) {
            mStats.updateSendMsgSize(msgSize);
        }

        if (mLastDisplayTime.end() > 5.0f) {
            std::cerr << ">> ProgMcrtMergeComputation.cc sendDup:" << mSendDup << ' '
                      << mStats.show(mElapsedSecFromStart.end()) << '\n';
            mStats.reset();
            mLastDisplayTime.start();
        }        
    }
#   endif // end DEBUG_MESSAGE_SEND_PROGRESSIVE
}

void
ProgMcrtMergeComputation::sendInfoOnlyProgressiveFrame(std::vector<std::string>& infoDataArray)
{
    MNRY_ASSERT(infoDataArray.size());

    piggyBackInfo(infoDataArray);

    mcrt::ProgressiveFrame::Ptr frameMsg(new mcrt::ProgressiveFrame);

    //
    // We are using progress value as special condition flag.
    // mProgress : 0.0 or bigger (positive value) : usually up to 1.0f
    //             Standard progressiveFrame message which contains frame buffer information.
    //             standard progressiveFrame message which containe
    // mProgress : negative value
    //             No frame buff information but includes text base information. This is not an image.
    //             All other header information is useless.
    //
    frameMsg->mHeader.mStatus = mcrt::ProgressiveFrame::CANCELLED; // for dummy condition
    frameMsg->mHeader.mProgress = -1.0f; // negative value is text base info data

    mFbSender.addAuxInfo(frameMsg, infoDataArray);

    //------------------------------

    ARRAS_LOG_DEBUG("Sending ProgressiveFrame msg-only");
    sendBpsUpdate(frameMsg->serializedLength());
    send(frameMsg, arras4::api::withSource(mSource));
    if (mCredit > 0) mCredit--;

    mLastInfoPacketSentTime = scene_rdl2::util::getSeconds();

#   ifdef DEBUG_MESSAGE_SEND_PROGRESSIVE
    std::cerr << ">> ProgMcrtMergeComputation.cc send progressiveFrame msg-only"
              << " msg:'" << msg << "'" << std::endl;
#   endif // end DEBUG_MESSAGE_SEND_PROGRESSIVE
}

void
ProgMcrtMergeComputation::sendProgressUpdateToMcrt()
{
    if (mSendProgressToMcrtTime.isInit()) {
        mSendProgressToMcrtTime.start();
        return;
    }

    if (mSendProgressToMcrtTime.end() < mSendProgressToMcrtIntervalSec) {
        return;
    }
    
    uint32_t syncId = mFbMsgMultiFrames->getDisplaySyncFrameId();
    float fraction = mFbSender.getProgressFraction();

    mcrt::GenericMessage::Ptr globalProgressMsg(new mcrt::GenericMessage);
    globalProgressMsg->mValue = mcrt_dataio::McrtControl::msgGen_globalProgress(syncId, fraction);
    send(globalProgressMsg);

    mSendProgressToMcrtTime.start();

    /* useful debug message
    std::cerr << ">> ProgMcrtMergeComputation.cc sendProgressUpdateToMcrt()"
              << " syncId:" << syncId
              << " fraction:" << fraction
              << " sendCmd:>" << globalProgressMsg->mValue << "<\n";
    */
}

void
ProgMcrtMergeComputation::processFeedback()
{
    scene_rdl2::rec_time::RecTime recTimeEvalFeedback;    
    recTimeEvalFeedback.start();

    uint32_t currSyncId = mFbMsgMultiFrames->getDisplaySyncFrameId();

    //------------------------------
    //
    // setup feedbackFbSender
    //
    mcrt_dataio::FbMsgSingleFrame* currFbMsgSingleFrame = mFbMsgMultiFrames->getDisplayFbMsgSingleFrame();

    mcrt::BaseFrame::Status feedbackStatus = mcrt::BaseFrame::Status::RENDERING;
    mcrt::BaseFrame::Status* overwriteStatus = nullptr;
    if (mFeedbackInitialized || mLastSentFeedbackSyncId != currSyncId) {
        feedbackStatus = mcrt::BaseFrame::Status::STARTED;
        overwriteStatus = &feedbackStatus;
        mFeedbackInitialized = false;
    }
    mFeedbackFbSender.setHeaderInfoAndFbReset(currFbMsgSingleFrame, overwriteStatus);

    //------------------------------
    //
    // snapshotDelta
    //
    if (!mFb.snapshotDelta(mFeedbackFbSender.getFb(),
                           mFeedbackFbSender.getFbActivePixels(),
                           mFeedbackFbSender.getCoarsePassStatus())) {
        // mFeedbackFbSender and mFb has different resolution -> error
        std::cerr << ">> ProgMcrtMergeComputation.cc processFeedback() snapshotDelta() failed\n";
        return;           // error early exit
    }

    //------------------------------
    //
    // progressiveFeedback construction
    //
    mcrt::ProgressiveFrame::Ptr frameMsg(new mcrt::ProgressiveFrame);

    {
        // This is a special machine-id that indicates merge computation node.
        constexpr int MERGE_COMPUTATION_ID = -2;
        frameMsg->mMachineId = MERGE_COMPUTATION_ID;
    }

    frameMsg->mSendImageActionId = ~static_cast<unsigned>(0); // set dummy data for feedback message
    frameMsg->mHeader.mRezedViewport.setViewport(mRezedViewport.mMinX, mRezedViewport.mMinY,
                                                 mRezedViewport.mMaxX, mRezedViewport.mMaxY);
    frameMsg->mHeader.mFrameId = currSyncId;
    frameMsg->mHeader.mStatus = mFeedbackFbSender.getFrameStatus();
    frameMsg->mHeader.mProgress = mFeedbackFbSender.getProgressFraction();
    if (mRoiViewportStatus) {
        frameMsg->mHeader.setViewport(mRoiViewport.mMinX, mRoiViewport.mMinY,
                                      mRoiViewport.mMaxX, mRoiViewport.mMaxY);
    } else {
        frameMsg->mHeader.mViewport.reset();
    }
    frameMsg->mSnapshotStartTime = mFeedbackFbSender.getSnapshotStartTime();

    {
        constexpr int STATUS_COARSE_PASS = 0;
        constexpr int STATUS_FINE_PASS = 1;
        frameMsg->mCoarsePassStatus =
            (mFeedbackFbSender.getCoarsePassStatus()) ?
            STATUS_COARSE_PASS :
            STATUS_FINE_PASS;
    }

    // Under regular progressiveFrame data send to the client situation, we update denoise related
    // information here, but we technically don¡Çt need these information under feedback logic.
    // So we just skip them.

    // We have to update mLastSentFeedbackSyncId for next feedback message
    mLastSentFeedbackSyncId = frameMsg->mHeader.mFrameId;

    //------------------------------

    // We use the same precision control of regular progressiveFrame message to the downstream.
    mFeedbackFbSender.setPrecisionControl(mPackTilePrecisionMode);

    mFeedbackFbSender.addBeautyBuffWithNumSample(frameMsg); // with numSample

    /* Commented out at this moment due to we are only considering Beauty buffer for testing.
       We need RenderBufferOdd info for supporting multi-machine adaptive sampling.
    if (mFb.getRenderBufferOddStatus()) {
        mFbSender.addRenderBufferOdd(frameMsg);
    }
    */

    //------------------------------
    //
    // progressiveFeedback
    //
    mcrt::ProgressiveFeedback::Ptr feedbackMsg(new mcrt::ProgressiveFeedback);
    {
        //
        // sendImageActionId data encoding
        //
        scene_rdl2::cache::CacheEnqueue cacheEnqueue(&feedbackMsg->mMergeActionTrackerData);
        currFbMsgSingleFrame->encodeMergeActionTracker(cacheEnqueue);

        if (mDebugFeedback && mDebugFeedback->isActive()) {
            ProgMcrtMergeDebugFeedbackFrame& currDebugFrame = mDebugFeedback->getCurrFrame();

            currDebugFrame.set(mFeedbackId, mFeedbackFbSender.getFb());
            for (unsigned machineId = 0; machineId < currFbMsgSingleFrame->getActiveMachines(); ++machineId) {
                ProgMcrtMergeDebugFeedbackMachine& currMachine = currDebugFrame.getMachine(machineId);

                mcrt_dataio::MergeActionTracker& currMergeActionTracker = currFbMsgSingleFrame->getMergeActionTracker(machineId);
                currMachine.set(currMergeActionTracker.getLastSendActionId(),
                                currMergeActionTracker.getLastPartialMergeTileId(),
                                currFbMsgSingleFrame->getFb(machineId));
            }

            mDebugFeedback->incrementId();
        }
    }

    feedbackMsg->mFeedbackId = mFeedbackId;
    feedbackMsg->mProgressiveFrame = frameMsg;
    send(feedbackMsg, arras4::api::withSource(mSource));
    
    mSendFeedbackTime.start(); // update last send feedback time

    sendBpsUpdate(feedbackMsg->serializedLength());

    // ===== This is a best for minimum debug message for feedbackMessage send =====
    // std::cerr << ">> ProgMcrtMergeComputation.cc sent feedback feedbackId:" << mFeedbackId << "\n";

    mFeedbackId++;

    //------------------------------

    mFeedbackEvalLog.set(recTimeEvalFeedback.end() * 1000.0f); // millisec

    // fps tracker update : We want to keep 2x longer record of current feedbackInterval
    mSendFeedbackFpsTracker.setKeepIntervalSec(mFeedbackIntervalSec * 2.0f);
    mSendFeedbackFpsTracker.set();

    // bandwidth tracker update : We want to keep 2x longer record of current feedbackInterval
    mSendFeedbackBandwidthTracker.setKeepIntervalSec(mFeedbackIntervalSec * 2.0f);
    mSendFeedbackBandwidthTracker.set(feedbackMsg->serializedLength());
}

void
ProgMcrtMergeComputation::updateNetIO()
{
    if (mSysUsage.updateNetIO()) { // update netIO info
        mGlobalNodeInfo.setMergeNetRecvBps(mSysUsage.getNetRecv());
        mGlobalNodeInfo.setMergeNetSendBps(mSysUsage.getNetSend());
    }
}

void
ProgMcrtMergeComputation::recvBpsUpdate(mcrt::ProgressiveFrame::ConstPtr frameMsg)
{
    size_t dataSizeTotal = 0;
    for (const mcrt::BaseFrame::DataBuffer &buffer: frameMsg->mBuffers) {
        dataSizeTotal += buffer.mDataLength;
    }
    mRecvBandwidthTracker.set(dataSizeTotal); // byte    
}

void
ProgMcrtMergeComputation::sendBpsUpdate(size_t messageSerializedByte)
{
    mSendBandwidthTracker.set(messageSerializedByte);
}

uint64_t    
ProgMcrtMergeComputation::calcMessageSize(mcrt::BaseFrame& frameMsg) const
{
    uint64_t msgSizeAll = 0;
    for (const mcrt::BaseFrame::DataBuffer &buffer: frameMsg.mBuffers) {
        msgSizeAll += buffer.mDataLength;
    }
    return msgSizeAll;
}

void
ProgMcrtMergeComputation::parserConfigureGenericMessage()
{
    Parser& parser = mParserGenericMessage;

    parser.description("merge computation generic message command");
    parser.opt("fps", "<fps>", "set fps interval by float",
               [&](Arg& arg) -> bool {
                   mFps = (arg++).as<float>(0);
                   mFpsSet = true;
                   return arg.fmtMsg("fps:%f\n", mFps);
               });
    parser.opt("clockOffset", "<hostname> <ms-float>", "set internal clock offset",
               [&](Arg& arg) -> bool {
                   return arg.msg("merge clockOffset command no longer supported\n");
               });
    parser.opt("cmd", "<nodeId> ...command...", "merge debug command. <nodeId> should be -2",
               [&](Arg& arg) -> bool {
                   int nodeId = (arg++).as<int>(0);
                   if (nodeId != -2) {
                       arg.shiftArgAll();
                       return true; // skip evaluation
                   }
                   setMessageHandlerToArg(arg);
                   return mParserDebugCommand.main(arg.childArg(std::string("cmd ") + std::to_string(nodeId)));
               });
}

void
ProgMcrtMergeComputation::parserConfigureDebugCommand()
{
    namespace str_util = scene_rdl2::str_util;

    Parser& parser = mParserDebugCommand;

    parser.description("merge computation debug command");
    parser.opt("merge", "<seamless|latest|lineup>", "set merge mode",
               [&](Arg& arg) -> bool { return debugCommandMerge(arg); });
    parser.opt("task", "<tile|pix>", "set task type",
               [&](Arg& arg) -> bool { return debugCommandTask(arg); });
    parser.opt("sendDup", "<n>", "set multiple send mode for debug",
               [&](Arg& arg) -> bool {
                   mSendDup = (arg++).as<int>(0);
                   return arg.fmtMsg("sendDup:%d\n", mSendDup);
               });
    parser.opt("partialMerge", "<tileTotal|show>",
               "set partial merge tile total. PartialMerge is off if this val and partialMergeRefresh == 0.0",
               [&](Arg& arg) {
                   if ((arg)() == "show") arg++;
                   else                   mPartialMergeTilesTotal = (arg++).as<int>(0);
                   return arg.fmtMsg("partialMerge %d tiles\n", mPartialMergeTilesTotal);
               });
    parser.opt("partialMergeRefresh", "<intervalSec|show>",
               "set partial merge refresh interval by sec. use partialMerge val when this is 0.0f",
               [&](Arg& arg) {
                   if ((arg)() == "show") arg++;
                   else                   mPartialMergeRefreshInterval = (arg++).as<float>(0);
                   return arg.fmtMsg("partialMergeRefreshInterval %s\n",
                                     str_util::secStr(mPartialMergeRefreshInterval).c_str());
               });
    parser.opt("snapshotDeltaRec", "...command...", "snapshotDeltaRec command",
               [&](Arg& arg) -> bool { return mParserDebugCommandSnapshotDeltaRec.main(arg.childArg()); });
    parser.opt("dispatchHost", "<hostname>", "set dispatch hostname",
               [&](Arg& arg) -> bool {
                   std::string hostname = (arg++)();
                   mGlobalNodeInfo.setDispatchHostName(hostname);
                   sendClockDeltaClientToDispatch();
                   return arg.fmtMsg("dispatch host:%s\n", hostname.c_str());
               });
    parser.opt("initFrame", "...command...", "initial frame control command",
               [&](Arg& arg) -> bool { return mParserDebugCommandInitialFrame.main(arg.childArg()); });
    parser.opt("stopMcrtControl", "<on|off|show>", "mcrt computation stop control at end of rendering",
               [&](Arg& arg) -> bool {
                   if ((arg)() == "show") arg++;
                   else                   mStopMcrtControl = (arg++).as<bool>(0);
                   return arg.msg(std::string("stopMcrtControl:") + str_util::boolStr(mStopMcrtControl));
               });
    parser.opt("tunnel", "<machineId|show>",
               "enable tunnel operation and only this machineId data is sent to client without merge."
               "negative value disables tunnel effect. This setup is staged and activated when a new "
               "frame is started.",
               [&](Arg& arg) {
                   if ((arg)() == "show") arg++;
                   else mTunnelMachineId = (arg++).as<int>(0);
                   return arg.fmtMsg("tunnelMachineId:%d\n", mTunnelMachineId);
               });
    parser.opt("feedback", "<on|off|show>",
               "enable/disable feedback logic. This condition will apply next render start timing",
               [&](Arg& arg) -> bool {
                   if (arg() != "show") setFeedbackActive((arg++).as<bool>(0));
                   else arg++;
                   return arg.msg(scene_rdl2::str_util::boolStr(mFeedbackActive) + '\n');
               });
    parser.opt("feedbackInterval", "<intervalSec|show>", "feedback interval by sec",
               [&](Arg& arg) -> bool {
                   if (arg() != "show") setFeedbackIntervalSec((arg++).as<float>(0));
                   else arg++;
                   return arg.msg(std::to_string(mFeedbackIntervalSec) + " sec\n");
               });
    parser.opt("feedbackDebug", "...command...", "command for debugFeedback logic",
               [&](Arg& arg) -> bool {
                   if (!mDebugFeedback) return arg.msg("debugFeedback logic is disabled\n");
                   return mDebugFeedback->getParser().main(arg.childArg());
               });
    parser.opt("feedbackStats", "", "show feedback statistical info",
               [&](Arg& arg) -> bool { return arg.msg(showFeedbackStats() + '\n'); });
    parser.opt("currSingleFrame", "...command...", "command for current single frame",
               [&](Arg& arg) -> bool {
                   return mFbMsgMultiFrames->getDisplayFbMsgSingleFrame()->getParser().main(arg.childArg());
               });
    parser.opt("numMachines", "", "show numMachines count",
               [&](Arg& arg) { return arg.msg(std::to_string(mNumMachines) + '\n'); });
}

void
ProgMcrtMergeComputation::parserConfigureDebugCommandSnapshotDeltaRec()
{
    Parser& parser = mParserDebugCommandSnapshotDeltaRec;
    
    parser.description("snapshotDeltaRec command");
    parser.opt("start", "", "start snapshot delta rec",
               [&](Arg& arg) -> bool {
                   mFb.snapshotDeltaRecStart();
                   return arg.msg("snapshotDelta REC start\n");
               });
    parser.opt("stop", "", "stop snapshot delta rec",
               [&](Arg& arg) -> bool {
                   mFb.snapshotDeltaRecStop();
                   return arg.msg("snapshotDelta REC stop\n");
               });
    parser.opt("reset", "", "reset snapshot delta rec",
               [&](Arg& arg) -> bool {
                   mFb.snapshotDeltaRecReset();
                   return arg.msg("snapshotDelta REC reset\n");
               });
    parser.opt("dump", "<filename>", "save snapshot delta data",
               [&](Arg& arg) -> bool {
                   std::string filename = (arg++)();
                   bool flag = true;
                   if (mFb.snapshotDeltaRecDump(filename)) {
                       arg.fmtMsg("snapshotDelta save OK filename:%s\n", filename.c_str());
                   } else {
                       arg.fmtMsg("snapshotDelta save Failed filename:%s\n", filename.c_str());
                       flag = false;
                   }
                   return flag;
               });
}

void
ProgMcrtMergeComputation::parserConfigureDebugCommandInitialFrame()
{
    Parser& parser = mParserDebugCommandInitialFrame;
    
    parser.description("initial frame control command ");
    parser.opt("parallel", "<on|off>", "set special parallel initial frame update mode",
               [&](Arg& arg) -> bool {
                   mParallelInitialFrameUpdateMode = (arg++).as<bool>(0);
                   return arg.msg(scene_rdl2::str_util::boolStr(mParallelInitialFrameUpdateMode) + '\n');
               });
    parser.opt("show", "", "show current information",
               [&](Arg& arg) -> bool {
                   return arg.msg(showParallelInitialFrameControlInfo() + '\n');
               });
}

bool
ProgMcrtMergeComputation::debugCommandMerge(Arg& arg)
{
    std::string mode = (arg++)();

    bool flag = true;
    if (mode == "seamless") {
        if (mFbMsgMultiFrames->changeMergeType
            (mcrt_dataio::FbMsgMultiFrames::MergeType::SEAMLESS_COMBINE, 1)) {
            arg.msg("mergeType set to seamless\n");
        } else {
            arg.msg("mergeType set to seamless failed.\n");
            flag = false;
        }
    } else if (mode == "latest") {
        if (mFbMsgMultiFrames->changeMergeType
            (mcrt_dataio::FbMsgMultiFrames::MergeType::PICKUP_LATEST, 1)) {
            arg.msg("mergeType set to latest\n");
        } else {
            arg.msg("mergeType set to latest failed.\n");
            flag = false;
        }
    } else if (mode == "lineup") {
        if (mFbMsgMultiFrames->changeMergeType
            (mcrt_dataio::FbMsgMultiFrames::MergeType::SYNCID_LINEUP, 8)) {
            arg.msg("mergeType set to lineup\n");
        } else {
            arg.msg("mergeType set to lineup failed.\n");
            flag = false;
        }
    } else {
        arg.fmtMsg("unknown merge mode:%s\n", mode.c_str());
        flag = false;
    }
    return flag;
}

bool
ProgMcrtMergeComputation::debugCommandTask(Arg& arg)
{
    std::string mode = (arg++)();

    bool flag = true;
    if (mode == "tile") {
        // TODO : probably not working anymore. needs double check
        mFbMsgMultiFrames->changeTaskType(mcrt_dataio::FbMsgSingleFrame::TaskType::NON_OVERLAPPED_TILE);
        arg.msg("taskType set to non_overlapped_tile\n");
    } else if (mode == "pix") {
        mFbMsgMultiFrames->changeTaskType(mcrt_dataio::FbMsgSingleFrame::TaskType::MULTIPLEX_PIX);
        arg.msg("taskType set to multiplex_pixel\n");
    } else {
        arg.fmtMsg("unknown task mode:%s\n", mode.c_str());
        flag = false;
    }
    return flag;
}

void
ProgMcrtMergeComputation::sendClockDeltaClientToDispatch()
{
    if (mGlobalNodeInfo.getDispatchHostName() == mGlobalNodeInfo.getMergeHostName()) {
        // dispatcher and merger are running on same host.
        // We don't need to do cloclDelta measurement in this case.
        return;
    }

    std::ostringstream ostr;
    // cmd <nodeId> clockDeltaClient <serverName> <port> <path>
    // nodeId = -3 is dispatch computation
    ostr << "cmd -3 clockDeltaClient "
         << mGlobalNodeInfo.getMergeHostName() << ' '
         << mGlobalNodeInfo.getMergeClockDeltaSvrPort() << ' '
         << mGlobalNodeInfo.getMergeClockDeltaSvrPath();
    mMsgSendHandler->sendMessage(ostr.str());
}

std::string    
ProgMcrtMergeComputation::showParallelInitialFrameControlInfo() const
{
    std::ostringstream ostr;
    ostr << "Parallel initial frame control {\n"
         << "  mParallelInitialFrameUpdateMode:"
         << scene_rdl2::str_util::boolStr(mParallelInitialFrameUpdateMode) << '\n'
         << "}";
    return ostr.str();
}

void
ProgMcrtMergeComputation::setMessageHandlerToArg(Arg& arg)
{
    // Set message handler here in order to send all arg.msg() to the client
    arg.setMessageHandler([&](const std::string &msg) -> bool {
            showMsg(msg, false);
            return true;
        });
}

void
ProgMcrtMergeComputation::showMsg(const std::string& msg, bool cerrOut)
{
    mGlobalNodeInfo.enqMergeGenericComment(msg);
    if (cerrOut) {
        std::cerr << msg;
    }
}

void
ProgMcrtMergeComputation::initFeedbackFbSender()
{
    std::cerr << ">>>>---- ProgMcrtMergeComputation.cc initFeedbackFbSender() ----<<<<\n";

    mFeedbackInitialized = true;
    mFeedbackFbSender.init(mRezedViewport);

    mSendFeedbackTime.start();
}

void
ProgMcrtMergeComputation::setFeedbackActive(bool flag)
{
    mFeedbackActive = flag;
}

void
ProgMcrtMergeComputation::setFeedbackIntervalSec(float sec)
{
    mFeedbackIntervalSec = sec;
}

std::string
ProgMcrtMergeComputation::showFeedbackStats() const
{
    using scene_rdl2::str_util::addIndent;
    using scene_rdl2::str_util::boolStr;
    using scene_rdl2::str_util::byteStr;

    auto showFeedbackCondition = [&]() -> std::string {
        mcrt_dataio::FbMsgSingleFrame* currFbMsgSingleFrame = mFbMsgMultiFrames->getDisplayFbMsgSingleFrame();
        if (!currFbMsgSingleFrame) return "runtimeFeedback:? currFbmsgSingleFrame is empty";
        std::ostringstream ostr;
        ostr
        << "runtimeFeedbackActive:" << boolStr(currFbMsgSingleFrame->getFeedbackActive())
        << " current runtime feedback condition";
        return ostr.str();
    };
    auto showFeedbackEvalLog = [&]() {
        std::ostringstream ostr;
        ostr
        << "feedbackEvalLog {\n"
        << addIndent(mFeedbackEvalLog.show()) << '\n'
        << "  average:" << mFeedbackEvalLog.getAvg() << " millisec\n"
        << "}";
        return ostr.str();
    };
    auto showSendFeedbackFpsTracker = [&]() {
        std::ostringstream ostr;
        ostr
        << "sendFeedbackFpsTracker {\n"
        << addIndent(mSendFeedbackFpsTracker.show()) << '\n'
        << "  fps:" << mSendFeedbackFpsTracker.getFps() << '\n'
        << "}";
        return ostr.str();
    };
    auto showSendFeedbackBandwidthTracker = [&]() {
        std::ostringstream ostr;
        ostr
        << "sendFeedbackBandwidthTracker {\n"
        << addIndent(mSendFeedbackBandwidthTracker.show()) << '\n'
        << "  bps:" << byteStr(static_cast<size_t>(mSendFeedbackBandwidthTracker.getBps())) << "/sec\n"
        << "}";
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "feedback stats {\n"
         << "  mFeedbackActive:" << boolStr(mFeedbackActive) << " (feedback action on/off switch user input)\n"
         << addIndent(showFeedbackCondition()) << '\n'
         << "  mFeedbackIntervalSec:" << mFeedbackIntervalSec << " sec\n"
         << "  mFeedbackId:" << mFeedbackId << '\n'
         << addIndent(showFeedbackEvalLog()) << '\n'
         << addIndent(showSendFeedbackFpsTracker()) << '\n'
         << addIndent(showSendFeedbackBandwidthTracker()) << '\n'
         << "}";
    return ostr.str();
}

} // namespace mcrt_computation

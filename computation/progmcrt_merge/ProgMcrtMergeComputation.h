// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <moonray/rendering/rndr/rndr.h>

#include <mcrt_computation/common/mcrt_logging/McrtLogging.h>

#include <computation_api/Computation.h>
#include <mcrt_dataio/engine/merger/FbMsgMultiFrames.h>
#include <mcrt_dataio/engine/merger/GlobalNodeInfo.h>
#include <mcrt_dataio/engine/merger/MergeFbSender.h>
#include <mcrt_dataio/engine/merger/MergeStats.h>
#include <mcrt_dataio/engine/merger/MsgSendHandler.h>
#include <mcrt_dataio/share/util/BandwidthTracker.h>
#include <mcrt_dataio/share/util/SysUsage.h>
#include <mcrt_messages/BaseFrame.h>
#include <mcrt_messages/GenericMessage.h>
#include <mcrt_messages/JSONMessage.h>

#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Fb.h>
#include <scene_rdl2/common/grid_util/Parser.h>
#include <scene_rdl2/common/math/Viewport.h>

#include <tbb/task_scheduler_init.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace moonray {
namespace rndr {
    class TileScheduler;
} // namespace rndr
} // namespace moonray

namespace mcrt_computation {

class PartialFrame;
class ProgressiveFrame;

class ProgMcrtMergeComputation : public arras4::api::Computation
{
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser;

    ProgMcrtMergeComputation(arras4::api::ComputationEnvironment *env);
    virtual ~ProgMcrtMergeComputation();

    virtual arras4::api::Result configure(const std::string &op,
                                          arras4::api::ObjectConstRef config);
    virtual void onIdle();
    virtual arras4::api::Result onMessage(const arras4::api::Message &message);

protected:
    void onStart();

private:
    using DataPtr = std::shared_ptr<uint8_t>;

    void setSource(arras4::api::ObjectConstRef source);

    void onCreditUpdate(const arras4::api::Message &msg);

    void onViewportChanged(const mcrt::BaseFrame &msg);

    void handleGenericMessage(mcrt::GenericMessage::ConstPtr msg);
    void sendCompleteToMcrt();

    void onJSONMessage(const mcrt::JSONMessage::ConstPtr &msg);

    void sendCredit(const arras4::api::Message &msg);

    void recvBpsUpdate(mcrt::ProgressiveFrame::ConstPtr frameMsg);
    void sendBpsUpdate(mcrt::ProgressiveFrame::Ptr frameMsg);
    void piggyBackInfo(std::vector<std::string> &infoDataArray);
    bool decodeMergeSendProgressiveFrame(std::vector<std::string> &infoDataArray);
    void sendProgressiveFrame(std::vector<std::string> &infoDataArray);
    void sendInfoOnlyProgressiveFrame(std::vector<std::string> &infoDataArray);

    uint64_t calcMessageSize(mcrt::BaseFrame &frameMsg) const;

    void parserConfigureGenericMessage();
    void parserConfigureDebugCommand();
    void parserConfigureDebugCommandSnapshotDeltaRec();
    void parserConfigureDebugCommandInitialFrame();
    bool debugCommandMerge(Arg& arg);
    bool debugCommandTask(Arg& arg);
    void sendClockDeltaClientToDispatch();
    std::string showParallelInitialFrameControlInfo() const;
    void setMessageHandlerToArg(Arg& arg);
    void showMsg(const std::string& msg, bool cerrOut);

    //------------------------------

    int mNumThreads;

    unsigned int mNumMachines;
    scene_rdl2::math::Viewport mRezedViewport;
    bool mRoiViewportStatus;
    scene_rdl2::math::Viewport mRoiViewport;

    // for ProgressiveFrame message
    using PackTilePrecisionMode = mcrt_dataio::MergeFbSender::PrecisionControl;
    PackTilePrecisionMode mPackTilePrecisionMode;
    std::unique_ptr<mcrt_dataio::FbMsgMultiFrames> mFbMsgMultiFrames;
    scene_rdl2::grid_util::Fb mFb;   // for combine all MCRT result into one image
    mcrt_dataio::MergeFbSender mFbSender; // for ProgressiveFrame message

    // Flag if we got a progressive frame message
    bool mHasProgressiveFrame;

    // flag use to determine if the first rendered frame has
    //  left the renderer to other downstream computations.
    //  this is used to signal that the time heavy on demand
    //  loading of assets has finished and the render has
    //  acutally begun rendering
    bool mFirstFrame;
    double mLastTime;
    bool mFpsSet;
    float mFps;

    bool mStopMcrtControl;
    double mLastPacketSentTime;
    double mLastInfoPacketSentTime;
    uint32_t mLastCompleteSyncId;     // keep last MCRT-control complete syncId
    uint32_t mLastMergeSyncId;        // keep last merged syncId
    uint32_t mCurrActiveRecvSyncId;
    int mLastPickDataMessageSyncId; // for PickDataMessage processing

    mcrt_dataio::MergeStats mStats;
    scene_rdl2::rec_time::RecTime mElapsedSecFromStart; // for debug
    scene_rdl2::rec_time::RecTime mLastDisplayTime;     // for debug

    std::shared_ptr<mcrt_dataio::MsgSendHandler> mMsgSendHandler;
    mcrt_dataio::GlobalNodeInfo mGlobalNodeInfo;
    mcrt_dataio::SysUsage mSysUsage;
    mcrt_dataio::BandwidthTracker mRecvBandwidthTracker;
    mcrt_dataio::BandwidthTracker mSendBandwidthTracker;

    mcrt_dataio::BandwidthTracker m1stSendBandwidthTracker;

    int mSendDup; // for debug : bandwidth tracking test

    float mPartialMergeRefreshInterval; // sec
    int mPartialMergeTilesTotal;

    arras4::api::UUID mPrevRecvMsg; // for debug message

    tbb::task_scheduler_init *mTaskScheduler;

    std::string mSource;        // source id, correlating incoming to outgoing messages
    
    int mInitialCredit;
    int mCredit;                // limits sending of outgoing messages. <0 disables.
    bool mSendCredit;           // if true (the default), send credit to mcrts

    McrtLogging mLogging;

    bool mParallelInitialFrameUpdateMode;

    Parser mParserGenericMessage;
    Parser mParserDebugCommand;
    Parser mParserDebugCommandSnapshotDeltaRec;
    Parser mParserDebugCommandInitialFrame;
};

} // namespace mcrt_computation


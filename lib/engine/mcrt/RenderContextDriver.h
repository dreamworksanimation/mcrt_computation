// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DeltaImageCache.h"
#include "McrtDebugFeedback.h"
#include "McrtUpdate.h"
#include "MessageHistory.h"
#include "TimingRecorder.h"
#include "Watcher.h"

#include <mcrt_dataio/engine/mcrt/McrtNodeInfoMapItem.h>
#include <mcrt_dataio/engine/merger/FbMsgMultiChans.h>
#include <mcrt_dataio/engine/merger/MergeActionTracker.h>
#include <mcrt_dataio/share/util/FloatValueTracker.h>
#include <mcrt_messages/GeometryData.h>
#include <mcrt_messages/OutputRates.h>
#include <mcrt_messages/ProgressiveFeedback.h>
#include <mcrt_messages/ProgressiveFrame.h>
#include <mcrt_messages/ProgressMessage.h>
#include <mcrt_messages/RenderMessages.h>
#include <message_api/messageapi_types.h>
#include <moonray/grid/engine_tool/McrtFbSender.h> // frame buffer info for message to downstream
#include <moonray/rendering/rndr/RenderContext.h>
#include <moonray/rendering/rndr/RenderOptions.h>
#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Fb.h>
#include <scene_rdl2/common/grid_util/Parser.h>
#include <scene_rdl2/common/math/Viewport.h>

#include <tbb/atomic.h>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace arras4 {
namespace api {
    class Address;
} // namespace api
} // namespace arras4

namespace mcrt_dataio {
    class BandwidthTracker;
    class FpsTracker;
    class SysUsage;
} // namespace mcrt_dataio

namespace scene_rdl2 {
namespace rec_time {
    class RecTimeLog;
} // namespace rec_time
} // namespace scene_rdl2

namespace mcrt_computation {

class McrtLogging;

class RenderContextDriverOptions
//
// This class is used to specify options for the constructor of RenderContextDriver
//
{
public:
    using PackTilePrecisionMode = moonray::engine_tool::McrtFbSender::PrecisionControl;
    using PostRenderPrepMainCallBack = std::function<void()>;
    using ProgressiveFrameSendCallBack =
        std::function<void(mcrt::ProgressiveFrame::Ptr msg, const std::string &source)>;
    using StartFrameCallBack = std::function<void(const bool reloadScn, const std::string &source)>;
    using StopFrameCallBack = std::function<void(const std::string &soruce)>;

    RenderContextDriverOptions(const PostRenderPrepMainCallBack& argPostRenderPrepMainCallBack,
                               const StartFrameCallBack& argStartFrameCallBack,
                               const StopFrameCallBack& argStopFrameCallBack,
                               const ProgressiveFrameSendCallBack& argSendInfoOnlyCallBack,
                               mcrt_dataio::SysUsage& argSysUsage,
                               mcrt_dataio::BandwidthTracker& argSendBandwidthTracker)
        : postRenderPrepMainCallBack {argPostRenderPrepMainCallBack}
        , startFrameCallBack {argStartFrameCallBack}
        , stopFrameCallBack {argStopFrameCallBack}
        , sendInfoOnlyCallBack {argSendInfoOnlyCallBack}
        , sysUsage {argSysUsage}
        , sendBandwidthTracker {argSendBandwidthTracker}
    {}

    int driverId {0};
    const moonray::rndr::RenderOptions* renderOptions {nullptr};
    int numMachineOverride {-1}; // -1 skips value set
    int machineIdOverride {-1}; // -1 skips value set
    const float* fps {nullptr};
    McrtLogging* mcrtLogging {nullptr}; // all logging related task will skip when we set nullptr
    bool* mcrtDebugLogCreditUpdateMessage {nullptr};
    PackTilePrecisionMode precisionMode {PackTilePrecisionMode::AUTO16};
    std::atomic<bool>* renderPrepCancel {nullptr};
    const PostRenderPrepMainCallBack& postRenderPrepMainCallBack {nullptr};
    const StartFrameCallBack& startFrameCallBack {nullptr};
    const StopFrameCallBack& stopFrameCallBack {nullptr};
    const ProgressiveFrameSendCallBack& sendInfoOnlyCallBack {nullptr};
    mcrt_dataio::FpsTracker* recvFeedbackFpsTracker {nullptr};
    mcrt_dataio::BandwidthTracker* recvFeedbackBandwidthTracker {nullptr};
    mcrt_dataio::SysUsage& sysUsage;
    mcrt_dataio::BandwidthTracker& sendBandwidthTracker;
};

class RenderContextDriver
//
// This class is designed for the core control of the interactive moonray session under arras.
// We can set multiple incoming message data and we can control the start and stop rendering.
// All APIs should call by the arras main thread.
// Internally, renderPrep operation is done by different thread (= RenderContextDriver thread)
// but it is hide from caller. You don't need to care about this.
// After finish renderPrep, MCRT stage starts by MCRT threads which is also hide from caller.
// After start MCRT threads, RenderContextDriver thread will sleep internally automatically.
//
// This object keeps a single RenderContext with related parameters which are tightly related
// to this renderContxt data for mcrt computation. 
//
// This object does not include send and receive message functionality and they should be managed
// by the caller (i.e. progmcrt computation itself).
// All the messages which need to process by renderContext are categorized into 2 groups
//
// a) queueing message
//  - Messages are queued and processed sometime later by applyUpdatesAndRestartRender()
//  - Messages are queued by enq*Message() APIs.
//
// b) immediate evaluation message
//  - Messages are executed right away without queueing
//  - You should use eval*() APIs
//
// All send message functionalities are implemented by call-back function and there is no way
// to send message from this object without relying callback.
//
// Followings are expected logic of interactive rendering session.
//   0) initialize
//   1) onMessage phase
//     receive message and process it
//     - queueing message : handled by enq*() API
//     - immediate evaluation message : handled by eval*() API
//   2) if we still have more messages then back to 1)
//   3) If there are no new messages more, start 4)
//   4) onIdle phase : this executes snapshot / send and start newFrame if needed.
//     - interval check (by isEnoughSendInterval()) if interval is short, goto 1)
//     - snapshot and send main operation (by sendDelta())
//     - start new frame (by applyUpdatesAndRestartRender())
//       - reset internal RenderContext and frame buffer.
//       - process queued messages to apply
//       - start renderPrep by different thread of arras thread
//   5) goto 1)
//
{
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using MessageContentConstPtr = arras4::api::MessageContentConstPtr;
    using EvalPickSendMsgCallBack = std::function<void(const MessageContentConstPtr &msg)>;
    using PackTilePrecisionMode = moonray::engine_tool::McrtFbSender::PrecisionControl;
    using PostMainCallBack = std::function<void()>;
    using ProgressiveFrameSendCallBack =
        std::function<void(mcrt::ProgressiveFrame::Ptr msg, const std::string &source)>;
    using StartFrameCallBack = std::function<void(const bool reloadScn, const std::string &source)>;
    using StopFrameCallBack = std::function<void(const std::string &soruce)>;
    using GetTimingRecFrameCallBack = std::function<TimingRecorder::TimingRecorderSingleFrameShPtr()>;

    // Non-copyable
    RenderContextDriver &operator =(const RenderContextDriver) = delete;
    RenderContextDriver(const RenderContextDriver &) = delete;

    explicit RenderContextDriver(const RenderContextDriverOptions& options);
    ~RenderContextDriver();

    int getDriverId() const { return mDriverId; }

    void start(); // start renderContextDriver main function. should be public for testing purpose
    
    //------------------------------
    //
    // queueing message
    //
    void enqGeometryMessage(const arras4::api::Message &msg);
    void enqRdlMessage(const arras4::api::Message &msg, const float recvTimingSec);
    void enqRenderControlMessage(const arras4::api::Message &msg, const float recvTimingSec);
    void enqRenderSetupMessage(const arras4::api::Message &msg, const float recvTimingSec);
    void enqROIResetMessage(const arras4::api::Message &msg, const float recvTimingSec); // ROI = off
    void enqROISetMessage(const arras4::api::Message &msg, const float recvTimingSec); // ROI = on
    void enqViewportMessage(const arras4::api::Message &msg, const float recvTimingSec);

    //------------------------------
    //
    // immediate evaluation message
    //
    void evalInvalidateResources(const arras4::api::Message &msg);
    void evalOutputRatesMessage(const arras4::api::Message &msg);
    void evalPickMessage(const arras4::api::Message &msg, EvalPickSendMsgCallBack sendCallBack);
    void evalProgressiveFeedbackMessage(const arras4::api::Message& msg);

    void evalMultiMachineGlobalProgressUpdate(unsigned currSyncId, float fraction);
    void evalRenderCompleteMultiMachine(unsigned currSyncId);

    void setReceivedSnapshotRequest(bool flag) { mReceivedSnapshotRequest = flag; }

    //------------------------------
    //
    // debug command control
    //
    // return command execution result condition.
    bool evalDebugCommand(Arg &arg);

    // Set message handler to the arg in order to send all arg.msg() to the downstream.
    void setMessageHandlerToArg(Arg &arg);

    //------------------------------
    //
    // onIdle related API
    //
    bool isEnoughSendInterval(const float fps, const bool dispatchGatesFrame);
    bool isEnoughSendIntervalHeartBeat(const float checkFps);

    void sendDelta(ProgressiveFrameSendCallBack callBackSend);

    void applyUpdatesAndRestartRender(const GetTimingRecFrameCallBack& getTimingRecFrameCallBack);

    //------------------------------

    std::string show() const;

private:
    using McrtUpdateShPtr = std::shared_ptr<McrtUpdate>;
    using DataPtr = std::shared_ptr<uint8_t>;
    using EncodingType = moonray::engine_tool::ImgEncodingType;
    using Parser = scene_rdl2::grid_util::Parser;
    using RP_RESULT = moonray::rndr::RenderContext::RP_RESULT;
    using ProgressiveFeedbackConstPtr = mcrt::ProgressiveFeedback::ConstPtr;

    //------------------------------

    moonray::rndr::RenderContext *getRenderContext();
    moonray::rndr::RenderContext *resetRenderContext(); // rm old then create new RenderContext
    scene_rdl2::rdl2::SceneContext *getSceneContextBackup();
    scene_rdl2::rdl2::SceneContext *resetSceneContextBackup(); // rm old then create new SceneContextBackup
    static void updateSceneContextBackup(scene_rdl2::rdl2::SceneContext *sceneContext,
                                         const std::string &manifest, const std::string &payload);

    bool renderPrepMain();      // return true:OK false:canceled
    void heartBeatMain();

    void showMsg(const std::string &msg, bool cerrOut = true);

    std::string showInitFrameControlStat() const;
    std::string showMultiBankControlStat() const;

    std::string showFeedbackStats() const;

    //------------------------------

    void processRdlMessage(const MessageContentConstPtr &msg,
                           arras4::api::ObjectConstRef src);
    void processRenderSetupMessage(const MessageContentConstPtr &msg,
                                   arras4::api::ObjectConstRef src);
    void processROIMessage(const MessageContentConstPtr &msg,
                           arras4::api::ObjectConstRef src);
    void processViewportMessage(const MessageContentConstPtr &msg,
                                arras4::api::ObjectConstRef src);
    void processRenderControlStartMessage(const MessageContentConstPtr &msg,
                                          arras4::api::ObjectConstRef src);

    void initializeBuffers();
    void setSource(arras4::api::ObjectConstRef src);
    void updateLoggingMode();

    //------------------------------

    void handlePick(const uint32_t syncId,
                    const mcrt::RenderMessages::PickMode mode, const int x, const int y,
                    mcrt::JSONMessage::Ptr &result);

    //------------------------------

    void debugCommandParserConfigure();
    bool debugCommandRenderContext(Arg &arg);

    //------------------------------

    void startFrame();
    
    // stopFrame and return status which is just before stopFrame oepration
    // return true : renderContext was rendering condition
    //        false : renderContext was stop condition
    bool stopFrame();
    void requestStopAtPassBoundary(uint32_t syncId);

    void reconstructSceneFromBackup(); // internally creates new RenderContext

    //------------------------------

    bool preprocessQueuedMessage(); // return true if there is forceReload message inside unified queue

    bool isReadyToSend(const double now);

    // return sent last data status.
    // return true : send FINISHED condition progressive message
    //        false : send non-FINISHED condition progressive message
    bool sendProgressiveFrameMessage(const bool directToClient,
                                     std::vector<std::string>& infoDataArray,
                                     ProgressiveFrameSendCallBack callBackSend); // MTsafe
    void sendProgressiveFrameMessageInfoOnly(std::vector<std::string>& infoDataArray); // MTsafe 

    float getRenderProgress();
    float getGlobalRenderProgress();
    bool checkOutputRatesInterval(const std::string &name);
    bool isMultiMachine() const { return mNumMachinesOverride > 1; }
    bool haveUpdates() const { return (mGeometryUpdate != nullptr) || !mMcrtUpdates.empty(); }
    void updateInfoData(std::vector<std::string>& infoDataArray);
    void updateExecModeMcrtNodeInfo();
    void updateNetIO();
    void piggyBackStatsInfo(std::vector<std::string>& infoDataArray);
    void piggyBackTimingRec(std::vector<std::string>& infoDataArray);
    mcrt::BaseFrame::ImageEncoding encoTypeConvert(EncodingType enco) const;
    void applyConfigOverrides();

    //------------------------------

    void setFeedbackActive(bool flag);
    void setFeedbackIntervalSec(float sec);
    void feedbackFbViewportCheck(ProgressiveFeedbackConstPtr feedbackMsg);
    void decodeFeedbackImageData(ProgressiveFeedbackConstPtr feedbackMsg);
    bool decodeMergeActionTracker(ProgressiveFeedbackConstPtr feedbackMsg);
    void constructFeedbackMinusOne();

    //------------------------------
    //------------------------------
    //
    // RenderContextDriver thread control related members
    //
    int mDriverId;

    Watcher mRenderPrepWatcher;
    Watcher mHeartBeatWatcher;

    //------------------------------
    //
    // Parameters which are defined by progmcrt_computation object
    //
    int mNumMachinesOverride;
    int mMachineIdOverride; // machineId of this process
    const float* mFps {nullptr};

    McrtLogging *mMcrtLogging; // McrtLogging pointer in order to update mode (debug/info/silent)
    bool* mMcrtDebugLogCreditUpdateMessage;

    std::atomic<bool> *mRenderPrepCancel; // renderPrep cancel condition flag address
    PostMainCallBack mPostRenderPrepMainCallBack; // call back function which is executed after main()
    StartFrameCallBack mStartFrameCallBack; // call back function which is called just before startFrame()
    StopFrameCallBack mStopFrameCallBack; // call back function which is called just after stopFrame()
    ProgressiveFrameSendCallBack mSendInfoOnlyCallBack; // call back func for sendInfoOnly

    mcrt_dataio::SysUsage& mSysUsage;

    PackTilePrecisionMode mPackTilePrecisionMode;

    //------------------------------
    //
    // Moonray related data
    //
    moonray::rndr::RenderOptions mRenderOptions;
    std::unique_ptr<moonray::rndr::RenderContext> mRenderContext {nullptr};
    std::unique_ptr<scene_rdl2::rdl2::SceneContext> mSceneContextBackup {nullptr};

    moonray::engine_tool::McrtFbSender mFbSender;

    //------------------------------
    //
    // Interactive operation related parameters
    //
    scene_rdl2::math::HalfOpenViewport mViewport; // viewport which used by last initializeBuffers()

    mcrt::GeometryData::ConstPtr mGeometryUpdate {nullptr};
    std::vector<McrtUpdateShPtr> mMcrtUpdates;

    bool mProgressiveFrameRecMode {false}; // for debugging purpose

    bool mFeedbackActiveUserInput {false}; // feedback action on/off switch user input
    bool mFeedbackActiveRuntime {false};   // feedback action on/off condition for render frame runtime
    float mFeedbackIntervalSec {1.0f};
    uint32_t mFeedbackId {~static_cast<uint32_t>(0)}; // last received feedback data's id
    DeltaImageCache mSentImageCache; // Cache of message data which sent to merge node
    scene_rdl2::grid_util::Fb mFeedbackFb; // decode result of feedback data from merge node
    mcrt_dataio::FbMsgMultiChans mFeedbackUpdates {/*debugMode=*/true}; // feedback data from merge node
    mcrt_dataio::MergeActionTracker mFeedbackMergeActionTracker;
    scene_rdl2::grid_util::Fb mFeedbackMinusOneFb;

    std::unique_ptr<McrtDebugFeedback> mMcrtDebugFeedback;

    unsigned mMultiBankTotal {1};
    std::vector<McrtUpdateShPtr> mMcrtUpdatesBacklog;
    
    bool mReceivedSnapshotRequest {false};

    mcrt::OutputRates mOutputRates; // controls frequency that render outputs are included in output
    uint32_t mOutputRatesFrameCount {0}; // frame counter for output rates control

    uint32_t mSyncId {0};

    std::string mSource; // source id, correlating incoming to outgoing messages

    // 1st frame snapshsot timing control
    int mInitFrameNonDelayedSnapshotMaxNode {0};
    float mInitFrameDelayedSnapshotStepMS {6.25f}; // each host delay offset by millisec.
    double mInitFrameDelayedSnapshotSec {-1.0};

    //------------------------------
    //
    // Flags and counters which we are using to track particular conditions.
    //
    RP_RESULT mLastTimeRenderPrepResult {RP_RESULT::FINISHED};

    double mLastTimeOfEnoughIntervalForSend {0.0}; // last timing when it passed enough interval for send action
    double mLastTimeOfEnoughIntervalForHeartBeat {0.0}; // Same as above but this is regarding the heartbeat thread

    int mLastSnapshotFilmActivity {0}; // film activity value of last snapshot, reset to 0 when restart render

    uint32_t mSnapshotId {0}; // snapshot id from render start, reset to 0 when restart render
    uint32_t mSendImageActionId {0}; // send image id from the beginning of starting process and never reset to 0

    bool mReloadingScene {true}; // indicates the scene was constructed from scratch

    int mRenderCounter {0}; // rendering action (= render start) counter from process started
    int mRenderCounterLastSnapshot {0}; // rendering action counter at last snapshot

    double mInitialSnapshotReadyTime {0.0}; // initial snapshot ready time for new frame
    bool mBeforeInitialSnapshot {false}; // In order to track the very first snapshot data of the new frame.
    bool mSentCompleteFrame {true}; // Condition flag about sending a snapshot of the completed condition.

    // Set to true when we've sent a final pixel info buffer for this frame.
    // At that point we no longer need to snapshot the pixel info buffer any further.
    bool mSentFinalPixelInfoBuffer {true};

    //------------------------------
    //
    // Statistical info and debug command parser
    //
    std::shared_ptr<scene_rdl2::rec_time::RecTimeLog> mSnapshotToSendTimeLog
    {
        std::make_shared<scene_rdl2::rec_time::RecTimeLog>()
    };
    TimingRecorder::TimingRecorderSingleFrameShPtr mTimingRecFrame;
    mcrt_dataio::FloatValueTracker mFeedbackEvalLog {10}; // unit is millisec : keepEventTotal = 10
    mcrt_dataio::FloatValueTracker mFeedbackLatency {10}; // unit is millisec : keepEventTotal = 10

    mcrt_dataio::BandwidthTracker& mSendBandwidthTracker;
    mcrt_dataio::FpsTracker* mRecvFeedbackFpsTracker;
    mcrt_dataio::BandwidthTracker* mRecvFeedbackBandwidthTracker;

    MessageHistory mMessageHistory; // experimental code for multi-bank logic. still, a work in progress

    Parser mParserDebugCommand;
    Parser mParserDebugCommandSnapshotDeltaRec;
    Parser mParserInitialFrameControl;
    Parser mParserMultiBankControl;
    Parser mParserLoggingControl;

    //------------------------------

    bool mMcrtLoggingInfo; // current info logging condition
    bool mMcrtLoggingDebug; // current debug logging condition

    //------------------------------

    std::mutex mMutexSendAction; // mutex for send action
    std::mutex mMutexMcrtNodeInfoMapItem; // mutex for mMcrtNodeInfoMapItem access
    mcrt_dataio::McrtNodeInfoMapItem mMcrtNodeInfoMapItem;
};

} // namespace mcrt_computation

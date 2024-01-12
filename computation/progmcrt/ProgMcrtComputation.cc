// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ProgMcrtComputation.h"

#include <arras4_log/LogEventStream.h>
#include <mcrt_computation/engine/mcrt/RenderContextDriver.h>
#include <mcrt_dataio/engine/mcrt/McrtControl.h>
#include <mcrt_messages/CreditUpdate.h>
#include <mcrt_messages/ProgressiveFeedback.h>
#include <mcrt_messages/RDLMessage_LeftEye.h>
#include <mcrt_messages/RDLMessage_RightEye.h>
#include <mcrt_messages/ViewportMessage.h>
#include <moonray/rendering/rndr/RenderContext.h>
#include <scene_rdl2/render/util/StrUtil.h>

#include <algorithm>    // std::transform
#include <cctype>       // std::tolower
#include <thread>       // std::thread::hardware_concurrency()

// This directive is used only for development purpose.
// This directive shows some debug messages to the stderr in order to make
// shortest latency of message display to the ssh windows.
// However this directive *** SHOULD BE COMMENTED OUT *** for release version.
//#define DEVELOP_VER_MESSAGE

//#define USE_RAAS_DEBUG_FILENAME

#ifndef MOONRAY_EXEC_MODE_DEFAULT
#define MOONRAY_EXEC_MODE_DEFAULT AUTO
#endif

// stringification helper macros
#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

namespace mcrt_computation {

COMPUTATION_CREATOR(ProgMcrtComputation);

namespace {

    // Configuration constants
    const std::string sConfigScene = "scene";
    const std::string sConfigDsopath = "dsopath";
    const std::string sConfigEnableDepthBuffer = "enableDepthBuffer";
    const std::string sConfigFps = "fps";
    const std::string sConfigNumMachines = "numMachines";
    const std::string sConfigMachineId = "machineId";
    const std::string sConfigFrameGating = "frameGating";
    const std::string sConfigFastGeometry = "fastGeometry";
    const std::string sConfigPackTilePrecision = "packTilePrecision";
    const std::string sRenderMode = "renderMode";
    const std::string sApplicationMode = "applicationMode";
    const std::string sExecMode = "exec_mode";
    const std::string sInitialCredit = "initialCredit";

    const std::string AOV_BEAUTY = "beauty";
    const std::string AOV_DEPTH = "depth";

    // KEY used to indicate that a RenderSetupMessage originated from an upstream computation, and not a client
    // Used to get around the lack of message intents: http://jira.anim.dreamworks.com/browse/NOVADEV-985
    const std::string RENDER_SETUP_KEY = "776CD313-6D4B-40A4-82D2-C61F2FD055A9";

} // namespace

ProgMcrtComputation::ProgMcrtComputation(arras4::api::ComputationEnvironment* env)
    : Computation(env)
    , mOptions()
{
#   ifdef USE_RAAS_DEBUG_FILENAME
    char * delayFilename = getenv("RAAS_DEBUG_FILENAME");
    if (delayFilename) {
        std::cerr << ">> ProgMcrtComputation.cc debug wait loop START"
                  << " delayFilename:" << delayFilename << std::endl;
        while (access(delayFilename, R_OK)) {
            unsigned int const DELTA_TIME = 3;
            sleep(DELTA_TIME);
            std::cerr << ">> ProgMcrtComputation.cc sleep pid:" << (size_t)getpid() << std::endl;
        }
        std::cerr << ">> ProgMcrtComputation.cc debug wait loop END" << std::endl;
    }
#   endif // end USE_RAAS_DEBUG_FILENAME
}

arras4::api::Object
ProgMcrtComputation::property(const std::string& name)
{
    if (name == arras4::api::PropNames::wantsHyperthreading) {
        return true;
    }
    return arras4::api::Object();
}

arras4::api::Result
ProgMcrtComputation::configure(const std::string& op,
                               arras4::api::ObjectConstRef aConfig)
{   
#   ifdef DEVELOP_VER_MESSAGE
    // Sometime we need hostname of computation to trackdown the issue.
    std::cerr << ">> ProgMcrtComputation.cc hostname:" << mcrt_dataio::MiscUtil::getHostName() << '\n';
    std::cerr << ">> ProgMcrtComputation.cc configure() -a-\n";
#   endif // end DEVELOP_VER_MESSAGE

    if (op == "start") {
        onStart();
        return arras4::api::Result::Success;
    } else if (op == "stop") {
        onStop();
        return arras4::api::Result::Success;
    } else if (op != "initialize") {
        return arras4::api::Result::Unknown; 
    }      

    // Turn off the use of the depth buffer by default.
    mOptions.setGeneratePixelInfo(false);

    // Optionally, enable the depth buffer
    if (aConfig[sConfigEnableDepthBuffer].isBool()) {
        mOptions.setGeneratePixelInfo(aConfig[sConfigEnableDepthBuffer].asBool());
    }

    // Override defaults with settings from the config.
    if (aConfig[sConfigScene].isString()) {
        mOptions.setSceneFiles({aConfig[sConfigScene].asString()});
    }
    if (aConfig[sConfigDsopath].isString()) {
        mOptions.setDsoPath(aConfig[sConfigDsopath].asString());
    }
    if (aConfig[sConfigFps].isNumeric()) {
        mFps = aConfig[sConfigFps].asFloat();
    }
    if (aConfig[sConfigNumMachines].isIntegral()) {
        mNumMachinesOverride = aConfig[sConfigNumMachines].asInt();
    }
    if (aConfig[sConfigMachineId].isIntegral()) {
        mMachineIdOverride = aConfig[sConfigMachineId].asInt();
    }  

    // set the number of threads
    if (aConfig[arras4::api::ConfigNames::maxThreads].isIntegral()) {
        mOptions.setThreads(aConfig[arras4::api::ConfigNames::maxThreads].asInt());
    }
    else {
        mOptions.setThreads(1);
    }

    if (aConfig[sConfigFrameGating].isBool()) {
        mDispatchGatesFrame = aConfig[sConfigFrameGating].asBool();
    }

    if (aConfig[sConfigPackTilePrecision].isString()) {
        if (aConfig[sConfigPackTilePrecision].asString() == "auto32") {
            // auto switching UC8 and H16 for coarse pass, F32 for non-coarse pass
            mPackTilePrecisionMode = PackTilePrecisionMode::AUTO32;
            ARRAS_LOG_INFO("PackTile precision auto32 mode");
        } else if (aConfig[sConfigPackTilePrecision].asString() == "auto16") {
            // auto switching UC8 and H16 for coarse pass, H16 for non-coarse pass
            mPackTilePrecisionMode = PackTilePrecisionMode::AUTO16;
            ARRAS_LOG_INFO("PackTile precision auto16 mode");
        } else if (aConfig[sConfigPackTilePrecision].asString() == "full32") {
            // always use F32
            mPackTilePrecisionMode = PackTilePrecisionMode::FULL32;
            ARRAS_LOG_INFO("PackTile precision full32 mode");
        } else if (aConfig[sConfigPackTilePrecision].asString() == "full16") {
            // always use F16
            mPackTilePrecisionMode = PackTilePrecisionMode::FULL16;
            ARRAS_LOG_INFO("PackTile precision full16 mode");
        }
    }

    if (aConfig[sConfigFastGeometry].isBool() &&
        aConfig[sConfigFastGeometry].asBool()) {
        mOptions.setFastGeometry();
    }

    if (mNumMachinesOverride <= 1) {
        mOptions.setRenderMode(moonray::rndr::RenderMode::PROGRESSIVE);
    } else {
        // We use time-based checkpoint mode for multi-machine configuration.
        // We have trouble stopping mcrt computation at render complete timing
        // if we use PROGRESSIVE mode for many machine situations.
        mOptions.setRenderMode(moonray::rndr::RenderMode::PROGRESS_CHECKPOINT);
    }
    if (aConfig[sRenderMode].isString()) {
        if (aConfig[sRenderMode].asString() == "realtime") {
            // We still keep multi-machine realtime mode but this does not work well
            // due to the fact the current merge node is not tested well for realtime
            // rendering. It might have performance issues.
            mOptions.setRenderMode(moonray::rndr::RenderMode::REALTIME);
        } else {
            if (mNumMachinesOverride <= 1) {
                ARRAS_LOG_INFO("Unrecognized render mode, setting to default Progressive Mode");
            } else {
                ARRAS_LOG_INFO("Unrecognized render mode, setting to default Checkpoint(timebased) Mode");
            }
        }
    }

    // Undefined mode is backward compatible with previous behavior and has been added as such.
    mOptions.setApplicationMode(moonray::rndr::ApplicationMode::UNDEFINED);
    if (aConfig[sApplicationMode].isString()) {
        if (aConfig[sApplicationMode].asString() == "motionCapture") {
            mOptions.setApplicationMode(moonray::rndr::ApplicationMode::MOTIONCAPTURE);
        } else if(aConfig[sApplicationMode].asString() == "motioncapture") {
            mOptions.setApplicationMode(moonray::rndr::ApplicationMode::MOTIONCAPTURE);
        } else {
            ARRAS_LOG_ERROR("APPLICATION MODE SET TO UNDEFIND");
        }
    }

    // Stringify this definition value
    std::string execMode = EXPAND_AND_QUOTE(MOONRAY_EXEC_MODE_DEFAULT);
    // Make it lowercase
    std::transform(execMode.begin(), execMode.end(), execMode.begin(),
        [] (unsigned char c) { return std::tolower(c); });

    if (aConfig[sExecMode].isString()) {
        // See moonray/lib/rendering/rndr/RenderOptions.cc
        // 4 possible options : "auto", "vectorized", "scalar", "xpu"
        execMode = aConfig[sExecMode].asString();
    }
    mOptions.setDesiredExecutionMode(execMode);

    if (aConfig[sInitialCredit].isIntegral()) {
        mInitialCredit = aConfig[sInitialCredit].asInt();
    }

    arras4::api::Object name = environment("computation.name");
    if (name.isString()) {
        mName = name.asString();
    }
    arras4::api::Object addr = environment("computation.address");
    if (!addr.isNull()) {
        mAddress.fromObject(addr);
    }

    std::string version("(unknown)");
    const char* cversion = std::getenv("REZ_MCRT_COMPUTATION_VERSION");
    if (cversion) version = cversion;
    ARRAS_ATHENA_TRACE(0,
                       arras4::log::Session(mAddress.session.toString())
                       << "{trace:mcrt} version mcrt_computation-" << version
                       << " host " << mcrt_dataio::MiscUtil::getHostName());

    mSysUsage.getCpuUsage();  // do initial call for CPU usage monitor

    return arras4::api::Result::Success;
}

ProgMcrtComputation::~ProgMcrtComputation()
{
}

void
ProgMcrtComputation::onStart()
{
    MNRY_ASSERT(mMachineIdOverride >= 0);

    parserConfigureGenericMessage();

    // Run global init (creates a RenderDriver) This *must* be called on the same thread we
    // intend to call RenderContext::startFrame from.
    moonray::rndr::initGlobalDriver(mOptions);

    mRenderContextDriverMaster.reset(new RenderContextDriverMaster(mNumMachinesOverride,
                                                                   mMachineIdOverride,
                                                                   mSysUsage,
                                                                   mSendBandwidthTracker,
                                                                   &mLogging,
                                                                   &mLogDebug_creditUpdateMessage,
                                                                   mPackTilePrecisionMode,
                                                                   &mRecvFeedbackFpsTracker,
                                                                   &mRecvFeedbackBandwidthTracker));
    mRenderContextDriverMaster->
        addDriver(&mOptions,
                  &mRenderPrepCancel,
                  &mFps,
                  [&]() {}, // PostMainCallBack function
                  [&](const bool reloadScn, const std::string& source) { // startFrameCallBack
                      sendProgressMessage("renderPrep", reloadScn ? "start" : "restart", source);
                      if (mRecLoad) mRecLoad->startLog(); // CPU load logger start
                  },
                  [&](const std::string& source) { // stopFrameCallBack
                      sendProgressMessage("shading", "stop", source);
                      if (mRecLoad) mRecLoad->stopLog(); // CPU load logger stop
                  },
                  [&](mcrt::ProgressiveFrame::Ptr msg, const std::string& source) { // sendInfoOnlyCallBack
                      // send progressiveFrame message which only store statistical info
                      send(msg, arras4::api::withSource(source));
                      if (mCredit > 0) mCredit--;
                  }
                  ); // construct primary renderContextDriver

    // reset current credit to initial credit. If this is >= 0, credit rate control will be enabled
    // otherwise it is inactive
    mCredit = mInitialCredit;

    sendProgressMessage("ready", "start", "");
}

void
ProgMcrtComputation::onStop()
{
    // Shutdown the renderer
    mRenderContextDriverMaster = nullptr;

    moonray::rndr::cleanUpGlobalDriver();

    if (mRecLoad) {
        delete mRecLoad;
        mRecLoad = 0x0;
    }
}

void
ProgMcrtComputation::onIdle()
{
    // RenderContextDriverMaster can support multiple renderContextDriver. However at this moment,
    // we only use primary renderContextDriver (i.e. driverId = 0).
    RenderContextDriver* driver = mRenderContextDriverMaster->getDriver(0);
    if (!driver->isEnoughSendInterval(mFps, mDispatchGatesFrame)) {
        return; // Interval-wise, we are not ready to send data
    }

    if (mCredit != 0) {
        driver->sendDelta([&](mcrt::ProgressiveFrame::Ptr msg, const std::string& source) {
                              // send progressiveFrame message with delta image and statistical info
                              // and also send progress message
                              sendProgressMessageStageShading(msg->mHeader.mStatus,
                                                              msg->mHeader.mProgress,
                                                              source);

                              send(msg, arras4::api::withSource(source));
                              if (mCredit > 0) mCredit--;

                              if (msg->mHeader.mStatus == mcrt::BaseFrame::Status::FINISHED) {
                                  if (mRecLoad) mRecLoad->stopLog(); // CPU load logger stop
                              }
                          });
    }

    driver->applyUpdatesAndRestartRender([&]() -> TimingRecorder::TimingRecorderSingleFrameShPtr {
            return mTimingRecorder.newFrame();
        });
}

void
ProgMcrtComputation::sendProgressMessage(const std::string& stage,
                                         const std::string& event,
                                         const std::string& source)
{
    mcrt::ProgressMessage::Ptr msg(new mcrt::ProgressMessage);
    msg->object()["computationType"] = "progmcrt";
    msg->object()["stage"] = stage;
    msg->object()["event"] = event;
    if (!source.empty()) {
        msg->object()["source"] = source;
    }

    send(msg);

    std::ostringstream ostr;
    ostr << "{trace:mcrt}"
         << " stage " << stage
         << " " << event
         << " " << mAddress.computation.toString();
    if (!source.empty()) {
        ostr << " " << source;
    }

    ARRAS_ATHENA_TRACE(0,
                       arras4::log::Session(mAddress.session.toString())
                       << ostr.str());
}

void
ProgMcrtComputation::sendProgressMessageStageShading(const mcrt::BaseFrame::Status& status,
                                                     const float progress,
                                                     const std::string& source)
{
    std::string stage = "shading";
    std::string event = "";
    switch (status) {
    case mcrt::BaseFrame::Status::STARTED   : event = "start";    break;
    case mcrt::BaseFrame::Status::RENDERING : event = "pending";  break;
    case mcrt::BaseFrame::Status::FINISHED  : event = "complete"; break;
    default : break;
    }
    int traceLevel = (event == "pending") ? 1 : 0;
    
    mcrt::ProgressMessage::Ptr msg(new mcrt::ProgressMessage);
    msg->object()["computationType"] = "progmcrt";
    msg->object()["stage"] = stage;
    msg->object()["event"] = event;
    msg->object()["progress"] = progress;
    msg->object()["source"] = source;

    send(msg);

    ARRAS_ATHENA_TRACE(traceLevel,
                       arras4::log::Session(mAddress.session.toString())
                       << "{trace:mcrt} stage " << stage << " " << event << " "
                       << progress << " " << mAddress.computation.toString() << " "
                       << source);
}

//------------------------------------------------------------------------------

arras4::api::Result
ProgMcrtComputation::onMessage(const arras4::api::Message& aMsg)
{
    if (aMsg.classId() != mcrt::CreditUpdate::ID ||
        mLogDebug_creditUpdateMessage) {
        ARRAS_LOG_DEBUG("MCRT received message: %s", aMsg.describe().c_str());
    }

    if (aMsg.classId() == mcrt::GenericMessage::ID) {
        // This is McrtControl + debugging message and
        // we have to execute regardless of mDebugSuspendExecution condition
        mcrt::GenericMessage::ConstPtr gm = aMsg.contentAs<mcrt::GenericMessage>();
        if (gm) handleGenericMessage(gm);
        return arras4::api::Result::Success;
    }

    // RenderContextDriverMaster can support multiple renderContextDriver in order to quickly
    // test multi-renderContext driver configuration for the future plan. However at this moment,
    // we only use primary renderContextDriver (i.e. driverId = 0).
    RenderContextDriver* driver = mRenderContextDriverMaster->getDriver(0);
    if (aMsg.classId() == mcrt::GeometryData::ID) {
        driver->enqGeometryMessage(aMsg);
    } else if (aMsg.classId() == mcrt::RDLMessage::ID) {
        driver->enqRdlMessage(aMsg, mTimingRecorder.getSec());
    } else if (aMsg.classId() == mcrt::RDLMessage_LeftEye::ID) {
        driver->enqRdlMessage(aMsg, mTimingRecorder.getSec());
    } else if (aMsg.classId() == mcrt::RDLMessage_RightEye::ID) {
        driver->enqRdlMessage(aMsg, mTimingRecorder.getSec());
    } else if (aMsg.classId() == mcrt::ViewportMessage::ID) { 
        driver->enqViewportMessage(aMsg, mTimingRecorder.getSec());

    } else if (aMsg.classId() == mcrt::JSONMessage::ID) {
        onJSONMessage(aMsg);

    } else if (aMsg.classId() == mcrt::ProgressiveFeedback::ID) {
        driver->evalProgressiveFeedbackMessage(aMsg);

    //------------------------------

    } else if (aMsg.classId() == mcrt::CreditUpdate::ID) {
        onCreditUpdate(aMsg);
    } else {
        std::cerr << ">> ProgMcrtComputation.cc onMessage ===>>> Unknown <<<===\n";
        return arras4::api::Result::Unknown;
    }

    return arras4::api::Result::Success;
}

void
ProgMcrtComputation::parserConfigureGenericMessage()
{
    Parser& parser = mParserGenericMessage;
    parser.description("genericMsg command");

    // We set no error about unknown option condition
    // Merge computation might send some generic message and we want to ignore them without error
    parser.setErrorUnknownOption(false);

    parser.opt("snapshot", "", "execute snapshot",
               [&](Arg &) -> bool {
                   // RenderContextDriverMaster can support multiple renderContextDriver in order to quickly
                   // test multi-renderContext driver configuration for the future plan.
                   // However at this moment, we only use primary renderContextDriver (i.e. driverId = 0).
                   mRenderContextDriverMaster->getDriver(0)->setReceivedSnapshotRequest(true);
                   return true;
               });
    parser.opt("fps", "<fps>", "set fps value",
               [&](Arg &arg) -> bool {
                   mFps = (arg++).as<float>(0);
                   return true;
               });
    parser.opt("cmd", "<nodeId> ...command...", "mcrt debug command",
               [&](Arg &arg) -> bool {
                   int nodeId = (arg++).as<int>(0);
                   Arg childArg = arg.childArg(std::string("cmd ") + std::to_string(nodeId));
                   bool flag = true;
                   if (nodeId == mMachineIdOverride || nodeId == -1) {
                       RenderContextDriver *driver = mRenderContextDriverMaster->getDriver(0);
                       flag = driver->evalDebugCommand(childArg);
                   }
                   return flag;
               });
}

void
ProgMcrtComputation::handleGenericMessage(mcrt::GenericMessage::ConstPtr msg)
{
    mcrt_dataio::McrtControl mcrtControl(mMachineIdOverride);
    if (mcrtControl.isCommand(msg->mValue)) { // McrtControl command test
        //
        // McrtControl command execution (from McrtMerge)
        //
        if (mcrtControl.
            run(msg->mValue,
                [&](uint32_t currSyncId) { // callBackRenderCompleteProcedure()
                    // RenderContextDriverMaster can support multiple renderContextDriver in order to
                    // quickly test multi-renderContext driver configuration for the future plan.
                    // However at this moment, we only use primary renderContextDriver (i.e. driverId = 0).
                    RenderContextDriver *driver = mRenderContextDriverMaster->getDriver(0);
                    driver->evalRenderCompleteMultiMachine(currSyncId);
                    return true;
                },
                [&](uint32_t currSyncId, float fraction) { // callBackGlobalProgressUpdate()
                    // RenderContextDriverMaster can support multiple renderContextDriver in order to
                    // quickly test multi-renderContext driver configuration for the future plan.
                    // However at this moment, we only use primary renderContextDriver (i.e. driverId = 0).
                    RenderContextDriver *driver = mRenderContextDriverMaster->getDriver(0);
                    driver->evalMultiMachineGlobalProgressUpdate(currSyncId, fraction);
                    return true;
                })) {
            // processed McrtControl command w/ OK condition
        } else {
            ARRAS_LOG_ERROR("McrtControl command failed");
        }
        return;
    }

    Arg arg(msg->mValue);
    RenderContextDriver* driver = mRenderContextDriverMaster->getDriver(0);
    driver->setMessageHandlerToArg(arg); // setup message handler in order to send message to the downstream
    if (!mParserGenericMessage.main(arg)) {
        arg.msg("parserGenericMessage failed");
    }
}

void
ProgMcrtComputation::onJSONMessage(const arras4::api::Message& msg)
{
    mcrt::JSONMessage::ConstPtr jm = msg.contentAs<mcrt::JSONMessage>();
    if (!jm) return;

    const std::string messageID = jm->messageId();

    // RenderContextDriverMaster can support multiple renderContextDriver in order to quickly
    // test multi-renderContext driver configuration for the future plan. However at this moment,
    // we only use primary renderContextDriver (i.e. driverId = 0).
    RenderContextDriver *driver = mRenderContextDriverMaster->getDriver(0);

    if (messageID == mcrt::RenderMessages::RENDER_CONTROL_ID) {
        driver->enqRenderControlMessage(msg, mTimingRecorder.getSec());
    } else if (messageID == mcrt::RenderMessages::PICK_MESSAGE_ID) {
        driver->evalPickMessage(msg,
                                [&](const arras4::api::MessageContentConstPtr &msg) {
                                    send(msg);
                                });
    } else if (messageID == mcrt::RenderMessages::SET_ROI_OPERATION_ID) {
        driver->enqROISetMessage(msg, mTimingRecorder.getSec());
    } else if (messageID == mcrt::RenderMessages::SET_ROI_STATUS_OPERATION_ID) {
        driver->enqROIResetMessage(msg, mTimingRecorder.getSec());
    } else if (messageID == mcrt::RenderMessages::INVALIDATE_RESOURCES_ID) {
        driver->evalInvalidateResources(msg);
    } else if (messageID == mcrt::RenderMessages::RENDER_SETUP_ID) {
        ARRAS_LOG_INFO("Render Setup");
        driver->enqRenderSetupMessage(msg, mTimingRecorder.getSec());
    } else if (messageID == mcrt::OutputRates::SET_OUTPUT_RATES_ID) {
        driver->evalOutputRatesMessage(msg);

    //------------------------------

    } else if (messageID == mcrt::RenderMessages::LOGGING_CONFIGURATION_MESSAGE_ID) {
        std::cerr << ">> ProgMcrtComputation.cc onJSONMessage ===>>> LOGGING_CONFIGURATION_MESSAGE_ID <<<===\n";
        auto &payload = jm->messagePayload(); 
        arras4::log::Logger::Level level =
            static_cast<arras4::log::Logger::Level>(payload[Json::Value::ArrayIndex(0)].asInt());
        arras4::log::Logger::instance().setThreshold(level);
#if 0
    } else if (messageID == SELECT_AOV_ID) {
        // Handle AOV selection
#endif
    }
}

void 
ProgMcrtComputation::onCreditUpdate(const arras4::api::Message& msg)
{
    if (mCredit >= 0) {
        mcrt::CreditUpdate::ConstPtr c = msg.contentAs<mcrt::CreditUpdate>();
        if (c) c->applyTo(mCredit,mInitialCredit);
    }
}

} // namespace mcrt_computation

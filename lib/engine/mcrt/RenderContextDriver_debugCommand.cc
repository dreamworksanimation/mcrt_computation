// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "RenderContextDriver.h"

#include <moonray/rendering/rndr/RenderContext.h>
#include <moonray/rendering/rndr/RenderPrepExecTracker.h>
#include <moonray/rendering/shading/UdimTexture.h>

#include <scene_rdl2/render/logging/logging.h>

namespace mcrt_computation {

void
RenderContextDriver::debugCommandParserConfigure()
{
    namespace str_util = scene_rdl2::str_util;

    auto showHead = []() -> std::string {
        return ">>> RenderContextDriver.cc debugCommand : ";
    };

    Parser& parser = mParserDebugCommand;
    parser.description("mcrt computation debug command"); 
    parser.opt("snapshotDeltaRec", "...command...", "snapshotDeltaRec command",
               [&](Arg &arg) -> bool { return mParserDebugCommandSnapshotDeltaRec.main(arg.childArg()); });
    parser.opt("showPrecision", "", "show AOV precision information",
               [&](Arg &arg) -> bool { return arg.msg(mFbSender.jsonPrecisionInfo() + '\n'); });
    parser.opt("reconstructFromBackup", "", "test for reconstruct by backupSceneContext",
               [&](Arg &arg) -> bool {
                   reconstructSceneFromBackup();
                   return arg.msg("reconstructSceneFromBackup() completed\n");
               });
    parser.opt("renderContext", "...command...", "renderContext command",
               [&](Arg &arg) -> bool { return debugCommandRenderContext(arg); });
    parser.opt("fbSender", "...command...", "McrtFbSender command",
               [&](Arg& arg) -> bool { return mFbSender.getParser().main(arg.childArg()); });
    parser.opt("initFrameCtrl", "...command...", "initial frame control",
               [&](Arg& arg) -> bool { return mParserInitialFrameControl.main(arg.childArg()); });
    parser.opt("messageHistory", "...command...", "message history related command",
               [&](Arg& arg) -> bool { return mMessageHistory.getParser().main(arg.childArg()); });
    parser.opt("multiBank", "...command...", "multi-bank related command",
               [&](Arg& arg) -> bool { return mParserMultiBankControl.main(arg.childArg()); });
    parser.opt("logging", "...command...", "logging related command",
               [&](Arg& arg) -> bool { return mParserLoggingControl.main(arg.childArg()); });
    parser.opt("feedback", "<on|off|show>",
               "enable/disable feedback logic. This condition will apply next render start timing",
               [&](Arg& arg) -> bool {
                   if (arg() != "show") setFeedbackActive((arg++).as<bool>(0));
                   else arg++;
                   return arg.msg(str_util::boolStr(mFeedbackActiveUserInput) + '\n');
               });
    parser.opt("feedbackInterval", "<intervalSec|show>", "feedback interval by sec",
               [&](Arg& arg) -> bool {
                   if (arg() != "show") setFeedbackIntervalSec((arg++).as<float>(0));
                   else arg++;
                   return arg.msg(std::to_string(mFeedbackIntervalSec) + " sec\n");
               });
    parser.opt("feedbackFb", "...command...", "feedback fb command",
               [&](Arg& arg) -> bool { return mFeedbackFb.getParser().main(arg.childArg()); });
    parser.opt("feedbackUpdate", "...command...", "feedback update command",
               [&](Arg& arg) -> bool { return mFeedbackUpdates.getParser().main(arg.childArg()); });
    parser.opt("feedbackDebug", "...command...", "mcrt debug feedback command",
               [&](Arg& arg) -> bool {
                   if (!mMcrtDebugFeedback) return arg.msg("mcrt debug feedback logic is disabled\n");
                   return mMcrtDebugFeedback->getParser().main(arg.childArg());
               });
    parser.opt("feedbackStats", "", "show feedback statistical information",
               [&](Arg& arg) -> bool { return arg.msg(showFeedbackStats() + '\n'); });
    parser.opt("sentImageCache", "...command...", "sent image data cache command",
               [&](Arg& arg) -> bool { return mSentImageCache.getParser().main(arg.childArg()); });
    parser.opt("progressiveFrameRec", "<on|off|show>", "progressiveFrame data rec mode for debug",
               [&](Arg& arg) {
                   if (arg() != "show") mProgressiveFrameRecMode = (arg++).as<bool>(0);
                   else arg++;
                   return arg.msg(str_util::boolStr(mProgressiveFrameRecMode) + '\n');
               });

    //------------------------------

    Parser& parserSSR = mParserDebugCommandSnapshotDeltaRec;
    parserSSR.description("snapshotDelta rec related command");
    parserSSR.opt("dump", "<filename>", "dump snapshotDelta to file",
                  [&](Arg &arg) -> bool {
                      std::string filename = arg(0);
                      mFbSender.snapshotDeltaRecStop(); // just in case we have to stop
                      std::ostringstream ostr;
                      if (mFbSender.snapshotDeltaRecDump(filename)) {
                          ostr << showHead() << "snapshotDeltaRecDump file:" << filename << " done";
                      } else {
                          ostr << showHead() << "snapshotDeltaRecDump file:" << filename << " failed";
                      }
                      return arg.msg(ostr.str() + '\n');
                  });
    parserSSR.opt("reset", "", "reset snapshotDelta rec logic",
                  [&](Arg &arg) -> bool {
                      mFbSender.snapshotDeltaRecReset();
                      return arg.msg(showHead() + "snapshotDeltaRecReset\n");
                  });
    parserSSR.opt("start", "", "start snapshotDelta rec logic",
                  [&](Arg &arg) -> bool {
                      mFbSender.snapshotDeltaRecStart();
                      return arg.msg(showHead() + "snapshotDeltaRecStart\n");
                  });
    parserSSR.opt("stop", "", "stop snapshotDelta rec logic",
                  [&](Arg &arg) -> bool {
                      mFbSender.snapshotDeltaRecStop();            
                      return arg.msg(showHead() + "snapshotDeltaRecStop\n");
                  });

    //------------------------------

    Parser& parserIFC = mParserInitialFrameControl;
    parserIFC.description("initial frame control command");
    parserIFC.opt("max", "<numNodes>", "target max mcrt total for 1st frame non-delayed snapshot",
                  [&](Arg& arg) -> bool {
                      mInitFrameNonDelayedSnapshotMaxNode = (arg++).as<int>(0);
                      mInitFrameDelayedSnapshotSec = -1.0; // reset
                      std::ostringstream ostr;
                      ostr << "initial frame non delayed snapshot max node = "
                           << mInitFrameNonDelayedSnapshotMaxNode;
                      return arg.msg(ostr.str() + '\n');
                  });
    parserIFC.opt("delay", "<millisec>", "initial frame snapshot delay step for each node",
                  [&](Arg& arg) -> bool {
                      mInitFrameDelayedSnapshotStepMS = (arg++).as<float>(0);
                      mInitFrameDelayedSnapshotSec = -1.0; // reset
                      return arg.fmtMsg("initial frame snapshot delay step = %f ms\n",
                                        mInitFrameDelayedSnapshotStepMS);
                  });
    parserIFC.opt("show", "", "show current condition",
                  [&](Arg& arg) -> bool {
                      return arg.msg(showInitFrameControlStat() + '\n');
                  });

    //------------------------------

    Parser& parserMBC = mParserMultiBankControl;
    parserMBC.description("multi-bank control command");
    parserMBC.opt("show", "", "show current condition",
                  [&](Arg& arg) -> bool {
                      return arg.msg(showMultiBankControlStat() + '\n');
                  });
    parserMBC.opt("total", "<n>", "number of multi-bank (default 1)",
                  [&](Arg& arg) -> bool {
                      mMultiBankTotal = (arg++).as<unsigned>(0);
                      return arg.fmtMsg("multi-bank total:%d\n", mMultiBankTotal);
                  });

    //------------------------------

    Parser& parserL = mParserLoggingControl;
    parserL.description("logging control command");
    parserL.opt("global", "<on|off|show>", "logging enabled on/off or show current info",
                [&](Arg& arg) -> bool {
                    using LogEventRegistry = scene_rdl2::rdl2::ShaderLogEventRegistry;
                    if (arg() == "show") arg++;
                    else LogEventRegistry::setLoggingEnabled((arg++).as<bool>(0));
                    return arg.fmtMsg("logging global switch %s\n",
                                      str_util::boolStr(LogEventRegistry::getLoggingEnabled()).c_str());
                });
    parserL.opt("udimMissing", "<on|off|show>",
                "udim missing texture warning switch on/off or show current info",
                [&](Arg& arg) -> bool {
                    using UdimTexture = moonray::shading::UdimTexture;
                    if (arg() == "show") arg++;
                    else UdimTexture::setUdimMissingTextureWarningSwitch((arg++).as<bool>(0));
                    return arg.fmtMsg("udim missing warning %s\n",
                                      str_util::boolStr(UdimTexture::getUdimMissingTextureWarningSwitch()).c_str());
                });
    parserL.opt("debugLogCreditUpdate", "<on|off|show>", "debug logging condition for creditUpdate message",
                [&](Arg& arg) -> bool {
                    if (mMcrtDebugLogCreditUpdateMessage) {
                        if (arg() == "show") arg++;
                        else (*mMcrtDebugLogCreditUpdateMessage) = (arg++).as<bool>(0);
                        return arg.fmtMsg("debugLogCreditUpdate %s\n",
                                          str_util::boolStr(*mMcrtDebugLogCreditUpdateMessage).c_str());
                    } else {
                        arg++;
                        return arg.msg("mcrtDebugLogCreditUpdateMessage flag is null. skip\n");
                    }
                });
}

bool
RenderContextDriver::evalDebugCommand(Arg &arg)
{
    setMessageHandlerToArg(arg);
    return mParserDebugCommand.main(arg);
}

void
RenderContextDriver::setMessageHandlerToArg(Arg &arg)
{
    // Set message handler here in order to send all arg.msg() to the downstream.
    arg.setMessageHandler([&](const std::string &msg) -> bool {
            showMsg(msg, false);
            return true;
        });
}

//-----------------------------------------------------------------------------------------

bool
RenderContextDriver::debugCommandRenderContext(Arg &arg)
{
    // setup message handler in order to send all execTracker's internal message to the downstream.
    // We should not remove this handler because removing the hander requires MTsafe logic and it is not
    // implemented yet.
    if (getRenderContext()->needToSetExecTrackerMsgHandlerCallBack()) {
        getRenderContext()->setExecTrackerMsgHandlerCallBack([&](const std::string &msg) {
                mMcrtNodeInfoMapItem.getMcrtNodeInfo().enqGenericComment(msg);
            });
    }

    return getRenderContext()->getParser().main(arg.childArg());
}

} // namespace mcrt_computation

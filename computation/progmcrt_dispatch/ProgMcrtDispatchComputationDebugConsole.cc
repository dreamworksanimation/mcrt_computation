// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#include "ProgMcrtDispatchComputationDebugConsole.h"
#include "ProgMcrtDispatchComputation.h"

#include <arras4_log/Logger.h>
#include <mcrt_dataio/share/util/MiscUtil.h>
#include <mcrt_messages/GenericMessage.h>
#include <mcrt_messages/JSONMessage.h>
#include <mcrt_messages/RenderMessages.h>

#include <cmath>                // fabsf()
#include <sstream>

namespace mcrt_computation {

int
ProgMcrtDispatchComputationDebugConsole::open(const int portNumber,
                                              ProgMcrtDispatchComputation *dispatchComputation)
{
    mDispatchComputation = dispatchComputation;

    mFps = dispatchComputation->mFps;

    int port = mTlSvr.open(portNumber);
    ARRAS_LOG_INFO("DebugConsole host:%s port:%d\n", mcrt_dataio::MiscUtil::getHostName().c_str(), port);

    return port;
}

bool
ProgMcrtDispatchComputationDebugConsole::eval()
{
    if (!mDispatchComputation) return false;

    std::string cmdLine;

    int rt = mTlSvr.recv(cmdLine);
    if (rt < 0) {
        return false;
    } else if (rt == 0) {
        // empty console input
    } else {
        ARRAS_LOG_INFO("DebugConsole:%s\n", cmdLine.c_str());
        cmdParse(cmdLine);
    }

    return true;
}

void
ProgMcrtDispatchComputationDebugConsole::sendDispatchHostNameToMerge()
{
    std::ostringstream ostr;
    ostr << "dispatchHost " << mcrt_dataio::MiscUtil::getHostName();
    sendCmdLine(-2, "cmd", ostr.str());
}

void
ProgMcrtDispatchComputationDebugConsole::close()
{
    mTlSvr.close();
    mDispatchComputation = 0x0;
}

void    
ProgMcrtDispatchComputationDebugConsole::cmdHelp()
{
    std::ostringstream ostr;
    ostr << "help {\n"
         << "  clockOffset <hostname> <offsetMs> : set internal clock offset by ms for mcrt computation\n"
         << "  fps <val> : set fps rate\n"
         << "  help : show this message\n"
         << "  roi <x0> <y0> <x1> <y1> : set ROI viewport\n"
         << "  roiReset : reset ROI viewport\n"
         << "  send <nodeId> <command...> : send command to the node.\n"
         << "    <nodeId> : -1=for_all_mcrt_node, -2=for_merger, -3=dispatch\n"
         << "    <command> :\n"
         << "      complete <syncId>             : stop render when current syncId == <syncId>\n"
         << "      start             : for debug : back from stop condition and start render\n"
         << "      stop              : for debug : stop and skip all messages\n"
         << "      snapshotDeltaRecStart         : start snapshotDelta rec logic\n"
         << "      snapshotDeltaRecStop          : stop  snapshotDelta rec logic\n"
         << "      snapshotDeltaRecReset         : reset snapshotDelta rec logic\n"
         << "      snapshotDeltaRecDump filename : dump snapshotDelta (need stop first)\n"
         << "    ==[node computation (render configure) command]==\n"
         << "    commands are kept internally and applied at every frame.\n"
         << "    send -1 task <taskType> : change task schedule type at every render configure stage \n"
         << "                 tile    : set progMcrt as task tile mode\n"
         << "                 pix     : multiplex pixel distribution mode\n"
         << "                 <empty> : cancel task setup for render configure if call w/o task type\n"
         << "    ==[merger computation command]==\n"
         << "    merger computation command required nodeId=-2\n"
         << "    send -2 merge <mergeMode> : change mergeMode dynamically at runtime\n"
         << "                  seamless : merger seamless-combine mode\n"
         << "                  latest   : merger pickup-latest mode\n"
         << "                  lineup   : merger syncid-lineup mode\n"
         << "    send -2 partialMergeRefresh <sec> : set partial merge refresh interval by sec\n"
         << "    send -2 dispatchHost <hostname> : set progmcrt_dispatch hostname\n"
         << "  show : show last sent info\n"
         << "}\n";

    mTlSvr.send(ostr.str());
}

void
ProgMcrtDispatchComputationDebugConsole::cmdParse(const std::string &cmdLine)
{
    if (cmdCmp("clockOffset", cmdLine)) {
        cmdClockOffset(cmdLine);
    } else if (cmdCmp("fps", cmdLine)) {
        cmdFps(cmdLine);
    } else if (cmdCmp("help", cmdLine)) {
        cmdHelp();
    } else if (cmdCmp("roiReset", cmdLine)) { // This if statement should be before "roi"
        cmdRoiReset();
    } else if (cmdCmp("roi", cmdLine)) {
        cmdRoi(cmdLine);
    } else if (cmdCmp("show", cmdLine)) {
        cmdShow();
    } else if (cmdCmp("send", cmdLine)) {
        cmdSend(cmdLine);
    } else {
        std::ostringstream ostr;
        ostr << "> unknown command>" << cmdLine;
        mTlSvr.send(ostr.str());
    }
}

void
ProgMcrtDispatchComputationDebugConsole::cmdClockOffset(const std::string &cmdLine)
{
    sendClockOffset(cmdLine);

    std::ostringstream ostr;
    ostr << "> " << cmdLine;
    mTlSvr.send(ostr.str());
}

void
ProgMcrtDispatchComputationDebugConsole::cmdFps(const std::string &cmdLine)
{
    std::istringstream istr(cmdLine);
    std::string token;
    float fps;

    istr >> token;              // skip "fps"
    istr >> token;
    std::istringstream(token) >> fps;

    //------------------------------

    mDispatchComputation->mFps = fps;
    sendFpsMsg(fps);

    //------------------------------

    std::ostringstream ostr;
    ostr << "> update fps rate ... " << fps << std::endl;
    mTlSvr.send(ostr.str());
}

void
ProgMcrtDispatchComputationDebugConsole::cmdRoi(const std::string &cmdLine)
{
    std::istringstream istr(cmdLine);
    std::string token;
    int x0, y0, x1, y1;

    istr >> token;              // skip "roi"
    istr >> x0 >> y0 >> x1 >> y1;

    sendRoiViewport(x0, y0, x1, y1);

    std::ostringstream ostr;
    ostr << "> roi " << x0 << ' ' << y0 << ' ' << x1 << ' ' << y1 << '\n';
    mTlSvr.send(ostr.str());
}

void    
ProgMcrtDispatchComputationDebugConsole::cmdRoiReset()
{
    sendRoi(false);
    mTlSvr.send("> roiReset\n");
}

void
ProgMcrtDispatchComputationDebugConsole::cmdShow()
{
    std::ostringstream ostr;
    ostr << "status {\n"
         << "          fps:" << mFps << '\n'
         << "          roi:" << ((mRoi)? "true": "false") << '\n'
         << "  roiViewport:"
         << mRoiViewport[0] << ' ' << mRoiViewport[1] << ' '
         << mRoiViewport[2] << ' ' << mRoiViewport[3] << '\n'
         << "}\n";
    mTlSvr.send(ostr.str());
}

void
ProgMcrtDispatchComputationDebugConsole::cmdSend(const std::string &cmdLine)
{
    std::istringstream istr(cmdLine);
    std::string token;
    int nodeId;
    istr >> token;              // skip "send"
    istr >> nodeId;

    std::ostringstream ostr;
    for (unsigned i = 0; ; ++i) {
        istr >> token;
        if (!istr) break;
        if (i > 0) ostr << ' ';
        ostr << token;
    }
    std::string args = ostr.str();

    sendCmdLine(nodeId, "cmd", args);

    mTlSvr.send(cmdLine);
}

bool
ProgMcrtDispatchComputationDebugConsole::cmdCmp(const std::string &cmdName, const std::string &cmdLine) const
{
    size_t lenCmdName = cmdName.length();
    size_t lenCmdLine = cmdLine.length();
    if ( lenCmdName > lenCmdLine ) {
        return false;
    }

    std::string ccmd = cmdLine.substr(0, lenCmdName);
    if (cmdName == ccmd) {
        return true;
    }
    return false;
}

void
ProgMcrtDispatchComputationDebugConsole::sendClockOffset(const std::string &clockOffset)
{
    if (mClockOffset == clockOffset) return;
    mClockOffset = clockOffset;
    
    mcrt::GenericMessage::Ptr msg(new mcrt::GenericMessage);
    msg->mValue = mClockOffset;
    mDispatchComputation->send(msg);
}

void
ProgMcrtDispatchComputationDebugConsole::sendFpsMsg(const float fps)
{
    if (fabsf(mFps - fps) < 0.001f) return;
    mFps = fps;

    mcrt::GenericMessage::Ptr msg(new mcrt::GenericMessage);

    std::ostringstream ostr;
    ostr << "fps " << mFps;
    msg->mValue = ostr.str();
    mDispatchComputation->send(msg);
}

void
ProgMcrtDispatchComputationDebugConsole::sendRoiViewport(const int x0, const int y0, const int x1, const int y1)
{
    if (mRoi &&
        mRoiViewport[0] == x0 && mRoiViewport[1] == y0 &&
        mRoiViewport[2] == x1 && mRoiViewport[3] == y1) {
        return;
    }

    mDispatchComputation->send(mcrt::RenderMessages::createRoiMessage(x0, y0, x1, y1));
    mRoiViewport[0] = x0; mRoiViewport[1] = y0; mRoiViewport[2] = x1; mRoiViewport[3] = y1;
    mRoi = true;

    std::ostringstream ostr;
    ostr << ">> ProgMcrtDispatchComputationDebugConsole."
         << " roi:" << x0 << ' ' << y0 << ' ' << x1 << ' ' << y1;
    ARRAS_LOG_INFO(ostr.str());
}

void
ProgMcrtDispatchComputationDebugConsole::sendRoi(const bool st)
{
    if (mRoi == st) return;

    mDispatchComputation->send(mcrt::RenderMessages::createRoiStatusMessage(st));
    mRoi = st;
}

void
ProgMcrtDispatchComputationDebugConsole::sendCmdLine(const int nodeId,
                                                     const std::string &cmdName,
                                                     const std::string &args)
{
    mcrt::GenericMessage::Ptr msg(new mcrt::GenericMessage);

    std::ostringstream ostr;
    ostr << cmdName << ' ' << nodeId << ' ' << args;
    msg->mValue = ostr.str();
    mDispatchComputation->send(msg);
}

} // namespace mcrt_computation


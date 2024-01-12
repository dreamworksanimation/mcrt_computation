// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#pragma once

//
// -- Debug console for dispatch computation --
//
// Connect from telnet via socket and change some parameters dynamically during
// active rendering conditions.
// This functionality is also used to get some info from dispatch computation
// by merge computation in order to get GlobalNodeInfo related data.
//
#include <scene_rdl2/common/grid_util/TlSvr.h>

namespace mcrt_computation {

class ProgMcrtDispatchComputation;

class ProgMcrtDispatchComputationDebugConsole {
public:
    ProgMcrtDispatchComputationDebugConsole() :
        mDispatchComputation(0x0), mFps(0.0f), mRoi(false), mRoiViewport() {}
    ~ProgMcrtDispatchComputationDebugConsole() { close(); }

    int open(const int portNumber, ProgMcrtDispatchComputation *dispatchComputation);

    bool eval();

    void sendDispatchHostNameToMerge();

    void close();

protected:

    void cmdParse(const std::string &cmdLine);
    void cmdClockOffset(const std::string &cmdLine);
    void cmdFps(const std::string &cmdLine);
    void cmdHelp();
    void cmdRoi(const std::string &cmdLine);
    void cmdRoiReset();
    void cmdShow();
    void cmdSend(const std::string &cmdLine);
    bool cmdCmp(const std::string& cmdName, const std::string& cmdLine) const;

    void sendClockOffset(const std::string &clockOffset);
    void sendFpsMsg(const float fps);
    void sendRoiViewport(const int x0, const int y0, const int x1, const int y1);
    void sendRoi(const bool st);
    void sendCmdLine(const int nodeId, const std::string &cmdName, const std::string &args);

    ProgMcrtDispatchComputation *mDispatchComputation;
    scene_rdl2::grid_util::TlSvr mTlSvr;

    std::string mClockOffset; // last sent clockOffset to down stream computation
    float       mFps;         // last sent fps to down stream computation
    bool        mRoi;
    int         mRoiViewport[4];
};

} // namespace mcrt_computation


// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ProgMcrtMergeDebugFeedback.h"

#include <sstream>

namespace mcrt_computation {

void ProgMcrtMergeDebugFeedbackMachine::set(const unsigned lastSendImageActionId,
                                            const unsigned lastPartialMergeTileId,
                                            const Fb& decodedFb)
{
    mLastSendImageActionId = lastSendImageActionId;
    mLastPartialMergeTileId = lastPartialMergeTileId;
    mDecodedFb.copy(nullptr, decodedFb);
}

bool ProgMcrtMergeDebugFeedbackMachine::saveBeautyPPM(const std::string& filePath,
                                                      const uint32_t feedbackId,
                                                      const MessageOutFunc& messageOutFunc) const
{
    return mDecodedFb.saveBeautyPPM(makeFilename(filePath, feedbackId) + "_beauty.ppm",
                                    messageOutFunc);
}

bool ProgMcrtMergeDebugFeedbackMachine::saveBeautyFBD(const std::string& filePath,
                                                      const uint32_t feedbackId,
                                                      const MessageOutFunc& messageOutFunc) const
{
    return mDecodedFb.saveBeautyFBD(makeFilename(filePath, feedbackId) + "_beauty.fbd",
                                    messageOutFunc);
}

bool ProgMcrtMergeDebugFeedbackMachine::saveBeautyNumSamplePPM(const std::string& filePath,
                                                               const uint32_t feedbackId,
                                                               const MessageOutFunc& messageOutFunc) const
{
    return mDecodedFb.saveBeautyNumSamplePPM(makeFilename(filePath, feedbackId) + "_beautyNumSample.ppm",
                                             messageOutFunc);
}

bool ProgMcrtMergeDebugFeedbackMachine::saveBeautyNumSampleFBD(const std::string& filePath,
                                                               const uint32_t feedbackId,
                                                               const MessageOutFunc& messageOutFunc) const
{
    return mDecodedFb.saveBeautyNumSampleFBD(makeFilename(filePath, feedbackId) + "_beautyNumSample.fbd",
                                             messageOutFunc);
}

std::string ProgMcrtMergeDebugFeedbackMachine::show() const
{
    std::ostringstream ostr;
    ostr << "ProgMcrtMergeDebugFeedbackMachine {\n"
         << "  mMachineId:" << mMachineId << '\n'
         << "}";
    return ostr.str();
}

std::string ProgMcrtMergeDebugFeedbackMachine::makeFilename(const std::string& filePath,
                                                            const uint32_t feedbackId) const
{
    std::ostringstream ostr;
    ostr << filePath << "merge"
         << "_fId" << feedbackId
         << "_mId" << mMachineId;
    return ostr.str();
}

//------------------------------------------------------------------------------------------

void ProgMcrtMergeDebugFeedbackFrame::set(const uint32_t feedbackId,
                                          const Fb& mergedFb)
{
    mFeedbackId = feedbackId;
    mMergedFb.copy(nullptr, mergedFb);
}

bool ProgMcrtMergeDebugFeedbackFrame::saveBeautyPPM(const std::string& filePath,
                                                    const MessageOutFunc& messageOutFunc) const
{
    bool flag = saveBeautyMergedAllPPM(filePath, messageOutFunc);
    for (const auto& machine : mMachineTbl) {
        if (!machine.saveBeautyPPM(filePath, mFeedbackId, messageOutFunc)) {
            flag = false;
        }
    }
    return flag;
}

bool ProgMcrtMergeDebugFeedbackFrame::saveBeautyFBD(const std::string& filePath,
                                                    const MessageOutFunc& messageOutFunc) const
{
    bool flag = saveBeautyMergedAllFBD(filePath, messageOutFunc);
    for (const auto& machine : mMachineTbl) {
        if (!machine.saveBeautyFBD(filePath, mFeedbackId, messageOutFunc)) {
            flag = false;
        }
    }
    return flag;
}

bool ProgMcrtMergeDebugFeedbackFrame::saveBeautyNumSamplePPM(const std::string& filePath,
                                                             const MessageOutFunc& messageOutFunc) const
{
    bool flag = saveBeautyNumSampleMergedAllPPM(filePath, messageOutFunc);
    for (const auto& machine : mMachineTbl) {
        if (!machine.saveBeautyNumSamplePPM(filePath, mFeedbackId, messageOutFunc)) {
            flag = false;
        }
    }
    return flag;
}

bool ProgMcrtMergeDebugFeedbackFrame::saveBeautyNumSampleFBD(const std::string& filePath,
                                                             const MessageOutFunc& messageOutFunc) const
{
    bool flag = saveBeautyNumSampleMergedAllFBD(filePath, messageOutFunc);
    for (const auto& machine : mMachineTbl) {
        if (!machine.saveBeautyNumSampleFBD(filePath, mFeedbackId, messageOutFunc)) {
            flag = false;
        }
    }
    return flag;
}

std::string ProgMcrtMergeDebugFeedbackFrame::show() const
{
    std::ostringstream ostr;
    ostr << "ProgMcrtMergeDebugFeedbackFrame {\n"
         << "  mFeedbackId:" << mFeedbackId << '\n'
         << "  mMachineTbl (size:" << mMachineTbl.size() << ") {\n";
    for (unsigned i = 0; i < mMachineTbl.size(); ++i) {
        ostr << scene_rdl2::str_util::addIndent(std::string("i:") + std::to_string(i) + ' ' +
                                                mMachineTbl[i].show(), 2) + '\n';
    }
    ostr << "  }\n"
         << "}";
    return ostr.str();
}

bool
ProgMcrtMergeDebugFeedbackFrame::saveBeautyMergedAllPPM(const std::string& filePath,
                                                        const MessageOutFunc& messageOutFunc) const
{
    return mMergedFb.saveBeautyPPM(makeFilename(filePath) + "_beauty.ppm", messageOutFunc);
}

bool
ProgMcrtMergeDebugFeedbackFrame::saveBeautyMergedAllFBD(const std::string& filePath,
                                                        const MessageOutFunc& messageOutFunc) const
{
    return mMergedFb.saveBeautyFBD(makeFilename(filePath) + "_beauty.fbd", messageOutFunc);
}

bool
ProgMcrtMergeDebugFeedbackFrame::saveBeautyNumSampleMergedAllPPM(const std::string& filePath,
                                                                 const MessageOutFunc& messageOutFunc) const
{
    return mMergedFb.saveBeautyNumSamplePPM(makeFilename(filePath) + "_beautyNumSample.ppm", messageOutFunc);
}

bool
ProgMcrtMergeDebugFeedbackFrame::saveBeautyNumSampleMergedAllFBD(const std::string& filePath,
                                                                 const MessageOutFunc& messageOutFunc) const
{
    return mMergedFb.saveBeautyNumSampleFBD(makeFilename(filePath) + "_beautyNumSample.fbd", messageOutFunc);
}

std::string ProgMcrtMergeDebugFeedbackFrame::makeFilename(const std::string& filePath) const
{
    std::ostringstream ostr;
    ostr << filePath << "mergeAll"
         << "_fId" << mFeedbackId;
    return ostr.str();
}

//------------------------------------------------------------------------------------------

bool ProgMcrtMergeDebugFeedback::saveBeautyPPM(const uint32_t feedbackId,
                                               const MessageOutFunc& messageOutFunc) const
{
    size_t frameId = findFrameId(feedbackId);
    if (frameId >= mFrameTbl.size()) {
        std::ostringstream ostr;
        ostr << "Could not find target frame data. feedbackId:" << feedbackId;
        if (messageOutFunc) return messageOutFunc(ostr.str());
        return false;
    }

    return mFrameTbl[frameId].saveBeautyPPM(mSavePath, messageOutFunc);
}

bool ProgMcrtMergeDebugFeedback::saveBeautyFBD(const uint32_t feedbackId,
                                               const MessageOutFunc& messageOutFunc) const
{
    size_t frameId = findFrameId(feedbackId);
    if (frameId >= mFrameTbl.size()) {
        std::ostringstream ostr;
        ostr << "Could not find target frame data. feedbackId:" << feedbackId;
        if (messageOutFunc) return messageOutFunc(ostr.str());
        return false;
    }

    return mFrameTbl[frameId].saveBeautyFBD(mSavePath, messageOutFunc);
}

bool ProgMcrtMergeDebugFeedback::saveBeautyNumSamplePPM(const uint32_t feedbackId,
                                                        const MessageOutFunc& messageOutFunc) const
{
    size_t frameId = findFrameId(feedbackId);
    if (frameId >= mFrameTbl.size()) {
        std::ostringstream ostr;
        ostr << "Could not find target frame data. feedbackId:" << feedbackId;
        if (messageOutFunc) return messageOutFunc(ostr.str());
        return false;
    }

    return mFrameTbl[frameId].saveBeautyNumSamplePPM(mSavePath, messageOutFunc);
}

bool ProgMcrtMergeDebugFeedback::saveBeautyNumSampleFBD(const uint32_t feedbackId,
                                                        const MessageOutFunc& messageOutFunc) const
{
    size_t frameId = findFrameId(feedbackId);
    if (frameId >= mFrameTbl.size()) {
        std::ostringstream ostr;
        ostr << "Could not find target frame data. feedbackId:" << feedbackId;
        if (messageOutFunc) return messageOutFunc(ostr.str());
        return false;
    }

    return mFrameTbl[frameId].saveBeautyNumSampleFBD(mSavePath, messageOutFunc);
}

std::string ProgMcrtMergeDebugFeedback::show() const
{
    std::ostringstream ostr;
    ostr << "ProgMcrtMergeDebugFeedback {\n"
         << "  mCurrId:" << mCurrId << '\n'
         << "  mFrameTbl (size:" << mFrameTbl.size() << ") {\n";
    for (unsigned i = 0; i < mFrameTbl.size(); ++i) {
        ostr << scene_rdl2::str_util::addIndent(std::string("i:") + std::to_string(i) + ' ' +
                                                mFrameTbl[i].show(), 2) + '\n';
    }
    ostr << "  }\n"
         << "}";
    return ostr.str();
}

size_t ProgMcrtMergeDebugFeedback::findFrameId(const uint32_t feedbackId) const
{
    for (size_t i = 0; i < mFrameTbl.size(); ++i) {
        if (mFrameTbl[i].getFeedbackId() == feedbackId) return i;
    }
    return mFrameTbl.size();
}

void ProgMcrtMergeDebugFeedback::parserConfigure()
{
    mParser.description("debugFeedback command");
    mParser.opt("active", "<on|off|show>", "set active switch or show current condition of debugFeedback",
                [&](Arg& arg) -> bool {
                    if (arg() == "show") arg++;
                    else mActive = (arg++).as<bool>(0);
                    return arg.fmtMsg("active %s\n", scene_rdl2::str_util::boolStr(mActive).c_str());
                });
    mParser.opt("show", "", "show all debugFeedback data",
                [&](Arg& arg) -> bool { return arg.msg(show() + '\n'); });
    mParser.opt("savePathSet", "<directory-path>", "set save data directory. should be ended by '/'",
                [&](Arg& arg) -> bool {
                    mSavePath = arg++();
                    return arg.msg(std::string("savePath:" + mSavePath + '\n'));
                });
    mParser.opt("savePathShow", "", "show current save path",
                [&](Arg& arg) -> bool {
                    if (mSavePath.empty()) return arg.msg("savePath is empty\n");
                    return arg.msg(mSavePath + '\n');
                });
    mParser.opt("saveBeautyFramePPM", "<feedbackId>", "save beauty data by PPM format",
                [&](Arg& arg) -> bool {
                    return saveBeautyPPM(arg++.as<int>(0),
                                         [&](const std::string& msg) { return arg.msg(msg + '\n'); });
                });
    mParser.opt("saveBeautyFrameFBD", "<feedbackId>", "save beauty data by FBD format",
                [&](Arg& arg) -> bool {
                    return saveBeautyFBD(arg++.as<int>(0),
                                         [&](const std::string& msg) { return arg.msg(msg + '\n'); });
                });
    mParser.opt("saveBeautyNumSampleFramePPM", "<feedbackId>", "save beautyNumSample data by PPM format",
                [&](Arg& arg) -> bool {
                    return saveBeautyNumSamplePPM(arg++.as<int>(0),
                                                  [&](const std::string& msg) { return arg.msg(msg + '\n'); });
                });
    mParser.opt("saveBeautyNumSampleFrameFBD", "<feedbackId>", "save beautyNumSample data by FBD format",
                [&](Arg& arg) -> bool {
                    return saveBeautyNumSampleFBD(arg++.as<int>(0),
                                                  [&](const std::string& msg) { return arg.msg(msg + '\n'); });
                });
}

} // namespace mcrt_computation

// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "McrtDebugFeedback.h"

namespace mcrt_computation {

std::string
McrtDebugFeedbackFrame::show() const
{
    std::ostringstream ostr;
    ostr << "McrtDebugFeedbackFrame {\n"
         << "  mFeedbackId:" << mFeedbackId << '\n'
         << "  mDecodedSendImageActionId:" << mDecodedSendImageActionId << '\n'
         << "  mLastPartialMergeTileId:" << mLastPartialMergeTileId << '\n'
         << "}";
    return ostr.str();
}

bool
McrtDebugFeedbackFrame::saveBeautyPPM(const std::string& filePath,
                                      const unsigned machineId,
                                      const MessageOutFunc& messageOutFunc) const
{
    std::string filenameHead = makeFilename(filePath, machineId) + "_beauty_";

    bool flag = true;
    if (!mFeedbackFb.saveBeautyPPM(filenameHead + "feedback.ppm", messageOutFunc)) flag = false;
    if (!mDecodedFb.saveBeautyPPM(filenameHead + "decoded.ppm", messageOutFunc)) flag = false;
    if (!mMergedFb.saveBeautyPPM(filenameHead + "merged.ppm", messageOutFunc)) flag = false;
    if (!mMinusOneFb.saveBeautyPPM(filenameHead + "minusOne.ppm", messageOutFunc)) flag = false;
    return flag;
}

bool
McrtDebugFeedbackFrame::saveBeautyFBD(const std::string& filePath,
                                      const unsigned machineId,
                                      const MessageOutFunc& messageOutFunc) const
{
    std::string filenameHead = makeFilename(filePath, machineId) + "_beauty_";

    bool flag = true;
    if (!mFeedbackFb.saveBeautyFBD(filenameHead + "feedback.fbd", messageOutFunc)) flag = false;
    if (!mDecodedFb.saveBeautyFBD(filenameHead + "decoded.fbd", messageOutFunc)) flag = false;
    if (!mMergedFb.saveBeautyFBD(filenameHead + "merged.fbd", messageOutFunc)) flag = false;
    if (!mMinusOneFb.saveBeautyFBD(filenameHead + "minusOne.fbd", messageOutFunc)) flag = false;
    return flag;
}

bool
McrtDebugFeedbackFrame::saveBeautyNumSamplePPM(const std::string& filePath,
                                               const unsigned machineId,
                                               const MessageOutFunc& messageOutFunc) const
{
    std::string filenameHead = makeFilename(filePath, machineId) + "_beautyNumSample_";

    bool flag = true;
    if (!mFeedbackFb.saveBeautyNumSamplePPM(filenameHead + "feedback.ppm", messageOutFunc)) flag = false;
    if (!mDecodedFb.saveBeautyNumSamplePPM(filenameHead + "decoded.ppm", messageOutFunc)) flag = false;
    if (!mMergedFb.saveBeautyNumSamplePPM(filenameHead + "merged.ppm", messageOutFunc)) flag = false;
    if (!mMinusOneFb.saveBeautyNumSamplePPM(filenameHead + "minusOne.ppm", messageOutFunc)) flag = false;
    return flag;
}

bool
McrtDebugFeedbackFrame::saveBeautyNumSampleFBD(const std::string& filePath,
                                               const unsigned machineId,
                                               const MessageOutFunc& messageOutFunc) const
{
    std::string filenameHead = makeFilename(filePath, machineId) + "_beautyNumSample_";

    bool flag = true;
    if (!mFeedbackFb.saveBeautyNumSampleFBD(filenameHead + "feedback.fbd", messageOutFunc)) flag = false;
    if (!mDecodedFb.saveBeautyNumSampleFBD(filenameHead + "decoded.fbd", messageOutFunc)) flag = false;
    if (!mMergedFb.saveBeautyNumSampleFBD(filenameHead + "merged.fbd", messageOutFunc)) flag = false;
    if (!mMinusOneFb.saveBeautyNumSampleFBD(filenameHead + "minusOne.fbd", messageOutFunc)) flag = false;
    return flag;
}

std::string
McrtDebugFeedbackFrame::makeFilename(const std::string& filePath, const unsigned machineId) const
{
    std::ostringstream ostr;
    ostr << filePath << "mcrt"
         << "_fId" << mFeedbackId
         << "_mId" << machineId;
    return ostr.str();
}

//------------------------------------------------------------------------------------------

bool McrtDebugFeedback::saveBeautyPPM(const uint32_t feedbackId,
                                      const MessageOutFunc& messageOutFunc) const
{
    size_t frameId = findFrameId(feedbackId);
    if (frameId >= mFrameTbl.size()) {
        std::ostringstream ostr;
        ostr << "Could not find target frame data. feedbackId:" << feedbackId;
        if (messageOutFunc) return messageOutFunc(ostr.str());
        return false;
    }

    return mFrameTbl[frameId].saveBeautyPPM(mSavePath, mMachineId, messageOutFunc);
}

bool McrtDebugFeedback::saveBeautyFBD(const uint32_t feedbackId,
                                      const MessageOutFunc& messageOutFunc) const
{
    size_t frameId = findFrameId(feedbackId);
    if (frameId >= mFrameTbl.size()) {
        std::ostringstream ostr;
        ostr << "Could not find target frame data. feedbackId:" << feedbackId;
        if (messageOutFunc) return messageOutFunc(ostr.str());
        return false;
    }

    return mFrameTbl[frameId].saveBeautyFBD(mSavePath, mMachineId, messageOutFunc);
}

bool McrtDebugFeedback::saveBeautyNumSamplePPM(const uint32_t feedbackId,
                                               const MessageOutFunc& messageOutFunc) const
{
    size_t frameId = findFrameId(feedbackId);
    if (frameId >= mFrameTbl.size()) {
        std::ostringstream ostr;
        ostr << "Could not find target frame data. feedbackId:" << feedbackId;
        if (messageOutFunc) return messageOutFunc(ostr.str());
        return false;
    }

    return mFrameTbl[frameId].saveBeautyNumSamplePPM(mSavePath, mMachineId, messageOutFunc);
}

bool McrtDebugFeedback::saveBeautyNumSampleFBD(const uint32_t feedbackId,
                                               const MessageOutFunc& messageOutFunc) const
{
    size_t frameId = findFrameId(feedbackId);
    if (frameId >= mFrameTbl.size()) {
        std::ostringstream ostr;
        ostr << "Could not find target frame data. feedbackId:" << feedbackId;
        if (messageOutFunc) return messageOutFunc(ostr.str());
        return false;
    }

    return mFrameTbl[frameId].saveBeautyNumSampleFBD(mSavePath, mMachineId, messageOutFunc);
}

std::string McrtDebugFeedback::show() const
{
    std::ostringstream ostr;
    ostr << "McrtDebugFeedback {\n"
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

size_t McrtDebugFeedback::findFrameId(const uint32_t feedbackId) const
{
    for (size_t i = 0; i < mFrameTbl.size(); ++i) {
        if (mFrameTbl[i].getFeedbackId() == feedbackId) return i;
    }
    return mFrameTbl.size();
}

void McrtDebugFeedback::parserConfigure()
{
    mParser.description("mcrt debugFeedback command");
    mParser.opt("active", "<on|off|show>", "set active switch or show current condition of mcrtDebugFeedback",
                [&](Arg& arg) -> bool {
                    if (arg() == "show") arg++;
                    else mActive = (arg++).as<bool>(0);
                    return arg.fmtMsg("active %s\n", scene_rdl2::str_util::boolStr(mActive).c_str());
                });
    mParser.opt("show", "", "show all mcrt debugFeedback data",
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

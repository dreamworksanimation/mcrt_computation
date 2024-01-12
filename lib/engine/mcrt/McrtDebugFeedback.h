// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Fb.h>
#include <scene_rdl2/common/grid_util/Parser.h>

namespace mcrt_computation {

class McrtDebugFeedbackFrame
//
// This class keeps all related internal framebuffer regarding the single progressiveFreedback message
// receive action at MCRT computation and we can save them by debugConsole command.
// This is used for debugging/verifying purposes for image feedback logic at MCRT computation.
// 
{
public:
    using Fb = scene_rdl2::grid_util::Fb;
    using MessageOutFunc = std::function<bool(const std::string& msg)>; // only for debugging purposes

    McrtDebugFeedbackFrame() = default;

    void setFeedbackId(const uint32_t feedbackId) { mFeedbackId = feedbackId; }
    uint32_t getFeedbackId() const { return mFeedbackId; }

    void setFeedbackFb(const Fb& src) { mFeedbackFb.copy(nullptr, src); }

    void setDeltaImageCacheFb(unsigned decodedSendImageActionId,
                              unsigned lastPartialMergeTileId,
                              const Fb& decodedFb, const Fb& mergedFb) {
        mDecodedSendImageActionId = decodedSendImageActionId;
        mLastPartialMergeTileId = lastPartialMergeTileId;
        mDecodedFb.copy(nullptr, decodedFb);
        mMergedFb.copy(nullptr, mergedFb);
    }

    void setMinusOneFb(const Fb& src) { mMinusOneFb.copy(nullptr, src); }

    bool saveBeautyPPM(const std::string& filePath,
                       const unsigned machineId,
                       const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyFBD(const std::string& filePath,
                       const unsigned machineId,
                       const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyNumSamplePPM(const std::string& filePath,
                                const unsigned machineId,
                                const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyNumSampleFBD(const std::string& filePath,
                                const unsigned machineId,
                                const MessageOutFunc& messageOutFunc = nullptr) const;

    std::string show() const;

private:

    std::string makeFilename(const std::string& filePath, const unsigned machineId) const;

    //------------------------------

    uint32_t mFeedbackId {~static_cast<uint32_t>(0)};
    Fb mFeedbackFb;

    unsigned mDecodedSendImageActionId {~static_cast<unsigned>(0)}; // last decoded sendImageActionId
    unsigned mLastPartialMergeTileId {~static_cast<unsigned>(0)}; // last partial merge tileId
    Fb mDecodedFb;
    Fb mMergedFb;

    Fb mMinusOneFb;
};

class McrtDebugFeedback
//
// This class is used for debugging/verifying purposes for image feedback logic at MCRT computation.
// This object keeps all feedback-related internal framebuffer regarding to the multiple
// progressiveFeedback message receive actions and we can save them by debugConsole command for
// verifying purposes.
// 
{
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser;
    using MessageOutFunc = std::function<bool(const std::string& msg)>;

    McrtDebugFeedback(const unsigned maxFrames, unsigned machineId)
        : mActive(false)
        , mMachineId(machineId)
        , mCurrId(0)
        , mFrameTbl(maxFrames)
        , mSavePath("./")
    {
        parserConfigure();
    }

    bool isActive() const { return mActive; }

    McrtDebugFeedbackFrame& getCurrFrame() { return mFrameTbl[mCurrId]; }

    void incrementId() {
        ++mCurrId;
        if (mCurrId >= mFrameTbl.size()) mCurrId = 0;
    }

    bool saveBeautyPPM(const uint32_t feedbackId,
                       const MessageOutFunc& messageOutputFunc = nullptr) const;
    bool saveBeautyFBD(const uint32_t feedbackId,
                       const MessageOutFunc& messageOutputFunc = nullptr) const;
    bool saveBeautyNumSamplePPM(const uint32_t feedbackId,
                                const MessageOutFunc& messageOutputFunc = nullptr) const;
    bool saveBeautyNumSampleFBD(const uint32_t feedbackId,
                                const MessageOutFunc& messageOutputFunc = nullptr) const;

    std::string show() const;

    Parser& getParser() { return mParser; }

private:

    size_t findFrameId(const uint32_t feedbackId) const;

    void parserConfigure();

    //------------------------------
    
    bool mActive;

    unsigned mMachineId;
    unsigned mCurrId;
    std::vector<McrtDebugFeedbackFrame> mFrameTbl; // This is vector but never change it's size

    std::string mSavePath;

    Parser mParser;
};

} // namespace mcrt_computation

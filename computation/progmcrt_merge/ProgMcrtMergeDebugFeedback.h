// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Fb.h>
#include <scene_rdl2/common/grid_util/Parser.h>

#include <string>

namespace mcrt_computation {

class ProgMcrtMergeDebugFeedbackMachine
//
// This class keeps internal framebuffer regarding the single MCRT computation of single progressiveFeedback
// send action at Merge computation and we can save it by debugConsole command.
// This is used for debugging/verifying purposes.
//
{
public:
    using Fb = scene_rdl2::grid_util::Fb;
    using MessageOutFunc = std::function<bool(const std::string& msg)>; // only for debugging purposes

    void setMachineId(const unsigned id) { mMachineId = id; }

    void set(const unsigned lastSendImageActionId,
             const unsigned lastPartialMergeTileId,
             const Fb& decodedFb);

    bool saveBeautyPPM(const std::string& filePath,
                       const uint32_t feedbackId,
                       const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyFBD(const std::string& filePath,
                       const uint32_t feedbackId,
                       const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyNumSamplePPM(const std::string& filePath,
                                const uint32_t feedbackId,
                                const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyNumSampleFBD(const std::string& filePath,
                                const uint32_t feedbackId,
                                const MessageOutFunc& messageOutFunc = nullptr) const;

    std::string show() const;

private:

    std::string makeFilename(const std::string& filePath, const uint32_t feedbackId) const;

    unsigned mMachineId {0};

    unsigned mLastSendImageActionId {0};
    unsigned mLastPartialMergeTileId {0};

    // decoded progressiveFrame data from this mcrt computation
    Fb mDecodedFb;
};

class ProgMcrtMergeDebugFeedbackFrame
//
// This class keeps all related internal framebuffer regarding the single progressiveFeedback
// message send action at Merge computation and we can save them by debugConsole command.
// This is used for debugging/verifying purposes.
//
{
public:
    using Fb = scene_rdl2::grid_util::Fb;
    using MessageOutFunc = std::function<bool(const std::string& msg)>; // only for debugging purposes

    explicit ProgMcrtMergeDebugFeedbackFrame(const unsigned numMachines)
        : mMachineTbl(numMachines)
    {
        for (unsigned i = 0; i < numMachines; ++i) mMachineTbl[i].setMachineId(i);
    }
    
    void set(const uint32_t feedbackId, const Fb& mergedFb);

    uint32_t getFeedbackId() const { return mFeedbackId; }

    ProgMcrtMergeDebugFeedbackMachine& getMachine(const unsigned machineId) { return mMachineTbl[machineId]; }

    bool saveBeautyPPM(const std::string& filePath,
                       const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyFBD(const std::string& filePath,
                       const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyNumSamplePPM(const std::string& filePath,
                                const MessageOutFunc& messageOutFunc = nullptr) const;
    bool saveBeautyNumSampleFBD(const std::string& filePath,
                                const MessageOutFunc& messageOutFunc = nullptr) const;

    std::string show() const;

private:

    bool saveBeautyMergedAllPPM(const std::string& filePath,
                                const MessageOutFunc& messageOutFunc) const;
    bool saveBeautyMergedAllFBD(const std::string& filePath,
                                const MessageOutFunc& messageOutFunc) const;
    bool saveBeautyNumSampleMergedAllPPM(const std::string& filePath,
                                         const MessageOutFunc& messageOutFunc) const;
    bool saveBeautyNumSampleMergedAllFBD(const std::string& filePath,
                                         const MessageOutFunc& messageOutFunc) const;
    std::string makeFilename(const std::string& filePath) const;

    uint32_t mFeedbackId {~static_cast<uint32_t>(0)};
    
    Fb mMergedFb;

    // This is vector but only setup by setMachineTotal() and never change during runtime
    std::vector<ProgMcrtMergeDebugFeedbackMachine> mMachineTbl; // mMachineTbl[machineId]
};

class ProgMcrtMergeDebugFeedback
//
// This class is used for debugging/verifying purposes for image feedback logic at Merge computation.
// This object keeps all feedback-related internal framebuffer regarding to the multiple
// progressiveFreedback message send actions and we can save them by debugConsole command for verifying
// purposes.
//
{
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser;
    using MessageOutFunc = std::function<bool(const std::string& msg)>; // only for debugging purposes

    ProgMcrtMergeDebugFeedback(const unsigned maxFrames, const unsigned numMachines)
        : mFrameTbl(maxFrames, ProgMcrtMergeDebugFeedbackFrame(numMachines))
    {
        parserConfigure();
    }

    bool isActive() const { return mActive; }

    ProgMcrtMergeDebugFeedbackFrame& getCurrFrame() { return mFrameTbl[mCurrId]; }

    void incrementId()
    {
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

    bool mActive {false};

    unsigned mCurrId {0};
    std::vector<ProgMcrtMergeDebugFeedbackFrame> mFrameTbl; // This is vector but never change it's size

    std::string mSavePath {"./"};

    Parser mParser;
};

} // namespace mcrt_computation

// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#pragma once

#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Parser.h>
#include <scene_rdl2/common/rec_time/RecTime.h>

#include <memory>
#include <vector>

namespace mcrt_computation {

class MessageHistoryFrame;
class MessageHistory;

class MessageHistoryMessage
{
public:
    MessageHistoryMessage(const MessageHistoryFrame* renderHistoryFrame,
                         unsigned myMessageId,
                         unsigned syncId,
                         float sec)
        : mMessageHistoryFrame(renderHistoryFrame)
        , mMyMessageId(myMessageId)
        , mSyncId(syncId)
        , mTimeStampSec(sec)
    {}

    unsigned syncId() const { return mSyncId; }
    float timeStampSec() const { return mTimeStampSec; }

    std::string show() const;

private:
    const MessageHistoryFrame* mMessageHistoryFrame;
    const unsigned mMyMessageId;
    const unsigned mSyncId;
    const float mTimeStampSec;
};

class MessageHistoryFrame
{
public:
    using MessageHistoryMessageShPtr = std::shared_ptr<MessageHistoryMessage>;

    MessageHistoryFrame(const MessageHistory* renderHistory, unsigned frameId)
        : mMessageHistory(renderHistory)
        , mFrameId(frameId)
    {}

    void set(unsigned syncId, float sec);
    const MessageHistoryMessage* get(unsigned messageId) const;
    const MessageHistoryMessage* getLast() const;
    float getLastTimeStamp() const;

    unsigned getMaxSyncId() const { return !mMessages.size() ? 0 : mMessages.back()->syncId(); }

    std::string show(bool simple) const;

private:
    const MessageHistory* mMessageHistory;

    const unsigned mFrameId;
    std::vector<MessageHistoryMessageShPtr> mMessages;
};

class MessageHistory
{
public:
    using MessageHistoryFrameShPtr = std::shared_ptr<MessageHistoryFrame>;
    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser;

    MessageHistory();

    void reset();

    void newFrame();
    void setReceiveData(unsigned syncId);

    const MessageHistoryFrame* getFrame(unsigned frameId) const;

    std::string show(bool simple) const;

    Parser& getParser() { return mParser; }

private:
    
    void parserConfigure();

    //------------------------------

    unsigned mFrameCounter;

    bool mSkip;
    std::vector<MessageHistoryFrameShPtr> mHistory;

    scene_rdl2::rec_time::RecTime mTime;

    Parser mParser;
};

} // namespace mcrt_computation


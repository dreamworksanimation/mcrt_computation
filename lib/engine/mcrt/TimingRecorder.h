// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#pragma once

#include <scene_rdl2/common/rec_time/RecTime.h>

#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace mcrt_computation {

class TimingRecorderSingleFrame
//
// mcrt computation¡Çs single frame rendering timing record, especially renderPrep and snapshot.
//
{
public:
    TimingRecorderSingleFrame(const uint64_t globalBaseFromEpoch, scene_rdl2::rec_time::RecTime& globalBase)
        : mGlobalBaseFromEpoch(globalBaseFromEpoch)
        , mGlobalBase(globalBase)
        , mTotalMsg(0)
        , mOldestMsgRecvTiming(0.0f)
        , mNewestMsgRecvTiming(0.0f)
        , mRenderPrepStartTiming(0.0f)
        , mRenderPrepEndTiming(0.0f)
        , mSendFrameCounter(0)
        , mSnapshotStartTiming(0.0f)
        , mSnapshotEndTiming(0.0f)
        , mSendTiming(0.0f)
    {}

    void setMsgTimingRange(unsigned totalMsg, float oldSec, float newSec);
    void setRenderPrepStartTiming(); // MTsafe
    void setRenderPrepEndTiming(); // MTsafe

    void newSnapshotSendAction();
    void setSnapshotStartTiming();
    void setSnapshotEndTiming();
    void setSendSnapshotTiming();

    bool is1stFrame() const { return mSendFrameCounter == 1; }

    uint64_t getGlobalBaseFromEpoch() const { return mGlobalBaseFromEpoch; }
    unsigned getTotalMsg() const { return mTotalMsg; }
    float getOldestMsgRecvTiming() const { return mOldestMsgRecvTiming; }
    float getNewestMsgRecvTiming() const { return mNewestMsgRecvTiming; }
    float getRenderPrepStartTiming() const { return mRenderPrepStartTiming; }
    float getRenderPrepEndTiming() const { return mRenderPrepEndTiming; }
    float getSnapshotStartTiming() const { return mSnapshotStartTiming; }
    float getSnapshotEndTiming() const { return mSnapshotEndTiming; }
    float getSendTiming() const { return mSendTiming; }

    std::string show() const;

private:
    const uint64_t mGlobalBaseFromEpoch;
    scene_rdl2::rec_time::RecTime& mGlobalBase;

    unsigned mTotalMsg;
    float mOldestMsgRecvTiming; // sec
    float mNewestMsgRecvTiming; // sec

    mutable std::mutex mMutex;

    float mRenderPrepStartTiming; // sec
    float mRenderPrepEndTiming; // sec

    unsigned mSendFrameCounter;
    float mSnapshotStartTiming; // sec
    float mSnapshotEndTiming; // sec
    float mSendTiming; // sec
};

class TimingRecorder
{
public:
    using TimingRecorderSingleFrameShPtr = std::shared_ptr<TimingRecorderSingleFrame>;

    TimingRecorder();

    float getSec() const { return mGlobalBase.end(); }

    TimingRecorderSingleFrameShPtr newFrame();

private:
    uint64_t mGlobalBaseFromEpoch;
    scene_rdl2::rec_time::RecTime mGlobalBase;

    TimingRecorderSingleFrameShPtr mFrame;
};

} // namespace mcrt_computation


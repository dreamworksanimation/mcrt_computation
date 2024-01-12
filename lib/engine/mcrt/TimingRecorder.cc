// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#include "TimingRecorder.h"

#include <mcrt_dataio/share/util/MiscUtil.h>
#include <scene_rdl2/render/util/StrUtil.h>

namespace mcrt_computation {

void
TimingRecorderSingleFrame::setMsgTimingRange(unsigned totalMsg, float oldSec, float newSec)
{
    mTotalMsg = totalMsg;
    mOldestMsgRecvTiming = oldSec;
    mNewestMsgRecvTiming = newSec;
}

void
TimingRecorderSingleFrame::setRenderPrepStartTiming() // MTsafe
{
    std::lock_guard<std::mutex> lock(mMutex);
    mRenderPrepStartTiming = mGlobalBase.end();
}

void
TimingRecorderSingleFrame::setRenderPrepEndTiming() // MTsafe
{
    std::lock_guard<std::mutex> lock(mMutex);
    mRenderPrepEndTiming = mGlobalBase.end();
}

void
TimingRecorderSingleFrame::newSnapshotSendAction()
{
    mSendFrameCounter++;
}

void
TimingRecorderSingleFrame::setSnapshotStartTiming()
{
    mSnapshotStartTiming = mGlobalBase.end();
}

void
TimingRecorderSingleFrame::setSnapshotEndTiming()
{
    mSnapshotEndTiming = mGlobalBase.end();
}

void
TimingRecorderSingleFrame::setSendSnapshotTiming()
{
    mSendTiming = mGlobalBase.end();
}

std::string
TimingRecorderSingleFrame::show() const
{
    using scene_rdl2::str_util::secStr;

    std::ostringstream ostr;
    ostr << "timingRecord {\n"
         << "  totalRdlMsg:" << mTotalMsg << '\n'
         << "  oldestMsgRecv:" << secStr(mOldestMsgRecvTiming) << '\n'
         << "  newestMsgRecv:" << secStr(mNewestMsgRecvTiming)
         << " delta:" << secStr(mNewestMsgRecvTiming - mNewestMsgRecvTiming) << '\n'
         << "  renderPrepStart:" << secStr(mRenderPrepStartTiming) << '\n'
         << "  renderPrepEnd:" << secStr(mRenderPrepEndTiming)
         << " delta:" << secStr(mRenderPrepEndTiming - mRenderPrepStartTiming) << '\n'
         << "  mSendFrameCounter:" << mSendFrameCounter << '\n'
         << "  SnapshotStart:" << secStr(mSnapshotStartTiming) << '\n'
         << "  SnapshotEnd:" << secStr(mSnapshotEndTiming)
         << " delta:" << secStr(mSnapshotEndTiming - mSnapshotStartTiming) << '\n'
         << "  Send:" << secStr(mSendTiming)
         << " delta-fromMsgRcv:" << secStr(mSendTiming - mOldestMsgRecvTiming) << '\n'
         << "}";
    return ostr.str();
}

//------------------------------------------------------------------------------------------

TimingRecorder::TimingRecorder()
{
    mGlobalBaseFromEpoch = mcrt_dataio::MiscUtil::getCurrentMicroSec(); // return microsec from Epoch
    mGlobalBase.start();
}

TimingRecorder::TimingRecorderSingleFrameShPtr
TimingRecorder::newFrame()
{
    mFrame = std::make_shared<TimingRecorderSingleFrame>(mGlobalBaseFromEpoch, mGlobalBase);
    return mFrame;
}

} // namespace mcrt_computation


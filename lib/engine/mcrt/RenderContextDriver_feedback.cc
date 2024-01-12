// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "RenderContextDriver.h"

#include <mcrt_dataio/share/util/BandwidthTracker.h>
#include <mcrt_dataio/share/util/FpsTracker.h>
#include <scene_rdl2/render/cache/CacheDequeue.h>

namespace mcrt_computation {

void    
RenderContextDriver::evalProgressiveFeedbackMessage(const arras4::api::Message& msg)
{
    scene_rdl2::rec_time::RecTime recTimeEvalFeedbackMessage;
    recTimeEvalFeedbackMessage.start();

    ProgressiveFeedbackConstPtr feedbackMsg = msg.contentAs<mcrt::ProgressiveFeedback>();
    if (!feedbackMsg) {
        return;           // not a progressiveFeedback message -> exit
    }

    if (mRecvFeedbackFpsTracker) {
        // fps tracker update : We want to keep 2x longer record of current feedbackInterval
        mRecvFeedbackFpsTracker->setKeepIntervalSec(mFeedbackIntervalSec * 2.0f);
        mRecvFeedbackFpsTracker->set();
    }
    if (mRecvFeedbackBandwidthTracker) {
        // bandwidth tracker update : We want to keep 2x longer record of current feedbackInterval
        mRecvFeedbackBandwidthTracker->setKeepIntervalSec(mFeedbackIntervalSec * 2.0f);
        mRecvFeedbackBandwidthTracker->set(feedbackMsg->serializedLength()); // update bandwidth tracker
    }

    mFeedbackId = feedbackMsg->mFeedbackId;

    // ===== This is best for minimum debug message for feedbackMessage receive =====
    /*
    std::cerr << ">> RenderContextDriver_feedback.cc evalProgressiveFeedbackMessage()"
              << " feedbackId:" << mFeedbackId << '\n';
    */

    if (mMcrtDebugFeedback && mMcrtDebugFeedback->isActive()) {
        mMcrtDebugFeedback->getCurrFrame().setFeedbackId(mFeedbackId);
    }

    feedbackFbViewportCheck(feedbackMsg);
    decodeFeedbackImageData(feedbackMsg);

    try {
        if (decodeMergeActionTracker(feedbackMsg)) {
            constructFeedbackMinusOne();
        } else {
            // This feedabck data does not include my sendImageActionId. So minusOne should be the same as
            // previous result
            // copyFeedbackToMinusOne(); // OLD
        }
    } catch (scene_rdl2::except::RuntimeError& e) {
        std::cerr << ">> RenderContextDriver_feedback.cc evalProgressiveFeedbackMessage()"
                  << " decodeMergeActionTracker() failed. RuntimeError:" << e.what() << '\n';
    }

    float sec = recTimeEvalFeedbackMessage.end();
    mFeedbackEvalLog.set(sec * 1000.0f); // millisec

    if (mMcrtDebugFeedback && mMcrtDebugFeedback->isActive()) {
        mMcrtDebugFeedback->incrementId();
    }
}

void
RenderContextDriver::setFeedbackActive(bool flag)
{
    mFeedbackActiveUserInput = flag;
}

void
RenderContextDriver::setFeedbackIntervalSec(float sec)
{
    mFeedbackIntervalSec = sec;
}

void
RenderContextDriver::feedbackFbViewportCheck(ProgressiveFeedbackConstPtr feedbackMsg)
{
    scene_rdl2::math::Viewport currViewport(feedbackMsg->mProgressiveFrame->getRezedViewport().minX(),
                                            feedbackMsg->mProgressiveFrame->getRezedViewport().minY(),
                                            feedbackMsg->mProgressiveFrame->getRezedViewport().maxX(),
                                            feedbackMsg->mProgressiveFrame->getRezedViewport().maxY());

    if (mFeedbackFb.getRezedViewport() != currViewport) {
        mFeedbackFb.init(currViewport);
    }
}

void    
RenderContextDriver::decodeFeedbackImageData(ProgressiveFeedbackConstPtr feedbackMsg)
{
    try {
        if (!mFeedbackUpdates.push(/* delayDecode = */ false,
                                   *(feedbackMsg->mProgressiveFrame),
                                   mFeedbackFb,
                                   /* parallel = */ false,
                                   /* skipLatencyLog = */ true)) {
            std::cerr << "RenderContextDriver_feedback.cc ERROR : evalProgressiveFeedbackMessage() failed.\n";
        }
    } catch (scene_rdl2::except::RuntimeError& e) {
        std::cerr << ">> RenderContextDriver_feedback.cc mFeedbackUpdates.push() failed. RuntimeError:" << e.what() << '\n';
    }

    if (mMcrtDebugFeedback && mMcrtDebugFeedback->isActive()) {
        mMcrtDebugFeedback->getCurrFrame().setFeedbackFb(mFeedbackFb);
    }
}

bool
RenderContextDriver::decodeMergeActionTracker(ProgressiveFeedbackConstPtr feedbackMsg)
{
    scene_rdl2::cache::CacheDequeue cacheDequeue(feedbackMsg->mMergeActionTrackerData.data(),
                                                 feedbackMsg->mMergeActionTrackerData.size());

    bool active = false;
    while (true) {
        int machineId = cacheDequeue.deqVLInt();
        if (machineId < 0) break;

        if (machineId != mMachineIdOverride) {
            mcrt_dataio::MergeActionTracker::decodeDataSkipOnMCRTComputation(cacheDequeue);
        } else {
            mFeedbackMergeActionTracker.decodeDataOnMCRTComputation(cacheDequeue);
            active = true;
            break;
        }
    }
    if (!active) {
        return false; // There is no active MergeActionTracking info for this feedback message
    }

    std::string warning;
    try {
        float latencySec = mSentImageCache.decodeMessage(mFeedbackMergeActionTracker.getData(), warning);
        if (latencySec != 0.0f) {
            mFeedbackLatency.set(latencySec * 1000.0f); // millisec
        }
    } catch (scene_rdl2::except::RuntimeError& e) {
        std::ostringstream ostr;
        ostr << ">> RenderContextDriver_feedback.cc decodeMergeActionTracker()"
             << " mSentImageCache.decodeMessage() failed. RuntimeError:" << e.what() << '\n';
        throw scene_rdl2::except::RuntimeError(ostr.str());
    }

    if (!warning.empty()) {
        std::cerr << ">> RenderContextDriver_feedback.cc " << warning << '\n';
    }

    if (mMcrtDebugFeedback && mMcrtDebugFeedback->isActive()) {
        mMcrtDebugFeedback->getCurrFrame().
            setDeltaImageCacheFb(mSentImageCache.getDecodedSendImageActionId(),
                                 mSentImageCache.getLastPartialMergeTileId(),
                                 mSentImageCache.getDecodedFb(),
                                 mSentImageCache.getMergedFb());
    }

    return true;
}

void
RenderContextDriver::constructFeedbackMinusOne()
{
    if (mFeedbackFb.getWidth() != mSentImageCache.getWidth() ||
        mFeedbackFb.getHeight() != mSentImageCache.getHeight()) {
        std::cerr << ">> RenderContextDriver_feedback.cc constructFeedbackMinusOne() failed. reso mismatch\n";
        return;                 // resolution mismatch
    }

    std::string errorMsg;
    if (!mFeedbackMinusOneFb.calcMinusOneRenderBuffer(mFeedbackFb, mSentImageCache.getMergedFb(), &errorMsg)) {
        std::cerr << errorMsg;
        return;
    }

    if (mMcrtDebugFeedback && mMcrtDebugFeedback->isActive()) {
        mMcrtDebugFeedback->getCurrFrame().setMinusOneFb(mFeedbackMinusOneFb);
    }
}

std::string
RenderContextDriver::showFeedbackStats() const
{
    using scene_rdl2::str_util::addIndent;
    using scene_rdl2::str_util::boolStr;
    using scene_rdl2::str_util::byteStr;

    auto showFeedbackEvalLog = [&]() -> std::string {
        std::ostringstream ostr;
        ostr
        << "feedbackEvalLog {\n"
        << addIndent(mFeedbackEvalLog.show()) << '\n'
        << "  average:" << mFeedbackEvalLog.getAvg() << " millisec\n"
        << "}";
        return ostr.str();
    };
    auto showFeedbackLatency = [&]() -> std::string {
        std::ostringstream ostr;
        ostr
        << "feedbackLatency {\n"
        << addIndent(mFeedbackLatency.show()) << '\n'
        << "  average:" << mFeedbackLatency.getAvg() << " millisec\n"
        << "}";
        return ostr.str();
    };
    auto showRecvFeedbackBandwidthTracker = [&]() -> std::string {
        std::ostringstream ostr;
        ostr
        << "recvFeedbackBandwidthTracker {\n"
        << addIndent(mRecvFeedbackBandwidthTracker->show()) << '\n'
        << "  bps:" << byteStr(static_cast<size_t>(mRecvFeedbackBandwidthTracker->getBps())) << "/sec\n"
        << "}";
        return ostr.str();
    };
    auto showRecvFeedbackFpsTracker = [&]() -> std::string {
        std::ostringstream ostr;
        ostr
        << "recvFeedbackFpsTracker {\n"
        << addIndent(mRecvFeedbackFpsTracker->show()) << '\n'
        << "  fps:" << mRecvFeedbackFpsTracker->getFps() << '\n'
        << "}";
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "feedback stats {\n"
         << "  mFeedbackActiveUserInput:" << boolStr(mFeedbackActiveUserInput) << '\n'
         << "  mFeedbackActiveRuntime:" << boolStr(mFeedbackActiveRuntime) << '\n'
         << "  mFeedbackIntervalSec:" << mFeedbackIntervalSec << " sec\n"
         << "  mFeedbackId:" << mFeedbackId << '\n'
         << addIndent(showFeedbackEvalLog()) << '\n'
         << addIndent(showFeedbackLatency()) << '\n'
         << addIndent(mSentImageCache.show()) << '\n';
    if (mRecvFeedbackBandwidthTracker) {
        ostr << addIndent(showRecvFeedbackBandwidthTracker()) << '\n';
    }
    if (mRecvFeedbackFpsTracker) {
        ostr << addIndent(showRecvFeedbackFpsTracker()) << '\n';
    }
    ostr << '}';

    return ostr.str();
}

} // namespace mcrt_computation

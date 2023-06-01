// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "RenderContextDriverMaster.h"
#include "RenderContextDriver.h"

#include <moonray/rendering/shading/UdimTexture.h>

#include <scene_rdl2/render/logging/logging.h>
#include <scene_rdl2/render/util/StrUtil.h>

namespace mcrt_computation {

RenderContextDriverMaster::RenderContextDriverMaster(int numMachineOverride,
                                                     int machineIdOverride,
                                                     McrtLogging* mcrtLogging,
                                                     bool* mcrtDebugLogCreditUpdateMessage,
                                                     PackTilePrecisionMode packTilePrecisionMode,
                                                     mcrt_dataio::FpsTracker* recvFeedbackFpsTracker,
                                                     mcrt_dataio::BandwidthTracker* recvFeedbackBandwidthTracker)
    : mNumMachineOverride(numMachineOverride)
    , mMachineIdOverride(machineIdOverride)
    , mMcrtLogging(mcrtLogging)
    , mMcrtDebugLogCreditUpdateMessage(mcrtDebugLogCreditUpdateMessage)
    , mPackTilePrecisionMode(packTilePrecisionMode)
    , mRecvFeedbackFpsTracker(recvFeedbackFpsTracker)
    , mRecvFeedbackBandwidthTracker(recvFeedbackBandwidthTracker)
    , mLastDriverId(-1)
{
    if (numMachineOverride > 1) {
        // If number of machines is fixed during the session (= current condition), this implementation
        // is OK to setup logging global switch and other warning related conditions for multi-machine
        // specific configuration.
        // We have to reconsider where we should set up these conditions when we will be able
        // to dynamically changing the total number of machines during one session in the future.

        // For more than 1 backend mcrt computations, we simply turn off all logging functionality in
        // order to increase the interactivity. If mcrt_computation is dumping lots of warning/error to
        // the log, this is a huge bottleneck and we can not check these log messages from the client via
        // ClientReceiverFb APIs anyway at this moment.
        // We should consider how to check backend computations error/warning from the client under
        // a multi-machine context.
        scene_rdl2::logging::LogEventRegistry<scene_rdl2::rdl2::Shader>::setLoggingGlobalSwitch(false);

        // Udim missing texture warning is also a huge bottleneck of multi-machine interactivity.
        // We turn off warning messages here as well.
        moonray::shading::UdimTexture::setUdimMissingTextureWarningSwitch(false);
    }
}

RenderContextDriverMaster::~RenderContextDriverMaster()
{
}

int
RenderContextDriverMaster::addDriver(const moonray::rndr::RenderOptions *renderOptionsPtr,
                                     std::atomic<bool> *renderPrepCancel,
                                     const PostMainCallBack &postMainCallBack,
                                     const StartFrameCallBack &startFrameCallBack,
                                     const StopFrameCallBack &stopFrameCallBack)
{
    RenderContextDriverOptions options(postMainCallBack, startFrameCallBack, stopFrameCallBack);
    options.driverId = ++mLastDriverId;
    options.renderOptions = renderOptionsPtr;
    options.numMachineOverride = mNumMachineOverride;
    options.machineIdOverride = mMachineIdOverride;
    options.mcrtLogging = mMcrtLogging;
    options.mcrtDebugLogCreditUpdateMessage = mMcrtDebugLogCreditUpdateMessage;
    options.precisionMode = mPackTilePrecisionMode;
    options.renderPrepCancel = renderPrepCancel;
    options.recvFeedbackFpsTracker = mRecvFeedbackFpsTracker;
    options.recvFeedbackBandwidthTracker = mRecvFeedbackBandwidthTracker;

    /*
    RenderContextDriverUqPtr ptr(new RenderContextDriver(++mLastDriverId,
                                                         renderOptionsPtr,
                                                         mNumMachineOverride,
                                                         mMachineIdOverride,
                                                         mMcrtLogging,
                                                         mMcrtDebugLogCreditUpdateMessage,
                                                         mPackTilePrecisionMode,
                                                         renderPrepCancel,
                                                         postMainCallBack,
                                                         startFrameCallBack,
                                                         stopFrameCallBack,
                                                         mRecvFeedbackFpsTracker,
                                                         mRecvFeedbackBandwidthTracker));
    */
    RenderContextDriverUqPtr ptr(new RenderContextDriver(options));
    mArray.push_back(std::move(ptr));
    return mArray.back()->getDriverId();
}

bool
RenderContextDriverMaster::rmDriver(const int driverId)
{
    int arrayId = findArrayId(driverId);
    if (arrayId < 0) return false;

    if (arrayId >= 0) {
        mArray.erase(mArray.begin() + arrayId);
    }
    return true;
}

RenderContextDriver *
RenderContextDriverMaster::getDriver(const int driverId)
{
    int arrayId = findArrayId(driverId);
    if (arrayId < 0) return nullptr;
    return mArray[arrayId].get(); // return raw pointer
}

std::string
RenderContextDriverMaster::show() const
{
    auto showArrayItem = [&](const size_t id) -> std::string {
        return "i:" + std::to_string(id) + ' ' + mArray[id]->show();
    };

    std::ostringstream ostr;
    ostr << "RenderContextDriverMaster {\n"
         << "  mLastDriverId:" << mLastDriverId << '\n';
    ostr << "  mArray (size:" << mArray.size() << ") {\n";
    for (size_t i = 0; i < mArray.size(); ++i) {
        ostr << scene_rdl2::str_util::addIndent(showArrayItem(i), 2) << '\n';
    }
    ostr << "  }\n";
    ostr << "}";
    return ostr.str();
}

//------------------------------------------------------------------------------------------

int    
RenderContextDriverMaster::findArrayId(const int driverId)
//
// return mArray's index when found driver which has driverId
// otherwise return -1
//
{
    for (size_t i = 0; i < mArray.size(); ++i) {
        if (mArray[i]->getDriverId() == driverId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace mcrt_computation

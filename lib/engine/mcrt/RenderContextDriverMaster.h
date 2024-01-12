// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "RenderContextDriver.h"

#include <memory>
#include <vector>

namespace moonray {
namespace rndr {
    class RenderOptions;
}
}

namespace arras4 {
namespace api {
    class Address;
}    
}

namespace mcrt_computation {

class McrtLogging;
class RenderContextDriver;

class RenderContextDriverMaster
//
// This class maintains multiple RenderContextDriver. However, at this moment we are using a single
// RenderContextDriver configuration only. Support multiple renderContextDriver is still in the pretty
// early stage of test level and needs more work. I would like to keep this class as is in order to
// quickly start testing for the next level of multi renderContextDriver support.
//
{
public:
    using PackTilePrecisionMode = RenderContextDriver::PackTilePrecisionMode;
    using PostMainCallBack = std::function<void()>;
    using ProgressiveFrameSendCallBack =
        std::function<void(mcrt::ProgressiveFrame::Ptr msg, const std::string &source)>;
    using RenderContextDriverUqPtr = std::unique_ptr<RenderContextDriver>;
    using StartFrameCallBack = std::function<void(const bool reloadScn, const std::string &source)>;
    using StopFrameCallBack = std::function<void(const std::string &soruce)>;

    // Non-copyable
    RenderContextDriverMaster &operator = (const RenderContextDriverMaster) = delete;
    RenderContextDriverMaster(const RenderContextDriverMaster &) = delete;

    RenderContextDriverMaster(int numMachineOverride,
                              int machineIdOverride,
                              mcrt_dataio::SysUsage& sysUsage,
                              mcrt_dataio::BandwidthTracker& sendBandwidthTracker,
                              McrtLogging *mcrtLogging = nullptr,
                              bool* mcrtDebugLogCreditUpdateMessage = nullptr,
                              PackTilePrecisionMode precisionMode = PackTilePrecisionMode::AUTO16,
                              mcrt_dataio::FpsTracker* recvFeedbackFpsTracker = nullptr,
                              mcrt_dataio::BandwidthTracker* recvFeedbackBandwidthTracker = nullptr);
    ~RenderContextDriverMaster(); // destroy all drivers

    // return newly created driver's driverId
    int addDriver(const moonray::rndr::RenderOptions* renderOptionsPtr = nullptr,
                  std::atomic<bool>* renderPrepCancel = nullptr,
                  const float* fps = nullptr,
                  const PostMainCallBack& postMainCallBack = nullptr,
                  const StartFrameCallBack& startFrameCallBack = nullptr,
                  const StopFrameCallBack& stopFrameCallBack = nullptr,
                  const ProgressiveFrameSendCallBack& sendInfoOnlyCallBack = nullptr);

    bool rmDriver(const int driverId); // invalid driverId returns false. otherwise true

    // return renderContextDriver. return nullptr when driverId is invalid
    RenderContextDriver *getDriver(const int driverId);

    std::string show() const;

private:
    int findArrayId(const int driverId); // return mArray's index

    //------------------------------

    int mNumMachineOverride;
    int mMachineIdOverride;
    mcrt_dataio::SysUsage& mSysUsage;
    mcrt_dataio::BandwidthTracker& mSendBandwidthTracker;
    McrtLogging *mMcrtLogging;
    bool* mMcrtDebugLogCreditUpdateMessage;
    PackTilePrecisionMode mPackTilePrecisionMode;
    mcrt_dataio::FpsTracker* mRecvFeedbackFpsTracker;
    mcrt_dataio::BandwidthTracker* mRecvFeedbackBandwidthTracker;

    int mLastDriverId {-1};
    std::vector<RenderContextDriverUqPtr> mArray;
};

} // namespace mcrt_computation

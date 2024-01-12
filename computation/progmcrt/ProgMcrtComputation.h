// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mcrt_computation/engine/mcrt/RenderContextDriverMaster.h>
#include <mcrt_computation/engine/mcrt/TimingRecorder.h>

#include <mcrt_computation/common/rec_load/RecLoad.h>
#include <mcrt_computation/common/mcrt_logging/McrtLogging.h>
#include <computation_api/Computation.h>
#include <mcrt_dataio/share/util/BandwidthTracker.h>
#include <mcrt_dataio/share/util/FpsTracker.h>
#include <mcrt_dataio/share/util/SysUsage.h>
#include <mcrt_messages/BaseFrame.h>
#include <mcrt_messages/GenericMessage.h>
#include <moonray/rendering/rndr/RenderOptions.h>

#include <atomic>
#include <string>
#include <vector>

namespace mcrt_computation {

class ProgMcrtComputation : public arras4::api::Computation
{
public:
    using PackTilePrecisionMode = RenderContextDriver::PackTilePrecisionMode;
    using Parser = scene_rdl2::grid_util::Parser;
    using Arg = scene_rdl2::grid_util::Arg;

    explicit ProgMcrtComputation(arras4::api::ComputationEnvironment* env);
    virtual ~ProgMcrtComputation();

    virtual arras4::api::Result configure(const std::string& op, arras4::api::ObjectConstRef config);
    virtual void onIdle();
    virtual arras4::api::Result onMessage(const arras4::api::Message& message);
    virtual arras4::api::Object property(const std::string& name);

protected:
    void onStart();
    void onStop();

private: 
    void handleGenericMessage(mcrt::GenericMessage::ConstPtr jm);
    void onJSONMessage(const arras4::api::Message& msg);
    void onCreditUpdate(const arras4::api::Message& msg);

    void parserConfigureGenericMessage();

    void sendProgressMessage(const std::string& stage, const std::string& event, const std::string& source);
    void sendProgressMessageStageShading(const mcrt::BaseFrame::Status& status,
                                         const float progress,
                                         const std::string& source);

    //------------------------------

    std::unique_ptr<RenderContextDriverMaster> mRenderContextDriverMaster {nullptr};

    moonray::rndr::RenderOptions mOptions;

    PackTilePrecisionMode mPackTilePrecisionMode {PackTilePrecisionMode::AUTO16};

    std::atomic<bool> mRenderPrepCancel {false};

    float mFps {12.0f};

    bool mDispatchGatesFrame {false}; // Flag to indicate if images are gated upstream    

    // Stores "num machines" and "machine id" SceneVariable overrides
    // to be applied once the RenderContext is set up.
    int mNumMachinesOverride {-1};
    int mMachineIdOverride {-1};

    rec_load::RecLoad *mRecLoad {nullptr}; // for performance analysis : CPU load logger

    //------------------------------
    //
    // statistical information
    //
    mcrt_dataio::SysUsage mSysUsage;
    mcrt_dataio::BandwidthTracker mSendBandwidthTracker {2.0f};         // sec
    mcrt_dataio::FpsTracker mRecvFeedbackFpsTracker {2.0f};             // sec (dynamically changed @ runtime)
    mcrt_dataio::BandwidthTracker mRecvFeedbackBandwidthTracker {2.0f}; // sec (dynamically changed @ runtime)

    TimingRecorder mTimingRecorder;

    //------------------------------

    int mInitialCredit {-1};
    int mCredit {-1};              // limits sending of outgoing messages. <0 disables.
    McrtLogging mLogging;

    bool mLogDebug_creditUpdateMessage {false};

    std::string mName;             // computation.name
    arras4::api::Address mAddress; // computation.address

    Parser mParserGenericMessage;
};

} // namespace mcrt_computation

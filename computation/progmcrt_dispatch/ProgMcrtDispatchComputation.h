// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

//
// This directive is only enable for debugging purpose.
// This debug console mode will open telnet port and it's not appropriate for final promote binary.
//
#define DEBUG_CONSOLE_MODE
#ifdef DEBUG_CONSOLE_MODE
#include "ProgMcrtDispatchComputationDebugConsole.h"
#endif  // end DEBUG_CONSOLE_MODE

#include <computation_api/Computation.h>
#include <mcrt_messages/GenericMessage.h>
#include <mcrt_messages/GeometryData.h>
#include <mcrt_messages/JSONMessage.h>
#include <mcrt_messages/RDLMessage.h>
#include <message_api/messageapi_names.h>
#include <mcrt_computation/common/mcrt_logging/McrtLogging.h>

#include <string>

namespace mcrt_computation {

class ProgMcrtDispatchUpdate
{
public:
    ProgMcrtDispatchUpdate(const arras4::api::Message &msg) :
        message(msg.contentAs<mcrt::RDLMessage>()),
        source(msg.get(arras4::api::MessageData::sourceId))
    {}

    mcrt::RDLMessage::ConstPtr message;
    arras4::api::Object source;
};

class JsonUpdate {
public:
    JsonUpdate(const arras4::api::Message &msg) :
        message(msg.contentAs<mcrt::JSONMessage>()),
        source(msg.get(arras4::api::MessageData::sourceId))
    {}

    mcrt::JSONMessage::ConstPtr message;
    arras4::api::Object source;
};

class ProgMcrtDispatchComputation : public arras4::api::Computation
{
public:
    ProgMcrtDispatchComputation(arras4::api::ComputationEnvironment *env);
    virtual ~ProgMcrtDispatchComputation() {};

    virtual arras4::api::Result configure(const std::string &op, arras4::api::ObjectConstRef config);
    virtual void onIdle();
    virtual arras4::api::Result onMessage(const arras4::api::Message &message);

    friend class ProgMcrtDispatchComputationDebugConsole;

private:
    void runtimeVerifySyncId(int syncId);
    void runtimeVerifyJsonSyncId(int syncId);
    void handleGenericMessage(mcrt::GenericMessage::ConstPtr gm);
    void parseDebugCommand(std::vector<std::string> &args);

    //------------------------------
    
    float mFps;
    bool mContinuous;
    std::string mMcrtOutput;    // command line

    mcrt::GeometryData::ConstPtr mGeometryUpdate;
    std::vector<ProgMcrtDispatchUpdate> mRdlUpdates;
    std::vector<JsonUpdate> mJsonUpdates;
    bool mReceivedGeometryUpdate;
    bool mReceivedRdlUpdate;
    bool mReceivedJsonUpdate;
  
    double mLastTime;
    int mLastSyncId;            // received last syncId for verify
    int mLastJsonSyncId;        // received last JSON syncId for verify
    int mJsonSyncId;            // local syncId for JSON

#   ifdef DEBUG_CONSOLE_MODE
    ProgMcrtDispatchComputationDebugConsole mDebugConsole;
#   endif  // end DEBUG_CONSOLE_MODE

    McrtLogging mLogging;
};

} // namespace mcrt_computation


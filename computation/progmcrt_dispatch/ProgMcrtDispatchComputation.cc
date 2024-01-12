// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "ProgMcrtDispatchComputation.h"

#include <mcrt_dataio/share/util/ClockDelta.h>
#include <mcrt_messages/GenericMessage.h>
#include <mcrt_messages/GeometryData.h>
#include <mcrt_messages/RDLMessage.h>
#include <mcrt_messages/RenderMessages.h>
#include <message_api/Message.h>
#include <scene_rdl2/common/platform/Platform.h> // for scene_rdl2::util::getSeconds()

#include <cstdlib>
#include <iostream>
#include <string>

// This directive activates debug console feature and dispatch computation can accept telnet connection
//#define DEBUG_CONSOLE

namespace {
    // Configuration constants
    const std::string sConfigFps = "fps";
    const std::string sConfigContinuous = "continuous";
} // namespace

namespace mcrt_computation {

COMPUTATION_CREATOR(ProgMcrtDispatchComputation);

ProgMcrtDispatchComputation::ProgMcrtDispatchComputation(arras4::api::ComputationEnvironment *env) : 
    Computation(env),
    mFps(12.0f),
    mContinuous(false),
    mReceivedGeometryUpdate(false),
    mReceivedRdlUpdate(false),
    mReceivedJsonUpdate(false),
    mLastTime(0.0),
    mLastSyncId(-1),
    mLastJsonSyncId(-1),
    mJsonSyncId(-1)
{
    mRdlUpdates.reserve(10);
}

arras4::api::Result
ProgMcrtDispatchComputation::configure(const std::string &op,
                                       arras4::api::ObjectConstRef &aConfig)
{
    if (op == "start") {
        return arras4::api::Result::Success;
    } else if (op == "stop") {
        return arras4::api::Result::Success;
    } else if (op != "initialize") {
        return arras4::api::Result::Unknown; 
    }      

    if (aConfig[sConfigFps].isNumeric()) {
        mFps = aConfig[sConfigFps].asFloat();
    }

    if (aConfig[sConfigContinuous].isBool()) {
        mContinuous = aConfig[sConfigContinuous].asBool();
    }

#   ifdef DEBUG_CONSOLE_MODE
    mDebugConsole.open(20000, this); // port number is hardcoded
#   endif // end DEBUG_CONSOLE_MODE

    // This notifies dispatch computation's hostname to the merge computation.
    mDebugConsole.sendDispatchHostNameToMerge(); // This does not require debugConsole open.

    return arras4::api::Result::Success;
}

void
ProgMcrtDispatchComputation::onIdle()
{
#   ifdef DEBUG_CONSOLE_MODE
    mDebugConsole.eval();
#   endif // end DEBUG_CONSOLE_MODE

    // Is it time to kick out a frame yet?
    double now = scene_rdl2::util::getSeconds();
    if (now - mLastTime < (1.0f / mFps)) {
        return;
    }

    if (mContinuous) {
        // otherwise, request a snapshot (if we haven't received any other updates)
        if (!(mReceivedGeometryUpdate || mReceivedRdlUpdate)) {
            mcrt::GenericMessage::Ptr snapshotMsg(new mcrt::GenericMessage);
            snapshotMsg->mValue = "snapshot";
            send(snapshotMsg);
        }
    }

    // Forward the most geometry update, if received.
    if (mReceivedGeometryUpdate) {
        runtimeVerifySyncId(mGeometryUpdate->mFrame);

        // mGeometry should have proper syncId
        send(mGeometryUpdate);

        mReceivedGeometryUpdate = false;
        mGeometryUpdate.reset();
    }

    if(mReceivedRdlUpdate) {
        for (auto iter = mRdlUpdates.begin(); iter != mRdlUpdates.end(); ++iter) {
            const ProgMcrtDispatchUpdate& update = *iter;
            runtimeVerifySyncId(update.message->mSyncId);
            send(update.message, arras4::api::withSource(update.source));
        }
        mRdlUpdates.clear();
        mReceivedRdlUpdate = false;
    }

    if (mReceivedJsonUpdate) {
        for (auto iter = mJsonUpdates.begin(); iter != mJsonUpdates.end(); ++iter) {
            const JsonUpdate &update = *iter;
            if (update.message->messageId() == mcrt::RenderMessages::PICK_MESSAGE_ID ||
                update.message->messageId() == mcrt::RenderMessages::INVALIDATE_RESOURCES_ID) {
                // Technically we can add "syncId" regarding these JSON messages easily.
                // At this moment, only PICK_MESSAGE_ID is using syncId.
                // SyncId is not using by INVALIDATE_RESOURCES_ID but harmless.
                if (update.message->messagePayload()["syncId"].empty()) {
                    // We don't have syncId data inside received JSON message.
                    // We should add syncId based on the local JSON syncId counter
                    mcrt::JSONMessage::Ptr copied(new mcrt::JSONMessage(*(update.message)));
                    copied->messagePayload()["syncId"] = ++mJsonSyncId; // pre-increment is intentional
                    send(copied, arras4::api::withSource(update.source));
                } else {
                    // We have syncId data inside received JSON message.
                    // We expects this syncId is properly updated by client.
                    runtimeVerifyJsonSyncId(update.message->messagePayload()["syncId"].asInt());
                    send(update.message, arras4::api::withSource(update.source));
                }

            } else {
                // We can not add syncId information as one of the member of JSON::Value
                send(update.message, arras4::api::withSource(update.source));
            }
        }
        mJsonUpdates.clear();
        mReceivedJsonUpdate = false;
    }

    mLastTime = now;
}

arras4::api::Result
ProgMcrtDispatchComputation::onMessage(const arras4::api::Message &msg)
{
    if (msg.classId() == mcrt::GenericMessage::ID) {
        mcrt::GenericMessage::ConstPtr gm = msg.contentAs<mcrt::GenericMessage>();
        if (gm) handleGenericMessage(gm);
    } else if (msg.classId() == mcrt::GeometryData::ID) {
        mGeometryUpdate = msg.contentAs<mcrt::GeometryData>();
        mReceivedGeometryUpdate = true;
    } else if (msg.classId() == mcrt::RDLMessage::ID) {
        mRdlUpdates.push_back(ProgMcrtDispatchUpdate(msg));
        mReceivedRdlUpdate = true;
    } else if (msg.classId() == mcrt::JSONMessage::ID) {
        mJsonUpdates.push_back(JsonUpdate(msg));
        mReceivedJsonUpdate = true;
    } else {
        return arras4::api::Result::Unknown;
    }
    return arras4::api::Result::Success;
}

void
ProgMcrtDispatchComputation::runtimeVerifySyncId(int syncId)
{
    auto verify = [&](int syncId) -> bool {
        if (syncId == -1 || // received default value syncId
            (mLastSyncId != -1 && mLastSyncId >= syncId)) { // no or wrong update of syncId
            return false; // verify failed
        }
        mLastSyncId = syncId;
        return true;
    };

    if (!verify(syncId)) {
        std::ostringstream ostr;
        ostr << "RUNTIME-VERIFY : received syncId was not updated properly on GeometryData/Rdl message. "
             << " syncId:" << syncId << " mLastSyncId:" << mLastSyncId;
        ARRAS_LOG_ERROR(ostr.str());
    }
}

void
ProgMcrtDispatchComputation::runtimeVerifyJsonSyncId(int syncId)
{
    auto verify = [&](int syncId) -> bool {
        if (syncId == -1 || // received default value syncId
            (mLastJsonSyncId != -1 && mLastJsonSyncId >= syncId)) { // no or wrong update of syncId
            return false; // verify failed
        }
        mLastJsonSyncId = syncId;
        return true;
    };

    if (!verify(syncId)) {
        ARRAS_LOG_ERROR("RUNTIME-VERIFY : received JSON syncId was not updated properly on JSON message");
    }
}

void
ProgMcrtDispatchComputation::handleGenericMessage(mcrt::GenericMessage::ConstPtr msg)
{
    std::istringstream istr(msg->mValue);
    std::string token;

    istr >> token;
    if (token == "cmd") {
        //
        // interactive debug command
        //
        int nodeId;
        istr >> nodeId;
        if (nodeId == -3) { // dispatch
            std::vector<std::string> args;
            for (unsigned i = 0; ; ++i) {
                istr >> token;
                if (!istr) break;
                args.emplace_back(token);
            }
            parseDebugCommand(args);
        }
    }
}

void
ProgMcrtDispatchComputation::parseDebugCommand(std::vector<std::string> &args)
{
    // Using std::cerr is on purpose.
    
    if (args[0] == "-") {
        std::cerr << "usage\n"
                  << "  clockDeltaClient <mergeHostName> <port> <path> : clockDelta client run"
                  << std::endl;
        return;
    }

    if (args[0] == "clockDeltaClient") {
        const std::string &mergeHostName = args[1];
        int port = std::stoi(args[2]);
        const std::string &path = args[3];

        /* useful debug dump
        std::cerr << ">> ProgMcrtDispatchComputation.cc"
                  << " mergeHostName:" << mergeHostName
                  << " port:" << port
                  << " path:" << path << std::endl;
        */

        if (!mcrt_dataio::ClockDelta::clientMain
            (mergeHostName, port, path, mcrt_dataio::ClockDelta::NodeType::DISPATCH)) {
            ARRAS_LOG_ERROR("cloclDelta::clientMain() failed");
        }
    }
}

} // namespace mcrt_computation


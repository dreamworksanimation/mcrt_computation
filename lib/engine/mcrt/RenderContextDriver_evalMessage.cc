// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#include "RenderContextDriver.h"

#include <arras4_log/Logger.h> // ARRAS_LOG_INFO
#include <mcrt_messages/JSONUtils.h>
#include <moonray/rendering/rndr/RenderContext.h>

//#define DEBUG_MSG_EVAL

namespace mcrt_computation {

void
RenderContextDriver::evalInvalidateResources(const arras4::api::Message &msg)
{
    mcrt::JSONMessage::ConstPtr jMsg = msg.contentAs<mcrt::JSONMessage>();
    if (!jMsg) {
        return;                 // not a JSON message -> exit
    }
    if (jMsg->messageId() != mcrt::RenderMessages::INVALIDATE_RESOURCES_ID) {
        return;
    }

#   ifdef DEBUG_MSG_EVAL
    std::cerr << ">> RenderContextDriver_evalMessage.cc evalInvalidateResources()\n";
#   endif // end DEBUG_MSG_EVAL

    ARRAS_LOG_INFO("Invalidating resources");
    std::vector<std::string> resourcesToInvalidate =
        mcrt::ValueVector(jMsg->messagePayload()[mcrt::RenderMessages::INVALIDATE_RESOURCES_PAYLOAD_LIST]);

    bool invalidateAll = false;
    for (const std::string &currResource : resourcesToInvalidate) {
        if (currResource == "*") {
            invalidateAll = true;
            break;
        }
    }

    setSource(msg.get(arras4::api::MessageData::sourceId));

    bool restartRender = stopFrame();
    if (invalidateAll) {
        mRenderContext->invalidateAllTextureResources();
    } else {
        mRenderContext->invalidateTextureResources(resourcesToInvalidate);
    }
    if (restartRender) {
        startFrame();
    }
}

void    
RenderContextDriver::evalOutputRatesMessage(const arras4::api::Message &msg)
{
    mcrt::JSONMessage::ConstPtr jMsg = msg.contentAs<mcrt::JSONMessage>();
    if (!jMsg) {
        return;                 // not a JSON message -> exit
    }
    if (jMsg->messageId() != mcrt::OutputRates::SET_OUTPUT_RATES_ID) {
        return;
    }

#   ifdef DEBUG_MSG_EVAL
    std::cerr << ">> RenderContextDriver_evalMessage.cc evalOutputRatesMessage()\n";
#   endif // end DEBUG_MSG_EVAL

    setSource(msg.get(arras4::api::MessageData::sourceId));

    mOutputRates.setFromMessage(jMsg);
    mOutputRatesFrameCount = 0;
}

void
RenderContextDriver::evalPickMessage(const arras4::api::Message &msg, EvalPickSendMsgCallBack sendCallBack)
{
    mcrt::JSONMessage::ConstPtr jMsg = msg.contentAs<mcrt::JSONMessage>();
    if (!jMsg) {
        return;                 // not a JSON message -> exit
    }
    if (jMsg->messageId() != mcrt::RenderMessages::PICK_MESSAGE_ID) {
        return;
    }

#   ifdef DEBUG_MSG_EVAL
    std::cerr << ">> RenderContextDriver_evalMessage.cc evalPickMessage()\n";
#   endif // end DEBUG_MSG_EVAL

    setSource(msg.get(arras4::api::MessageData::sourceId));

    // Handle Picking
    auto &payload = jMsg->messagePayload();
    int x = payload[mcrt::RenderMessages::PICK_MESSAGE_PAYLOAD_PIXEL][Json::Value::ArrayIndex(0)].asInt();
    int y = payload[mcrt::RenderMessages::PICK_MESSAGE_PAYLOAD_PIXEL][Json::Value::ArrayIndex(1)].asInt();
    int syncId = -1;
    if (!payload["syncId"].empty()) {
        syncId = payload["syncId"].asInt(); // We expect syncId is properly set up by upstream computation
    }

    mcrt::RenderMessages::PickMode mode =
        static_cast<mcrt::RenderMessages::PickMode>
        (payload[mcrt::RenderMessages::PICK_MESSAGE_PAYLOAD_MODE].asInt());

    /* useful debug message
    std::cerr << ">> RenderContextDriverUpdate.cc evalPickMessage()"
              << " x:" << x << " y:" << y << " mode:" << (int)mode << " clientData:" << jMsg->mClientData
              << " syncId:" << syncId << '\n';
    */

    ARRAS_LOG_INFO("ClientID: %s  (x: %d, y: %d) mode: %d",
                   jMsg->mClientData.c_str(), x, y, static_cast<int>(mode));

    mcrt::JSONMessage::Ptr resultMsg = mcrt::RenderMessages::createPickDataMessage(x, y, jMsg->mClientData);
    handlePick(static_cast<uint32_t>(syncId), mode, x, y, resultMsg);

    sendCallBack(resultMsg);
}

void
RenderContextDriver::evalMultiMachineGlobalProgressUpdate(unsigned currSyncId, float fraction)
{
#   ifdef DEBUG_MSG_EVAL
    std::cerr << ">> RenderContextDriver_evalMessage.cc evalMultiMachineGlobalProgressUpdate()\n";
#   endif // end DEBUG_MSG_EVAL

    if (currSyncId == mSyncId) {
        if (mRenderContext) {
            mRenderContext->setMultiMachineGlobalProgressFraction(fraction);
        }
    }
}

void
RenderContextDriver::evalRenderCompleteMultiMachine(unsigned currSyncId)
{
#   ifdef DEBUG_MSG_EVAL
    std::cerr << ">> RenderContextDriver_evalMessage.cc evalRenderCompleteMultiMachine()\n";
#   endif // end DEBUG_MSG_EVAL

    if (currSyncId == mSyncId) {
        if (mRenderContext->isFrameRendering() && !mRenderContext->isFrameComplete()) {
            // Set request stopAtPassBoundary if we are rendering
            mRenderContext->requestStopRenderAtPassBoundary();
            ARRAS_LOG_INFO("RenderComplete sequence start : requested stop_at_pass_boundary");
        }
    }
}

//------------------------------------------------------------------------------------------

void
RenderContextDriver::handlePick(const uint32_t syncId,
                                const mcrt::RenderMessages::PickMode mode,
                                const int x, const int y,
                                mcrt::JSONMessage::Ptr &result)
{
    // We need syncId information for merge node computation under multi-machine context and
    // also the client wants to get syncId.
    result->messagePayload()["syncId"] = syncId;

    switch (mode) {
    case mcrt::RenderMessages::PickMode::QUERY_LIGHT_CONTRIBUTIONS: {
        moonray::shading::LightContribArray rdlLights;
        mRenderContext->handlePickLightContributions(x, y, rdlLights);

        Json::Value lights;
        Json::Value contributions;
        // Loop through the lights and populate the json
        // values
        for (uint i = 0; i < rdlLights.size(); ++i) {
            lights.append(rdlLights[i].first->getName());
            contributions.append(rdlLights[i].second);
        }

        result->messagePayload()[mcrt::RenderMessages::PICK_DATA_MESSAGE_PAYLOAD_LIGHTS] = lights;
        result->messagePayload()[mcrt::RenderMessages::PICK_DATA_MESSAGE_PAYLOAD_LIGHT_CONTRIBUTIONS] =
            contributions;
    } break;
    case mcrt::RenderMessages::PickMode::QUERY_GEOMETRY: {
        const scene_rdl2::rdl2::Geometry* geometry = mRenderContext->handlePickGeometry(x, y);

        Json::Value geom = geometry ? geometry->getName() : "";
        result->messagePayload()[mcrt::RenderMessages::PICK_DATA_MESSAGE_PAYLOAD_GEOMETRY] = geom;
    } break;
    case mcrt::RenderMessages::PickMode::QUERY_GEOMETRY_PART: {
        std::string parts;
        const scene_rdl2::rdl2::Geometry* geometry = mRenderContext->handlePickGeometryPart(x, y, parts);

        Json::Value part = parts;
        Json::Value geom = geometry ? geometry->getName() : "";
        result->messagePayload()[mcrt::RenderMessages::PICK_DATA_MESSAGE_PAYLOAD_GEOMETRY_PARTS] = part;
        result->messagePayload()[mcrt::RenderMessages::PICK_DATA_MESSAGE_PAYLOAD_GEOMETRY] = geom;
    } break;
    case mcrt::RenderMessages::PickMode::QUERY_POSITION_AND_NORMAL:
        break;
    case mcrt::RenderMessages::PickMode::QUERY_CELL_INSPECTOR:
        break;
    case mcrt::RenderMessages::PickMode::QUERY_MATERIAL: {
        const scene_rdl2::rdl2::Material* materials = mRenderContext->handlePickMaterial(x, y);

        Json::Value material = materials ? materials->getName() : "";
        result->messagePayload()[mcrt::RenderMessages::PICK_DATA_MESSAGE_PAYLOAD_MATERIALS] = material;
    } break;
    default:
        break;
    }
}

} // namespace mcrt_computation


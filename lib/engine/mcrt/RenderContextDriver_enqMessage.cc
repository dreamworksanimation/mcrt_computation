// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "RenderContextDriver.h"

#include <arras4_log/Logger.h> // ARRAS_LOG_INFO
#include <mcrt_computation/common/mcrt_logging/McrtLogging.h>
#include <mcrt_messages/RDLMessage.h>
#include <mcrt_messages/ViewportMessage.h>

//#define DEBUG_MSG_ENQ
//#define DEBUG_MSG_PROCESS

namespace mcrt_computation {

void
RenderContextDriver::enqGeometryMessage(const arras4::api::Message &msg)
{
#   ifdef DEBUG_MSG_ENQ
    std::cerr << ">> RenderContextDriver_enqMessage.cc enqGeometryMessage()\n";
#   endif // end DEBUG_MSG_ENQ

    mGeometryUpdate = msg.contentAs<mcrt::GeometryData>();
}

void
RenderContextDriver::enqRdlMessage(const arras4::api::Message &msg,
                                   const float recvTimingSec)
{
    mcrt::RDLMessage::ConstPtr rdlMsg = msg.contentAs<mcrt::RDLMessage>();
    if (!rdlMsg) {
        return;                 // not a rdl message -> exit
    }

    mMessageHistory.setReceiveData(rdlMsg->mSyncId);

#   ifdef DEBUG_MSG_ENQ
    std::cerr << ">> RenderContextDriver_enqMessage.cc enqRdlMessage() syncId:" << rdlMsg->mSyncId << '\n';
#   endif // end DEBUG_MSG_ENQ

    McrtUpdateShPtr shPtr =
        std::make_shared<McrtUpdate>(msg,
                                     std::bind(&RenderContextDriver::processRdlMessage,
                                               this,
                                               std::placeholders::_1,
                                               std::placeholders::_2),
                                     [&](const MessageContentConstPtr &msg) -> McrtUpdate::MsgType {
                                         mcrt::RDLMessage::ConstPtr rdlMsg =
                                             std::static_pointer_cast<const mcrt::RDLMessage>(msg);
                                         if (!rdlMsg->mForceReload) return McrtUpdate::MsgType::RDL;
                                         return McrtUpdate::MsgType::RDL_FORCE_RELOAD;
                                     },
                                     recvTimingSec);

    // If we're forcing a reload, clear the message queue and only use this message
    if (rdlMsg->mForceReload) {
        mMcrtUpdates.clear();
    }
    // Finally, add the message. If it's a normal delta update message, queue it normally
    mMcrtUpdates.push_back(shPtr);
}

void    
RenderContextDriver::enqRenderControlMessage(const arras4::api::Message &msg,
                                             const float recvTimingSec)
{
    mcrt::JSONMessage::ConstPtr jMsg = msg.contentAs<mcrt::JSONMessage>();
    if (!jMsg) {
        return;                 // not a JSON message -> exit
    }
    if (jMsg->messageId() != mcrt::RenderMessages::RENDER_CONTROL_ID) {
        return;
    }

    const std::string operation =
        jMsg->messagePayload()[mcrt::RenderMessages::RENDER_CONTROL_PAYLOAD_OPERATION].asString();

    if (operation == mcrt::RenderMessages::RENDER_CONTROL_PAYLOAD_OPERATION_START) {
#       ifdef DEBUG_MSG_ENQ
        std::cerr << ">> RenderContextDriver_enqMessage.cc enqRenderControlMessage() START\n";
#       endif // end DEBUG_MSG_ENQ

        ARRAS_LOG_INFO("Msg-> Start Rendering!");
        mMcrtUpdates.push_back
            (std::make_shared<McrtUpdate>(msg,
                                          std::bind(&RenderContextDriver::processRenderControlStartMessage,
                                                    this,
                                                    std::placeholders::_1,
                                                    std::placeholders::_2),
                                          [&](const MessageContentConstPtr &) -> McrtUpdate::MsgType {
                                              return McrtUpdate::MsgType::RENDER_START;
                                          },
                                          recvTimingSec));

    } else if (operation == mcrt::RenderMessages::RENDER_CONTROL_PAYLOAD_OPERATION_STOP) {
#       ifdef DEBUG_MSG_ENQ
        std::cerr << ">> RenderContextDriver_enqMessage.cc enqRenderControlMessage() STOP\n";
#       endif // end DEBUG_MSG_ENQ

        setSource(msg.get(arras4::api::MessageData::sourceId));

        ARRAS_LOG_INFO("Msg-> Stop Rendering");
        //
        // We should not process render-stop messages by queueing logic. Render-stop messages
        // are special and we want to stop rendering immediately. We don't need to wait for
        // message execution by onIdle().
        //
        if (mRenderPrepCancel) {
            // STOP renderMessages only enables renderPrep cancel request flag.
            // This flag will reset false after finish mRenderContext->startFrame()
            (*mRenderPrepCancel) = true;
            {
                // propagate renderPrep cancel condition to the downstream computation
                std::lock_guard<std::mutex> lock(mMutexMcrtNodeInfoMapItem);
                mMcrtNodeInfoMapItem.getMcrtNodeInfo().setRenderPrepCancel((*mRenderPrepCancel));
            }
        }
        stopFrame();
    }
}

void
RenderContextDriver::enqRenderSetupMessage(const arras4::api::Message &msg,
                                           const float recvTimingSec)
{
    // probably we don't need this API anymore. Toshi (Aug/24/2021)

    mcrt::JSONMessage::ConstPtr jMsg = msg.contentAs<mcrt::JSONMessage>();
    if (!jMsg) {
        return;                 // not a JSON message -> exit
    }
    if (jMsg->messageId() != mcrt::RenderMessages::RENDER_SETUP_ID) {
        return;
    }

    // HACK! Verify that this message came with the right KEY.  This ensures that messages from the Client
    // aren't delivered before messages from upstream computations.  This is a hack until we have proper
    // intent-message routing rules:  @see http://jira.anim.dreamworks.com/browse/NOVADEV-985

    // KEY used to indicate that a RenderSetupMessage originated from an upstream computation, and not a client
    // Used to get around the lack of message intents: http://jira.anim.dreamworks.com/browse/NOVADEV-985
    const std::string RENDER_SETUP_KEY = "776CD313-6D4B-40A4-82D2-C61F2FD055A9";

    if (jMsg->mClientData != RENDER_SETUP_KEY) {
        ARRAS_LOG_DEBUG("Ignoring non-client Render Setup Message");
        return; // ignore message as it originated from the client
    }

#   ifdef DEBUG_MSG_ENQ
    std::cerr << ">> RenderContextDriver_enqMessage.cc enqRenderSetupMessage()\n";
#   endif // end DEBUG_MSG_ENQ

    // Reset this so we'll force the updates to applied even if we haven't sent out a frame since our last
    // Render Setup Message
    mRenderCounter = 0;

    // Clean up the queue since we do not want to process further updates
    mMcrtUpdates.clear();

    // Push it to the queue
    mMcrtUpdates.push_back
        (std::make_shared<McrtUpdate>(msg,
                                      std::bind(&RenderContextDriver::processRenderSetupMessage,
                                                this,
                                                std::placeholders::_1,
                                                std::placeholders::_2),
                                      [&](const MessageContentConstPtr &) -> McrtUpdate::MsgType {
                                          return McrtUpdate::MsgType::UNKNOWN;
                                      },
                                      recvTimingSec));
}

void
RenderContextDriver::enqROIResetMessage(const arras4::api::Message &msg,
                                        const float recvTimingSec)
{
    mcrt::JSONMessage::ConstPtr jMsg = msg.contentAs<mcrt::JSONMessage>();
    if (!jMsg) {
        return;                 // not a JSON message -> exit
    }
    if (jMsg->messageId() != mcrt::RenderMessages::SET_ROI_STATUS_OPERATION_ID) {
        return;
    }

    // Check if we're disabling the ROI
    auto &payload = jMsg->messagePayload();
    if (payload[Json::Value::ArrayIndex(0)].asBool()) {
        // Just skip set ROI=on condition. In order to do this, we expected to use enqSetROIMessage()
        return;
    }

#   ifdef DEBUG_MSG_ENQ
    std::cerr << ">> RenderContextDriver_enqMessage.cc enqROIResetMessage()\n";
#   endif // end DEBUG_MSG_ENQ

    ARRAS_LOG_INFO("enqueue reset ROI viewport");
    mMcrtUpdates.push_back
        (std::make_shared<McrtUpdate>(msg,
                                      std::bind(&RenderContextDriver::processROIMessage,
                                                this, 
                                                std::placeholders::_1,
                                                std::placeholders::_2),
                                      [&](const MessageContentConstPtr &) -> McrtUpdate::MsgType {
                                          return McrtUpdate::MsgType::ROI_DISABLE;
                                      },
                                      recvTimingSec));
}

void
RenderContextDriver::enqROISetMessage(const arras4::api::Message &msg,
                                      const float recvTimingSec)
{
    mcrt::JSONMessage::ConstPtr jMsg = msg.contentAs<mcrt::JSONMessage>();
    if (!jMsg) {
        return;                 // not a JSON message -> exit
    }
    if (jMsg->messageId() != mcrt::RenderMessages::SET_ROI_OPERATION_ID) {
        return;
    }

#   ifdef DEBUG_MSG_ENQ
    std::cerr << ">> RenderContextDriver_enqMessage.cc enqROISetMessage()\n";
#   endif // end DEBUG_MSG_ENQ
    {
        auto &payload = jMsg->messagePayload();
        scene_rdl2::math::Viewport viewport =
            scene_rdl2::math::Viewport(payload[Json::Value::ArrayIndex(0)].asInt(),
                                       payload[Json::Value::ArrayIndex(1)].asInt(),
                                       payload[Json::Value::ArrayIndex(2)].asInt(),
                                       payload[Json::Value::ArrayIndex(3)].asInt());

        ARRAS_LOG_INFO("enqueue new ROI viewport : (%d, %d, %d, %d) (%d x %d)",
                       viewport.mMinX, viewport.mMinY, viewport.mMaxX, viewport.mMaxY,
                       viewport.width(), viewport.height());

        /* useful debug message
        std::cerr << ">> RenderContextDriverUpdate.cc enqROISetMessag()"
                  << " (" << viewport.mMinX << ", "
                  << viewport.mMinY << ", "
                  << viewport.mMaxX << ", "
                  << viewport.mMaxY << ") ("
                  << viewport.width() << " x "
                  << viewport.height() << ")\n";
        */
    }

    mMcrtUpdates.push_back
        (std::make_shared<McrtUpdate>(msg,
                                      std::bind(&RenderContextDriver::processROIMessage,
                                                this, 
                                                std::placeholders::_1,
                                                std::placeholders::_2),
                                      [&](const MessageContentConstPtr &) -> McrtUpdate::MsgType {
                                          return McrtUpdate::MsgType::ROI_SET;
                                      },
                                      recvTimingSec));
}

void
RenderContextDriver::enqViewportMessage(const arras4::api::Message &msg,
                                        const float recvTimingSec)
{
    mcrt::ViewportMessage::ConstPtr vpMsg = msg.contentAs<mcrt::ViewportMessage>();
    if (!vpMsg) {
        return;                 // not a viewport message -> exit
    }

#   ifdef DEBUG_MSG_ENQ
    std::cerr << ">> RenderContextDriver_enqMessage.cc enqViewportMessage()\n";
#   endif // end DEBUG_MSG_ENQ

    mMcrtUpdates.push_back
        (std::make_shared<McrtUpdate>(msg,
                                      std::bind(&RenderContextDriver::processViewportMessage,
                                                this,
                                                std::placeholders::_1,
                                                std::placeholders::_2),
                                      [&](const MessageContentConstPtr &) -> McrtUpdate::MsgType {
                                          return McrtUpdate::MsgType::VIEWPORT;
                                      },
                                      recvTimingSec));
}

//------------------------------------------------------------------------------------------

void
RenderContextDriver::processRdlMessage(const MessageContentConstPtr &msg,
                                       arras4::api::ObjectConstRef src)
{
    moonray::rndr::RenderContext *renderContext = getRenderContext();
    scene_rdl2::rdl2::SceneContext *sceneContextBackup = getSceneContextBackup();

    mcrt::RDLMessage::ConstPtr rdlMsg = std::static_pointer_cast<const mcrt::RDLMessage>(msg);

#   ifdef DEBUG_MSG_PROCESS
    std::cerr << ">> RenderContextDriver_enqMessage.cc processRdlMessage() syncId:" << rdlMsg->mSyncId << '\n';
#   endif // end DEBUG_MSG_PROCESS

    setSource(src);
    mSyncId = rdlMsg->mSyncId;

    if (renderContext) {
        // reset global progress for multi-machine configuration
        renderContext->setMultiMachineGlobalProgressFraction(0.0f);
    }

    // If we haven't begun rendering yet (mRenderContext is NULL),
    // or we need to restart rendering from scratch, do so here
    std::string oldCancelInfo;
    moonray::rndr::RenderContext::MsgHandler oldMsgHandler;
    if (rdlMsg->mForceReload || !renderContext) {
        // This is a debugging purpose.
        // We have to set the same cancelCodePos/msgHandler to the reconstructed renderContext.
        oldMsgHandler = renderContext->getExecTrackerMsgHandlerCallBack();                
        oldCancelInfo = renderContext->execTrackerCancelInfoEncode();
        
        // We have to reconstruct fresh renderContext and sceneContextBackup
        renderContext = resetRenderContext(); // reconstruct renderContext
        sceneContextBackup = resetSceneContextBackup();
        mReloadingScene = true;
    }

    MNRY_ASSERT(renderContext && "Cannot apply a scene update without a RenderContext!");
    renderContext->updateScene(rdlMsg->mManifest, rdlMsg->mPayload);
    updateSceneContextBackup(sceneContextBackup, rdlMsg->mManifest, rdlMsg->mPayload);

    scene_rdl2::rdl2::SceneVariables &sceneVars = renderContext->getSceneContext().getSceneVariables();

    // Check if the resolution changed and re init the buffers
    scene_rdl2::math::HalfOpenViewport currentViewport = sceneVars.getRezedRegionWindow();

    if (mViewport != currentViewport && renderContext->isInitialized()) {
        ARRAS_LOG_INFO("MCRT: Detected change in resolution");
        initializeBuffers();
    }

    if (!renderContext->isInitialized()) {
        std::stringstream initMessages;
        renderContext->initialize(initMessages); // renderLayer (mLayer) is updated here
        applyConfigOverrides(); // Just in case, we do applyConfigOverrides() just after init renderContext
        initializeBuffers();

        if (!oldCancelInfo.empty()) {
            // restore old information for debug
            renderContext->setExecTrackerMsgHandlerCallBack(oldMsgHandler);
            renderContext->execTrackerCancelInfoDecode(oldCancelInfo);
        }
    }

    // Check whether debug/info logging should be enabled
    {
        const bool infoLogging = sceneVars.get(scene_rdl2::rdl2::SceneVariables::sInfoKey);
        const bool debugLogging = sceneVars.get(scene_rdl2::rdl2::SceneVariables::sDebugKey);

        bool update = false;
        if (infoLogging != mMcrtLoggingInfo) {
            mMcrtLoggingInfo = infoLogging;
            update = true;
        }
        if (debugLogging != mMcrtLoggingDebug) {
            mMcrtLoggingDebug = debugLogging;
            update = true;
        }

        if (update) {
            updateLoggingMode();
        }
    }
}

void
RenderContextDriver::processRenderSetupMessage(const MessageContentConstPtr &msg,
                                               arras4::api::ObjectConstRef src)
{
#   ifdef DEBUG_MSG_PROCESS
    std::cerr << ">> RenderContextDriver_enqMessage.cc processRenderSetupMessage()\n";
#   endif // end DEBUG_MSG_PROCESS

    ARRAS_LOG_DEBUG("Processing Render Setup Message...");
    setSource(src);

    // Initialize the render context
    resetRenderContext();
    mReloadingScene = true;
    ARRAS_LOG_DEBUG("Done processing");
}

void
RenderContextDriver::processROIMessage(const MessageContentConstPtr &msg,
                                       arras4::api::ObjectConstRef src)
{
    mcrt::JSONMessage::ConstPtr jMsg = std::static_pointer_cast<const mcrt::JSONMessage>(msg);
    const std::string messageID = jMsg->messageId();

    scene_rdl2::rdl2::SceneVariables &sceneVars = mRenderContext->getSceneContext().getSceneVariables();

    // Set a new region of interest (ROI) 
    if (messageID == mcrt::RenderMessages::SET_ROI_OPERATION_ID) {
        scene_rdl2::math::HalfOpenViewport currViewport;
        bool currRoiCondition = sceneVars.getSubViewport(currViewport);
        
        auto &payload = jMsg->messagePayload();
        scene_rdl2::math::HalfOpenViewport newViewport =
            scene_rdl2::math::HalfOpenViewport(payload[Json::Value::ArrayIndex(0)].asInt(),
                                               payload[Json::Value::ArrayIndex(1)].asInt(),
                                               payload[Json::Value::ArrayIndex(2)].asInt(),
                                               payload[Json::Value::ArrayIndex(3)].asInt());

        if (!currRoiCondition || currViewport != newViewport) {
#           ifdef DEBUG_MSG_PROCESS
            std::cerr << ">> RenderContextDriver_enqMessage.cc processROIMessage() enable ROI\n";
#           endif // end DEBUG_MSG_PROCESS

            // We only update ROI information if needed
            ARRAS_LOG_INFO("Setting ROI viewport to (%d, %d, %d, %d) (%d x %d)",
                           newViewport.mMinX, newViewport.mMinY, newViewport.mMaxX, newViewport.mMaxY,
                           newViewport.width(), newViewport.height());
            {
                scene_rdl2::rdl2::SceneVariables::UpdateGuard guard(&sceneVars);
                std::vector<int> newViewportVector =
                    {newViewport.mMinX, newViewport.mMinY, newViewport.mMaxX, newViewport.mMaxY};
                sceneVars.set(scene_rdl2::rdl2::SceneVariables::sSubViewport, newViewportVector);
            }
            mFbSender.setRoiViewport(newViewport);
            // for multi-machine configuration, ROI buffers are handled in the merge computation properly

            setSource(src);

            /* useful debug message
            std::cerr << ">> RenderContextDriverUpdate.cc processROIMessage() SET_ROI_OPERATION_ID"
                      << " (" << newViewport.mMinX << ", "
                      << newViewport.mMinY << ", "
                      << newViewport.mMaxX << ", "
                      << newViewport.mMaxY << ") ("
                      << newViewport.width() << " x "
                      << newViewport.height() << ")\n";
            */
        }

    // Turn off the region of interest (ROI)
    } else if (messageID == mcrt::RenderMessages::SET_ROI_STATUS_OPERATION_ID) {
#       ifdef DEBUG_MSG_PROCESS
        std::cerr << ">> RenderContextDriver_enqMessage.cc processROIMessage() set Disable ROI\n";
#       endif // end DEBUG_MSG_PROCESS

        ARRAS_LOG_INFO("Disable ROI viewport");
        sceneVars.disableSubViewport();
        mFbSender.resetRoiViewport();
        setSource(src);
    }
}

void
RenderContextDriver::processViewportMessage(const MessageContentConstPtr &msg,
                                            arras4::api::ObjectConstRef src)
{
#   ifdef DEBUG_MSG_PROCESS
    std::cerr << ">> RenderContextDriver_enqMessage.cc processViewportMessage()\n";
#   endif // end DEBUG_MSG_PROCESS

    mcrt::ViewportMessage::ConstPtr vpMsg = std::static_pointer_cast<const mcrt::ViewportMessage>(msg);
    setSource(src);

    if (!mRenderContext) {
        resetRenderContext();
    }
 
    // Ideally, the Viewport message should have set 4 corner positions of the viewport but we only set image
    // width and height here. This means the Viewport message should have (0,0) as left down corner position.

    scene_rdl2::rdl2::SceneVariables &sceneVars = mRenderContext->getSceneContext().getSceneVariables();

    int currImageWidth = sceneVars.get(scene_rdl2::rdl2::SceneVariables::sImageWidth);
    int currImageHeight = sceneVars.get(scene_rdl2::rdl2::SceneVariables::sImageHeight);
    if (currImageWidth != vpMsg->width() || currImageHeight != vpMsg->height()) {
        scene_rdl2::rdl2::SceneVariables::UpdateGuard guard(&sceneVars);
        
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sImageWidth, vpMsg->width());
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sImageHeight, vpMsg->height());

#       ifdef DEBUG_MSG_PROCESS
        std::cerr << ">> RenderContextDriverUpdate.cc processViewportMessage"
                  << " width:" << vpMsg->width()
                  << " height:" << vpMsg->height() << '\n';
#       endif // end DEBUG_MSG_PROCESS

        initializeBuffers();
    }
}

void
RenderContextDriver::processRenderControlStartMessage(const MessageContentConstPtr &/*msg*/,
                                                      arras4::api::ObjectConstRef src)
//
// We don't need anything inside this function. Only update source information.
// Because, in any case, renderPrep will be started after finish all mMcrtUpdates items are processed.
//    
{
#   ifdef DEBUG_MSG_PROCESS
    std::cerr << ">> RenderContextDriver_enqMessage.cc processRenderControlStartMessage()\n";
#   endif // end DEBUG_MSG_PROCESS

    setSource(src);

    ARRAS_LOG_DEBUG("processRenderControlStartMessage() Starting frame...");
}

//------------------------------------------------------------------------------------------

void
RenderContextDriver::initializeBuffers()
{
    scene_rdl2::rdl2::SceneVariables &sceneVars = mRenderContext->getSceneContext().getSceneVariables();
    scene_rdl2::math::HalfOpenViewport viewport = sceneVars.getRezedRegionWindow();
    mViewport = viewport;

    //------------------------------

    mFbSender.init(viewport.width(), viewport.height()); // we have to set viewport size

    // RenderContext::hasPixelInfoBuffer() potentially returns false
    // if called prior to RenderContext::startFrame(), so we'll check our options to see
    // if we should have a pixel info buffer.
    if (mRenderOptions.getGeneratePixelInfo()) {
        mFbSender.initPixelInfo(true);
    } else {
        mFbSender.initPixelInfo(false);
    }

    // RenderOutput
    //    RenderOutput buffers are initialized in initializeRenderOutputs()
    //    They depend on scene data and are not known until after
    //    mRenderContext->startFrame() is called.
}

void
RenderContextDriver::setSource(arras4::api::ObjectConstRef src)
{
    if (src.isString()) {
        arras4::api::UUID uuid(src.asString());
        if (!uuid.isNull())
            mSource = uuid.toString();
    }
}

void
RenderContextDriver::updateLoggingMode()
{
    if (!mMcrtLogging) return;

    if (mMcrtLoggingDebug) {
        // debug logging includes info logging
        mMcrtLogging->enableDebug(true);
        mMcrtLogging->enableInfo(true);

    } else if (mMcrtLoggingInfo) {
        mMcrtLogging->enableDebug(false);
        mMcrtLogging->enableInfo(true);

    } else {
        mMcrtLogging->enableDebug(false);
        mMcrtLogging->enableInfo(false);
    }
}

} // namespace mcrt_computation

// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <message_api/messageapi_types.h>
#include <message_api/Message.h>

#include <message_impl/Envelope.h>
#include <message_impl/PeerMessageEndpoint.h>

#include <shared_impl/MessageDispatcher.h>
#include <shared_impl/MessageHandler.h>
#include <message_impl/PeerMessageEndpoint.h>

#include <mcrt_messages/RDLMessage.h>
#include <mcrt_messages/RenderedFrame.h>
#include <mcrt_messages/JSONMessage.h>
#include <mcrt_messages/RenderMessages.h>

#include <network/IPCSocketPeer.h>

#include <scene_rdl2/scene/rdl2/BinaryWriter.h>
#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <scene_rdl2/scene/rdl2/Utils.h>

#include <iostream>
#include <unistd.h>
#include <thread>

using namespace arras4::network;
using namespace arras4::api;
using namespace arras4::impl;
using namespace mcrt;

Peer* createClientPeer(const std::string& addr)
{ 
    std::cout << "Connecting to " << addr << std::endl;
    IPCSocketPeer *peer = new IPCSocketPeer();
    peer->connect(addr);
    std::cout << "Connected" << std::endl;
    return peer;
}

class ClientMessageHandler : public MessageHandler
{
public:
    ClientMessageHandler() {}
    void handleMessage(const Message& message);
    void onIdle() {}
};

std::string statusString(BaseFrame::Status code)
{
    switch (code) {
    case BaseFrame::STARTED: return "Started";
    case BaseFrame::RENDERING: return "Rendering";
    case BaseFrame::FINISHED: return "Finished";
    case BaseFrame::CANCELLED: return "Cancelled";
    case BaseFrame::ERROR: return "Error";
    default: return "Unknown Status";
    }
}

void ClientMessageHandler::handleMessage(const Message& msg)
{
     if (msg.classId() == RenderedFrame::ID) {
        RenderedFrame::ConstPtr frameMsg = msg.contentAs<RenderedFrame>();
        int progressPercent = frameMsg->getProgress() * 100;
        std::string status = statusString(frameMsg->getStatus());
        std::cout << "Frame " << frameMsg->getWidth() << "x" <<  frameMsg->getHeight()
                  << "    " << progressPercent << "% " << status << std::endl;
     } else if (msg.classId() == JSONMessage::ID) {
         JSONMessage::ConstPtr jsonMsg = msg.contentAs<JSONMessage>();
         if (jsonMsg->messageId() == RenderMessages::LOGGING_MESSAGE_ID) {
             int level = jsonMsg->messagePayload()[RenderMessages::LOGGING_MESSAGE_PAYLOAD_LEVEL].asInt();
             std::string text = jsonMsg->messagePayload()[RenderMessages::LOGGING_MESSAGE_PAYLOAD_STRING].asString();
             std::cout << "log[" << level << "]: " << text << std::endl;
         } else {
             std::cout << "JSON Message : " << jsonMsg->messageName() << std::endl;
         }
     } else {
         std::cout << "Message: " << msg.describe() << std::endl;
     }
}

Envelope makeRdlMessage(const std::string& filePath)
{
    scene_rdl2::rdl2::SceneContext* sc = new scene_rdl2::rdl2::SceneContext();
    scene_rdl2::rdl2::readSceneFromFile(filePath,*sc);
    RDLMessage* content = new RDLMessage();
    scene_rdl2::rdl2::BinaryWriter w(*sc);
    w.toBytes(content->mManifest,content->mPayload);
    return Envelope(content);
}
    
int usage() 
{
    std::cout << "Args: ipcaddr rdlafile" << std::endl;
    return -1;
}

int main(int argc, char** argv)
{
     if (argc < 3)
         return usage();
     Peer* peer = createClientPeer(argv[1]);
     ClientMessageHandler handler;
     std::shared_ptr<MessageEndpoint> endpoint = std::make_shared<PeerMessageEndpoint>(*peer,true, "none none");
     MessageDispatcher disp("client",
                            handler);
     disp.startQueueing(endpoint);
     ExecutionLimits limits;
     disp.startDispatching(limits);
     Envelope rdl = makeRdlMessage(argv[2]);
     disp.send(rdl);
     disp.waitForExit();
     return 0;
     
}

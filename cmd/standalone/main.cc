// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <standalone/StandaloneEnvironment.h>

#include <message_api/messageapi_types.h>
#include <message_api/Message.h>

#include <computation_api/standard_names.h>
#include <computation_api/Computation.h>

#include <mcrt_messages/RDLMessage.h>
#include <mcrt_messages/RenderedFrame.h>
#include <mcrt_messages/JSONMessage.h>
#include <mcrt_messages/RenderMessages.h>

#include <scene_rdl2/scene/rdl2/BinaryWriter.h>
#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <scene_rdl2/scene/rdl2/Utils.h>

#include <iostream>
#include <chrono>
#include <thread>

constexpr const char* NAME = "mcrt";
constexpr const char* DSO_NAME = "libmcrt_computation_progmcrt.so";
constexpr unsigned MAX_MEMORY_MB = 16384;
constexpr unsigned MAX_THREADS = 10;
constexpr float FPS = 1.0;

using namespace arras4::api;
using namespace arras4::impl;
using namespace mcrt;

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

void handleMessage(const Message& message)
{
    if (message.classId() == RenderedFrame::ID) {
        RenderedFrame::ConstPtr frameMsg = message.contentAs<RenderedFrame>();
        int progressPercent = static_cast<int>(frameMsg->getProgress() * 100);
        std::string status = statusString(frameMsg->getStatus());
        std::cout << "Frame " << frameMsg->getWidth() << "x" <<  frameMsg->getHeight()
                  << "    " << progressPercent << "% " << status << std::endl;
    } else if (message.classId() == JSONMessage::ID) {
        JSONMessage::ConstPtr jsonMsg = message.contentAs<JSONMessage>();
        if (jsonMsg->messageId() == RenderMessages::LOGGING_MESSAGE_ID) {
            int level = jsonMsg->messagePayload()[RenderMessages::LOGGING_MESSAGE_PAYLOAD_LEVEL].asInt();
            std::string text = jsonMsg->messagePayload()[RenderMessages::LOGGING_MESSAGE_PAYLOAD_STRING].asString();
            std::cout << "log[" << level << "]: " << text << std::endl;
        } else {
            std::cout << "JSON Message : " << jsonMsg->messageName() << std::endl;
        }
    } else {
        std::cout << "Message: " << message.describe() << std::endl;
    }
}

int usage() 
{
    std::cout << "Args: rdlafile" << std::endl;
    return -1;
}

int main(int argc, char** argv)
{
     if (argc < 2)
         return usage();

     std::string rdlaPath(argv[1]);

     std::cout << "Loading dso " << DSO_NAME << std::endl;
     StandaloneEnvironment env(NAME,
                               DSO_NAME,
                               handleMessage);

     std::cout << "Initializing computation" << std::endl;
     Object config;
     config[ConfigNames::maxMemoryMB] = MAX_MEMORY_MB;
     config[ConfigNames::maxThreads] = MAX_THREADS;
     config["fps"] = FPS;
     config["enablePackTile"] = false;
     if (env.initializeComputation(config) == Result::Invalid) {
         std::cerr << "ERROR: Initialization failed" << std::endl;
         return -2;
     }

     std::cout << "Sending scene: " << rdlaPath << std::endl;
       
     scene_rdl2::rdl2::SceneContext* sc = new scene_rdl2::rdl2::SceneContext();
     scene_rdl2::rdl2::readSceneFromFile(rdlaPath,*sc);
     RDLMessage* content = new RDLMessage();
     scene_rdl2::rdl2::BinaryWriter w(*sc);
     w.toBytes(content->mManifest,content->mPayload);
     MessageContentConstPtr cp(content);
     env.sendMessage(cp);

     std::cout << "Entering idle loop" << std::endl;
     while (true) {
         env.performIdle();
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
     }
}

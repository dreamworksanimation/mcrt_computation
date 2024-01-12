// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#pragma once

#include <message_api/Message.h>
#include <message_api/messageapi_names.h>
#include <message_api/Object.h>

namespace mcrt_computation {

class McrtUpdate
//
// All enqueued messages at onMessage phase are stored into RenderContextDriver and processed later
// by applyUpdatesAndRestartRender(). This object is a container of each message that is used
// in order to store different messages into a single unified vector.
//
{
public:
    enum class MsgType : int {
        UNKNOWN = 0x0,
        RDL,
        RDL_FORCE_RELOAD,
        RENDER_START,
        ROI_SET,
        ROI_DISABLE,
        VIEWPORT
    };

    using MessageContentConstPtr = arras4::api::MessageContentConstPtr;
    using UpdateFunc =
        std::function<void(const MessageContentConstPtr &, arras4::api::ObjectConstRef)>;
    using MsgTypeFunc = std::function<MsgType(const MessageContentConstPtr &)>;
    
    McrtUpdate(const arras4::api::Message &msg,
               UpdateFunc updateFunc,
               MsgTypeFunc msgTypeFunc,
               float msgRecvTimingSec)
        : mActive(true)
        , mMessage(msg.content())
        , mSource(msg.get(arras4::api::MessageData::sourceId))
        , mUpdateFunc(updateFunc)
        , mMsgTypeFunc(msgTypeFunc)
        , mMsgRecvTiming(msgRecvTimingSec)
    {}
    
    void disable() { mActive = false; }
    bool isActive() { return mActive; }

    void operator()() { if (mActive) mUpdateFunc(mMessage, mSource); }
        
    MsgType msgType() const { return mMsgTypeFunc(mMessage); }

    static std::string msgTypeStr(const MsgType &msgType);

    float getMsgRecvTiming() const { return mMsgRecvTiming; }

private:
    bool mActive;

    MessageContentConstPtr mMessage;
    arras4::api::Object mSource;
    UpdateFunc mUpdateFunc;  
    MsgTypeFunc mMsgTypeFunc;

    float mMsgRecvTiming; // sec
};

} // namespace mcrt_computation


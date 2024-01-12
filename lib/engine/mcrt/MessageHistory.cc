// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#include "MessageHistory.h"

#include <scene_rdl2/render/util/StrUtil.h>

#include <iomanip>
#include <iostream>
#include <sstream>

namespace mcrt_computation {

std::string
MessageHistoryMessage::show() const
{
    namespace str_util = scene_rdl2::str_util;

    int w = str_util::getNumberOfDigits(mMessageHistoryFrame->getMaxSyncId());

    const MessageHistoryMessage* prevMessage =
        (mMyMessageId == 0) ? nullptr : mMessageHistoryFrame->get(mMyMessageId - 1);

    std::ostringstream ostr;
    ostr << "mSyncId:" << std::setw(w) << mSyncId
         << " mTimeStampSec:" << str_util::secStr(mTimeStampSec);
    if (prevMessage) {
        ostr << " delta:" << str_util::secStr(mTimeStampSec - prevMessage->mTimeStampSec);
    }
    return ostr.str();
}

//------------------------------------------------------------------------------------------

void
MessageHistoryFrame::set(unsigned syncId, float sec)
{
    mMessages.push_back(std::make_shared<MessageHistoryMessage>(this, mMessages.size(), syncId, sec));
}

const MessageHistoryMessage*
MessageHistoryFrame::get(unsigned messageId) const
{
    if (messageId >= mMessages.size()) return nullptr;
    return mMessages[messageId].get();
}

const MessageHistoryMessage*
MessageHistoryFrame::getLast() const
{
    return !mMessages.size() ? nullptr : get(static_cast<unsigned>(mMessages.size()) - 1);
}

float
MessageHistoryFrame::getLastTimeStamp() const
{
    return !getLast() ? 0.0f : getLast()->timeStampSec();
}

std::string
MessageHistoryFrame::show(bool simple) const
{
    namespace str_util = scene_rdl2::str_util;

    int w = str_util::getNumberOfDigits(static_cast<unsigned>(mMessages.size()));

    auto showMessage = [&](size_t i) -> std::string {
        std::ostringstream ostr;
        ostr << "i:" << std::setw(w) << i << ' ' << mMessages[i]->show();
        return ostr.str();
    };

    auto getIntervalSecFromPrevFrame = [&]() -> float {
        if (!mFrameId) return 0.0f; // This is initial frame and no previous frame
        if (!mMessages.size()) return 0.0f; // no message data for current frame yet

        const MessageHistoryFrame* prevFrame = mMessageHistory->getFrame(mFrameId - 1);
        float prevFrameLastTimeStamp = prevFrame->getLastTimeStamp();
        float currFrameTopTimeStamp = mMessages[0]->timeStampSec();
        std::cerr << ">> MessageHistory.cc mFrameId:" << mFrameId
        << " prevFrameLastTimeStamp:" << prevFrameLastTimeStamp
        << " currFrameTopTimeStamp:" << currFrameTopTimeStamp << '\n';
        return currFrameTopTimeStamp - prevFrameLastTimeStamp;
    };

    auto showRange = [&]() -> std::string {
        unsigned startSyncId = 0;
        unsigned endSyncId = 0;
        float startTime = 0.0f;
        float endTime = 0.0f;
        if (!mMessages.empty()) {
            startSyncId = mMessages.front()->syncId();
            endSyncId = mMessages.back()->syncId();
            startTime = mMessages.front()->timeStampSec();
            endTime = mMessages.back()->timeStampSec();
        }
        unsigned deltaSyncId = endSyncId - startSyncId;
        float deltaTime = endTime - startTime;

        // syncId always increased and as a result always be startSyncId < endSyncId.
        int w = str_util::getNumberOfDigits(endSyncId);

        std::ostringstream ostr;
        ostr
        << "start:(syncId:" << std::setw(w) << startSyncId << " time:" << str_util::secStr(startTime) << ")\n"
        << "  end:(syncId:" << std::setw(w) << endSyncId << " time:" << str_util::secStr(endTime) << ")\n"
        << "delta:(syncId:" << std::setw(w) << deltaSyncId << " time:" << str_util::secStr(deltaTime) << ")";
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "Frame {\n"
         << "  mFrameId:" << mFrameId << '\n'
         << "  intervalSec:" << str_util::secStr(getIntervalSecFromPrevFrame()) << '\n'
         << str_util::addIndent(showRange()) << '\n';
    if (!simple) {
        ostr << "  mMessages (size:" << mMessages.size() << ") {\n";
        for (size_t i = 0; i < mMessages.size(); ++i) {
            ostr << str_util::addIndent(showMessage(i), 2) << '\n';
        }
        ostr << "  }\n";
    } else {
        ostr << "  mMessages (size:" << mMessages.size() << ")\n";
    }
    ostr << "}";
    return ostr.str();
}

//------------------------------------------------------------------------------------------

MessageHistory::MessageHistory()
    : mFrameCounter(0)
    , mSkip(true)
{
    mTime.start();

    parserConfigure();
}

void
MessageHistory::reset()
{
    mHistory.clear();
    mTime.start();
}

void
MessageHistory::newFrame()
{
    if (!mSkip) {
        mHistory.push_back(std::make_shared<MessageHistoryFrame>(this, mFrameCounter));
        mFrameCounter++;
    }
}

void
MessageHistory::setReceiveData(unsigned syncId)
{
    if (mSkip) return;

    if (mHistory.empty()) {
        newFrame();
    }

    mHistory.back()->set(syncId, mTime.end());
}

const MessageHistoryFrame*
MessageHistory::getFrame(unsigned frameId) const
{
    if (frameId >= mHistory.size()) return nullptr;
    return mHistory[frameId].get();
}

std::string
MessageHistory::show(bool simple) const
{
    namespace str_util = scene_rdl2::str_util;

    auto showFrame = [&](size_t i, int w) -> std::string {
        std::ostringstream ostr;
        ostr << "i:" << std::setw(w) << i << ' ' << mHistory[i]->show(simple);
        return ostr.str();
    };

    int w = str_util::getNumberOfDigits(static_cast<unsigned>(mHistory.size()));

    std::ostringstream ostr;
    ostr << "MessageHistory {\n"
         << "  mFrameCounter:" << mFrameCounter << '\n'
         << "  mSkip:" << str_util::boolStr(mSkip) << '\n'
         << "  mHistory (size:" << mHistory.size() << ") {\n";
    for (size_t i = 0; i < mHistory.size(); ++i) {
        ostr << str_util::addIndent(showFrame(i, w), 2) << '\n';
    }
    ostr << "  }\n"
         << "}";
    return ostr.str();
}

void
MessageHistory::parserConfigure()
{
    mParser.description("render history command");
    mParser.opt("show", "", "show all info",
                [&](Arg& arg) -> bool { return arg.msg(show(false) + '\n'); });
    mParser.opt("showSimple", "", "simple version of show command",
                [&](Arg& arg) -> bool { return arg.msg(show(true) + '\n'); });
    mParser.opt("rec", "<on|off>", "record message history on|off switch (default off)",
                [&](Arg& arg) -> bool {
                    mSkip = !(arg++).as<bool>(0);
                    return arg.fmtMsg("sw %s\n", scene_rdl2::str_util::boolStr(!mSkip).c_str());
                });
    mParser.opt("reset", "", "reset all history",
                [&](Arg& arg) -> bool { reset(); return arg.msg("RESET\n"); });
}

} // namespace mcrt_computation


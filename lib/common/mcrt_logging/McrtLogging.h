// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#pragma once

// Connects moonray logger to Arras logging, so that log
// messages from the moonray library go to the Arras logger
//
// debug and info messages are not sent to the logger unless
// explicitly enabled (primarily for performance reasons)

#include <arras4_log/Logger.h>

namespace mcrt_computation {

class McrtLogging
{
public:
    McrtLogging() {
	setInstance(this);
    }
    ~McrtLogging() {
	setInstance(nullptr);
    }

    void enableInfo(bool enable) { mEnableInfo = enable; }
    void enableDebug(bool enable) { mEnableDebug = enable; }

    bool isEnableInfo() const { return mEnableInfo; }
    bool isEnableDebug() const { return mEnableDebug; }

    void logDebug(const std::string& s) {
	if (mEnableDebug) {
	    ::arras4::log::Logger::instance().logMessage(
		::arras4::log::Logger::LOG_DEBUG, s);
	}
    }
    void logInfo(const std::string& s) {
	if (mEnableInfo) {
	    ::arras4::log::Logger::instance().logMessage(
		::arras4::log::Logger::LOG_INFO, s);
	}
    }
    void logWarn(const std::string& s) {
	::arras4::log::Logger::instance().logMessage(
	    ::arras4::log::Logger::LOG_WARN, s);
    }
    void logError(const std::string& s) {
	::arras4::log::Logger::instance().logMessage(
	    ::arras4::log::Logger::LOG_ERROR, s);
    }
    void logFatal(const std::string& s) {
	::arras4::log::Logger::instance().logMessage(
	    ::arras4::log::Logger::LOG_FATAL, s);
    }

    static McrtLogging* getInstance();
    static void setInstance(McrtLogging*);

private:
    bool mEnableInfo{false};
    bool mEnableDebug{false};

    static std::atomic<McrtLogging*> sInstance;
};

}

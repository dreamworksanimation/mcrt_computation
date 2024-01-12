// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#include "McrtUpdate.h"

namespace mcrt_computation {

// static function
std::string
McrtUpdate::msgTypeStr(const MsgType &msgType)
{
    switch (msgType) {
    case MsgType::UNKNOWN : return "UNKNOWN";
    case MsgType::RDL : return "RDL";
    case MsgType::RDL_FORCE_RELOAD : return "RDL_FORCE_RELOAD";
    case MsgType::RENDER_START : return "RENDER_START";
    case MsgType::ROI_SET : return "ROI_SET";
    case MsgType::ROI_DISABLE : return "ROI_DISABLE";
    case MsgType::VIEWPORT : return "VIEWPORT";
    default : return "?";
    }
}
    
} // namespace mcrt_computation


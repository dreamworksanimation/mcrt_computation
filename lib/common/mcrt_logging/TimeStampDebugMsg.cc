// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "TimeStampDebugMsg.h"

#include <scene_rdl2/common/rec_time/RecTime.h>

#include <iostream>
#include <sstream>

namespace mcrt_computation {

// static function
void
TimeStampDebugMsg::out(const std::string& moduleName,
                       const std::string& label,
                       const std::string& msg)
{
    std::ostringstream ostr;
    ostr << ">> timeStamp, "
         << moduleName << ", "
         << scene_rdl2::rec_time::RecTime::getCurrentMicroSec() << ", "
         << "label=" << label << ", "
         << "msg=" << msg;
    std::cerr << ostr.str() << '\n';
}

} // namespace mcrt_computation

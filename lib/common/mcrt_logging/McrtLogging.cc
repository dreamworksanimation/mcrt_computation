// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#include "McrtLogging.h"

namespace mcrt_computation {

std::atomic<McrtLogging*> McrtLogging::sInstance(nullptr);

McrtLogging* 
McrtLogging::getInstance() { return sInstance; }

void 
McrtLogging::setInstance(McrtLogging* instance) { sInstance = instance; }


} // namespace mcrt_computation


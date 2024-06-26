/*
 * Copyright (c) 2019 SK Telecom Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "location_agent.hh"

namespace NuguCapability {

static const char* CAPABILITY_NAME = "Location";
static const char* CAPABILITY_VERSION = "1.0";

LocationAgent::LocationAgent()
    : Capability(CAPABILITY_NAME, CAPABILITY_VERSION)
{
}

void LocationAgent::setCapabilityListener(ICapabilityListener* clistener)
{
    if (clistener)
        location_listener = dynamic_cast<ILocationListener*>(clistener);
}

void LocationAgent::updateInfoForContext(NJson::Value& ctx)
{
    NJson::Value location;
    LocationInfo location_info {};

    location["version"] = getVersion();

    if (location_listener)
        location_listener->requestContext(location_info);

    // set current if latitude and longitude conditions are satisfied
    if (!location_info.latitude.empty() && !location_info.longitude.empty()) {
        NJson::Value current;

        current["latitude"] = location_info.latitude;
        current["longitude"] = location_info.longitude;
        location["current"] = current;
    }

    ctx[getName()] = location;
}

} // NuguCapability

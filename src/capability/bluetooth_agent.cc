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

#include <string.h>

#include "base/nugu_log.h"
#include "bluetooth_agent.hh"

namespace NuguCapability {

static const char* CAPABILITY_NAME = "Bluetooth";
static const char* CAPABILITY_VERSION = "1.1";

BluetoothAgent::BluetoothAgent()
    : Capability(CAPABILITY_NAME, CAPABILITY_VERSION)
{
}

void BluetoothAgent::initialize()
{
    if (initialized) {
        nugu_warn("It's already initialized.");
        return;
    }

    Capability::initialize();

    context_info = "{}";
    bt_device_info.device_name = "";
    bt_device_info.power_on = false;
    bt_device_info.is_active_device = false;
    bt_device_info.is_paired_device = false;

    addReferrerEvents("StartDiscoverableModeSucceeded", "StartDiscoverableMode");
    addReferrerEvents("StartDiscoverableModeFailed", "StartDiscoverableMode");
    addReferrerEvents("FinishDiscoverableModeSucceeded", "FinishDiscoverableMode");
    addReferrerEvents("FinishDiscoverableModeFailed", "FinishDiscoverableMode");
    addReferrerEvents("MediaControlPlaySucceeded", "Play");
    addReferrerEvents("MediaControlPlayFailed", "Play");
    addReferrerEvents("MediaControlStopSucceeded", "Stop");
    addReferrerEvents("MediaControlStopFailed", "Stop");
    addReferrerEvents("MediaControlPauseSucceeded", "Pause");
    addReferrerEvents("MediaControlPauseFailed", "Pause");
    addReferrerEvents("MediaControlNextSucceeded", "Next");
    addReferrerEvents("MediaControlNextFailed", "Next");
    addReferrerEvents("MediaControlPreviousSucceeded", "Previous");
    addReferrerEvents("MediaControlPreviousFailed", "Previous");

    initialized = true;
}

void BluetoothAgent::deInitialize()
{
    initialized = false;
}

void BluetoothAgent::setCapabilityListener(ICapabilityListener* clistener)
{
    if (clistener)
        bt_listener = dynamic_cast<IBluetoothListener*>(clistener);
}

void BluetoothAgent::parsingDirective(const char* dname, const char* message)
{
    nugu_dbg("message: %s", message);

    if (!strcmp(dname, "StartDiscoverableMode"))
        parsingStartDiscoverableMode(message);
    else if (!strcmp(dname, "FinishDiscoverableMode"))
        parsingFinishDiscoverableMode(message);
    else if (!strcmp(dname, "Play"))
        parsingPlay(message);
    else if (!strcmp(dname, "Stop"))
        parsingStop(message);
    else if (!strcmp(dname, "Pause"))
        parsingPause(message);
    else if (!strcmp(dname, "Next"))
        parsingNext(message);
    else if (!strcmp(dname, "Previous"))
        parsingPrevious(message);
    else
        nugu_warn("%s[%s] is not support %s directive", getName().c_str(), getVersion().c_str(), dname);
}

void BluetoothAgent::updateInfoForContext(Json::Value& ctx)
{
    Json::Value root;
    Json::Value device;
    Json::Value device_profile;
    Json::Value active_device;

    root["version"] = getVersion();

    if (!bt_device_info.device_name.empty()) {
        device["name"] = bt_device_info.device_name;
        device["status"] = bt_device_info.power_on ? "ON" : "OFF";

        if (bt_device_info.is_paired_device) {
            for (const auto& profile : bt_device_info.profiles) {
                device_profile["name"] = profile.name;
                device_profile["enabled"] = profile.enable ? "TRUE" : "FALSE";

                device["profiles"].append(device_profile);
            }
        }
        root["device"] = device;
    }

    if (bt_device_info.is_active_device) {
        if (bt_device_info.active_device_id.size())
            active_device["id"] = bt_device_info.active_device_id;
        if (bt_device_info.active_device_name.size())
            active_device["name"] = bt_device_info.active_device_name;

        active_device["streaming"] = bt_device_info.active_device_streaming;

        root["activeDevice"] = active_device;
    }

    ctx[getName()] = root;
}

void BluetoothAgent::setDeviceInformation(BTDeviceInfo device_info)
{
    nugu_dbg("update bt device information");

    bt_device_info = device_info;

    printDeviceInformation();
}

void BluetoothAgent::startDiscoverableModeSucceeded(bool has_paired_devices)
{
    sendEventDiscoverableMode("StartDiscoverableModeSucceeded", has_paired_devices);
}

void BluetoothAgent::startDiscoverableModeFailed(bool has_paired_devices)
{
    sendEventDiscoverableMode("StartDiscoverableModeFailed", has_paired_devices);
}

void BluetoothAgent::finishDiscoverableModeSucceeded()
{
    sendEventCommon("FinishDiscoverableModeSucceeded");
}

void BluetoothAgent::finishDiscoverableModeFailed()
{
    sendEventCommon("FinishDiscoverableModeFailed");
}

void BluetoothAgent::connectSucceeded()
{
    sendEventCommon("ConnectSucceeded");
}

void BluetoothAgent::connectFailed()
{
    sendEventCommon("ConnectFailed");
}

void BluetoothAgent::disconnectSucceeded()
{
    sendEventCommon("DisconnectSucceeded");
}

void BluetoothAgent::disconnectFailed()
{
    sendEventCommon("DisconnectFailed");
}

void BluetoothAgent::mediaControlPlaySucceeded()
{
    sendEventCommon("MediaControlPlaySucceeded");
}

void BluetoothAgent::mediaControlPlayFailed()
{
    sendEventCommon("MediaControlPlayFailed");
}

void BluetoothAgent::mediaControlStopSucceeded()
{
    sendEventCommon("MediaControlStopSucceeded");
}

void BluetoothAgent::mediaControlStopFailed()
{
    sendEventCommon("MediaControlStopFailed");
}

void BluetoothAgent::mediaControlPauseSucceeded()
{
    sendEventCommon("MediaControlPauseSucceeded");
}

void BluetoothAgent::mediaControlPauseFailed()
{
    sendEventCommon("MediaControlPauseFailed");
}

void BluetoothAgent::mediaControlNextSucceeded()
{
    sendEventCommon("MediaControlNextSucceeded");
}

void BluetoothAgent::mediaControlNextFailed()
{
    sendEventCommon("MediaControlNextFailed");
}

void BluetoothAgent::mediaControlPreviousSucceeded()
{
    sendEventCommon("MediaControlPreviousSucceeded");
}

void BluetoothAgent::mediaControlPreviousFailed()
{
    sendEventCommon("MediaControlPreviousFailed");
}

void BluetoothAgent::sendEventCommon(const std::string& ename, EventResultCallback cb)
{
    Json::FastWriter writer;
    Json::Value root;

    if (ps_id.size() == 0) {
        nugu_error("there is something wrong [%s]", ename.c_str());
        return;
    }

    root["playServiceId"] = ps_id;

    sendEvent(ename, getContextInfo(), writer.write(root), std::move(cb));
}

void BluetoothAgent::sendEventDiscoverableMode(const std::string& ename, bool has_paired_devices, EventResultCallback cb)
{
    Json::FastWriter writer;
    Json::Value root;

    if (ps_id.size() == 0) {
        nugu_error("there is something wrong [%s]", ename.c_str());
        return;
    }

    root["playServiceId"] = ps_id;
    root["hasPairedDevices"] = has_paired_devices;

    sendEvent(ename, getContextInfo(), writer.write(root), std::move(cb));
}

bool BluetoothAgent::parsingCommon(const char* message, Json::Value& root)
{
    Json::Reader reader;

    if (!reader.parse(message, root)) {
        nugu_error("parsing error");
        return false;
    }

    if (root["playServiceId"].empty()) {
        nugu_error("There is no mandatory data in directive message");
        return false;
    }

    ps_id = root["playServiceId"].asString();

    return true;
}

void BluetoothAgent::parsingStartDiscoverableMode(const char* message)
{
    Json::Value root;
    long duration_sec;

    if (!parsingCommon(message, root))
        return;

    duration_sec = root["durationInSeconds"].asLargestInt();

    if (bt_listener)
        bt_listener->startDiscoverableMode(duration_sec);
}

void BluetoothAgent::parsingFinishDiscoverableMode(const char* message)
{
    Json::Value root;

    parsingCommon(message, root);

    if (bt_listener)
        bt_listener->finishDiscoverableMode();
}

void BluetoothAgent::parsingPlay(const char* message)
{
    Json::Value root;

    parsingCommon(message, root);

    if (bt_listener)
        bt_listener->play();
}

void BluetoothAgent::parsingStop(const char* message)
{
    Json::Value root;

    parsingCommon(message, root);

    if (bt_listener)
        bt_listener->stop();
}

void BluetoothAgent::parsingPause(const char* message)
{
    Json::Value root;

    parsingCommon(message, root);

    if (bt_listener)
        bt_listener->pause();
}

void BluetoothAgent::parsingNext(const char* message)
{
    Json::Value root;

    parsingCommon(message, root);

    if (bt_listener)
        bt_listener->next();
}

void BluetoothAgent::parsingPrevious(const char* message)
{
    Json::Value root;

    parsingCommon(message, root);

    if (bt_listener)
        bt_listener->previous();
}

void BluetoothAgent::printDeviceInformation()
{
    nugu_dbg("bluetooth device information ==============================");
    nugu_dbg("adaptor name: %s", bt_device_info.device_name.c_str());
    nugu_dbg("power: %d", bt_device_info.power_on);
    for (const auto& profile : bt_device_info.profiles)
        nugu_dbg("profile[%s]: %s", profile.name.c_str(), profile.enable ? "TRUE" : "FALSE");
    if (bt_device_info.is_active_device) {
        nugu_dbg("* connected device *");
        nugu_dbg("  id: %s", bt_device_info.active_device_id.c_str());
        nugu_dbg("  name: %s", bt_device_info.active_device_name.c_str());
        nugu_dbg("  streaming: %s", bt_device_info.active_device_streaming.c_str());
    }
    nugu_dbg("===========================================================");
}

} // NuguCapability
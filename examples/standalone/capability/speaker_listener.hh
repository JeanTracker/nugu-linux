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

#ifndef __SPEAKER_LISTENER_H__
#define __SPEAKER_LISTENER_H__

#include <functional>

#include <capability/speaker_interface.hh>

using namespace NuguCapability;

class SpeakerListener : public ISpeakerListener {
    using nugu_volume_func = std::function<bool(int volume)>;
    using nugu_mute_func = std::function<bool(bool mute)>;

public:
    virtual ~SpeakerListener() = default;

    void requestSetVolume(const std::string& ps_id, SpeakerType type, int volume, bool linear) override;
    void requestSetMute(const std::string& ps_id, SpeakerType type, bool mute) override;

    void setSpeakerHandler(ISpeakerHandler* speaker);
    void setVolumeNuguSpeakerCallback(nugu_volume_func vns);
    void setMuteNuguSpeakerCallback(nugu_mute_func mns);

private:
    ISpeakerHandler* speaker_handler = nullptr;
    nugu_volume_func nugu_speaker_volume = nullptr;
    nugu_mute_func nugu_speaker_mute = nullptr;

public:
    auto getNuguSpeakerVolumeControl() -> decltype(nugu_speaker_volume);
    auto getNuguSpeakerMuteControl() -> decltype(nugu_speaker_mute);
};

#endif /* __SPEAKER_LISTENER_H__ */

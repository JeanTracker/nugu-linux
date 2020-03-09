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

#ifndef __NUGU_TTS_PLAYER_H__
#define __NUGU_TTS_PLAYER_H__

#include <string>

#include "base/nugu_pcm.h"
#include "clientkit/media_player_interface.hh"

namespace NuguCore {

using namespace NuguClientKit;

class TTSPlayerPrivate;
class TTSPlayer : public ITTSPlayer {
public:
    explicit TTSPlayer(int volume = NUGU_SET_VOLUME_MAX);
    virtual ~TTSPlayer();

    void addListener(IMediaPlayerListener* listener) override;
    void removeListener(IMediaPlayerListener* listener) override;

    bool write_audio(const char *data, int size) override;
    void write_done() override;

    bool setSource(const std::string& url) override;
    bool play() override;
    bool stop() override;
    bool pause() override;
    bool resume() override;
    bool seek(int sec) override;

    int position() override;
    bool setPosition(int position) override;

    int duration() override;
    bool setDuration(int duration) override;

    int volume() override;
    bool setVolume(int volume) override;

    bool mute() override;
    bool setMute(bool mute) override;

    bool loop() override;
    void setLoop(bool loop) override;

    bool isPlaying() override;

    MediaPlayerState state() override;
    bool setState(MediaPlayerState state) override;

    std::string stateString(MediaPlayerState state) override;
    std::string url() override;

private:
    void clearContent();

    TTSPlayerPrivate* d;
};

} // NuguCore
#endif

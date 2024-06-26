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

#ifdef ENABLE_VENDOR_LIBRARY
#include <keyword_detector.h>
#endif
#include <cstring>

#include "base/nugu_log.h"
#include "base/nugu_prof.h"

#include "nugu_timer.hh"
#include "wakeup_detector.hh"

namespace NuguCore {

// define default property values
static const char* KWD_SAMPLERATE = "16k";
static const char* KWD_FORMAT = "s16le";
static const char* KWD_CHANNEL = "1";

WakeupDetector::WakeupDetector()
{
    initialize(Attribute {});
}

WakeupDetector::~WakeupDetector()
{
    listener = nullptr;
}

WakeupDetector::WakeupDetector(Attribute&& attribute)
{
    initialize(std::move(attribute));
}

void WakeupDetector::sendWakeupEvent(WakeupState state, const std::string& id, float noise, float speech)
{
    if (!listener)
        return;

    mutex.lock();
    AudioInputProcessor::sendEvent([=] {
        if (listener)
            listener->onWakeupState(state, id, noise, speech);
    });
    mutex.unlock();
}

void WakeupDetector::setListener(IWakeupDetectorListener* listener)
{
    this->listener = listener;
}

void WakeupDetector::initialize(Attribute&& attribute)
{
    std::string sample = !attribute.sample.empty() ? attribute.sample : KWD_SAMPLERATE;
    std::string format = !attribute.format.empty() ? attribute.format : KWD_FORMAT;
    std::string channel = !attribute.channel.empty() ? attribute.channel : KWD_CHANNEL;

    setModelFile(attribute.model_net_file, attribute.model_search_file);

    AudioInputProcessor::init("kwd", sample, format, channel);

    std::memset(power_speech, 0, sizeof(power_speech));
    std::memset(power_noise, 0, sizeof(power_noise));

    preloadModelFile();
}

#ifdef ENABLE_VENDOR_LIBRARY
void WakeupDetector::loop()
{
    NUGUTimer* timer = new NUGUTimer(true);
    int pcm_size;

    nugu_dbg("Wakeup Thread: started");

    mutex.lock();
    thread_created = true;
    cond.notify_all();
    mutex.unlock();

    std::string id = listening_id;

    while (g_atomic_int_get(&destroy) == 0) {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock);
        lock.unlock();

        if (is_running == false)
            continue;

        is_started = true;
        id = listening_id;

        nugu_dbg("Wakeup Thread: kwd_is_running=%d", is_running);
        sendWakeupEvent(WakeupState::DETECTING, id);

        if (kwd_initialize(model_net_file.c_str(), model_search_file.c_str()) < 0
            || !recorder->start()) {
            nugu_error("kwd_initialize() or recorder->start() failed");

            is_running = false;
            kwd_deinitialize();

            sendWakeupEvent(WakeupState::FAIL, id);
            sendWakeupEvent(WakeupState::DONE, id);
            continue;
        }

        if (id != listening_id && !is_started) {
            nugu_dbg("restart wakeup");
            is_running = false;
        }

        pcm_size = recorder->getAudioFrameSize();

        while (is_running) {
            char pcm_buf[pcm_size];

            if (!recorder->isRecording()) {
                struct timespec ts;

                ts.tv_sec = 0;
                ts.tv_nsec = 10 * 1000 * 1000; /* 10ms */

                nugu_dbg("Wakeup Thread: not recording state");
                nanosleep(&ts, NULL);
                continue;
            }

            if (recorder->isMute()) {
                nugu_error("recorder is mute");

                if (is_running)
                    sendWakeupEvent(WakeupState::FAIL, id);
                break;
            }

            if (!recorder->getAudioFrame(pcm_buf, &pcm_size, 0)) {
                nugu_error("recorder->getAudioFrame() failed");

                if (is_running)
                    sendWakeupEvent(WakeupState::FAIL, id);
                break;
            }

            if (pcm_size == 0) {
                nugu_error("pcm_size result is 0");

                if (is_running)
                    sendWakeupEvent(WakeupState::FAIL, id);
                break;
            }

            setPower(kwd_get_power());
            if (kwd_put_audio((short*)pcm_buf, pcm_size) == 1) {
                float noise, speech;

                nugu_prof_mark(NUGU_PROF_TYPE_WAKEUP_KEYWORD_DETECTED);
                getPower(noise, speech);
                sendWakeupEvent(WakeupState::DETECTED, id, noise, speech);
                std::memset(power_speech, 0, sizeof(power_speech));
                std::memset(power_noise, 0, sizeof(power_noise));
                power_index = 0;
                break;
            }
        }

        // If is_running is false, it indicate wakeup is stopped explicitly.
        // So, it send WakeupState::STOPPED event.
        if (!is_running) {
            sendWakeupEvent(WakeupState::STOPPED, id);
        } else {
            is_running = false;
            recorder->stop();
        }

        kwd_deinitialize();

        if (g_atomic_int_get(&destroy) == 0)
            sendWakeupEvent(WakeupState::DONE, id);

        if (!is_started) {
            is_running = false;
            timer->setCallback([&]() {
                nugu_dbg("request to start audio input");
                startWakeup(listening_id);
            });
            timer->start();
        }
    }
    delete timer;

    nugu_dbg("Wakeup Thread: exited");
}
#else
void WakeupDetector::loop()
{
    mutex.lock();
    thread_created = true;
    cond.notify_all();
    mutex.unlock();

    std::string id = listening_id;

    while (g_atomic_int_get(&destroy) == 0) {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock);
        lock.unlock();

        if (is_running == false)
            continue;

        id = listening_id;

        nugu_dbg("Wakeup Thread: kwd_is_running=%d", is_running);
        sendWakeupEvent(WakeupState::DETECTING, id);

        nugu_error("nugu_wwd is not supported");
        sendWakeupEvent(WakeupState::FAIL, id);
        is_running = false;
    }
}
#endif

bool WakeupDetector::startWakeup(const std::string& id)
{
    listening_id = id;
    return AudioInputProcessor::start();
}

void WakeupDetector::stopWakeup()
{
    AudioInputProcessor::stop();
}

void WakeupDetector::changeModel(const std::string& model_net_file, const std::string& model_search_file)
{
    if (model_net_file.empty() || model_search_file.empty()) {
        nugu_warn("The model path is empty.");
        return;
    }

    stopWakeup();
    setModelFile(model_net_file, model_search_file);
}

void WakeupDetector::setPower(float power)
{
    power_noise[power_index % POWER_NOISE_PERIOD] = power;
    power_speech[power_index % POWER_SPEECH_PERIOD] = power;
    power_index++;
}

void WakeupDetector::getPower(float& noise, float& speech)
{
    noise = speech = 0;

    float min = 1000;
    for (auto n : power_noise) {
        if (n > 0 && min > n)
            min = n;
    }
    if (min != 1000)
        noise = min;

    float max = 0;
    for (auto s : power_speech) {
        if (s > 0 && max < s)
            max = s;
    }
    if (max)
        speech = max;
}

void WakeupDetector::setModelFile(const std::string& model_net_file, const std::string& model_search_file)
{
    if (model_net_file.empty() || model_search_file.empty()) {
        nugu_warn("The model net or search file is empty.");
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);

    this->model_net_file = model_net_file;
    nugu_dbg("kwd model net-file: %s", model_net_file.c_str());

    this->model_search_file = model_search_file;
    nugu_dbg("kwd model search-file: %s", model_search_file.c_str());
}

void WakeupDetector::preloadModelFile()
{
#ifdef ENABLE_VENDOR_LIBRARY
    if (kwd_initialize(model_net_file.c_str(), model_search_file.c_str()) < 0)
        nugu_error("kwd_initialize() failed");

    kwd_deinitialize();
#endif
}

} // NuguCore

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

#include <functional>
#include <glib.h>
#include <string>

#include <interface/capability/text_interface.hh>

#include "speech_operator.hh"

#ifndef __NUGU_SAMPLE_MANAGER_H__
#define __NUGU_SAMPLE_MANAGER_H__

class NuguSampleManager {
public:
    virtual ~NuguSampleManager() = default;

    using NetworkCallback = struct {
        std::function<bool()> connect;
        std::function<bool()> disconnect;
    };
    using Commander = struct {
        bool is_connected;
        int text_input;
        NetworkCallback network_callback;
        ITextHandler* text_handler;
        SpeechOperator* speech_operator;
    };

    static void error(const std::string& message);
    static void info(const std::string& message);

    bool handleArguments(const int& argc, char** argv);
    void prepare();
    void runLoop();

    const std::string& getModelPath();

    NuguSampleManager* setNetworkCallback(NetworkCallback callback);
    NuguSampleManager* setTextHandler(ITextHandler* text_handler);
    NuguSampleManager* setSpeechOperator(SpeechOperator* speech_operator);
    void handleNetworkResult(bool is_connected);

private:
    static void quit(int signal);
    static void showPrompt(void);
    static gboolean onKeyInput(GIOChannel* src, GIOCondition con, gpointer userdata);

    static const std::string C_RED;
    static const std::string C_YELLOW;
    static const std::string C_BLUE;
    static const std::string C_CYAN;
    static const std::string C_WHITE;
    static const std::string C_RESET;
    static Commander commander;
    static GMainLoop* loop;

    GMainContext* context = nullptr;
    std::string model_path = "./";
    bool is_prepared = false;
};

#endif /* __NUGU_SAMPLE_MANAGER_H__ */
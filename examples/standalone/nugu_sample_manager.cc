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

#include <iostream>
#include <string.h>
#include <unistd.h>

#include "nugu_log.h"
#include "nugu_sample_manager.hh"

const std::string NuguSampleManager::C_RED = "\033[1;91m";
const std::string NuguSampleManager::C_YELLOW = "\033[1;93m";
const std::string NuguSampleManager::C_BLUE = "\033[1;94m";
const std::string NuguSampleManager::C_CYAN = "\033[1;96m";
const std::string NuguSampleManager::C_WHITE = "\033[1;97m";
const std::string NuguSampleManager::C_RESET = "\033[0m";

NuguSampleManager::Commander NuguSampleManager::commander = { false, 0, { nullptr, nullptr }, nullptr, nullptr };
GMainLoop* NuguSampleManager::loop = nullptr;

void NuguSampleManager::error(const std::string& message)
{
    std::cout << C_RED
              << message
              << std::endl
              << C_RESET;
    std::cout.flush();
}

void NuguSampleManager::info(const std::string& message)
{
    std::cout << C_CYAN
              << message
              << std::endl
              << C_RESET;
    std::cout.flush();
}

bool NuguSampleManager::handleArguments(const int& argc, char** argv)
{
    int c;

    nugu_log_set_system(NUGU_LOG_SYSTEM_NONE);

    while ((c = getopt(argc, argv, "dm:")) != -1) {
        switch (c) {
        case 'd':
            nugu_log_set_system(NUGU_LOG_SYSTEM_STDERR);
            break;
        case 'm':
            model_path = optarg;
            break;
        default:
            std::cout << "Usage: " << argv[0] << " [-d] [-m model-path]" << std::endl;
            return false;
        }
    }

    return true;
}

void NuguSampleManager::prepare()
{
    if (is_prepared) {
        nugu_warn("It's already prepared.");
        return;
    }

    std::cout << "=======================================================\n";
    std::cout << "User Application\n";
    std::cout << " - Model path: " << model_path << std::endl;
    std::cout << "=======================================================\n";

    signal(SIGINT, &quit);

    context = g_main_context_default();
    loop = g_main_loop_new(context, FALSE);

    GIOChannel* channel = g_io_channel_unix_new(STDIN_FILENO);
    g_io_add_watch(channel, (GIOCondition)(G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL), onKeyInput, loop);
    g_io_channel_unref(channel);

    is_prepared = true;
}

void NuguSampleManager::runLoop()
{
    if (loop && is_prepared) {
        g_main_loop_run(loop);
        g_main_loop_unref(loop);

        info("mainloop exited");
    }
}

const std::string& NuguSampleManager::getModelPath()
{
    return model_path;
}

NuguSampleManager* NuguSampleManager::setNetworkCallback(NetworkCallback callback)
{
    commander.network_callback = callback;

    return this;
}

NuguSampleManager* NuguSampleManager::setTextHandler(ITextHandler* text_handler)
{
    commander.text_handler = text_handler;

    return this;
}

NuguSampleManager* NuguSampleManager::setSpeechOperator(SpeechOperator* speech_operator)
{
    commander.speech_operator = speech_operator;

    return this;
}

void NuguSampleManager::handleNetworkResult(bool is_connected)
{
    commander.is_connected = is_connected;

    showPrompt();
}

void NuguSampleManager::quit(int signal)
{
    if (loop)
        g_main_loop_quit(loop);
}

void NuguSampleManager::showPrompt(void)
{
    if (commander.text_input)
        std::cout << C_WHITE
                  << "\nEnter Service > "
                  << C_RESET;
    else {
        std::cout << C_YELLOW
                  << "=======================================================\n"
                  << C_RED
                  << "NUGU SDK Command (" << (commander.is_connected ? "Connected" : "Dis_connected") << ")\n"
                  << C_YELLOW
                  << "=======================================================\n"
                  << C_BLUE;

        if (commander.is_connected)
            std::cout << "w : start wakeup\n"
                      << "l : start listening\n"
                      << "s : stop listening\n"
                      << "t : text input\n";

        std::cout << "c : connect\n"
                  << "d : disconnect\n"
                  << "q : quit\n"
                  << C_YELLOW
                  << "-------------------------------------------------------\n"
                  << C_WHITE
                  << "Select Command > "
                  << C_RESET;
    }

    fflush(stdout);
}

gboolean NuguSampleManager::onKeyInput(GIOChannel* src, GIOCondition con, gpointer userdata)
{
    char keybuf[4096];

    if (fgets(keybuf, 4096, stdin) == NULL)
        return TRUE;

    if (strlen(keybuf) > 0) {
        if (keybuf[strlen(keybuf) - 1] == '\n')
            keybuf[strlen(keybuf) - 1] = '\0';
    }

    if (strlen(keybuf) < 1) {
        showPrompt();
        return TRUE;
    }

    // handle case when NuguClient is dis_connected
    if (!commander.is_connected) {
        if (g_strcmp0(keybuf, "q") != 0 && g_strcmp0(keybuf, "c") != 0 && g_strcmp0(keybuf, "d") != 0) {
            error("invalid input");
            showPrompt();
            return TRUE;
        }
    }

    if (commander.text_input) {
        commander.text_input = 0;

        if (commander.text_handler)
            commander.text_handler->requestTextInput(keybuf);
    } else if (g_strcmp0(keybuf, "w") == 0) {
        if (commander.speech_operator)
            commander.speech_operator->startWakeup();
    } else if (g_strcmp0(keybuf, "l") == 0) {
        if (commander.speech_operator)
            commander.speech_operator->startListening();
    } else if (g_strcmp0(keybuf, "s") == 0) {
        if (commander.speech_operator)
            commander.speech_operator->stopListening();

        showPrompt();
    } else if (g_strcmp0(keybuf, "t") == 0) {
        commander.text_input = 1;
        showPrompt();
    } else if (g_strcmp0(keybuf, "c") == 0) {
        if (commander.is_connected) {
            info("It's already connected.");
            showPrompt();
        } else {
            if (commander.network_callback.connect)
                commander.network_callback.connect();
        }
    } else if (g_strcmp0(keybuf, "d") == 0) {
        if (commander.network_callback.disconnect && commander.network_callback.disconnect()) {
            commander.is_connected = false;
        }
    } else if (g_strcmp0(keybuf, "q") == 0) {
        g_main_loop_quit((GMainLoop*)userdata);
    } else {
        error("invalid input");
        showPrompt();
    }

    return TRUE;
}
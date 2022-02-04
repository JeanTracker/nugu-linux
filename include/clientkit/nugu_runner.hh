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

#ifndef __NUGU_RUNNER_H__
#define __NUGU_RUNNER_H__

#include <functional>
#include <string>

namespace NuguClientKit {

/**
 * @file nugu_runner.hh
 * @defgroup NuguRunner NuguRunner
 * @ingroup SDKNuguClientKit
 * @brief Nugu Runner
 *
 * NuguRunner is a class that provides thread safety feature for using glib.
 * The nugu loop is based on GMainLoop provided by glib.
 *
 * @{
 */

/**
 * @brief ExecuteType
 */
enum class ExecuteType {
    Auto, /**< The method is executed synchronized if caller is on nugu loop, otherwise it is executed asyncronized */
    Queued, /**< The method is executed on next idle time even if caller is on nugu loop */
    Blocking /**< The caller is blocking until the method is executed done */
};

/**
 * @brief NuguRunnerPrivate
 */
class NuguRunnerPrivate;

/**
 * @brief NuguRunner
 */
class NuguRunner {
public:
    /**
     * @brief The request method callback
     */
    typedef std::function<void()> request_method;

public:
    NuguRunner();
    virtual ~NuguRunner();

    /**
     * @brief Request to execute method on nugu loop.
     * @param[in] tag tag name for debug output
     * @param[in] method request method
     * @param[in] type execute method type
     * @see ExecuteType
     */
    bool invokeMethod(const std::string& tag, request_method method, ExecuteType type = ExecuteType::Auto);

private:
    void addMethod2Dispatcher(const std::string& tag, NuguRunner::request_method method, ExecuteType type);

private:
    NuguRunnerPrivate* d;
};

} // NuguClientKit

#endif // __NUGU_RUNNER_H__

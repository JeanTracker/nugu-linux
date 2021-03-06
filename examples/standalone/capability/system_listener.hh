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

#ifndef __SYSTEM_LISTENER_H__
#define __SYSTEM_LISTENER_H__

#include <capability/system_interface.hh>

using namespace NuguCapability;

class SystemListener : public ISystemListener {
public:
    virtual ~SystemListener() = default;

    void onException(SystemException exception, const std::string& dialog_id) override;
    void onTurnOff(void) override;
    void onRevoke(RevokeReason reason) override;
    void onNoDirective(const std::string& dialog_id) override;
};

#endif /* __SYSTEM_LISTENER_H__ */

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

#include "sound_listener.hh"

void SoundListener::setCapabilityHandler(ICapabilityInterface* handler)
{
    if (handler)
        this->sound_handler = dynamic_cast<ISoundHandler*>(handler);
}

void SoundListener::handleBeep(BeepType beep_type, const std::string& dialog_id)
{
    switch (beep_type) {
    case BeepType::RESPONSE_FAIL:
        // step-1 : play related beep sound file
        // play related sound resource file

        // step-2 : send beep play result
        if (sound_handler)
            sound_handler->sendBeepResult(true);
        break;
    }
}

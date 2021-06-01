/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
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
#ifndef HDC_DAEMON_USB_H
#define HDC_DAEMON_USB_H
#include "daemon_common.h"

namespace Hdc {
class HdcDaemonUSB : public HdcUSBBase {
public:
    HdcDaemonUSB(const bool serverOrDaemonIn, void *ptrMainBase);
    virtual ~HdcDaemonUSB();
    int Initial();
    void Stop();
    int SendUSBRaw(HSession hSession, uint8_t *data, const int length);
    void OnNewHandshakeOK(const uint32_t sessionId);

private:
    static void OnUSBRead(uv_fs_t *req);
    static void WatchEPTimer(uv_timer_t *handle);
    int ConnectEPPoint(HUSB hUSB);
    int DispatchToWorkThread(HSession hSession, const uint32_t sessionId, uint8_t *readBuf, int readBytes);
    bool AvailablePacket(uint8_t *ioBuf, uint32_t *sessionId);
    void CloseEndpoint(HUSB hUSB);
    bool BeginEPRead(HSession hSession, uv_loop_t *loopDepend);
    bool ReadyForWorkThread(HSession hSession);
    int SendUSBIOSync(HSession hSession, HUSB hMainUSB, uint8_t *data, const int length);
    int LoopUSBRead(HSession hSession);
    HSession PrepareNewSession(uint8_t *pRecvBuf, int recvBytesIO);
    bool JumpAntiquePacket(const uint8_t &buf, ssize_t bytes) const;

    HSession usbMain;
    uint32_t currentSessionId = 0;  // USB mode,limit only one session
    uv_mutex_t sendEP;
    uv_timer_t checkEP;  // server-use
};
}  // namespace Hdc
#endif
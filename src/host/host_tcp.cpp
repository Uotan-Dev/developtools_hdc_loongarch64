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
#include "host_tcp.h"
#include "server.h"

namespace Hdc {
HdcHostTCP::HdcHostTCP(const bool serverOrDaemonIn, void *ptrMainBase)
    : HdcTCPBase(serverOrDaemonIn, ptrMainBase)
{
    broadcastFindWorking = false;
}

HdcHostTCP::~HdcHostTCP()
{
    WRITE_LOG(LOG_DEBUG, "~HdcHostTCP");
}

void HdcHostTCP::Stop()
{
}

void HdcHostTCP::RecvUDPEntry(const sockaddr *addrSrc, uv_udp_t *handle, const uv_buf_t *rcvbuf)
{
    char bufString[BUF_SIZE_TINY];
    uint16_t port = 0;
    char *p = strstr(rcvbuf->base, "-");
    if (!p) {
        return;
    }
    port = atoi(p + 1);
    if (!port) {
        return;
    }
    uv_ip4_name((sockaddr_in *)addrSrc, bufString, sizeof(bufString));
    string addrPort = string(bufString);
    addrPort += string(":") + std::to_string(port);
    lstDaemonResult.push_back(addrPort);
}

void HdcHostTCP::BroadcastTimer(uv_idle_t *handle)
{
    uv_stop(handle->loop);
}

// Executive Administration Network Broadcast Discovery, broadcastLanIP==which interface to broadcast
void HdcHostTCP::BroadcatFindDaemon(const char *broadcastLanIP)
{
    if (broadcastFindWorking) {
        return;
    }
    broadcastFindWorking = true;
    lstDaemonResult.clear();

    uv_loop_t loopBroadcast;
    uv_loop_init(&loopBroadcast);
    struct sockaddr_in addr;
    uv_udp_send_t req;
    uv_udp_t client;
    // send
    uv_ip4_addr(broadcastLanIP, 0, &addr);
    uv_udp_init(&loopBroadcast, &client);
    uv_udp_bind(&client, (const struct sockaddr *)&addr, 0);
    uv_udp_set_broadcast(&client, 1);
    uv_ip4_addr("255.255.255.255", DEFAULT_PORT, &addr);
    uv_buf_t buf = uv_buf_init((char *)HANDSHAKE_MESSAGE.c_str(), HANDSHAKE_MESSAGE.size());
    uv_udp_send(&req, &client, &buf, 1, (const struct sockaddr *)&addr, nullptr);
    // recv
    uv_udp_t server;
    server.data = this;
    uv_ip4_addr(broadcastLanIP, DEFAULT_PORT, &addr);
    uv_udp_init(&loopBroadcast, &server);
    uv_udp_bind(&server, (const struct sockaddr *)&addr, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&server, AllocStreamUDP, RecvUDP);
    // find timeout
    uv_timer_t tLastCheck;
    uv_timer_init(&loopBroadcast, &tLastCheck);
    uv_timer_start(&tLastCheck, (uv_timer_cb)BroadcastTimer, 1000,
        0);  // 3s timeout //debug 1s

    uv_run(&loopBroadcast, UV_RUN_DEFAULT);
    uv_loop_close(&loopBroadcast);
    broadcastFindWorking = false;
}

void HdcHostTCP::Connect(uv_connect_t *connection, int status)
{
    HSession hSession = (HSession)connection->data;
    delete connection;
    HdcSessionBase *ptrConnect = (HdcSessionBase *)hSession->classInstance;
    uint8_t *byteFlag = nullptr;
    if (status < 0) {
        goto Finish;
    }
    Base::SetTcpOptions((uv_tcp_t *)&hSession->hWorkTCP);
    WRITE_LOG(LOG_DEBUG, "HdcHostTCP::Connect");
    Base::StartWorkThread(&ptrConnect->loopMain, ptrConnect->SessionWorkThread, Base::FinishWorkThread, hSession);
    // wait for thread up
    while (hSession->childLoop.active_handles == 0) {
        uv_sleep(1);
    }
    // junk data to pullup acceptchild
    if (uv_fileno((const uv_handle_t *)&hSession->hWorkTCP, &hSession->fdChildWorkTCP)) {
        goto Finish;
    }
#ifdef UNIT_TEST
    hSession->fdChildWorkTCP = dup(hSession->fdChildWorkTCP);
#endif
    // The main thread is no longer read, handed over to the Child thread
    uv_read_stop((uv_stream_t *)&hSession->hWorkTCP);
    byteFlag = new uint8_t[1];  // free by SendCallback
    *byteFlag = SP_START_SESSION;
    Base::SendToStreamEx((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], (uint8_t *)byteFlag, 1, nullptr,
        (void *)Base::SendCallback, byteFlag);
    return;
Finish:
    WRITE_LOG(LOG_DEBUG, "Connect failed");
    ptrConnect->FreeSession(hSession->sessionId);
}

HSession HdcHostTCP::ConnectDaemon(const string &connectKey)
{
    char ip[BUF_SIZE_TINY] = "";
    uint16_t port = 0;
    if (Base::ConnectKey2IPPort(connectKey.c_str(), ip, &port) < 0) {
        return nullptr;
    }

    HdcSessionBase *ptrConnect = (HdcSessionBase *)clsMainBase;
    HSession hSession = ptrConnect->MallocSession(true, CONN_TCP, this);
    if (!hSession) {
        return nullptr;
    }
    hSession->connectKey = connectKey;
    struct sockaddr_in dest;
    uv_ip4_addr(ip, port, &dest);
    uv_connect_t *conn = new uv_connect_t();
    conn->data = hSession;
    uv_tcp_connect(conn, (uv_tcp_t *)&hSession->hWorkTCP, (const struct sockaddr *)&dest, Connect);
    return hSession;
}

void HdcHostTCP::FindLanDaemon()
{
    uv_interface_address_t *info;
    int count, i;
    char ipAddr[BUF_SIZE_TINY] = "";
    if (broadcastFindWorking) {
        return;
    }
    lstDaemonResult.clear();
    uv_interface_addresses(&info, &count);
    i = count;
    while (i--) {
        uv_interface_address_t interface = info[i];
        if (interface.address.address4.sin_family == AF_INET6) {
            continue;
        }
        uv_ip4_name(&interface.address.address4, ipAddr, sizeof(ipAddr));
        BroadcatFindDaemon(ipAddr);
    }
    uv_free_interface_addresses(info, count);
}
}  // namespace Hdc
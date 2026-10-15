// SPDX-License-Identifier: CC0-1.0
// Public-domain example code (CC0) - see ../LICENSE.
// =============================================================================
// SimulationControl.cpp - SIMULATION ONLY, TEST ONLY. See SimulationControl.h.
// =============================================================================

#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS // sscanf/getenv: bounded here (%15s, a fixed-size buffer), so MSVC's C4996 is noise
#endif

#include "SimulationControl.h"
#include "SimulatedForeignBus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET ControlSocket;
static const ControlSocket NO_SOCKET = INVALID_SOCKET;
#define CloseControlSocket closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int ControlSocket;
static const ControlSocket NO_SOCKET = -1;
#define CloseControlSocket close
#endif

namespace {

const char* CONTROL_PORT_VARIABLE = "BGW_SIMULATION_CONTROL_PORT";
ControlSocket g_controlSocket = NO_SOCKET;

bool ParseTable(const char* text, ForeignBus::Table* table) {
    if (strcmp(text, "input") == 0)   { *table = ForeignBus::Table::InputRegister;   return true; }
    if (strcmp(text, "holding") == 0) { *table = ForeignBus::Table::HoldingRegister; return true; }
    if (strcmp(text, "coil") == 0)    { *table = ForeignBus::Table::Coil;            return true; }
    return false;
}

// Runs one command; writes the answer into reply.
void RunCommand(const char* command, char* reply, size_t replyLength) {
    char verb[16] = { 0 };
    char tableText[16] = { 0 };
    unsigned unitId = 0;
    unsigned address = 0;
    unsigned value = 0;
    ForeignBus::Table table = ForeignBus::Table::InputRegister;

    const int fieldCount = sscanf(command, "%15s %u %15s %u %u", verb, &unitId, tableText, &address, &value);
    if (fieldCount < 2 || unitId > 255 || address > 65535 || value > 65535) {
        snprintf(reply, replyLength, "error out of range or malformed");
        return;
    }
    if (fieldCount >= 2 && (strcmp(verb, "offline") == 0 || strcmp(verb, "online") == 0)) {
        const bool online = strcmp(verb, "online") == 0;
        if (!SimulatedForeignBus::SetSlaveOnline((uint8_t)unitId, online)) {
            snprintf(reply, replyLength, "error unknown unit %u", unitId);
            return;
        }
        printf("SIMULATION (test control): slave %u (%s) is now %s.\n", unitId,
               SimulatedForeignBus::SlaveName((uint8_t)unitId), online ? "ONLINE" : "OFFLINE");
        snprintf(reply, replyLength, "ok");
        return;
    }
    if (fieldCount == 5 && strcmp(verb, "set") == 0 && ParseTable(tableText, &table)) {
        if (!SimulatedForeignBus::SetPoint((uint8_t)unitId, table, (uint16_t)address, (uint16_t)value)) {
            snprintf(reply, replyLength, "error no such point");
            return;
        }
        printf("SIMULATION (test control): slave %u %s %u = %u.\n", unitId,
               ForeignBus::TableName(table), address, value);
        snprintf(reply, replyLength, "ok");
        return;
    }
    if (fieldCount == 4 && strcmp(verb, "get") == 0 && ParseTable(tableText, &table)) {
        uint16_t pointValue = 0;
        if (!SimulatedForeignBus::GetPoint((uint8_t)unitId, table, (uint16_t)address, &pointValue)) {
            snprintf(reply, replyLength, "error no such point");
            return;
        }
        snprintf(reply, replyLength, "value %u", (unsigned)pointValue);
        return;
    }
    snprintf(reply, replyLength, "error unknown command");
}

} // namespace

namespace SimulationControl {

bool Start() {
    const char* portText = getenv(CONTROL_PORT_VARIABLE);
    if (portText == NULL || portText[0] == '\0') {
        return true; // off - the normal case
    }
    const long port = strtol(portText, NULL, 10);
    if (port <= 0 || port > 65535) {
        printf("Error: %s=%s is not a UDP port.\n", CONTROL_PORT_VARIABLE, portText);
        return false;
    }
    g_controlSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_controlSocket == NO_SOCKET) {
        return false;
    }
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only, never the network
    if (bind(g_controlSocket, (sockaddr*)&address, sizeof(address)) != 0) {
        printf("Error: could not bind the simulation control socket to 127.0.0.1:%ld.\n", port);
        CloseControlSocket(g_controlSocket);
        g_controlSocket = NO_SOCKET;
        return false;
    }
#if defined(_WIN32)
    u_long nonBlocking = 1;
    ioctlsocket(g_controlSocket, FIONBIO, &nonBlocking);
#else
    fcntl(g_controlSocket, F_SETFL, fcntl(g_controlSocket, F_GETFL, 0) | O_NONBLOCK);
#endif
    printf("SIMULATION: test control channel listening on 127.0.0.1:%ld (%s).\n", port, CONTROL_PORT_VARIABLE);
    return true;
}

void Service() {
    if (g_controlSocket == NO_SOCKET) {
        return;
    }
    char command[128];
    sockaddr_in sender;
#if defined(_WIN32)
    int senderLength = sizeof(sender);
#else
    socklen_t senderLength = sizeof(sender);
#endif
    const int received = (int)recvfrom(g_controlSocket, command, sizeof(command) - 1, 0,
                                       (sockaddr*)&sender, &senderLength);
    if (received <= 0) {
        return;
    }
    command[received] = '\0';
    char reply[64];
    RunCommand(command, reply, sizeof(reply));
    sendto(g_controlSocket, reply, (int)strlen(reply), 0, (sockaddr*)&sender, senderLength);
}

void Stop() {
    if (g_controlSocket != NO_SOCKET) {
        CloseControlSocket(g_controlSocket);
        g_controlSocket = NO_SOCKET;
    }
}

} // namespace SimulationControl

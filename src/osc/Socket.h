// Socket.h — platform socket includes + tiny helpers shared by OscEndpoint / OscSender.
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET evobox_socket_t;
#define EVOBOX_INVALID_SOCKET INVALID_SOCKET
#define EVOBOX_CLOSESOCKET(s) closesocket(s)
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int evobox_socket_t;
#define EVOBOX_INVALID_SOCKET (-1)
#define EVOBOX_CLOSESOCKET(s) ::close(s)
#endif

namespace evobox
{
// Windows needs WSAStartup once per process; no-op elsewhere. Safe to call repeatedly.
void socketsInit();
} // namespace evobox

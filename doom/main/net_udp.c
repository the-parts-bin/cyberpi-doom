// Chocolate Doom's UDP network module (net_sdl.c) on lwIP sockets instead of
// SDL_net. It keeps the name net_sdl_module, which d_loop.c, net_server.c and
// net_query.c refer to.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"
#include "net_defs.h"
#include "net_io.h"
#include "net_packet.h"
#include "net_sdl.h"

#define DEFAULT_PORT 2342

// Addresses are handed to the netcode as pointers that must stay valid, so
// each peer gets a fixed slot. Only the host's server and up to three
// clients talk, so a small table is plenty; packets from anyone beyond it
// are dropped.
#define MAX_ADDRS 16

typedef struct {
    net_addr_t net_addr;
    struct sockaddr_in sa;
    boolean used;
} addrpair_t;

static addrpair_t s_addrs[MAX_ADDRS];
static int s_sock = -1;
static int s_port = DEFAULT_PORT;

static net_addr_t *FindAddress(const struct sockaddr_in *sa)
{
    addrpair_t *empty = NULL;

    for (int i = 0; i < MAX_ADDRS; i++) {
        addrpair_t *a = &s_addrs[i];
        if (a->used && a->sa.sin_addr.s_addr == sa->sin_addr.s_addr && a->sa.sin_port == sa->sin_port)
            return &a->net_addr;
        if (!a->used && empty == NULL)
            empty = a;
    }
    if (empty == NULL)
        return NULL;

    empty->sa = *sa;
    empty->used = true;
    empty->net_addr.handle = &empty->sa;
    empty->net_addr.module = &net_sdl_module;
    return &empty->net_addr;
}

static void FreeAddress(net_addr_t *addr)
{
    for (int i = 0; i < MAX_ADDRS; i++) {
        if (addr == &s_addrs[i].net_addr) {
            s_addrs[i].used = false;
            return;
        }
    }
    I_Error("NET_UDP_FreeAddress: Attempted to remove an unused address!");
}

static boolean OpenSocket(int bind_port)
{
    int p = M_CheckParmWithArgs("-port", 1);
    if (p > 0)
        s_port = atoi(myargv[p + 1]);

    if (s_sock >= 0)
        return true;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0)
        return false;

    int one = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = htons(bind_port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(s_sock);
        s_sock = -1;
        return false;
    }
    return true;
}

static boolean InitClient(void)
{
    if (!OpenSocket(0))
        I_Error("NET_UDP_InitClient: Unable to open a socket!");
    return true;
}

static boolean InitServer(void)
{
    if (!OpenSocket(s_port))
        I_Error("NET_UDP_InitServer: Unable to bind to port %i", s_port);
    return true;
}

static void SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    struct sockaddr_in sa;

    if (addr == &net_broadcast_addr) {
        sa = (struct sockaddr_in){
            .sin_family = AF_INET,
            .sin_port = htons(s_port),
            .sin_addr.s_addr = htonl(INADDR_BROADCAST),
        };
    } else {
        sa = *(struct sockaddr_in *)addr->handle;
    }

    // UDP is unreliable anyway and the netcode resends what's missing, so a
    // full buffer is not an error.
    sendto(s_sock, packet->data, packet->len, 0, (struct sockaddr *)&sa, sizeof(sa));
}

static boolean RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    static uint8_t buf[1500];

    for (;;) {
        struct sockaddr_in sa;
        socklen_t sa_len = sizeof(sa);
        int len = recvfrom(s_sock, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr *)&sa, &sa_len);
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                I_Error("NET_UDP_RecvPacket: Error receiving packet: %d", errno);
            return false;
        }

        *addr = FindAddress(&sa);
        if (*addr == NULL)
            continue; // table full: ignore this sender

        *packet = NET_NewPacket(len);
        memcpy((*packet)->data, buf, len);
        (*packet)->len = len;
        return true;
    }
}

static void AddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{
    const struct sockaddr_in *sa = addr->handle;
    char ip[16];

    inet_ntoa_r(sa->sin_addr, ip, sizeof(ip));
    if (ntohs(sa->sin_port) == DEFAULT_PORT)
        M_snprintf(buffer, buffer_len, "%s", ip);
    else
        M_snprintf(buffer, buffer_len, "%s:%i", ip, ntohs(sa->sin_port));
}

// Numeric addresses only: there is no DNS on the game's access point.
static net_addr_t *ResolveAddress(char *address)
{
    char host[32];
    int port = s_port;

    M_StringCopy(host, address, sizeof(host));
    char *colon = strchr(host, ':');
    if (colon != NULL) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (inet_aton(host, &sa.sin_addr) == 0)
        return NULL;
    return FindAddress(&sa);
}

net_module_t net_sdl_module = {
    InitClient,
    InitServer,
    SendPacket,
    RecvPacket,
    AddrToString,
    FreeAddress,
    ResolveAddress,
};

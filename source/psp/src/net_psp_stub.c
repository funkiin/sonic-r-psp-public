#include <stdint.h>
#include <string.h>

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "net_transport.h"
#include "net/matchmaker.h"

static char s_slotNames[NET_MAX_PLAYERS][MM_MAX_USERNAME];
static char s_slotPlatforms[NET_MAX_PLAYERS][16];
static uint8_t s_slotRegions[NET_MAX_PLAYERS];
static char s_fallbackUsername[MM_MAX_USERNAME];

int net_transport_init(void) { return 0; }
int net_host_start(int port) { (void)port; return -1; }
int net_client_connect(const char *host_ip, int port) { (void)host_ip; (void)port; return -1; }
int net_send_to_host(const void *data, int len) { (void)data; (void)len; return -1; }
int net_broadcast(const void *data, int len) { (void)data; (void)len; return -1; }
int net_send_to(int player_slot, const void *data, int len)
{
    (void)player_slot; (void)data; (void)len;
    return -1;
}
int net_recv(void *buf, int maxlen, int *from_slot)
{
    (void)buf; (void)maxlen;
    if (from_slot) {
        *from_slot = -1;
    }
    return 0;
}
int net_register_client(int slot) { (void)slot; return -1; }
int net_discover_send(int port) { (void)port; return -1; }
int net_discover_check(char *host_ip, int host_ip_len)
{
    if (host_ip && host_ip_len > 0) {
        host_ip[0] = '\0';
    }
    return 0;
}
int net_is_active(void) { return 0; }
int net_is_host(void) { return 0; }
int net_local_slot(void) { return 0; }
void net_set_local_slot(int slot) { (void)slot; }
void net_close(void) {}
void net_unregister_slot(int slot) { (void)slot; }
int net_slot_is_connected(int slot) { return slot == 0; }
int net_probe_ping(const char *ip, int port, int timeout_ms)
{
    (void)ip; (void)port; (void)timeout_ms;
    return -1;
}

void NetRecvThread_Start(void) {}
void NetRecvThread_Stop(void) {}
void NetLevelSyncBarrier(void) {}
void NetInterpRecord(int playerIdx) { (void)playerIdx; }
void NetInterpApply(void) {}

void net_set_slot_name(int slot, const char *name)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) {
        return;
    }
    if (!name) {
        name = "";
    }
    strncpy(s_slotNames[slot], name, MM_MAX_USERNAME - 1);
    s_slotNames[slot][MM_MAX_USERNAME - 1] = '\0';
}

const char *net_get_slot_name(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) {
        return "";
    }
    return s_slotNames[slot];
}

void net_set_slot_platform(int slot, const char *platform, uint8_t region)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) {
        return;
    }
    if (!platform) {
        platform = "";
    }
    strncpy(s_slotPlatforms[slot], platform, sizeof(s_slotPlatforms[slot]) - 1);
    s_slotPlatforms[slot][sizeof(s_slotPlatforms[slot]) - 1] = '\0';
    s_slotRegions[slot] = region;
}

const char *net_get_slot_platform(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) {
        return "";
    }
    return s_slotPlatforms[slot];
}

uint8_t net_get_slot_region(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) {
        return 0;
    }
    return s_slotRegions[slot];
}

int net_lobby_mode_b(void) { return 0; }
void net_lobby_set_mode_b(int on) { (void)on; }
void net_lobby_set_mode_byte(unsigned char v) { (void)v; }

void net_deco_set_name(char *entry, const char *name)
{
    if (!entry) {
        return;
    }
    if (!name) {
        name = "";
    }
    strncpy(entry, name, 15);
    entry[15] = '\0';
}

void UpdateNetworkHost(void) {}
void SendNetworkHostData(void) {}
void UpdateNetworkClient(void) {}
void SendNetworkClientData(void) {}
void UpdateNetworkSync(const void *data, int len) { (void)data; (void)len; }
void SendNetworkPacket(const void *data, int len) { (void)data; (void)len; }
void WaitForNetworkData(void) {}
void ProcessNetworkGameData(void) {}
void ApplyNetworkPlayerState(void) {}
void CloseDirectPlaySession(void) { g_netSessionActive = 0; }
void CloseDirectPlayLobby(void) {}
void InitNetworkGame(void) {}
int net_should_run_physics(int playerIdx) { (void)playerIdx; return 1; }
int IsDirectPlayAvailable(void) { return 0; }
void EnumNetworkSessions(int flag) { (void)flag; }
void StartNetworkThread(void) {}
int OpenNetworkSession(int sessionDesc) { (void)sessionDesc; return 0; }
int BuildNetworkPhoneNumber(void) { return 0; }

int MatchmakerInit(void) { return 0; }
void MatchmakerShutdown(void) {}
int MatchmakerLoadToken(void) { return 0; }
void MatchmakerSaveToken(void) {}
int MatchmakerHasToken(void) { return 0; }
int MatchmakerRequestCode(void) { return 0; }
int MatchmakerPollToken(void) { return 0; }
int MatchmakerRefreshToken(void) { return 0; }
int MatchmakerGenerateQR(uint8_t *qrBuf, int *qrSize)
{
    (void)qrBuf;
    if (qrSize) {
        *qrSize = 0;
    }
    return 0;
}
int MatchmakerListSessions(MatchmakerSessionList *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return 0;
}
int MatchmakerCreateSession(const char *username, int port)
{
    (void)username; (void)port;
    return 0;
}
int MatchmakerKeepAlive(int player_count, const char *status)
{
    (void)player_count; (void)status;
    return 0;
}
void MatchmakerClearSession(void) {}
int UpnpOpenPort(int port) { (void)port; return 0; }
void UpnpClosePort(int port) { (void)port; }
const char *MatchmakerGetCode(void) { return ""; }
const char *MatchmakerGetToken(void) { return ""; }
const char *MatchmakerGetUsername(void) { return s_fallbackUsername; }
int64_t MatchmakerGetSessionId(void) { return -1; }
void MatchmakerSetFallbackUsername(const char *name)
{
    if (!name) {
        s_fallbackUsername[0] = '\0';
        return;
    }
    strncpy(s_fallbackUsername, name, sizeof(s_fallbackUsername) - 1);
    s_fallbackUsername[sizeof(s_fallbackUsername) - 1] = '\0';
}
void MatchmakerClearToken(void) {}

#include "net_upnp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define UPNP_CLOSE closesocket
#define UPNP_INVALID INVALID_SOCKET
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
typedef int sock_t;
#define UPNP_CLOSE close
#define UPNP_INVALID (-1)
#endif

extern void DebugLog(const char *fmt, ...);

/* ─── URL parsing ───────────────────────────────────────────────── */

typedef struct {
    char host[128];
    char port[8];
    char path[256];
} UrlParts;

static int parse_http_url(const char *url, UrlParts *out)
{
    memset(out, 0, sizeof(*out));
    const char *after = strstr(url, "//");
    if (!after) return 0;
    after += 2;

    const char *slash = strchr(after, '/');
    const char *colon = strchr(after, ':');

    if (colon && (!slash || colon < slash)) {
        int hlen = (int)(colon - after);
        if (hlen >= (int)sizeof(out->host)) hlen = (int)sizeof(out->host) - 1;
        memcpy(out->host, after, hlen);
        const char *pstart = colon + 1;
        const char *pend = slash ? slash : pstart + strlen(pstart);
        int plen = (int)(pend - pstart);
        if (plen >= (int)sizeof(out->port)) plen = (int)sizeof(out->port) - 1;
        memcpy(out->port, pstart, plen);
    } else {
        const char *hend = slash ? slash : after + strlen(after);
        int hlen = (int)(hend - after);
        if (hlen >= (int)sizeof(out->host)) hlen = (int)sizeof(out->host) - 1;
        memcpy(out->host, after, hlen);
        strcpy(out->port, "80");
    }

    if (slash)
        strncpy(out->path, slash, sizeof(out->path) - 1);
    else
        strcpy(out->path, "/");

    return 1;
}

/* ─── Get local IP via UDP connect trick ────────────────────────── */

static int get_local_ip(const char *target_ip, char *out, int maxlen)
{
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == UPNP_INVALID) return 0;

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(target_ip);
    serv.sin_port = htons(1900);

    if (connect(s, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        UPNP_CLOSE(s);
        return 0;
    }

    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (getsockname(s, (struct sockaddr *)&name, &namelen) < 0) {
        UPNP_CLOSE(s);
        return 0;
    }
    UPNP_CLOSE(s);

    if (name.sin_addr.s_addr == 0) return 0;

    const char *p = inet_ntop(AF_INET, &name.sin_addr, out, maxlen);
    return p != NULL;
}

/* ─── Simple HTTP GET (plain, for LAN device description) ───────── */

static int http_get_body(const char *url, char *buf, int bufsize)
{
    UrlParts parts;
    if (!parse_http_url(url, &parts)) return -1;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(parts.host, parts.port, &hints, &res) != 0) return -1;

    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == UPNP_INVALID) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(s, res->ai_addr, (int)res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        UPNP_CLOSE(s);
        return -1;
    }
    freeaddrinfo(res);

    char req[512];
    int reqlen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s:%s\r\nConnection: close\r\n\r\n",
        parts.path, parts.host, parts.port);

    int total = 0;
    while (total < reqlen) {
        int n = (int)send(s, req + total, reqlen - total, 0);
        if (n <= 0) { UPNP_CLOSE(s); return -1; }
        total += n;
    }

    int len = 0;
    for (;;) {
        int n = (int)recv(s, buf + len, bufsize - len - 1, 0);
        if (n <= 0) break;
        len += n;
    }
    buf[len] = '\0';
    UPNP_CLOSE(s);

    char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
        int bodylen = len - (int)(body - buf);
        memmove(buf, body, bodylen);
        buf[bodylen] = '\0';
        return bodylen;
    }
    return len;
}

/* ─── SSDP discovery + device description fetch ─────────────────── */

static const char SSDP_SEARCH[] =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST:239.255.255.250:1900\r\n"
    "MAN:\"ssdp:discover\"\r\n"
    "ST:urn:schemas-upnp-org:service:WANIPConnection:1\r\n"
    "MX:3\r\n"
    "\r\n";

static int extract_base_url(const char *url, char *base, int maxlen)
{
    const char *after = strstr(url, "//");
    if (!after) return 0;
    after += 2;
    const char *slash = strchr(after, '/');
    int len = slash ? (int)(slash - url) : (int)strlen(url);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(base, url, len);
    base[len] = '\0';
    return 1;
}

static int extract_host_ip(const char *url, char *ip, int maxlen)
{
    const char *after = strstr(url, "//");
    if (!after) return 0;
    after += 2;
    const char *end = strchr(after, ':');
    if (!end) end = strchr(after, '/');
    if (!end) end = after + strlen(after);
    int len = (int)(end - after);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(ip, after, len);
    ip[len] = '\0';
    return 1;
}

static int ssdp_discover(char *control_url, int maxlen, char *router_ip, int ip_maxlen)
{
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == UPNP_INVALID) return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &addr.sin_addr);

#ifdef _WIN32
    DWORD tv = 3000;
#else
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    int sent = (int)sendto(s, SSDP_SEARCH, (int)strlen(SSDP_SEARCH), 0,
                           (struct sockaddr *)&addr, sizeof(addr));
    if (sent <= 0) {
        DebugLog("UPnP: SSDP sendto failed (%d)\n", errno);
        UPNP_CLOSE(s);
        return 0;
    }

    char buf[2048];
    int n = (int)recv(s, buf, sizeof(buf) - 1, 0);
    UPNP_CLOSE(s);

    if (n <= 0) {
        DebugLog("UPnP: no SSDP response\n");
        return 0;
    }
    buf[n] = '\0';

    if (!strstr(buf, "WANIPConnection:1")) {
        DebugLog("UPnP: SSDP response missing WANIPConnection:1\n");
        return 0;
    }

    const char *loc = strstr(buf, "LOCATION: ");
    if (!loc) loc = strstr(buf, "Location: ");
    if (!loc) {
        DebugLog("UPnP: no LOCATION in SSDP response\n");
        return 0;
    }

    loc += 10;
    const char *end = strstr(loc, "\r\n");
    if (!end) end = strchr(loc, '\n');
    if (!end) return 0;

    int len = (int)(end - loc);
    while (len > 0 && (loc[len - 1] == ' ' || loc[len - 1] == '\r')) len--;

    char location_url[512];
    if (len >= (int)sizeof(location_url)) len = (int)sizeof(location_url) - 1;
    memcpy(location_url, loc, len);
    location_url[len] = '\0';

    DebugLog("UPnP: SSDP LOCATION: %s\n", location_url);

    extract_host_ip(location_url, router_ip, ip_maxlen);

    char base_url[256];
    extract_base_url(location_url, base_url, sizeof(base_url));

    char desc_xml[4096];
    int xml_len = http_get_body(location_url, desc_xml, sizeof(desc_xml));
    if (xml_len <= 0) {
        DebugLog("UPnP: failed to fetch device description\n");
        return 0;
    }

    char *wan = strstr(desc_xml, "WANIPConnection");
    if (!wan) {
        DebugLog("UPnP: WANIPConnection not found in device description\n");
        return 0;
    }

    char *ctrl = strstr(wan, "<controlURL>");
    if (!ctrl) {
        DebugLog("UPnP: <controlURL> not found\n");
        return 0;
    }
    ctrl += 12;

    char *ctrl_end = strstr(ctrl, "</controlURL>");
    if (!ctrl_end) return 0;

    int ctrl_len = (int)(ctrl_end - ctrl);

    if (ctrl[0] == 'h' && strncmp(ctrl, "http", 4) == 0) {
        if (ctrl_len >= maxlen) ctrl_len = maxlen - 1;
        memcpy(control_url, ctrl, ctrl_len);
        control_url[ctrl_len] = '\0';
    } else {
        int base_len = (int)strlen(base_url);
        if (base_len + ctrl_len >= maxlen) return 0;
        memcpy(control_url, base_url, base_len);
        memcpy(control_url + base_len, ctrl, ctrl_len);
        control_url[base_len + ctrl_len] = '\0';
    }

    DebugLog("UPnP: control URL: %s\n", control_url);
    return 1;
}

/* ─── SOAP request over plain HTTP ──────────────────────────────── */

static int soap_request(const char *control_url, const char *action,
                        const char *body, char *resp_buf, int resp_size)
{
    UrlParts parts;
    if (!parse_http_url(control_url, &parts)) return -1;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(parts.host, parts.port, &hints, &res) != 0) return -1;

    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == UPNP_INVALID) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(s, res->ai_addr, (int)res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        UPNP_CLOSE(s);
        return -1;
    }
    freeaddrinfo(res);

    int bodylen = (int)strlen(body);

    char req[2048];
    int off = 0;
    off += snprintf(req + off, sizeof(req) - off,
        "POST %s HTTP/1.0\r\n", parts.path);
    off += snprintf(req + off, sizeof(req) - off,
        "Host: %s:%s\r\n", parts.host, parts.port);
    off += snprintf(req + off, sizeof(req) - off,
        "Content-Type: text/xml; charset=\"utf-8\"\r\n");
    off += snprintf(req + off, sizeof(req) - off,
        "SOAPACTION: \"urn:schemas-upnp-org:service:WANIPConnection:1#%s\"\r\n", action);
    off += snprintf(req + off, sizeof(req) - off,
        "Content-Length: %d\r\n", bodylen);
    off += snprintf(req + off, sizeof(req) - off, "\r\n");

    int total = 0;
    while (total < off) {
        int n = (int)send(s, req + total, off - total, 0);
        if (n <= 0) { UPNP_CLOSE(s); return -1; }
        total += n;
    }

    total = 0;
    while (total < bodylen) {
        int n = (int)send(s, body + total, bodylen - total, 0);
        if (n <= 0) { UPNP_CLOSE(s); return -1; }
        total += n;
    }

    int resp_len = 0;
    for (;;) {
        int n = (int)recv(s, resp_buf + resp_len, resp_size - resp_len - 1, 0);
        if (n <= 0) break;
        resp_len += n;
    }
    resp_buf[resp_len] = '\0';
    UPNP_CLOSE(s);

    const char *sp = strstr(resp_buf, "HTTP/");
    if (!sp) return -1;
    sp = strchr(sp, ' ');
    if (!sp) return -1;
    return atoi(sp + 1);
}

/* ─── State ─────────────────────────────────────────────────────── */

static char s_controlUrl[512];
static int  s_portOpen = 0;
static int  s_openedPort = 0;

/* ─── Public API ────────────────────────────────────────────────── */

int net_upnp_open_port(int port)
{
    char router_ip[64] = {0};
    if (!ssdp_discover(s_controlUrl, sizeof(s_controlUrl), router_ip, sizeof(router_ip)))
        return 0;

    char local_ip[64];
    if (!get_local_ip(router_ip, local_ip, sizeof(local_ip))) {
        DebugLog("UPnP: couldn't determine local IP\n");
        return 0;
    }
    DebugLog("UPnP: local IP = %s\n", local_ip);

    char body[1024];
    snprintf(body, sizeof(body),
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        "<s:Body>\r\n"
        "<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\r\n"
        "  <NewRemoteHost></NewRemoteHost>\r\n"
        "  <NewExternalPort>%d</NewExternalPort>\r\n"
        "  <NewProtocol>UDP</NewProtocol>\r\n"
        "  <NewInternalPort>%d</NewInternalPort>\r\n"
        "  <NewInternalClient>%s</NewInternalClient>\r\n"
        "  <NewEnabled>1</NewEnabled>\r\n"
        "  <NewPortMappingDescription>SonicR</NewPortMappingDescription>\r\n"
        "  <NewLeaseDuration>3600</NewLeaseDuration>\r\n"
        "</u:AddPortMapping>\r\n"
        "</s:Body>\r\n"
        "</s:Envelope>",
        port, port, local_ip);

    char resp[2048];
    int status = soap_request(s_controlUrl, "AddPortMapping", body, resp, sizeof(resp));

    if (status == 200) {
        DebugLog("UPnP: port %d opened successfully\n", port);
        s_portOpen = 1;
        s_openedPort = port;
        return 1;
    }

    if (status == 500 && strstr(resp, "725")) {
        DebugLog("UPnP: router rejected lease=3600, retrying with lease=0\n");
        char *lease = strstr(body, "<NewLeaseDuration>3600</NewLeaseDuration>");
        if (lease) memcpy(lease + 18, "0   ", 4);

        status = soap_request(s_controlUrl, "AddPortMapping", body, resp, sizeof(resp));
        if (status == 200) {
            DebugLog("UPnP: port %d opened (permanent lease)\n", port);
            s_portOpen = 1;
            s_openedPort = port;
            return 1;
        }
    }

    DebugLog("UPnP: AddPortMapping failed (HTTP %d)\n", status);
    return 0;
}

void net_upnp_close_port(int port)
{
    if (!s_portOpen || s_openedPort != port) return;

    char body[512];
    snprintf(body, sizeof(body),
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        "<s:Body>\r\n"
        "<u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\r\n"
        "  <NewRemoteHost></NewRemoteHost>\r\n"
        "  <NewExternalPort>%d</NewExternalPort>\r\n"
        "  <NewProtocol>UDP</NewProtocol>\r\n"
        "</u:DeletePortMapping>\r\n"
        "</s:Body>\r\n"
        "</s:Envelope>",
        port);

    char resp[2048];
    soap_request(s_controlUrl, "DeletePortMapping", body, resp, sizeof(resp));
    s_portOpen = 0;
    s_openedPort = 0;

    DebugLog("UPnP: port %d close requested\n", port);
}

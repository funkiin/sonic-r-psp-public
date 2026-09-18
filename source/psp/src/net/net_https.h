#ifndef NET_HTTPS_H
#define NET_HTTPS_H

#define HTTPS_RESPONSE_BUF_SIZE 8192
#define HTTPS_TIMEOUT_SEC       10

typedef struct {
    char data[HTTPS_RESPONSE_BUF_SIZE];
    int  len;
    int  status;
} HttpsResponse;

int  net_https_init(void);
void net_https_shutdown(void);

long net_https_get(const char *url, const char *token, HttpsResponse *resp);
long net_https_post(const char *url, const char *token,
                    const char *body, HttpsResponse *resp);

#endif

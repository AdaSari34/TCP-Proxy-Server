#include "proxy.h"

void die_wsa(const char* msg) {
    int e = WSAGetLastError();
    fprintf(stderr, "%s (WSA error: %d)\n", msg, e);
}

int send_all(SOCKET s, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return -1;
        sent += n;
    }
    return 0;
}

void print_client_addr(SOCKET client) {
    struct sockaddr_storage ss;
    int slen = sizeof(ss);
    if (getpeername(client, (struct sockaddr*)&ss, &slen) == 0) {
        char host[NI_MAXHOST], serv[NI_MAXSERV];
        if (getnameinfo((struct sockaddr*)&ss, slen, host, sizeof(host),
            serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            printf("Client connected: %s:%s\n", host, serv);
        }
    }
}

int starts_with_ci(const char* s, const char* prefix) {
    while (*prefix && *s) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
        s++; prefix++;
    }
    return *prefix == '\0';
}

int recv_with_timeout(SOCKET s, char* buf, int cap, int timeout_sec) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int sel = select((int)s + 1, &rfds, NULL, NULL, &tv);
    if (sel == 0) return -2;
    if (sel == SOCKET_ERROR) return -1;
    return recv(s, buf, cap, 0);
}

int recv_until_double_crlf_timeout(SOCKET s, char* out, int out_cap, int timeout_sec) {
    int total = 0;
    out[0] = '\0';

    while (total < out_cap - 1) {
        int n = recv_with_timeout(s, out + total, out_cap - 1 - total, timeout_sec);
        if (n == -2) return -2;
        if (n <= 0) return n;

        total += n;
        out[total] = '\0';

        if (strstr(out, "\r\n\r\n") != NULL) return total;
    }
    return total;
}

int parse_host_port(const char* req, char* host_out, size_t host_out_sz, int* port_out) {
    *port_out = 80;
    host_out[0] = '\0';

    const char* p = req;
    while (*p) {
        const char* line_end = strstr(p, "\r\n");
        if (!line_end) break;
        size_t line_len = (size_t)(line_end - p);
        if (line_len == 0) break;

        if (line_len >= 5 && starts_with_ci(p, "Host:")) {
            const char* v = p + 5;
            while (*v == ' ' || *v == '\t') v++;

            char tmp[512];
            size_t n = line_len - (size_t)(v - p);
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, v, n);
            tmp[n] = '\0';

            char* colon = strchr(tmp, ':');
            if (colon) {
                *colon = '\0';
                *port_out = atoi(colon + 1);
            }
            strncpy_s(host_out, host_out_sz, tmp, _TRUNCATE);
            return (host_out[0] != '\0');
        }
        p = line_end + 2;
    }
    return 0;
}

int parse_connect_host_port(const char* req, char* host_out, int host_out_sz, int* port_out) {
    const char* line_end = strstr(req, "\r\n");
    if (!line_end) return 0;
    int flen = (int)(line_end - req);
    if (flen <= 0 || flen >= 512) return 0;

    char first_line[512];
    memcpy(first_line, req, flen);
    first_line[flen] = '\0';

    const char* prefix = "CONNECT ";
    if (strncmp(first_line, prefix, strlen(prefix)) != 0) return 0;

    const char* p = first_line + strlen(prefix);
    const char* sp = strchr(p, ' ');
    if (!sp) return 0;

    int tlen = (int)(sp - p);
    char target[256];
    memcpy(target, p, tlen);
    target[tlen] = '\0';

    char* colon = strrchr(target, ':');
    if (!colon) return 0;
    *colon = '\0';

    strncpy(host_out, target, host_out_sz - 1);
    *port_out = atoi(colon + 1);
    return 1;
}

int parse_content_length(const char* req) {
    const char* p = req;
    while (*p) {
        const char* line_end = strstr(p, "\r\n");
        if (!line_end) break;
        if (starts_with_ci(p, "Content-Length:")) {
            return atoi(p + 15);
        }
        p = line_end + 2;
    }
    return 0;
}

int rewrite_absolute_form_if_needed(const char* in_req, int in_len, char* out_req, int out_cap) {
    const char* line_end = strstr(in_req, "\r\n");
    if (!line_end) return 0;

    int first_len = (int)(line_end - in_req);
    char first_line[1024];
    if (first_len >= 1024) return 0;
    memcpy(first_line, in_req, first_len); first_line[first_len] = '\0';

    char method[32], target[2048], version[32];
    if (sscanf(first_line, "%31s %2047s %31s", method, target, version) != 3) return 0;

    if (starts_with_ci(target, "http://")) {
        const char* after = target + 7;
        const char* slash = strchr(after, '/');
        const char* path = slash ? slash : "/";

        char new_first[1100];
        snprintf(new_first, sizeof(new_first), "%s %s %s\r\n", method, path, version);

        const char* rest = line_end + 2;
        int rest_len = in_len - (int)(rest - in_req);
        int need = (int)strlen(new_first) + rest_len;
        if (need >= out_cap) return -1;

        memcpy(out_req, new_first, strlen(new_first));
        memcpy(out_req + strlen(new_first), rest, rest_len);
        return need;
    }
    return 0;
}

SOCKET connect_upstream(const char* host, int port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return INVALID_SOCKET;

    SOCKET s = INVALID_SOCKET;
    for (struct addrinfo* it = res; it; it = it->ai_next) {
        s = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, it->ai_addr, (int)it->ai_addrlen) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return s;
}
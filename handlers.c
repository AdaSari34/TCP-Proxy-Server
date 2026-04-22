#include "proxy.h"

void tunnel_relay_with_metrics(SOCKET client, SOCKET upstream, unsigned long long start_ms, const char* target_host, int target_port) {
    char buf[BUF_SIZE];
    unsigned long long bytes_c2s = 0;
    unsigned long long bytes_s2c = 0;
    unsigned long long ttfb_ms = 0;
    int ttfb_recorded = 0;

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client, &rfds);
        FD_SET(upstream, &rfds);

        SOCKET maxfd = (client > upstream) ? client : upstream;
        struct timeval tv;
        tv.tv_sec = IO_TIMEOUT_SEC;
        tv.tv_usec = 0;

        int sel = select((int)maxfd + 1, &rfds, NULL, NULL, &tv);
        if (sel == 0) {
            printf("Timeout: no activity for %d seconds. Closing session.\n", IO_TIMEOUT_SEC);
            break;
        }
        if (sel == SOCKET_ERROR) break;

        if (FD_ISSET(client, &rfds)) {
            int n = recv(client, buf, sizeof(buf), 0);
            if (n <= 0) break;
            if (send_all(upstream, buf, n) < 0) break;
            bytes_c2s += n;
        }

        if (FD_ISSET(upstream, &rfds)) {
            int n = recv(upstream, buf, sizeof(buf), 0);
            if (n <= 0) break;

            if (!ttfb_recorded) {
                ttfb_ms = GetTickCount64() - start_ms;
                ttfb_recorded = 1;
            }
            if (send_all(client, buf, n) < 0) break;
            bytes_s2c += n;
        }
    }

    unsigned long long duration = GetTickCount64() - start_ms;
    printf("\n===== SESSION METRICS =====\n");
    printf("Target: %s:%d\n", target_host, target_port);
    printf("Bytes client->server: %llu\n", bytes_c2s);
    printf("Bytes server->client: %llu\n", bytes_s2c);
    printf("TTFB (ms): %llu\n", ttfb_ms);
    printf("Session duration (ms): %llu\n", duration);
    printf("===========================\n\n");
}

void handle_http(SOCKET client, char* req, int n) {
    char host[256];
    int port = 80;

    if (!parse_host_port(req, host, sizeof(host), &port)) {
        const char* msg = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nMissing Host.\r\n";
        send_all(client, msg, (int)strlen(msg));
        return;
    }

    SOCKET upstream = connect_upstream(host, port);
    if (upstream == INVALID_SOCKET) {
        const char* msg = "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\nConnect failed.\r\n";
        send_all(client, msg, (int)strlen(msg));
        return;
    }

    char req2[BUF_SIZE];
    int rewritten_len = rewrite_absolute_form_if_needed(req, n, req2, BUF_SIZE);
    const char* req_to_send = (rewritten_len > 0) ? req2 : req;
    int req_to_send_len = (rewritten_len > 0) ? rewritten_len : n;

    if (rewritten_len > 0) printf("Rewrote absolute-form request.\n");

    unsigned long long start_ms = GetTickCount64();

    if (send_all(upstream, req_to_send, req_to_send_len) < 0) {
        closesocket(upstream);
        return;
    }

    int content_len = parse_content_length(req_to_send);
    if (content_len > 0) {
        const char* hdr_end = strstr(req_to_send, "\r\n\r\n");
        int hdr_len = hdr_end ? (int)((hdr_end + 4) - req_to_send) : req_to_send_len;
        int remaining = content_len - (req_to_send_len - hdr_len);

        char bbuf[BUF_SIZE];
        while (remaining > 0) {
            int chunk = (remaining > BUF_SIZE) ? BUF_SIZE : remaining;
            int rn = recv_with_timeout(client, bbuf, chunk, IO_TIMEOUT_SEC);
            if (rn <= 0) break;
            send_all(upstream, bbuf, rn);
            remaining -= rn;
        }
    }

    tunnel_relay_with_metrics(client, upstream, start_ms, host, port);

    shutdown(upstream, SD_BOTH);
    closesocket(upstream);
}

void handle_connect(SOCKET client, char* req) {
    char host[256];
    int port = 0;

    if (!parse_connect_host_port(req, host, sizeof(host), &port)) {
        const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
        send_all(client, bad, (int)strlen(bad));
        return;
    }

    SOCKET upstream = connect_upstream(host, port);
    if (upstream == INVALID_SOCKET) {
        const char* fail = "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n";
        send_all(client, fail, (int)strlen(fail));
        return;
    }

    const char* ok = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send_all(client, ok, (int)strlen(ok));
    printf("CONNECT tunnel established to %s:%d\n", host, port);

    unsigned long long start_ms = GetTickCount64();
    tunnel_relay_with_metrics(client, upstream, start_ms, host, port);

    shutdown(upstream, SD_BOTH);
    closesocket(upstream);
}

DWORD WINAPI ProxyThread(LPVOID lpParam) {
    ClientContext* ctx = (ClientContext*)lpParam;
    SOCKET client = ctx->client_sock;
    free(ctx);

    print_client_addr(client);

    char* req = (char*)malloc(BUF_SIZE);
    if (req) {
        int n = recv_until_double_crlf_timeout(client, req, BUF_SIZE, IO_TIMEOUT_SEC);

        if (n > 0) {
            printf("----- REQUEST -----\n%.*s\n-------------------\n", n, req);
            if (starts_with_ci(req, "CONNECT ")) {
                handle_connect(client, req);
            }
            else {
                handle_http(client, req, n);
            }
        }
        else {
            printf("Client closed or timeout before full headers.\n");
        }
        free(req);
    }

    closesocket(client);
    return 0;
}
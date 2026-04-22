#include "proxy.h"

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        die_wsa("socket failed");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        die_wsa("bind failed");
        return 1;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        die_wsa("listen failed");
        return 1;
    }

    printf("FINAL PROJECT: Multi-threaded Proxy listening on 127.0.0.1:%d\n", LISTEN_PORT);
    printf("Features: HTTP Forwarding, HTTPS Tunneling, Metrics, Timeouts\n");

    for (;;) {
        SOCKET client = accept(listen_sock, NULL, NULL);
        if (client == INVALID_SOCKET) {
            printf("accept failed, retrying...\n");
            continue;
        }

        ClientContext* ctx = (ClientContext*)malloc(sizeof(ClientContext));
        if (!ctx) {
            printf("Malloc failed for client context\n");
            closesocket(client);
            continue;
        }
        ctx->client_sock = client;

        HANDLE hThread = CreateThread(NULL, 0, ProxyThread, ctx, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
        else {
            printf("CreateThread failed\n");
            free(ctx);
            closesocket(client);
        }
    }

    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
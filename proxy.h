#ifndef PROXY_H
#define PROXY_H

#define _WIN32_WINNT 0x0601
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")

#define LISTEN_PORT 8080
#define BUF_SIZE 16384
#define IO_TIMEOUT_SEC 30

typedef struct {
    SOCKET client_sock;
} ClientContext;

void die_wsa(const char* msg);
int send_all(SOCKET s, const char* data, int len);
void print_client_addr(SOCKET client);
int starts_with_ci(const char* s, const char* prefix);
int recv_with_timeout(SOCKET s, char* buf, int cap, int timeout_sec);
int recv_until_double_crlf_timeout(SOCKET s, char* out, int out_cap, int timeout_sec);
int parse_host_port(const char* req, char* host_out, size_t host_out_sz, int* port_out);
int parse_connect_host_port(const char* req, char* host_out, int host_out_sz, int* port_out);
int parse_content_length(const char* req);
int rewrite_absolute_form_if_needed(const char* in_req, int in_len, char* out_req, int out_cap);
SOCKET connect_upstream(const char* host, int port);

void tunnel_relay_with_metrics(SOCKET client, SOCKET upstream, unsigned long long start_ms, const char* target_host, int target_port);
void handle_http(SOCKET client, char* req, int n);
void handle_connect(SOCKET client, char* req);
DWORD WINAPI ProxyThread(LPVOID lpParam);

#endif
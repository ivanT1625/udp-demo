#define _WIN32_WINNT 0x0600
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <iostream>
#include <cstring>
#pragma comment(lib, "ws2_32.lib")

#ifdef _WIN32
#include <windows.h>
#endif

constexpr int MAX_PAYLOAD = 1400;

struct Packet {
    uint32_t seq;
    uint32_t payload_len;
    char payload[MAX_PAYLOAD];
};

struct ReqRange {
    uint32_t start;
    uint32_t end;
};

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    uint16_t port = 9999;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        WSACleanup();
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    char buf[sizeof(Packet) > sizeof(ReqRange) ? sizeof(Packet) : sizeof(ReqRange)];
    struct sockaddr_in client{};
    socklen_t client_len = sizeof(client);

    uint32_t expected_seq = 1;
    uint64_t total_received = 0;
    uint64_t lost_estimated = 0;

    while (true) {
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         reinterpret_cast<struct sockaddr*>(&client), &client_len);
        if (n <= 0) continue;
        if (n < static_cast<int>(sizeof(uint32_t)*2)) continue;

        const Packet* pkt = reinterpret_cast<const Packet*>(buf);
        uint32_t seq = pkt->seq;
        total_received++;

        if (seq == expected_seq) {
            expected_seq++;
        } else if (seq > expected_seq) {
            uint32_t gap_start = expected_seq;
            uint32_t gap_end = seq - 1;
            uint32_t gap_size = gap_end - gap_start + 1;
            lost_estimated += gap_size;

            std::cout << "[LOSS DETECTED] Gap: " << gap_start << "-" << gap_end
                      << " (" << gap_size << " packets). Sending retransmission request.\n";

            ReqRange req{gap_start, gap_end};
            sendto(sock, reinterpret_cast<char*>(&req), sizeof(req), 0,
                   reinterpret_cast<struct sockaddr*>(&client), client_len);

            expected_seq = seq + 1;
        }

        static uint64_t last_print = 0;
        if (total_received - last_print >= 1000) {
            last_print = total_received;
            std::cout << "Total: " << total_received
                      << ", Estimated lost: " << lost_estimated
                      << ", Next expected: " << expected_seq << "\n";
        }
    }
}

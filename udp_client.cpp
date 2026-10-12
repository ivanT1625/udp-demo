#define _WIN32_WINNT 0x0600
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <iostream>
#include <cstring>
#include <thread>
#include <atomic>
#pragma comment(lib, "ws2_32.lib")

#ifdef _WIN32
#include <windows.h>
#define usleep(x) Sleep((x)/1000)
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

    const char* host = "127.0.0.1";
    uint16_t port = 9999;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        WSACleanup();
        return 1;
    }

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);

    char buf[sizeof(Packet) > sizeof(ReqRange) ? sizeof(Packet) : sizeof(ReqRange)];
    struct sockaddr_in from{};
    socklen_t from_len = sizeof(from);

    std::atomic<bool> running{true};
    uint32_t skip_count = 0;
    uint32_t current_seq = 1;

    auto send_packet = [&](uint32_t seq) {
        Packet pkt;
        pkt.seq = seq;
        pkt.payload_len = MAX_PAYLOAD;
        memset(pkt.payload, 'A', MAX_PAYLOAD);

        sendto(sock, reinterpret_cast<char*>(&pkt), sizeof(Packet), 0,
               reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
    };

    std::thread receiver([&]() {
        while (running.load()) {
            // Важно: вся строка вызова recvfrom должна быть на одной строке или корректно разбита
            int n = recvfrom(sock, buf, sizeof(buf), 0,
                             reinterpret_cast<struct sockaddr*>(&from), &from_len);
            if (n < static_cast<int>(sizeof(ReqRange))) continue;

            const ReqRange* req = reinterpret_cast<const ReqRange*>(buf);
            for (uint32_t s = req->start; s <= req->end; ++s) {
                send_packet(s);
            }
        }
    });

    while (running.load()) {
        if (std::cin.peek() != EOF) {
            std::string line;
            if (std::getline(std::cin, line)) {
                if (line.find("skip") == 0) {
                    try {
                        size_t pos = line.find(' ');
                        if (pos != std::string::npos) {
                            skip_count = std::stoul(line.substr(pos));
                            // ВАЖНО: сразу выводим подтверждение, чтобы видеть реакцию
                            std::cout << "[CLIENT] Skipping next " << skip_count << " packets (seq will jump to " 
                                      << (current_seq + skip_count) << ")\n";
                        }
                    } catch (...) {
                        std::cout << "Invalid skip command.\n";
                    }
                } else if (line == "quit") {
                    running.store(false);
                }
            }
        }

         if (skip_count > 0) {
            skip_count--;
            current_seq++; 
            // Увеличиваем паузу до 50мс (50000 мкс). 
            // Для skip 1000 это даст 50 секунд тишины — сервер точно заметит потерю.
            usleep(50000); 
            continue;
        }

        send_packet(current_seq);
        current_seq++;

        usleep(1000);  
    }

    running.store(false);
    receiver.join();
    closesocket(sock);
    WSACleanup();
    return 0;
}

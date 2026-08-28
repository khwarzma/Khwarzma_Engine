#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <csignal>

#include "khcomp/comp_engine.hpp"
#include "khshield/shield_engine.hpp"

// مسار الـ Socket الافتراضي
std::string socket_path = "/tmp/kh_core.sock";
int server_fd = -1;

void handle_signal(int sig)
{
    if (server_fd != -1)
    {
        close(server_fd);
        unlink(socket_path.c_str());
    }
    std::cout << "\n[KH-Core Daemon] Shutting down cleanly...\n";
    exit(sig);
}

void handle_client(int client_fd)
{
    char buffer[4096] = {0};
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0)
    {
        std::string request_payload(buffer);

        // استجابة JSON سريعة ومجردة مؤقتاً لتأكيد ربط الـ Daemon
        std::string response = "{\"status\":\"success\", \"engine\":\"kh-core\", \"processed\":true}\n";

        write(client_fd, response.c_str(), response.length());
    }
    close(client_fd);
}

int main()
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    unlink(socket_path.c_str()); // تنظيف أي سوكيت قديم

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create UNIX socket.\n";
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "Failed to bind UNIX socket.\n";
        return 1;
    }

    if (listen(server_fd, 10) < 0)
    {
        std::cerr << "Failed to listen on socket.\n";
        return 1;
    }

    std::cout << "[KH-Core Daemon] Listening on " << socket_path << "...\n";

    while (true)
    {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd >= 0)
        {
            std::thread(handle_client, client_fd).detach();
        }
    }

    return 0;
}
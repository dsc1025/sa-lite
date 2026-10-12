#pragma once

#include <winsock2.h>
#include <string>

class SocketConnection
{
public:
    SocketConnection();
    ~SocketConnection();
    SocketConnection(const SocketConnection&) = delete;
    SocketConnection& operator=(const SocketConnection&) = delete;

    void Connect(const sockaddr_in& endpoint, ULONGLONG deadline);
    void Disconnect() noexcept;
    bool IsConnected() const noexcept;
    bool Poll(int timeoutMs);
    void Send(const std::string& packet, ULONGLONG deadline);
    std::string Receive(ULONGLONG deadline, char terminator = '\n', std::size_t maxSize = 65500);

private:
    void Wait(bool writing, ULONGLONG deadline);

    SOCKET socket_ = INVALID_SOCKET;
    std::string pending_;
};

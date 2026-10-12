#include "SocketConnection.h"
#include <stdexcept>

namespace
{
    [[noreturn]] void SocketError(const char* operation)
    {
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(WSAGetLastError()));
    }
}

SocketConnection::SocketConnection()
{
    WSADATA data{};
    int error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error != 0)
        throw std::runtime_error("WSAStartup: " + std::to_string(error));
}

SocketConnection::~SocketConnection()
{
    Disconnect();
    WSACleanup();
}

void SocketConnection::Disconnect() noexcept
{
    if (socket_ != INVALID_SOCKET)
        closesocket(socket_);
    socket_ = INVALID_SOCKET;
    pending_.clear();
}

bool SocketConnection::IsConnected() const noexcept
{
    return socket_ != INVALID_SOCKET;
}

void SocketConnection::Wait(bool writing, ULONGLONG deadline)
{
    auto now = GetTickCount64();
    if (now >= deadline)
        throw std::runtime_error("Connection or login timed out");
    auto remaining = deadline - now;
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(remaining / 1000);
    timeout.tv_usec = static_cast<long>((remaining % 1000) * 1000);
    fd_set ready, errors;
    FD_ZERO(&ready);
    FD_ZERO(&errors);
    FD_SET(socket_, &ready);
    FD_SET(socket_, &errors);
    int result = select(0, writing ? nullptr : &ready, writing ? &ready : nullptr, &errors, &timeout);
    if (result == SOCKET_ERROR)
        SocketError("select");
    if (result == 0)
        throw std::runtime_error("Connection or login timed out");
    if (FD_ISSET(socket_, &errors))
        throw std::runtime_error("Socket connection failed");
}

void SocketConnection::Connect(const sockaddr_in& endpoint, ULONGLONG deadline)
{
    Disconnect();
    try
    {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET)
            SocketError("socket");
        u_long nonblocking = 1;
        if (ioctlsocket(socket_, FIONBIO, &nonblocking) == SOCKET_ERROR)
            SocketError("ioctlsocket");
        if (connect(socket_, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                SocketError("connect");
            Wait(true, deadline);
            int error = 0;
            int length = sizeof(error);
            if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == SOCKET_ERROR)
                SocketError("getsockopt");
            if (error != 0)
                throw std::runtime_error("connect: " + std::to_string(error));
        }
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

void SocketConnection::Send(const std::string& packet, ULONGLONG deadline)
{
    std::size_t sent = 0;
    while (sent < packet.size())
    {
        Wait(true, deadline);
        int count = send(socket_, packet.data() + sent, static_cast<int>(packet.size() - sent), 0);
        if (count == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            SocketError("send");
        }
        if (count == 0)
            throw std::runtime_error("Server closed connection");
        sent += count;
    }
}

std::string SocketConnection::Receive(ULONGLONG deadline, char terminator, std::size_t maxSize)
{
    for (;;)
    {
        auto end = pending_.find(terminator);
        if (end != std::string::npos)
        {
            if (end > maxSize)
                throw std::runtime_error("Protocol packet too large");
            auto packet = pending_.substr(0, end);
            pending_.erase(0, end + 1);
            return packet;
        }
        if (pending_.size() > maxSize)
            throw std::runtime_error("Protocol packet too large");
        Wait(false, deadline);
        char buffer[4096];
        int count = recv(socket_, buffer, sizeof(buffer), 0);
        if (count == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            SocketError("recv");
        }
        if (count == 0)
            throw std::runtime_error("Server closed connection");
        pending_.append(buffer, count);
    }
}

bool SocketConnection::Poll(int timeoutMs)
{
    if (pending_.find('\n') == std::string::npos)
    {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket_, &readable);
        timeval timeout{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        int ready = select(0, &readable, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR)
            SocketError("select");
        if (ready == 0)
            return false;
    }
    return true;
}

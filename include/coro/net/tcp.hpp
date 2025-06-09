#pragma once

#include <arpa/inet.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "coro/net/io_awaiter.hpp"

namespace coro::net
{

class tcp_connector
{
public:
    explicit tcp_connector(int sockfd) noexcept : m_sockfd(sockfd) {}

    tcp_read_awaiter read(char* buf, size_t len, int flags = 0) noexcept
    {
        return tcp_read_awaiter(m_sockfd, buf, len, flags);
    }

    tcp_write_awaiter write(char* buf, size_t len, int flags = 0) noexcept
    {
        return tcp_write_awaiter(m_sockfd, buf, len, flags);
    }

    tcp_close_awaiter close() noexcept { return tcp_close_awaiter(m_sockfd); }

private:
    int m_sockfd;
};

class tcp_server
{
public:
    explicit tcp_server(int port = ::coro::config::kDefaultPort) noexcept : tcp_server(nullptr, port) {}
    tcp_server(const char* addr, int port) noexcept;

    tcp_accept_awaiter accept(int flags = 0) noexcept;

private:
    int         m_listenfd;
    int         m_port;
    sockaddr_in m_servaddr;
};

class tcp_client
{
public:
    tcp_client(const char* addr, int port) noexcept;

    tcp_connect_awaiter connect(int flags = 0) noexcept;

private:
    int         m_clientfd;
    int         m_port;
    sockaddr_in m_servaddr;
};

}; // namespace coro::net

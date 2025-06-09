#include <exception>

#include "coro/log.hpp"
#include "coro/net/tcp.hpp"
#include "coro/utils.hpp"

namespace coro::net
{
tcp_server::tcp_server(const char* addr, int port) noexcept
{
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(m_listenfd != -1);

    coro::utils::set_fd_noblock(m_listenfd);

    memset(&m_servaddr, 0, sizeof(m_servaddr));
    m_servaddr.sin_family = AF_INET;
    m_servaddr.sin_port   = htons(port);
    if (addr != nullptr)
    {
        if (inet_pton(AF_INET, addr, &m_servaddr.sin_addr.s_addr) < 0)
        {
            log::info("addr invalid");
            std::terminate();
        }
    }
    else
    {
        m_servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(m_listenfd, (sockaddr*)&m_servaddr, sizeof(m_servaddr)) != 0)
    {
        log::info("server bind error");
        std::terminate();
    }

    if (listen(m_listenfd, ::coro::config::kBacklog) != 0)
    {
        log::info("server listen error");
        std::terminate();
    }
}

tcp_accept_awaiter tcp_server::accept(int flags) noexcept
{
    return tcp_accept_awaiter(m_listenfd, flags);
}

tcp_client::tcp_client(const char* addr, int port) noexcept
{
    m_clientfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(m_clientfd != -1);

    utils::set_fd_noblock(m_clientfd);

    memset(&m_servaddr, 0, sizeof(m_servaddr));
    m_servaddr.sin_family = AF_INET;
    m_servaddr.sin_port   = htons(port);
    if (addr != nullptr)
    {
        if (inet_pton(AF_INET, addr, &m_servaddr.sin_addr.s_addr) < 0)
        {
            assert(false);
        }
    }
    else
    {
        m_servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
}

tcp_connect_awaiter tcp_client::connect(int flags) noexcept
{
    return tcp_connect_awaiter(m_clientfd, (sockaddr*)&m_servaddr, sizeof(m_servaddr));
}

}; // namespace coro::net

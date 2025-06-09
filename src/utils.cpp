#include <cassert>
#include <fcntl.h>

#include "coro/utils.hpp"

namespace coro::utils
{
void set_fd_noblock(int fd) noexcept
{
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);

    flags |= O_NONBLOCK;
    assert(fcntl(fd, F_SETFL, flags) >= 0);
}

std::string& trim(std::string& s, const char* to_trim)
{
    if (s.empty())
    {
        return s;
    }

    s.erase(0, s.find_first_not_of(to_trim));
    s.erase(s.find_last_not_of(to_trim) + 1);
    return s;
}
}; // namespace coro::utils
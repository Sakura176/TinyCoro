#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace coro::utils
{
/**
 * @brief set the fd noblock
 *
 * @param fd
 */
void set_fd_noblock(int fd) noexcept;

inline void sleep(int64_t t) noexcept
{
    std::this_thread::sleep_for(std::chrono::seconds(t));
}

inline void msleep(int64_t t) noexcept
{
    std::this_thread::sleep_for(std::chrono::milliseconds(t));
}

inline void usleep(int64_t t) noexcept
{
    std::this_thread::sleep_for(std::chrono::microseconds(t));
}

/**
 * @brief remove to_trim from s
 *
 * @param s
 * @param to_trim
 * @return std::string&
 */
std::string& trim(std::string& s, const char* to_trim);
}; // namespace coro::utils
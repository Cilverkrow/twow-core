/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "CliInput.h"

#if !defined(WIN32)

#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

CliInput::CliInput(FILE* stream)
{
    if (!stream)
        return;
    // Keep our descriptor alive even if Master closes stdin during shutdown.
    // The duplicate shares O_NONBLOCK with stdin; restore the original flags
    // on destruction. No other reader may consume this stream concurrently.
    m_descriptor = fcntl(fileno(stream), F_DUPFD_CLOEXEC, 0);
    if (m_descriptor < 0)
        return;
    m_originalFlags = fcntl(m_descriptor, F_GETFL);
    if (m_originalFlags < 0 || fcntl(m_descriptor, F_SETFL, m_originalFlags | O_NONBLOCK) < 0)
    {
        close(m_descriptor);
        m_descriptor = -1;
    }
}

CliInput::~CliInput()
{
    if (m_descriptor >= 0)
    {
        fcntl(m_descriptor, F_SETFL, m_originalFlags);
        close(m_descriptor);
    }
}

CliInputResult CliInput::ReadLine(char* buffer, std::size_t bufferSize, unsigned timeoutMilliseconds)
{
    if (m_descriptor < 0 || !buffer || bufferSize <= MaxLineLength)
        return CliInputResult::Error;
    buffer[0] = '\0';
    if (m_endOfFile)
        return CliInputResult::EndOfFile;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMilliseconds);
    for (;;)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return CliInputResult::Timeout;
        // Round upward so a sub-millisecond remainder cannot cause a spin.
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
        const int wait = remaining > std::numeric_limits<int>::max() ?
                         std::numeric_limits<int>::max() : static_cast<int>(remaining);
        pollfd descriptor{m_descriptor, POLLIN, 0};
        const int ready = poll(&descriptor, 1, wait);
        if (ready == 0)
            return CliInputResult::Timeout;
        if (ready < 0)
            return errno == EINTR ? CliInputResult::Interrupted : CliInputResult::Error;
        if (descriptor.revents & POLLNVAL)
            return CliInputResult::Error;

        // No stdio read-ahead and no blocking read after readiness. Reading one
        // byte leaves subsequent complete commands available for the next call.
        char byte;
        const ssize_t received = read(m_descriptor, &byte, 1);
        if (received < 0)
        {
            if (errno == EINTR)
                return CliInputResult::Interrupted;
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && !(descriptor.revents & POLLERR))
                continue;
            return CliInputResult::Error;
        }
        if (received == 0)
        {
            m_length = 0;
            m_discarding = false;
            m_endOfFile = true;
            return CliInputResult::EndOfFile;
        }
        if (byte == '\n')
        {
            if (m_discarding)
            {
                m_discarding = false;
                m_length = 0;
                continue;
            }
            if (m_length && m_pending[m_length - 1] == '\r')
                --m_length;
            std::memcpy(buffer, m_pending.data(), m_length);
            buffer[m_length] = '\0';
            m_length = 0;
            return CliInputResult::Line;
        }
        if (m_discarding)
            continue;
        if (byte == '\0' || (m_length && m_pending[m_length - 1] == '\r') ||
            (m_length >= MaxLineLength && !(m_length == MaxLineLength && byte == '\r')))
        {
            m_discarding = true;
            m_length = 0;
            continue;
        }
        m_pending[m_length++] = byte;
    }
}

#endif

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

#include <sys/select.h>
#include <unistd.h>

CliInput::CliInput(FILE* stream)
    : m_stream(stream),
      m_descriptor(stream ? fileno(stream) : -1),
      m_configured(stream && m_descriptor >= 0 && setvbuf(stream, nullptr, _IONBF, 0) == 0)
{
}

CliInputResult CliInput::ReadLine(char* buffer, std::size_t bufferSize, unsigned timeoutMilliseconds)
{
    if (!m_configured || !buffer || bufferSize < 2)
        return CliInputResult::Error;

    fd_set readDescriptors;
    FD_ZERO(&readDescriptors);
    FD_SET(m_descriptor, &readDescriptors);

    timeval timeout;
    timeout.tv_sec = static_cast<long>(timeoutMilliseconds / 1000);
    timeout.tv_usec = static_cast<long>((timeoutMilliseconds % 1000) * 1000);

    const int ready = select(m_descriptor + 1, &readDescriptors, nullptr, nullptr, &timeout);
    if (ready == 0)
        return CliInputResult::Timeout;
    if (ready < 0)
        return errno == EINTR ? CliInputResult::Interrupted : CliInputResult::Error;

    errno = 0;
    if (fgets(buffer, static_cast<int>(bufferSize), m_stream))
        return CliInputResult::Line;
    if (feof(m_stream))
        return CliInputResult::EndOfFile;
    if (errno == EINTR)
    {
        clearerr(m_stream);
        return CliInputResult::Interrupted;
    }
    return CliInputResult::Error;
}

#endif

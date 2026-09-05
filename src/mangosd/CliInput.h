/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CLIINPUT_H
#define __CLIINPUT_H

#if !defined(WIN32)

#include <cstddef>
#include <cstdio>
#include <array>

enum class CliInputResult
{
    Line,
    Timeout,
    Interrupted,
    EndOfFile,
    Error
};

class CliInput
{
public:
    // Sole reader of the supplied stream; construct before any stdio reads.
    // LF/CRLF terminate a line of at most 255 payload bytes. The output has no
    // terminator and requires MaxLineLength+1 bytes (including the final NUL).
    // Timeout/EINTR preserve fragments. Oversized lines, embedded CR/NUL, and
    // unterminated EOF tails are discarded, never dispatched as commands.
    static constexpr std::size_t MaxLineLength = 255;
    explicit CliInput(FILE* stream);
    ~CliInput();
    CliInput(const CliInput&) = delete;
    CliInput& operator=(const CliInput&) = delete;

    CliInputResult ReadLine(char* buffer, std::size_t bufferSize, unsigned timeoutMilliseconds);

private:
    int m_descriptor = -1;
    int m_originalFlags = -1;
    bool m_endOfFile = false;
    bool m_discarding = false;
    std::array<char, MaxLineLength + 1> m_pending{};
    std::size_t m_length = 0;
};

#endif

#endif

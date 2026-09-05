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
    explicit CliInput(FILE* stream);

    CliInputResult ReadLine(char* buffer, std::size_t bufferSize, unsigned timeoutMilliseconds);

private:
    FILE* m_stream;
    int m_descriptor;
    bool m_configured;
};

#endif

#endif

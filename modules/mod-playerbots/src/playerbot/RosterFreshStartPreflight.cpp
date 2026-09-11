#include "RosterFreshStartPreflight.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

#include <openssl/sha.h>

namespace ai::roster::freshstart
{
namespace
{
char const* const kHeader = "ssc-rndbot-fresh-start-v1";

bool ValidUuidV4(std::string const& value)
{
    if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-' || value[14] != '4')
        return false;
    if (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b')
        return false;
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (index == 8 || index == 13 || index == 18 || index == 23)
            continue;
        if (!((value[index] >= '0' && value[index] <= '9') || (value[index] >= 'a' && value[index] <= 'f')))
            return false;
    }
    return true;
}

bool ParseUnsigned(std::string const& text, std::uint64_t maximum, std::uint64_t& value)
{
    if (text.empty() || (text.size() > 1 && text.front() == '0'))
        return false;
    std::uint64_t parsed = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        std::uint64_t const digit = static_cast<std::uint64_t>(c - '0');
        if (parsed > (maximum - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

bool IsWellFormedUtf8(std::string const& value)
{
    if (value.empty())
        return false;
    for (std::size_t index = 0; index < value.size();)
    {
        unsigned char const first = static_cast<unsigned char>(value[index]);
        std::size_t const count = first < 0x80 ? 1 : (first >= 0xc2 && first <= 0xdf ? 2 :
            (first >= 0xe0 && first <= 0xef ? 3 : (first >= 0xf0 && first <= 0xf4 ? 4 : 0)));
        if (!count || index + count > value.size())
            return false;
        std::uint32_t codepoint = count == 1 ? first : (first & (0x7fu >> count));
        for (std::size_t continuation = 1; continuation < count; ++continuation)
        {
            unsigned char const byte = static_cast<unsigned char>(value[index + continuation]);
            if ((byte & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((count == 2 && codepoint < 0x80) ||
            (count == 3 && (codepoint < 0x800 || (codepoint >= 0xd800 && codepoint <= 0xdfff))) ||
            (count == 4 && (codepoint < 0x10000 || codepoint > 0x10ffff)))
            return false;
        index += count;
    }
    return true;
}

std::string EncodeBase64Url(std::string const& input)
{
    static char const* const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string output;
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (unsigned char byte : input)
    {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            output.push_back(alphabet[(accumulator >> bits) & 0x3f]);
        }
    }
    if (bits)
        output.push_back(alphabet[(accumulator << (6 - bits)) & 0x3f]);
    return output;
}

bool DecodeBase64Url(std::string const& input, std::string& output)
{
    static char const* const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    if (input.empty() || input.size() % 4 == 1)
        return false;
    output.clear();
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (char character : input)
    {
        char const* const position = std::find(alphabet, alphabet + 64, character);
        if (position == alphabet + 64)
            return false;
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(position - alphabet);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            output.push_back(static_cast<char>((accumulator >> bits) & 0xff));
        }
    }
    return !bits || !(accumulator & ((1u << bits) - 1u));
}

bool Take(std::vector<std::string> const& lines, std::size_t& cursor, char const* key, std::string& value)
{
    if (cursor >= lines.size())
        return false;
    std::string const prefix = std::string(key) + "=";
    if (lines[cursor].compare(0, prefix.size(), prefix) != 0)
        return false;
    value = lines[cursor++].substr(prefix.size());
    return true;
}

bool ParseTrue(std::string const& value, bool& target)
{
    if (value == "true") { target = true; return true; }
    if (value == "false") { target = false; return true; }
    return false;
}
}

bool DomainFlags::AllExplicitlyEnabled() const
{
    return levelXp && quests && talents && professions && professionPlan && positionHomebind && deathCorpse && groupState;
}

std::string Hex(Sha256 const& hash)
{
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (std::uint8_t byte : hash)
        output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

bool ParseHex(std::string const& text, Sha256& hash)
{
    if (text.size() != 64)
        return false;
    for (std::size_t index = 0; index < hash.size(); ++index)
    {
        auto nibble = [](char character) -> int {
            if (character >= '0' && character <= '9') return character - '0';
            if (character >= 'A' && character <= 'F') return character - 'A' + 10;
            return -1;
        };
        int const high = nibble(text[index * 2]);
        int const low = nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        hash[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool SerializeCanonicalRequest(Request const& request, std::string& bytes, std::string& error)
{
    if (request.schemaVersion != kSchemaVersion || !ValidUuidV4(request.operationId) || !request.rosterVersion ||
        !IsWellFormedUtf8(request.actor) || !IsWellFormedUtf8(request.reason))
    {
        error = "FRESH_START_INVALID_REQUEST";
        return false;
    }
    std::ostringstream output;
    output << kHeader << '\n'
        << "schema_version=" << request.schemaVersion << '\n'
        << "operation_id=" << request.operationId << '\n'
        << "roster_version=" << request.rosterVersion << '\n'
        << "member_count=" << request.memberCount << '\n'
        << "roster_sha256=" << Hex(request.rosterSha256) << '\n'
        << "actor=" << EncodeBase64Url(request.actor) << '\n'
        << "reason=" << EncodeBase64Url(request.reason) << '\n'
        << "level_xp=" << (request.domains.levelXp ? "true" : "false") << '\n'
        << "quests=" << (request.domains.quests ? "true" : "false") << '\n'
        << "talents=" << (request.domains.talents ? "true" : "false") << '\n'
        << "professions=" << (request.domains.professions ? "true" : "false") << '\n'
        << "profession_plan=" << (request.domains.professionPlan ? "true" : "false") << '\n'
        << "position_homebind=" << (request.domains.positionHomebind ? "true" : "false") << '\n'
        << "death_corpse=" << (request.domains.deathCorpse ? "true" : "false") << '\n'
        << "group_state=" << (request.domains.groupState ? "true" : "false") << '\n';
    bytes = output.str();
    return true;
}

bool ParseCanonicalRequest(std::string const& bytes, Request& request, std::string& error)
{
    request = Request{};
    if (bytes.empty() || bytes.back() != '\n' || bytes.find('\r') != std::string::npos)
    {
        error = "FRESH_START_INVALID_REQUEST";
        return false;
    }
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin < bytes.size())
    {
        std::size_t const end = bytes.find('\n', begin);
        if (end == std::string::npos)
            break;
        lines.push_back(bytes.substr(begin, end - begin));
        begin = end + 1;
    }
    if (lines.size() != 16 || lines.front() != kHeader)
    {
        error = "FRESH_START_INVALID_REQUEST";
        return false;
    }
    std::size_t cursor = 1;
    std::string value;
    std::uint64_t parsed = 0;
    if (!Take(lines, cursor, "schema_version", value) || !ParseUnsigned(value, std::numeric_limits<std::uint32_t>::max(), parsed)) goto invalid;
    request.schemaVersion = static_cast<std::uint32_t>(parsed);
    if (!Take(lines, cursor, "operation_id", request.operationId) || !ValidUuidV4(request.operationId)) goto invalid;
    if (!Take(lines, cursor, "roster_version", value) || !ParseUnsigned(value, std::numeric_limits<std::uint64_t>::max(), request.rosterVersion)) goto invalid;
    if (!Take(lines, cursor, "member_count", value) || !ParseUnsigned(value, std::numeric_limits<std::uint32_t>::max(), parsed)) goto invalid;
    request.memberCount = static_cast<std::uint32_t>(parsed);
    if (!Take(lines, cursor, "roster_sha256", value) || !ParseHex(value, request.rosterSha256)) goto invalid;
    if (!Take(lines, cursor, "actor", value) || !DecodeBase64Url(value, request.actor) || !IsWellFormedUtf8(request.actor)) goto invalid;
    if (!Take(lines, cursor, "reason", value) || !DecodeBase64Url(value, request.reason) || !IsWellFormedUtf8(request.reason)) goto invalid;
    if (!Take(lines, cursor, "level_xp", value) || !ParseTrue(value, request.domains.levelXp)) goto invalid;
    if (!Take(lines, cursor, "quests", value) || !ParseTrue(value, request.domains.quests)) goto invalid;
    if (!Take(lines, cursor, "talents", value) || !ParseTrue(value, request.domains.talents)) goto invalid;
    if (!Take(lines, cursor, "professions", value) || !ParseTrue(value, request.domains.professions)) goto invalid;
    if (!Take(lines, cursor, "profession_plan", value) || !ParseTrue(value, request.domains.professionPlan)) goto invalid;
    if (!Take(lines, cursor, "position_homebind", value) || !ParseTrue(value, request.domains.positionHomebind)) goto invalid;
    if (!Take(lines, cursor, "death_corpse", value) || !ParseTrue(value, request.domains.deathCorpse)) goto invalid;
    if (!Take(lines, cursor, "group_state", value) || !ParseTrue(value, request.domains.groupState)) goto invalid;
    if (cursor != lines.size() || request.schemaVersion != kSchemaVersion || !request.rosterVersion)
        goto invalid;
    {
        std::string canonical;
        if (!SerializeCanonicalRequest(request, canonical, error) || canonical != bytes)
            goto invalid;
    }
    return true;

invalid:
    error = "FRESH_START_INVALID_REQUEST";
    return false;
}

Sha256 HashCanonicalRequest(std::string const& canonicalBytes)
{
    Sha256 result{};
    SHA256(reinterpret_cast<unsigned char const*>(canonicalBytes.data()), canonicalBytes.size(), result.data());
    return result;
}

std::string ValidatePreflight(Request const& request, PreflightState const& state)
{
    if (!state.persistentRosterEnabled)
        return "FRESH_START_ROSTER_DISABLED";
    if (!state.maintenanceMode)
        return "FRESH_START_MAINTENANCE_REQUIRED";
    if (!state.servicePresent)
        return "FRESH_START_SERVICE_UNAVAILABLE";
    if (request.memberCount != kRequiredMemberCount || state.memberCount != kRequiredMemberCount)
        return "FRESH_START_MEMBER_COUNT_MISMATCH";
    if (request.rosterVersion != state.rosterVersion)
        return "FRESH_START_ROSTER_VERSION_MISMATCH";
    if (request.rosterSha256 != state.rosterSha256)
        return "FRESH_START_ROSTER_HASH_MISMATCH";
    if (state.onlineTargetBots != 0)
        return "FRESH_START_TARGET_BOTS_ONLINE";
    if (!request.domains.AllExplicitlyEnabled())
        return "FRESH_START_DOMAIN_FLAGS_REQUIRED";
    return "FRESH_START_PREFLIGHT_OK";
}

ReplayDecision EvaluateReplay(bool operationFound, std::string const& storedOperationId,
    Sha256 const& storedRequestSha256, Request const& request, Sha256 const& requestSha256)
{
    if (!operationFound)
        return ReplayDecision::NEW_REQUEST;
    if (storedOperationId == request.operationId && storedRequestSha256 == requestSha256)
        return ReplayDecision::REPLAY_SAME_REQUEST;
    return ReplayDecision::REJECT_OPERATION_ID_MISMATCH;
}

bool HasUnchangedRosterPrefix(std::vector<std::uint32_t> const& previous,
    std::vector<std::uint32_t> const& current)
{
    return previous.size() <= current.size() && std::equal(previous.begin(), previous.end(), current.begin());
}
}

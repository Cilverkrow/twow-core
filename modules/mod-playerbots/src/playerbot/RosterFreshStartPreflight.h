#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ai::roster::freshstart
{
using Sha256 = std::array<std::uint8_t, 32>;

constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::uint32_t kRequiredMemberCount = 68;

struct DomainFlags
{
    bool levelXp = false;
    bool quests = false;
    bool talents = false;
    bool professions = false;
    bool professionPlan = false;
    bool positionHomebind = false;
    bool deathCorpse = false;
    bool groupState = false;

    bool AllExplicitlyEnabled() const;
};

struct Request
{
    std::uint32_t schemaVersion = kSchemaVersion;
    std::string operationId;
    std::uint64_t rosterVersion = 0;
    std::uint32_t memberCount = 0;
    Sha256 rosterSha256{};
    std::string actor;
    std::string reason;
    DomainFlags domains;
};

struct PreflightState
{
    bool persistentRosterEnabled = false;
    bool maintenanceMode = false;
    bool servicePresent = false;
    std::uint64_t rosterVersion = 0;
    std::uint32_t memberCount = 0;
    Sha256 rosterSha256{};
    std::uint32_t onlineTargetBots = 0;
};

enum class ReplayDecision
{
    NEW_REQUEST,
    REPLAY_SAME_REQUEST,
    REJECT_OPERATION_ID_MISMATCH
};

bool SerializeCanonicalRequest(Request const& request, std::string& bytes, std::string& error);
bool ParseCanonicalRequest(std::string const& bytes, Request& request, std::string& error);
Sha256 HashCanonicalRequest(std::string const& canonicalBytes);
std::string Hex(Sha256 const& hash);
bool ParseHex(std::string const& text, Sha256& hash);
std::string ValidatePreflight(Request const& request, PreflightState const& state);
ReplayDecision EvaluateReplay(bool operationFound, std::string const& storedOperationId,
    Sha256 const& storedRequestSha256, Request const& request, Sha256 const& requestSha256);
bool HasUnchangedRosterPrefix(std::vector<std::uint32_t> const& previous,
    std::vector<std::uint32_t> const& current);
}

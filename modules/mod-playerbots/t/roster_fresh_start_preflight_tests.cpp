#include "RosterFreshStartPreflight.h"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

ai::roster::freshstart::Request ValidRequest()
{
    using namespace ai::roster::freshstart;
    Request request;
    request.operationId = "566f48aa-07e2-49d1-9ddb-e43d63c4e635";
    request.rosterVersion = 7;
    request.memberCount = kRequiredMemberCount;
    request.rosterSha256 = HashCanonicalRequest("ordered-roster-68");
    request.actor = "local-console";
    request.reason = "fresh start";
    request.domains = { true, true, true, true, true, true, true, true };
    return request;
}

ai::roster::freshstart::PreflightState ValidState(ai::roster::freshstart::Request const& request)
{
    ai::roster::freshstart::PreflightState state;
    state.persistentRosterEnabled = true;
    state.maintenanceMode = true;
    state.servicePresent = true;
    state.rosterVersion = request.rosterVersion;
    state.memberCount = request.memberCount;
    state.rosterSha256 = request.rosterSha256;
    return state;
}

void TestCanonicalRoundTripAndHash()
{
    using namespace ai::roster::freshstart;
    Request request = ValidRequest();
    request.actor = "local-console-\xC3\xA4";
    request.reason = "fresh-start-\xE2\x9C\x93";
    std::string bytes, error;
    Request parsed;
    CHECK(SerializeCanonicalRequest(request, bytes, error));
    CHECK(ParseCanonicalRequest(bytes, parsed, error));
    std::string replay;
    CHECK(SerializeCanonicalRequest(parsed, replay, error));
    CHECK(bytes == replay);
    CHECK(HashCanonicalRequest(bytes) == HashCanonicalRequest(replay));
    CHECK(bytes.find('\r') == std::string::npos);
    CHECK(bytes.back() == '\n');
}

void TestParserRejectsMalformedFields()
{
    using namespace ai::roster::freshstart;
    std::string bytes, error;
    Request request = ValidRequest(), parsed;
    CHECK(SerializeCanonicalRequest(request, bytes, error));
    CHECK(!ParseCanonicalRequest(bytes + "unknown=true\n", parsed, error));
    CHECK(!ParseCanonicalRequest(bytes + "member_count=68\n", parsed, error));
    std::string missing = bytes;
    missing.erase(missing.find("group_state="));
    CHECK(!ParseCanonicalRequest(missing, parsed, error));
    std::string crlf = bytes;
    crlf.replace(crlf.find('\n'), 1, "\r\n");
    CHECK(!ParseCanonicalRequest(crlf, parsed, error));
    request.operationId = "not-a-v4-uuid";
    CHECK(!SerializeCanonicalRequest(request, bytes, error));
}

void TestPreflightFailuresAreFailClosed()
{
    using namespace ai::roster::freshstart;
    Request request = ValidRequest();
    PreflightState state = ValidState(request);
    CHECK(ValidatePreflight(request, state) == "FRESH_START_PREFLIGHT_OK");
    state.persistentRosterEnabled = false;
    CHECK(ValidatePreflight(request, state) == "FRESH_START_ROSTER_DISABLED");
    state = ValidState(request); state.servicePresent = false;
    CHECK(ValidatePreflight(request, state) == "FRESH_START_SERVICE_UNAVAILABLE");
    state.maintenanceMode = false;
    CHECK(ValidatePreflight(request, state) == "FRESH_START_MAINTENANCE_REQUIRED");
    state = ValidState(request); state.onlineTargetBots = 1;
    CHECK(ValidatePreflight(request, state) == "FRESH_START_TARGET_BOTS_ONLINE");
    state = ValidState(request); state.memberCount = 67;
    CHECK(ValidatePreflight(request, state) == "FRESH_START_MEMBER_COUNT_MISMATCH");
    state = ValidState(request); request.memberCount = 67;
    CHECK(ValidatePreflight(request, state) == "FRESH_START_MEMBER_COUNT_MISMATCH");
    request.memberCount = kRequiredMemberCount;
    state = ValidState(request); ++state.rosterVersion;
    CHECK(ValidatePreflight(request, state) == "FRESH_START_ROSTER_VERSION_MISMATCH");
    state = ValidState(request); state.rosterSha256 = HashCanonicalRequest("other");
    CHECK(ValidatePreflight(request, state) == "FRESH_START_ROSTER_HASH_MISMATCH");
    state = ValidState(request); request.domains.quests = false;
    CHECK(ValidatePreflight(request, state) == "FRESH_START_DOMAIN_FLAGS_REQUIRED");
}

void TestReplayAndStablePrefix()
{
    using namespace ai::roster::freshstart;
    Request request = ValidRequest();
    std::string bytes, error;
    CHECK(SerializeCanonicalRequest(request, bytes, error));
    Sha256 const hash = HashCanonicalRequest(bytes);
    CHECK(EvaluateReplay(false, "", {}, request, hash) == ReplayDecision::NEW_REQUEST);
    CHECK(EvaluateReplay(true, request.operationId, hash, request, hash) == ReplayDecision::REPLAY_SAME_REQUEST);
    request.reason = "different";
    std::string changed;
    CHECK(SerializeCanonicalRequest(request, changed, error));
    CHECK(EvaluateReplay(true, request.operationId, hash, request, HashCanonicalRequest(changed)) == ReplayDecision::REJECT_OPERATION_ID_MISMATCH);
    CHECK(HasUnchangedRosterPrefix({ 1, 2, 3 }, { 1, 2, 3, 4 }));
    CHECK(!HasUnchangedRosterPrefix({ 1, 2, 3 }, { 1, 4, 3, 5 }));
}
}

int main()
{
    TestCanonicalRoundTripAndHash();
    TestParserRejectsMalformedFields();
    TestPreflightFailuresAreFailClosed();
    TestReplayAndStablePrefix();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "ROSTER_FRESH_START_PREFLIGHT_TESTS=PASS\n";
    return EXIT_SUCCESS;
}

// twow-repo#595 (4): a .bot add login whose holder is destroyed before the callback is dropped, the others delivered.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>

#include "PendingBotLoginPolicy.h"

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

struct Holder {};
struct Pending
{
    std::uint32_t bot = 0;
    Holder* owner = nullptr;
};
}

int main()
{
    using namespace ai::pending_login;

    Holder master1, master2;
    std::map<int, Pending> pending;
    pending[1] = { 101, &master1 };
    pending[2] = { 102, &master1 };
    pending[3] = { 103, &master2 };

    Require(RouteOf(pending, 1) == Route::Deliver && RouteOf(pending, 3) == Route::Deliver, "pending logins are delivered");
    Require(RouteOf(pending, 9) == Route::Unknown, "a login not queued here: nothing to do");

    // master1 logs out before its two callbacks ran.
    Require(Orphan(pending, &master1) == 2, "both of master1's logins orphaned");
    Require(RouteOf(pending, 1) == Route::Orphaned && RouteOf(pending, 2) == Route::Orphaned, "dropped later");
    Require(RouteOf(pending, 3) == Route::Deliver, "negative: master2's login still delivered");
    Require(Orphan(pending, &master1) == 0, "a second destroy changes nothing");
    Require(Orphan(pending, static_cast<Holder const*>(nullptr)) == 0, "null owner never matches");

    std::cout << "pending_bot_login_policy_tests passed\n";
    return 0;
}

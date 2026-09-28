#include "TravelChoosePolicy.h"

#include <cstdlib>
#include <iostream>
#include <memory>

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

// Stands in for std::future: pending until *done is set.
struct FakeFuture
{
    std::shared_ptr<bool> done;

    bool valid() const { return static_cast<bool>(done); }
    std::future_status wait_for(std::chrono::seconds) const
    {
        return *done ? std::future_status::ready : std::future_status::timeout;
    }
};

FakeFuture Job(bool done)
{
    return FakeFuture{ std::make_shared<bool>(done) };
}
}

int main()
{
    using namespace ai::travel_choose;

    // Budget: 250 ms per choice and update.
    Require(!OverBudget(250), "250 ms is within the budget");
    Require(OverBudget(251), "251 ms is over the budget");

    // An aborted choice resumes where it stopped, for the same purpose only.
    Resume resume;
    Require(resume.SkipFor("quest") == 0, "a fresh choice skips nothing");
    resume.Abort("quest", 40);
    Require(resume.SkipFor("quest") == 40, "the next quest list skips the 40 checked candidates");
    Require(resume.SkipFor("rpg") == 0, "another purpose starts afresh");
    resume.Clear();
    Require(resume.SkipFor("quest") == 0, "a finished choice starts afresh");

    // Parking: only running jobs are kept; finished ones are dropped.
    ParkingLot<FakeFuture> lot;
    Require(lot.Park(FakeFuture{}) == 0, "an empty future is not parked");
    Require(lot.Park(Job(true)) == 0, "a finished job is not parked");

    FakeFuture first = Job(false);
    std::shared_ptr<bool> firstDone = first.done;
    Require(lot.Park(std::move(first)) == 1, "a running job is parked instead of waited for");
    Require(lot.Park(Job(false)) == 2, "a second running job is parked too");

    *firstDone = true;
    Require(lot.Pending() == 1, "a job that finished is dropped");
    Require(lot.Count() == 1 && lot.Max() == 2, "count and high-water mark are published");

    // Cap: when the lot is full no new job may start; collecting frees it again.
    ParkingLot<FakeFuture> full;
    std::vector<std::shared_ptr<bool>> jobs;
    for (size_t i = 0; i < ParkCap; ++i)
    {
        FakeFuture job = Job(false);
        jobs.push_back(job.done);
        Require(!full.Full(), "below the cap new jobs may start");
        full.Park(std::move(job));
    }
    Require(full.Full(), "at the cap no new job starts");
    *jobs.front() = true;
    Require(full.Pending() == ParkCap - 1 && !full.Full(), "a finished job is collected and frees a slot");

    std::cout << "travel_choose_policy_tests passed\n";
    return 0;
}

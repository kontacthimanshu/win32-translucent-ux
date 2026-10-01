// GenerationGuard (T051; research R-07): payloads from any generation but the current
// one are rejected and freed exactly once; a current payload is handed back untouched.

#include <te/core/GenerationGuard.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{

struct Payload
{
    te::Generation gen = 0;
    int value = 0;
};

// Counts how many payloads were actually freed.
struct CountingDeleter
{
    int* freed = nullptr;
    void operator()(Payload* payload) const
    {
        ++*freed;
        delete payload;
    }
};

using Owned = std::unique_ptr<Payload, CountingDeleter>;

Owned Make(te::Generation gen, int* freed, int value = 0)
{
    return Owned(new Payload{gen, value}, CountingDeleter{freed});
}

TEST(GenerationGuard, AdvanceStartsANewGeneration)
{
    te::GenerationGuard guard;
    const te::Generation first = guard.Advance();
    const te::Generation second = guard.Advance();
    EXPECT_GT(second, first);
    EXPECT_EQ(guard.Current(), second);
    EXPECT_TRUE(guard.IsCurrent(second));
    EXPECT_FALSE(guard.IsCurrent(first));
}

TEST(GenerationGuard, RewindRestoresAnEarlierGenerationWithoutReusingNumbers)
{
    te::GenerationGuard guard;
    const te::Generation shown = guard.Advance();
    const te::Generation failed = guard.Advance();
    guard.Rewind(shown); // the navigation to `failed` did not succeed
    EXPECT_TRUE(guard.IsCurrent(shown));
    EXPECT_FALSE(guard.IsCurrent(failed));
    EXPECT_GT(guard.Advance(), failed) << "a new request never reuses the failed number";
}

TEST(GenerationGuard, CurrentPayloadIsAccepted)
{
    te::GenerationGuard guard;
    const te::Generation gen = guard.Advance();
    int freed = 0;
    Owned accepted = guard.Accept(Make(gen, &freed, 42));
    ASSERT_NE(accepted, nullptr);
    EXPECT_EQ(accepted->value, 42);
    EXPECT_EQ(freed, 0) << "an accepted payload is not freed by the guard";
    EXPECT_EQ(guard.RejectedCount(), 0u);
    accepted.reset();
    EXPECT_EQ(freed, 1);
}

TEST(GenerationGuard, OlderPayloadIsRejectedAndFreedOnce)
{
    te::GenerationGuard guard;
    const te::Generation old = guard.Advance();
    guard.Advance();
    int freed = 0;
    Owned result = guard.Accept(Make(old, &freed));
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(freed, 1);
    EXPECT_EQ(guard.RejectedCount(), 1u);
}

TEST(GenerationGuard, NewerPayloadIsRejectedToo)
{
    // gen != current, not just gen < current: a payload can never be from the future,
    // so one that claims to be is treated as corrupt and dropped.
    te::GenerationGuard guard;
    const te::Generation gen = guard.Advance();
    int freed = 0;
    EXPECT_EQ(guard.Accept(Make(gen + 1, &freed)), nullptr);
    EXPECT_EQ(freed, 1);
}

TEST(GenerationGuard, NothingIsCurrentBeforeTheFirstNavigation)
{
    te::GenerationGuard guard;
    int freed = 0;
    EXPECT_EQ(guard.Accept(Make(guard.Current() + 1, &freed)), nullptr);
    EXPECT_EQ(freed, 1);
}

TEST(GenerationGuard, NullPayloadIsIgnored)
{
    te::GenerationGuard guard;
    guard.Advance();
    EXPECT_EQ(guard.Accept(Owned(nullptr, CountingDeleter{nullptr})), nullptr);
    EXPECT_EQ(guard.RejectedCount(), 0u);
}

TEST(GenerationGuard, ManyStalePayloadsAreEachFreedOnce)
{
    te::GenerationGuard guard;
    const te::Generation a = guard.Advance();
    const te::Generation b = guard.Advance();
    const te::Generation c = guard.Advance();
    int freed = 0;
    int accepted = 0;
    for (int i = 0; i < 100; ++i)
    {
        for (const te::Generation gen : {a, b, c})
        {
            if (Owned p = guard.Accept(Make(gen, &freed)))
            {
                ++accepted;
            }
        }
    }
    EXPECT_EQ(accepted, 100);
    EXPECT_EQ(freed, 300) << "200 rejected by the guard + 100 accepted, freed at scope end";
    EXPECT_EQ(guard.RejectedCount(), 200u);
}

// The UI-thread path: a WM_TE_* LPARAM that owns an EnumBatch.
TEST(GenerationGuard, TakeFromLParamFiltersRealPayloads)
{
    te::GenerationGuard guard;
    const te::Generation old = guard.Advance();
    const te::Generation current = guard.Advance();

    auto stale = std::make_unique<te::EnumBatch>();
    stale->gen = old;
    stale->items.resize(3);
    EXPECT_EQ(guard.Take<te::EnumBatch>(reinterpret_cast<LPARAM>(stale.release())), nullptr);

    auto fresh = std::make_unique<te::EnumBatch>();
    fresh->gen = current;
    fresh->items.resize(2);
    const std::unique_ptr<te::EnumBatch> taken =
        guard.Take<te::EnumBatch>(reinterpret_cast<LPARAM>(fresh.release()));
    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(taken->items.size(), 2u);
    EXPECT_EQ(guard.RejectedCount(), 1u);
}

TEST(GenerationGuardTest, IssueGivesAFreshNumberWithoutChangingTheCurrentOne)
{
    // The file icons get their own number on a DPI change (T081); the listing stays current.
    te::GenerationGuard guard;
    const te::Generation listing = guard.Advance();
    const te::Generation icons = guard.Issue();
    EXPECT_GT(icons, listing);
    EXPECT_EQ(guard.Current(), listing);
    EXPECT_TRUE(guard.IsCurrent(listing));
    EXPECT_GT(guard.Advance(), icons) << "a later navigation never reuses an issued number";
}

} // namespace

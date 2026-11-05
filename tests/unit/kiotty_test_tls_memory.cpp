#include "base/tls/kiotty_tls_memory.h"
#include "support/kiotty_test_tls.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

using kiotty::Stage;
using kiotty::net::OwnerTlsMemory;
using kiotty::net::ServerTlsLayout;
using kiotty::net::TlsMemoryFunctions;
using kiotty::net::TlsProfile;
using kiotty_test::TlsPump;
using kiotty_test::clientConfig;
using kiotty_test::move;
using kiotty_test::serverConfig;
using kiotty_test::take;

namespace
{
    typedef kiotty::net::TlsMemory          Memory;
    typedef OwnerTlsMemory<ServerTlsLayout> ServerMemory;

    const size_t PAIRS = 4;
    const size_t BLOCK_BYTES = 64;

    struct Profiles
    {
        TlsProfile server;
        TlsProfile client;
    };

    bool open(Profiles& profiles)
    {
        if (profiles.server.openServer(serverConfig()) != KIOTTY_SUCCESS)
        {
            return false;
        }
        return profiles.client.openClient(clientConfig()) == KIOTTY_SUCCESS;
    }

    bool connect(TlsPump& pump, Profiles& profiles)
    {
        if (pump.open(profiles.server, profiles.client) != KIOTTY_SUCCESS)
        {
            return false;
        }
        pump.handshake();
        return pump.done();
    }

    std::string roundTrip(TlsPump& pump, const std::string& text)
    {
        KiottyNetSpan span = { text.data(), text.size() };
        pump.client.encode(span);
        move(pump.client, pump.server);
        Stage<4096> plain;
        if (!pump.server.decode(plain).isOk())
        {
            return std::string();
        }
        return take(plain);
    }

    void* allocate(size_t bytes)
    {
        TlsMemoryFunctions functions = Memory::functions();
        return functions.allocate(bytes, __FILE__, __LINE__);
    }

    void release(void* block)
    {
        TlsMemoryFunctions functions = Memory::functions();
        functions.release(block, __FILE__, __LINE__);
    }

    class TlsMemory : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(Memory::installGlobal());
        }
    };
}

TEST_F(TlsMemory, InstalledIsTrueAfterFunctionsAreSetAndFalseWhenAnotherAllocatorIsAlreadyThere)
{
    EXPECT_TRUE(Memory::installed());
    EXPECT_TRUE(Memory::installGlobal());
    EXPECT_GT(Memory::globalClassCount(), 0u);
    EXPECT_GT(Memory::globalLiveBlocks(), 0u);
    EXPECT_EQ(Memory::lateFrees(), 0u);
}

TEST_F(TlsMemory, HandshakeInsideAScopeTakesBlocksFromThatOwnersSubpoolOnly)
{
    std::unique_ptr<ServerMemory> owner(new ServerMemory());
    std::unique_ptr<ServerMemory> other(new ServerMemory());
    ASSERT_TRUE(owner->published());
    ASSERT_TRUE(other->published());
    size_t global = Memory::globalLiveBlocks();
    {
        Memory::Scope scope(*owner);
        Profiles profiles;
        ASSERT_TRUE(open(profiles));
        std::vector<std::unique_ptr<TlsPump>> pumps;
        for (size_t i = 0; i < PAIRS; ++i)
        {
            pumps.emplace_back(new TlsPump(4096));
            ASSERT_TRUE(connect(*pumps.back(), profiles));
            EXPECT_EQ(roundTrip(*pumps.back(), "scoped"), "scoped");
        }
        EXPECT_GT(owner->liveBlocks(), 0u);
        EXPECT_EQ(other->liveBlocks(), 0u);
        EXPECT_EQ(Memory::globalLiveBlocks(), global);
    }
    EXPECT_EQ(owner->liveBlocks(), 0u);
    EXPECT_EQ(owner->failures(), 0u);
}

TEST_F(TlsMemory, GlobalStateStaysInTheGlobalArenaAfterAttachThread)
{
    std::unique_ptr<ServerMemory> owner(new ServerMemory());
    size_t global = Memory::globalLiveBlocks();
    size_t attached_global = 0;
    size_t attached_owner = 1;
    size_t during = 0;
    bool connected = false;
    std::thread worker([&]()
    {
        {
            Memory::Scope scope(*owner);
            attached_global = Memory::globalLiveBlocks();
            attached_owner = owner->liveBlocks();
            Profiles profiles;
            TlsPump pump(4096);
            connected = open(profiles) && connect(pump, profiles) && roundTrip(pump, "worker") == "worker";
            during = owner->liveBlocks();
        }
        Memory::detachThread();
    });
    worker.join();
    EXPECT_GT(attached_global, global);
    EXPECT_EQ(attached_owner, 0u);
    EXPECT_TRUE(connected);
    EXPECT_GT(during, 0u);
    EXPECT_EQ(owner->liveBlocks(), 0u);
    EXPECT_EQ(Memory::globalLiveBlocks(), global);
}

TEST_F(TlsMemory, SubpoolExhaustionFailsOnlyThatHandshakeAndCountsAFailure)
{
    std::unique_ptr<ServerMemory> owner(new ServerMemory());
    std::unique_ptr<ServerMemory> other(new ServerMemory());
    size_t global_failures = 0;
    for (size_t i = 0; i < Memory::globalClassCount(); ++i)
    {
        global_failures += Memory::globalUsage(i).failures;
    }
    {
        Memory::Scope scope(*owner);
        Profiles profiles;
        ASSERT_TRUE(open(profiles));
        std::vector<void*> taken;
        void* block = allocate(1);
        while (block != nullptr)
        {
            taken.push_back(block);
            block = allocate(1);
        }
        size_t failures = owner->failures();
        {
            TlsPump starved(4096);
            EXPECT_FALSE(connect(starved, profiles));
        }
        EXPECT_GT(owner->failures(), failures);
        {
            Memory::Scope inner(*other);
            Profiles others;
            TlsPump pump(4096);
            EXPECT_TRUE(open(others) && connect(pump, others));
        }
        for (void* held : taken)
        {
            release(held);
        }
        TlsPump fed(4096);
        EXPECT_TRUE(connect(fed, profiles));
    }
    size_t after = 0;
    for (size_t i = 0; i < Memory::globalClassCount(); ++i)
    {
        after += Memory::globalUsage(i).failures;
    }
    EXPECT_EQ(after, global_failures);
    EXPECT_EQ(owner->liveBlocks(), 0u);
    EXPECT_EQ(other->liveBlocks(), 0u);
    EXPECT_EQ(other->failures(), 0u);
}

TEST_F(TlsMemory, ReleaseOfABlockFromAnotherOwnersSubpoolFindsItThroughTheRegistry)
{
    std::unique_ptr<ServerMemory> first(new ServerMemory());
    std::unique_ptr<ServerMemory> second(new ServerMemory());
    size_t late = Memory::lateFrees();
    void* block = nullptr;
    {
        Memory::Scope scope(*first);
        block = allocate(BLOCK_BYTES);
    }
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(first->liveBlocks(), 1u);
    {
        Memory::Scope scope(*second);
        release(block);
    }
    EXPECT_EQ(first->liveBlocks(), 0u);
    EXPECT_EQ(second->liveBlocks(), 0u);
    EXPECT_EQ(Memory::lateFrees(), late);
}

TEST_F(TlsMemory, LateFreesAreZeroAfterEveryObjectIsClosed)
{
    std::unique_ptr<ServerMemory> owner(new ServerMemory());
    std::unique_ptr<Profiles> profiles(new Profiles());
    {
        Memory::Scope scope(*owner);
        ASSERT_TRUE(open(*profiles));
        TlsPump pump(4096);
        ASSERT_TRUE(connect(pump, *profiles));
    }
    EXPECT_GT(owner->liveBlocks(), 0u);
    profiles.reset();
    Memory::detachThread();
    EXPECT_EQ(owner->liveBlocks(), 0u);
    EXPECT_EQ(Memory::lateFrees(), 0u);
}

TEST_F(TlsMemory, DetachThreadReturnsCurrentToTheGlobalArena)
{
    std::unique_ptr<ServerMemory> owner(new ServerMemory());
    size_t owned = 1;
    bool global_grew = false;
    size_t attached = 0;
    size_t detached = 0;
    size_t again = 0;
    std::thread worker([&]()
    {
        {
            Memory::Scope scope(*owner);
            Memory::detachThread();
            size_t global = Memory::globalLiveBlocks();
            void* block = allocate(BLOCK_BYTES);
            global_grew = Memory::globalLiveBlocks() > global;
            owned = owner->liveBlocks();
            release(block);
        }
        {
            Memory::Scope scope(*owner);
            attached = Memory::globalLiveBlocks();
        }
        Memory::detachThread();
        detached = Memory::globalLiveBlocks();
        {
            Memory::Scope scope(*owner);
            again = Memory::globalLiveBlocks();
        }
        Memory::detachThread();
    });
    worker.join();
    EXPECT_TRUE(global_grew);
    EXPECT_EQ(owned, 0u);
    EXPECT_LT(detached, attached);
    EXPECT_GT(again, detached);
}

TEST_F(TlsMemory, NestedScopesRestoreThePreviousArenaOnExit)
{
    std::unique_ptr<ServerMemory> outer(new ServerMemory());
    std::unique_ptr<ServerMemory> inner(new ServerMemory());
    {
        Memory::Scope first(*outer);
        void* a = allocate(BLOCK_BYTES);
        {
            Memory::Scope second(*inner);
            void* b = allocate(BLOCK_BYTES);
            EXPECT_EQ(inner->liveBlocks(), 1u);
            release(b);
        }
        void* c = allocate(BLOCK_BYTES);
        EXPECT_EQ(outer->liveBlocks(), 2u);
        EXPECT_EQ(inner->liveBlocks(), 0u);
        release(a);
        release(c);
    }
    size_t global = Memory::globalLiveBlocks();
    void* d = allocate(BLOCK_BYTES);
    EXPECT_EQ(Memory::globalLiveBlocks(), global + 1);
    release(d);
    EXPECT_EQ(outer->liveBlocks(), 0u);
    EXPECT_EQ(Memory::globalLiveBlocks(), global);
}

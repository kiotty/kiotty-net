#if !defined(KIOTTY_TEST_TRACKED_H)
#define KIOTTY_TEST_TRACKED_H

namespace kiotty_test
{
    struct Ledger
    {
        int acquired {0};
        int released {0};
        int copies {0};
        int moves {0};
        int destructor_runs {0};

        int live() const { return acquired - released; }
    };

    class Tracked
    {
    public:
        Tracked() :
            _ledger(nullptr),
            _id(0)
        {
        }

        Tracked(Ledger& ledger, int id) :
            _ledger(&ledger),
            _id(id)
        {
            ++_ledger->acquired;
        }

        Tracked(const Tracked& other) :
            _ledger(other._ledger),
            _id(other._id)
        {
            if (_ledger != nullptr)
            {
                ++_ledger->acquired;
                ++_ledger->copies;
            }
        }

        Tracked(Tracked&& other) noexcept :
            _ledger(other._ledger),
            _id(other._id)
        {
            if (_ledger != nullptr)
            {
                ++_ledger->acquired;
                ++_ledger->moves;
            }
            other.letGo();
        }

        Tracked& operator=(const Tracked& other)
        {
            if (this != &other)
            {
                letGo();

                _ledger = other._ledger;
                _id     = other._id;

                if (_ledger != nullptr)
                {
                    ++_ledger->acquired;
                    ++_ledger->copies;
                }
            }
            return *this;
        }

        Tracked& operator=(Tracked&& other) noexcept
        {
            if (this != &other)
            {
                letGo();

                _ledger = other._ledger;
                _id     = other._id;

                if (_ledger != nullptr)
                {
                    ++_ledger->acquired;
                    ++_ledger->moves;
                }
                other.letGo();
            }
            return *this;
        }

        ~Tracked()
        {
            if (_ledger != nullptr)
            {
                ++_ledger->destructor_runs;
            }
            letGo();
        }

        bool owns() const { return _ledger != nullptr; }
        int id() const { return _id; }

    private:
        void letGo()
        {
            if (_ledger != nullptr)
            {
                ++_ledger->released;
                _ledger = nullptr;
            }
        }

        Ledger* _ledger;
        int     _id;
    };
}

#endif

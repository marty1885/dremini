#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <drogon/utils/Utilities.h>
#include <sys/mman.h>
#include <unistd.h>

namespace dremini
{
// Exploitation friction: process-random encoding plus a slot-bound integrity
// tag. This keyed mixer is not a cryptographic MAC and does not defend against
// an attacker who can read the secret or write the protected page.
// Each pointed-to type has independent secrets, allocated read-only at first
// use. The secret allocation intentionally lasts for the process lifetime.
template <typename T> class GuardedPointer
{
    static std::uint64_t rotateLeft(std::uint64_t value) noexcept
    {
        return (value << 17) | (value >> 47);
    }
    static std::uint64_t mix(std::uint64_t value) noexcept
    {
        value ^= value >> 30;
        value *= UINT64_C(0xbf58476d1ce4e5b9);
        value ^= value >> 27;
        value *= UINT64_C(0x94d049bb133111eb);
        return value ^ (value >> 31);
    }
    struct Secret
    {
        std::uint64_t mask;
        std::uint64_t authentication[2];
    };
    static const Secret &secret()
    {
        static const Secret *const value = [] {
            const auto pageSize = sysconf(_SC_PAGESIZE);
            if (pageSize <= 0 || sizeof(Secret) > static_cast<std::size_t>(pageSize))
                std::terminate();
            void *const page = mmap(nullptr, static_cast<std::size_t>(pageSize),
                                    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
                std::terminate();
            auto *memory = static_cast<Secret *>(page);
            if (!drogon::utils::secureRandomBytes(memory, sizeof(Secret)))
                std::terminate();
            if (mprotect(page, static_cast<std::size_t>(pageSize), PROT_READ) != 0)
                std::terminate();
            return memory;
        }();
        return *value;
    }
    std::uint64_t tag(std::uint64_t encoded) const noexcept
    {
        const auto &key = secret().authentication;
        const auto slot = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
        return mix(encoded ^ key[0]) ^ mix(slot ^ key[1] ^ encoded);
    }

  public:
    explicit GuardedPointer(T *value = nullptr) noexcept
    {
        reset(value);
    }
    GuardedPointer(const GuardedPointer &other) noexcept
    {
        reset(other.get());
    }
    GuardedPointer &operator=(const GuardedPointer &other) noexcept
    {
        reset(other.get());
        return *this;
    }
    void reset(T *value = nullptr) noexcept
    {
        encoded_ = rotateLeft(reinterpret_cast<std::uintptr_t>(value) ^ secret().mask ^
                              reinterpret_cast<std::uintptr_t>(this));
        authentication_ = tag(encoded_);
    }
    T *get() const noexcept
    {
        const auto expected = tag(encoded_);
        if (expected != authentication_)
            std::terminate();
        return reinterpret_cast<T *>(((encoded_ >> 17) | (encoded_ << 47)) ^ secret().mask ^
                                     reinterpret_cast<std::uintptr_t>(this));
    }
    T *operator->() const noexcept
    {
        return get();
    }
    T &operator*() const noexcept
    {
        return *get();
    }
    explicit operator bool() const noexcept
    {
        return get() != nullptr;
    }

  private:
    std::uint64_t encoded_;
    std::uint64_t authentication_;
};

template <typename T> class GuardedOwner
{
  public:
    explicit GuardedOwner(T *value = nullptr) noexcept : pointer_(value)
    {
    }
    GuardedOwner(const GuardedOwner &) = delete;
    GuardedOwner &operator=(const GuardedOwner &) = delete;
    ~GuardedOwner()
    {
        delete pointer_.get();
    }
    void reset(T *value = nullptr) noexcept
    {
        auto *old = pointer_.get();
        pointer_.reset(value);
        delete old;
    }
    T *get() const noexcept
    {
        return pointer_.get();
    }
    T *operator->() const noexcept
    {
        return get();
    }
    T &operator*() const noexcept
    {
        return *get();
    }
    explicit operator bool() const noexcept
    {
        return bool(pointer_);
    }

  private:
    GuardedPointer<T> pointer_;
};
} // namespace dremini

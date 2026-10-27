#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <sodium.h>
#include <utility>

namespace dremini
{
// Exploitation friction: process-random encoding plus a slot-bound integrity
// tag. A readable process secret is not a boundary against arbitrary read.
// Each pointed-to type has independent secrets, allocated read-only at first
// use. The secret allocation intentionally lasts for the process lifetime.
template <typename T> class GuardedPointer
{
    static std::uint64_t rotateLeft(std::uint64_t value) noexcept
    {
        return (value << 17) | (value >> 47);
    }
    struct Secret
    {
        std::uint64_t mask;
        unsigned char authentication[crypto_shorthash_KEYBYTES];
    };
    static const Secret &secret()
    {
        static const Secret *const value = [] {
            if (sodium_init() < 0)
                std::terminate();
            auto *memory = static_cast<Secret *>(sodium_malloc(sizeof(Secret)));
            if (!memory)
                std::terminate();
            randombytes_buf(memory, sizeof(Secret));
            if (sodium_mprotect_readonly(memory) != 0)
                std::terminate();
            return memory;
        }();
        return *value;
    }
    std::uint64_t tag(std::uint64_t encoded) const noexcept
    {
        const std::uint64_t input[]{encoded, reinterpret_cast<std::uintptr_t>(this)};
        std::uint64_t output;
        crypto_shorthash(reinterpret_cast<unsigned char *>(&output),
                         reinterpret_cast<const unsigned char *>(input), sizeof(input),
                         secret().authentication);
        return output;
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
        if (sodium_memcmp(&expected, &authentication_, sizeof(expected)) != 0)
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

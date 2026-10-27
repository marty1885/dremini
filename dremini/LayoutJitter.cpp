#include "LayoutJitter.hpp"

#include <drogon/utils/Utilities.h>

#include <exception>

namespace dremini
{
LayoutJitter::LayoutJitter()
{
    if (!drogon::utils::secureRandomBytes(random_.data(), sizeof(random_)))
        std::terminate();
    const auto size = 64 + random_[0] % 4033;
    heap_.reset(new unsigned char[size]);
    auto *memory = static_cast<volatile unsigned char *>(heap_.get());
    touchStack(memory, size);
}

std::size_t LayoutJitter::stackBytes() const noexcept
{
    return 64 + random_[1] % 61 * 16;
}

void LayoutJitter::touchStack(volatile unsigned char *memory, std::size_t size) const noexcept
{
    // Fully initialize every byte. Each aligned word has noncanonical high
    // bits on x86-64 (including LA57). Every byte also has high bits 10,
    // so misaligned words are noncanonical too.
    for (std::size_t index = 0; index < size; ++index)
    {
        const auto byte = index % 8;
        memory[index] = byte >= 6 ? static_cast<unsigned char>(0xa5) :
            static_cast<unsigned char>(0x80 | ((random_[index % 2] >> (byte * 8)) & 0x3f));
    }
}
}  // namespace dremini

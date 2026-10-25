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
    heap_ = std::make_unique<unsigned char[]>(size);
    auto *memory = static_cast<volatile unsigned char *>(heap_.get());
    for (std::size_t index = 0; index < size; index += 64)
        memory[index] = static_cast<unsigned char>(random_[0] >> (index % 56));
    memory[size - 1] = static_cast<unsigned char>(random_[1]);
}

std::size_t LayoutJitter::stackBytes() const noexcept
{
    return 64 + random_[1] % 61 * 16;
}

void LayoutJitter::touchStack(volatile unsigned char *memory, std::size_t size) const noexcept
{
    for (std::size_t index = 0; index < size; index += 64)
        memory[index] = static_cast<unsigned char>(random_[1] >> (index % 56));
    memory[size - 1] = static_cast<unsigned char>(random_[0]);
}
}  // namespace dremini

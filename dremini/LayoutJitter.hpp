#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "GuardedPointer.hpp"

namespace dremini
{
class LayoutJitter
{
  public:
    LayoutJitter();
    ~LayoutJitter() { delete[] heap_.get(); }
    LayoutJitter(const LayoutJitter &) = delete;
    LayoutJitter &operator=(const LayoutJitter &) = delete;
    [[nodiscard]] std::size_t stackBytes() const noexcept;
    void touchStack(volatile unsigned char *memory, std::size_t size) const noexcept;

  private:
    std::array<std::uint64_t, 2> random_{};
    dremini::GuardedPointer<unsigned char> heap_;
};
}  // namespace dremini

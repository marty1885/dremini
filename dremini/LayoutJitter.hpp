#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace dremini
{
class LayoutJitter
{
  public:
    LayoutJitter();
    [[nodiscard]] std::size_t stackBytes() const noexcept;
    void touchStack(volatile unsigned char *memory, std::size_t size) const noexcept;

  private:
    std::array<std::uint64_t, 2> random_{};
    std::unique_ptr<unsigned char[]> heap_;
};
}  // namespace dremini

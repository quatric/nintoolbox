#pragma once

#include <cstdint>

#include <gsl/gsl-lite.hpp>

namespace latte
{

std::string
disassemble(const gsl::span<const uint8_t> &binary, bool isSubroutine = false);

} // namespace latte

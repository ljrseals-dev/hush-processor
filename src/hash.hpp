#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include "result.hpp"
namespace fk::hash {
std::string sha256_hex(const unsigned char* data, std::size_t n);
Result<std::string> sha256_file(const std::filesystem::path& path);
}  // namespace fk::hash

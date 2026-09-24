#pragma once

#include <cstddef>
#include <string>

using String = std::string;

namespace obfuscation {
inline String obfuscateToBase64(const std::string& value) { return value; }
inline std::string deobfuscateFromBase64(const char* value, size_t, bool* ok, bool*) {
  if (ok) *ok = true;
  return value;
}
}  // namespace obfuscation

#pragma once
#include <cstdint>

namespace nfd::fw {

struct CacheStats {
  uint64_t interests = 0;
  uint64_t hits      = 0;
  uint64_t evictions = 0;
};

// “extern” means “the real variable lives elsewhere”
extern CacheStats g_cacheStats;

} // namespace nfd::fw


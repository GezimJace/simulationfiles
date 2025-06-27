#include "cms.hpp"

#include <algorithm>     // std::min
#include <functional>    // std::hash
#include <string>

namespace {

/** Fixed 32-bit odd constant (fraction of golden ratio) for mix */
constexpr uint32_t PRIME = 0x9E3779B9u;

/** Cheap per-row mix: xor with a unique 32-bit seed */
inline std::size_t
mix(std::size_t base, uint32_t seed)
{
  return base ^ static_cast<std::size_t>(seed);
}

} // unnamed namespace
/* --------------------------------------------------------------------- */

CountMinSketch::CountMinSketch(std::size_t d, std::size_t w)
  : m_depth(d)
  , m_width(w)
  , m_table(d, std::vector<uint32_t>(w, 0))
  , m_seed(d)
{
  for (std::size_t i = 0; i < m_depth; ++i)
    m_seed[i] = static_cast<uint32_t>((i + 1) * PRIME);
}

void
CountMinSketch::increment(const ndn::Name& name)
{
  const std::string key  = name.toUri();                // TLV→URI once
  const std::size_t base = std::hash<std::string>{}(key);

  for (std::size_t i = 0; i < m_depth; ++i) {
    std::size_t idx = mix(base, m_seed[i]) % m_width;
    ++m_table[i][idx];
  }
}

uint64_t
CountMinSketch::estimate(const ndn::Name& name) const
{
  const std::string key  = name.toUri();
  const std::size_t base = std::hash<std::string>{}(key);

  uint64_t est = UINT64_MAX;
  for (std::size_t i = 0; i < m_depth; ++i) {
    std::size_t idx = mix(base, m_seed[i]) % m_width;
    est = std::min<uint64_t>(est, m_table[i][idx]);
  }
  return est;
}


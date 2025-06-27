#ifndef CMS_HPP
#define CMS_HPP

#include <vector>
#include <cstddef>
#include <cstdint>
#include <ndn-cxx/name.hpp>

/** Simple Count–Min Sketch for positive-integer frequencies. */
class CountMinSketch
{
public:
  /** @param d depth  (number of hash rows)
      @param w width  (counters per row) */
  CountMinSketch(std::size_t d, std::size_t w);

  void     increment(const ndn::Name& name);
  uint64_t estimate (const ndn::Name& name) const;

private:
  /* declaration order == initialiser list order (avoids -Wreorder) */
  std::size_t                                   m_depth;
  std::size_t                                   m_width;
  std::vector<std::vector<uint32_t>>            m_table; ///< [d][w] counters
  std::vector<uint32_t>                         m_seed;  ///< per-row seed
};

#endif // CMS_HPP


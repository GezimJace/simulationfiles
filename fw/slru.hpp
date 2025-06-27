#pragma once
#include <list>
#include <unordered_map>
#include <memory>
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/data.hpp>

/** Simple two-segment LRU (SLRU) with fixed segment sizes.
 *
 *  ─ New insertions start in the probation segment.
 *  ─ A hit in probation promotes the entry to the protected segment.
 *  ─ A hit in protected refreshes its MRU position.
 *  ─ Eviction policy:
 *        • if the cache is full, evict LRU of probation;
 *        • if probation becomes empty, evict LRU of protected.
 */
class SlruCache
{
public:
  using DataPtr = std::shared_ptr<const ndn::Data>;

  /// @param probationCap  #entries in probation segment
  /// @param protectedCap  #entries in protected segment
  explicit SlruCache(size_t probationCap = 50, size_t protectedCap = 50);

  bool      contains(const ndn::Name& name) const;
  bool      insert  (const ndn::Name& name, const DataPtr& data);
  bool      isFull() const;
  ndn::Name selectVictim() const;

  /// Lookup; returns nullptr on miss
  DataPtr fetch(const ndn::Name& name);

private:
  // ─ helpers ------------------------------------------------------------
  using List = std::list<ndn::Name>;          // MRU at front
  using Map  = std::unordered_map<ndn::Name,
                                  std::pair<DataPtr, List::iterator>>;

  void promoteToProtected(const ndn::Name& name, List::iterator lit);
  void ensureCaps();                          // fix any overflow

  // ─ members ------------------------------------------------------------
  size_t m_capProb;
  size_t m_capProt;

  List m_probList;        ///< probation   (MRU front)
  List m_protList;        ///< protected   (MRU front)
  Map  m_store;           ///< name → (data, iterator into its list)
};


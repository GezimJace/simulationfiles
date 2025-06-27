#include "slru.hpp"
#include "NFD/daemon/common/logger.hpp"
#include <cassert>

NFD_LOG_INIT(slru);

using ndn::Name;
using ndn::Data;

SlruCache::SlruCache(size_t probationCap, size_t protectedCap)
  : m_capProb(probationCap)
  , m_capProt(protectedCap)
{
  assert(m_capProb + m_capProt > 0);
}

// ────────────────────────────────────────────────────────────────
// helpers
void
SlruCache::promoteToProtected(const Name& name, List::iterator lit)
{
  m_probList.erase(lit);
  m_protList.push_front(name);
  m_store[name].second = m_protList.begin();

  if (m_protList.size() > m_capProt) {
    Name demoted = m_protList.back();
    m_protList.pop_back();
    m_probList.push_front(demoted);
    m_store[demoted].second = m_probList.begin();
  }
}

void
SlruCache::ensureCaps()
{
  if (isFull()) {
    if (!m_probList.empty()) {
      Name victim = m_probList.back();
      m_probList.pop_back();
      m_store.erase(victim);
      NFD_LOG_INFO("SLRU-EVICT " << victim); // ✔ instrumentation
    }
    else if (!m_protList.empty()) {
      Name victim = m_protList.back();
      m_protList.pop_back();
      m_store.erase(victim);
      NFD_LOG_INFO("SLRU-EVICT " << victim);
    }
  }
}

// ────────────────────────────────────────────────────────────────
// queries
bool
SlruCache::contains(const Name& name) const
{
  return m_store.find(name) != m_store.end();
}

bool
SlruCache::isFull() const
{
  return (m_probList.size() + m_protList.size()) >=
         (m_capProb + m_capProt);
}

Name
SlruCache::selectVictim() const
{
  if (!m_probList.empty())  return m_probList.back();
  if (!m_protList.empty())  return m_protList.back();
  return Name();            // empty
}

// ────────────────────────────────────────────────────────────────
// insert / fetch
bool
SlruCache::insert(const Name& name, const DataPtr& data)
{
  auto it = m_store.find(name);
  if (it != m_store.end()) {
    it->second.first = data;
    fetch(name);                  // refresh position
    NFD_LOG_INFO("SLRU-INSERT " << name); 
    return true;
  }

  m_probList.push_front(name);    // new → MRU probation
  m_store.emplace(name,
                  std::make_pair(data, m_probList.begin()));
  ensureCaps();
  return true;
}

SlruCache::DataPtr
SlruCache::fetch(const Name& name)
{
  auto it = m_store.find(name);
  if (it == m_store.end())
    return nullptr;

  auto listIt = it->second.second;

  // Are we still in probation?
  if (std::find(m_probList.begin(), m_probList.end(), name)
        != m_probList.end()) {
    promoteToProtected(name, listIt);
  }
  else {                                     // already in protected
    m_protList.erase(listIt);
    m_protList.push_front(name);             // move to MRU
    it->second.second = m_protList.begin();
  }
  NFD_LOG_INFO("SLRU-HIT   " << name); 
  return it->second.first;
}


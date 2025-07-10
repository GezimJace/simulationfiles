// custom-strategy.cpp -------------------------------------------------
// (Full implementation with metric‑gathering additions: cache & energy)
// ‑‑ revised to use a shared CacheStats declared in cache‑stats.hpp (hits
// & evictions are now updated inside slru.cpp; this file only counts
// Interests).

#include "custom-strategy.hpp"
#include "cache-stats.hpp"          // <‑‑ new shared stats header

#include "NFD/daemon/common/logger.hpp"
#include <ndn-cxx/security/key-chain.hpp>
#include <vector>
#include <ns3/simulator.h>
#include <ns3/node-list.h>
#include <ns3/node.h>
#include <ns3/basic-energy-source.h>

// extra standard headers
#include <cstdint>
#include <fstream>
#include <random>
#include <algorithm>

// ---------------------------------------------------------------------------
//  Simple per‑operation energy model (anonymous namespace keeps symbols local)
// ---------------------------------------------------------------------------
namespace {
// Per‑operation energy costs (arbitrary units; tune as needed)
constexpr double E_INTEREST_RX  = 1.0;
constexpr double E_INTEREST_TX  = 1.0;
constexpr double E_DATA_RX      = 2.0;
constexpr double E_DATA_TX      = 2.0;
constexpr double E_CACHE_INSERT = 1.5;

// Global vector indexed by ns‑3 NodeId (energy accounting)
std::vector<double> g_nodeEnergy;

// pull the shared statistics object into this TU
using nfd::fw::g_cacheStats;

// One‑time initializer that sizes the vector once nodes exist
struct EnergyInit {
  EnergyInit() {
    uint32_t n = ns3::NodeList::GetNNodes();
    g_nodeEnergy.resize(n, 0.0);
  }
} s_energyInit;

// Helper – add energy to the current context/node
constexpr double UNIT_TO_J = 0.005;          // tune as needed

inline void addEnergy(double units)
{
  using namespace ns3;
  uint32_t id = Simulator::GetContext();   // “current node id” for the event

  /* 1. ensure the vector is big enough */
  if (id >= g_nodeEnergy.size())
    g_nodeEnergy.resize(id + 1, 0.0);

  g_nodeEnergy[id] += units;

  /* 2. drain the battery, if the node has one */
  Ptr<Node> node = NodeList::GetNode(id);
  if (!node)
    return;

  Ptr<BasicEnergySource> src = node->GetObject<BasicEnergySource>();
  if (src)
    src->ConsumeEnergy(units * UNIT_TO_J);
}

// -----------------------------------------------------------------------
//  At‑end‑of‑simulation metric dump helper
// -----------------------------------------------------------------------
static void PrintMetrics()
{

  // 1. cache‑stats.txt
  std::ofstream cs("metrics/cache-stats.txt");
cs << "interests " << g_cacheStats.interests                     << '\n'
   << "hits "      << g_cacheStats.hits                          << '\n'
   << "misses "    << (g_cacheStats.interests - g_cacheStats.hits) << '\n'
   << "evictions " << g_cacheStats.evictions                     << '\n';

if (g_cacheStats.interests > 0) {
  double hitRate = static_cast<double>(g_cacheStats.hits) / g_cacheStats.interests;
  cs << "hitrate " << hitRate << '\n';          // 0.0 – 1.0
} else {
  cs << "hitrate 0\n";
}

}

} // anonymous namespace (energy + metrics helpers)

// ---------------------------------------------------------------------------
//  Module‑wide TLV constants (kept in same anon‑namespace block)
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t TLV_ACCESS_DELTA  = 0xF0;
constexpr uint32_t TLV_ACCESS_VECTOR = 0xF1;
constexpr uint32_t TLV_THETA_VECTOR  = 0xF2;
constexpr uint32_t TLV_THETA_PAIR    = 0xF3;
} // anonymous namespace (TLVs)

// ---------------------------------------------------------------------------
//  Strategy registration boilerplate
// ---------------------------------------------------------------------------
NFD_LOG_INIT(CustomStrategy);

namespace nfd::fw {

// -----------------------------------------------------------------------
//  Shared statistics object – single definition lives here
// -----------------------------------------------------------------------
CacheStats g_cacheStats;

const ndn::Name CustomStrategy::STRATEGY_NAME =
  ndn::Name("/localhost/nfd/strategy/custom").appendVersion(1);

NFD_REGISTER_STRATEGY(CustomStrategy);

const ndn::Name&
CustomStrategy::getStrategyName()
{
  return STRATEGY_NAME;
}

// ---------------------------------------------------------------------------
//  Ctor
// ---------------------------------------------------------------------------
CustomStrategy::CustomStrategy(Forwarder& forwarder, const ndn::Name& name)
  : BestRouteStrategy(forwarder)
  , m_cms(4, 2048)          // 4 rows × 2 KiB each
  , m_slru(25, 25)           // 5 probation + 5 protected (total 10 entries)
  , m_rng(std::random_device{}())
  , m_uni(0.0, 1.0)
{
  this->setInstanceName(name);
  scheduleNextReport();

  // Dump metrics when the simulator terminates
  ns3::Simulator::ScheduleDestroy(&PrintMetrics);
}

// ---------------------------------------------------------------------------
//  afterReceiveInterest – SLRU hit & upstream forwarding
// ---------------------------------------------------------------------------
void CustomStrategy::afterReceiveInterest(const ndn::Interest&          interest,
                                          const nfd::FaceEndpoint&      ingress,
                                          const std::shared_ptr<pit::Entry>& pitEntry)
{
  // Count every Interest arrival
  ++g_cacheStats.interests;

  // Energy: Interest Rx cost
  addEnergy(E_INTEREST_RX);

  const ndn::Name& name = interest.getName();

  // 1. Serve from SLRU (cache hit)
  if (m_slru.contains(name)) {
    if (auto dataPtr = m_slru.fetch(name)) {
      this->sendData(*dataPtr, ingress.face, pitEntry);
      addEnergy(E_DATA_TX);   // Tx energy for Data (hits counted inside slru.cpp)
    }
    return; // no upstream forwarding
  }

  // 2. Record Interest for periodic report
  ++m_accessCounter[name].total;

  // 3. Forward upstream via BestRoute – count Tx energy
  addEnergy(E_INTEREST_TX);
  BestRouteStrategy::afterReceiveInterest(interest, ingress, pitEntry);
}

// ---------------------------------------------------------------------------
//  beforeSatisfyInterest – CMS update & cache admission
// ---------------------------------------------------------------------------
void CustomStrategy::beforeSatisfyInterest(const ndn::Data&                   data,
                                           const nfd::FaceEndpoint&           ingress,
                                           const std::shared_ptr<pit::Entry>& pitEntry)
{
  // Energy: Data arriving from upstream
  addEnergy(E_DATA_RX);

  const ndn::Name& name = data.getName();

  // Control‑plane packets from the fog controller are never cached/forwarded
  if (name.size() >= 2 && name.getSubName(0, 2) == ndn::Name("/fog/instruction")) {
    receiveFogInstruction(data);
    return;
  }

  // 1. Update frequency sketch
  m_cms.increment(name);

  // 2. Probabilistic cache admission (θ_cache)
  double theta = m_defaultTheta;
  if (auto it = m_thetaCache.find(name); it != m_thetaCache.end())
    theta = it->second;

  if (m_uni(m_rng) < theta) {
    uint64_t estNew  = m_cms.estimate(name);
    auto     dataPtr = std::make_shared<ndn::Data>(data);

    if (!m_slru.isFull()) {
      m_slru.insert(name, dataPtr);
      addEnergy(E_CACHE_INSERT);                // Energy: cache insert cost
    }
    else {
      ndn::Name victim    = m_slru.selectVictim();
      uint64_t  estVictim = m_cms.estimate(victim);

      if (estVictim <= estNew) {
        m_slru.insert(name, dataPtr);
        addEnergy(E_CACHE_INSERT);              // Energy: cache insert cost
        // Eviction counter is incremented inside slru.cpp
      }
    }
  }

  // 3. Standard BestRoute downstream satisfaction
  BestRouteStrategy::beforeSatisfyInterest(data, ingress, pitEntry);
}

// ---------------------------------------------------------------------------
//  Fog‑controller θ_cache update parser
// ---------------------------------------------------------------------------
void CustomStrategy::receiveFogInstruction(const ndn::Data& inst)
{
  const ndn::Block& payload = inst.getContent();
  if (!payload.hasWire() || payload.type() != TLV_THETA_VECTOR) {
    NFD_LOG_WARN("FOG_INSTRUCTION malformed, ignore");
    return;
  }

  ndn::Block seq = payload;
  seq.parse();

  for (const ndn::Block& pair : seq.elements()) {
    if (pair.type() != TLV_THETA_PAIR)
      continue;

    pair.parse();
    auto it      = pair.elements_begin();
    ndn::Name name(*it); ++it;
    uint64_t thetaFixed = ndn::readNonNegativeInteger(*it);

    double theta = std::clamp(static_cast<double>(thetaFixed) / 10000.0, 0.0, 1.0);
    m_thetaCache[name] = theta;
    NFD_LOG_INFO("θ_cache updated " << name << " ← " << theta);
  }
}

// ---------------------------------------------------------------------------
//  Periodic access report (unchanged)
// ---------------------------------------------------------------------------
void CustomStrategy::scheduleNextReport()
{
  using namespace ns3;
  m_reportEvent = Simulator::Schedule(m_reportInterval,
                                      &CustomStrategy::sendAccessReport, this);
}

void CustomStrategy::sendAccessReport()
{
  ndn::EncodingBuffer payload;
  size_t nonZero = 0;

  for (auto& [name, info] : m_accessCounter) {
    uint64_t delta = info.total - info.last;
    if (delta == 0)
      continue;

    nonZero++;
    info.last = info.total;

    payload.prependVarNumber(delta);
    payload.prependVarNumber(TLV_ACCESS_DELTA);
    name.wireEncode(payload);
  }

  if (nonZero == 0) {
    scheduleNextReport();
    return;
  }

  payload.prependVarNumber(payload.size());
  payload.prependVarNumber(TLV_ACCESS_VECTOR);

  ndn::Name rptName("/fog/access-report");
  rptName.appendVersion();

  auto data = std::make_shared<ndn::Data>(rptName);
  data->setContent(payload.block());
  data->setFreshnessPeriod(ndn::time::seconds(1));

  static ndn::security::KeyChain keyChain;
  keyChain.sign(*data);

  for (auto& face : this->getFaceTable()) {
    std::string uri = face.getRemoteUri().toString();
    bool isLocal = uri.rfind("internal://", 0) == 0 ||
                   uri.rfind("appFace://",  0) == 0 ||
                   uri.find("contentstore")    != std::string::npos;
    if (isLocal)
      continue;
    face.sendData(*data);
  }

  NFD_LOG_INFO("ACCESS-REPORT sent entries=" << nonZero);
  scheduleNextReport();
}

} // namespace nfd::fw


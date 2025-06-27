// custom-strategy.cpp
// ===================
#include "custom-strategy.hpp"
#include "NFD/daemon/common/logger.hpp" 
#include <memory> 
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/interest.hpp>
#include "ns3/simulator.h"
#include <ndn-cxx/util/random.hpp>
#include <ndn-cxx/lp/tags.hpp>

NFD_LOG_INIT(CustomStrategy);

namespace {
constexpr uint32_t TLV_ACCESS_DELTA   = 0xF0;  // (name,Δ) pair
constexpr uint32_t TLV_ACCESS_VECTOR  = 0xF1;  // top-level sequence
}


namespace nfd {
namespace fw {

// ────────────────────────────────────────────────────────────────
//  Registration boilerplate
// ────────────────────────────────────────────────────────────────
const ndn::Name CustomStrategy::STRATEGY_NAME =
  ndn::Name("/localhost/nfd/strategy/custom").appendVersion(1);

NFD_REGISTER_STRATEGY(CustomStrategy);

const ndn::Name&
CustomStrategy::getStrategyName()
{
  return STRATEGY_NAME;
}

// ────────────────────────────────────────────────────────────────
//  Ctor
// ────────────────────────────────────────────────────────────────
CustomStrategy::CustomStrategy(Forwarder& forwarder, const ndn::Name& name)
  : BestRouteStrategy(forwarder)
  , m_cms(4, 2048)
  , m_slru(50, 50)          // 50 probation + 50 protected
  , m_rng(std::random_device{}())
  , m_uni(0.0, 1.0)
{
  this->setInstanceName(name);
  
  scheduleNextReport();

}

// ────────────────────────────────────────────────────────────────
//  afterReceiveInterest – counts + cache hit + duplicate aggregation
// ────────────────────────────────────────────────────────────────
void
CustomStrategy::afterReceiveInterest(const ndn::Interest&          interest,
                                     const nfd::FaceEndpoint&      ingress,
                                     const std::shared_ptr<nfd::pit::Entry>& pitEntry)
{
  const ndn::Name& name = interest.getName();

  // 0️⃣  Serve directly if we have it in SLRU
  if (m_slru.contains(name)) {
    auto dataPtr = m_slru.fetch(name);
    if (dataPtr != nullptr) {
      NFD_LOG_INFO("SLRU-HIT   " << name);   // ✔ less noisy
      this->sendData(*dataPtr, ingress.face, pitEntry);
    }
    return;                                   // no upstream forwarding
  }

  // 1️⃣  Increment per-content counter
  ++m_accessCounter[name].total;

  // 2️⃣  Duplicate aggregation check
  if (pitEntry->hasInRecords()) {
    BestRouteStrategy::afterReceiveInterest(interest, ingress, pitEntry);
    return;
  }

  // 3️⃣  Forward upstream via BestRoute logic
  BestRouteStrategy::afterReceiveInterest(interest, ingress, pitEntry);
}

// ────────────────────────────────────────────────────────────────
//  beforeSatisfyInterest – CMS + SLRU admission
// ────────────────────────────────────────────────────────────────
void
CustomStrategy::beforeSatisfyInterest(const ndn::Data&                 data,
                                      const nfd::FaceEndpoint&         ingress,
                                      const std::shared_ptr<nfd::pit::Entry>& pit)
{
  const ndn::Name& name = data.getName();

  // ── 1. recognise fog-controller control packets ────────────────
  if (name.size() >= 2 &&
      name.getSubName(0, 2) == ndn::Name("/fog/instruction")) {
    receiveFogInstruction(data);          // update θ-table
    return;                               // do NOT forward or cache this Data
  }

  // ── 2. frequency update ────────────────────────────────────────
  m_cms.increment(name);
  NFD_LOG_INFO("CMS-INC " << name);

  // ── 3. probabilistic admission  θ_cache ------------------------
  double theta = m_defaultTheta;          // 0.5 fallback
  if (auto it = m_thetaCache.find(name); it != m_thetaCache.end())
    theta = it->second;

  if (m_uni(m_rng) < theta) {
    uint64_t est     = m_cms.estimate(name);
    auto     dataPtr = std::make_shared<ndn::Data>(data);

    if (!m_slru.isFull()) {               // store has room
      m_slru.insert(name, dataPtr);
      NFD_LOG_INFO("SLRU-ADMIT " << name);
    }
    else {                                // store full → compare victim
      ndn::Name victim     = m_slru.selectVictim();
      uint64_t  estVictim  = m_cms.estimate(victim);

      NFD_LOG_INFO("CMS-COMP victim=" << victim
                    << " est=" << estVictim
                    << " | new=" << name
                    << " est=" << est);

      if (estVictim < est) {              // admission rule
        m_slru.insert(name, dataPtr);
        NFD_LOG_INFO("SLRU-ADMIT " << name);
      }
    }
  }

  BestRouteStrategy::beforeSatisfyInterest(data, ingress, pit);
}

void
CustomStrategy::scheduleNextReport()
{
  using namespace ns3;
  m_reportEvent =
      Simulator::Schedule(m_reportInterval,
                          &CustomStrategy::sendAccessReport, this);
}


void
CustomStrategy::sendAccessReport()
{
  ndn::EncodingBuffer payload;
  size_t nonZero = 0;

  for (auto& kv : m_accessCounter) {
    const ndn::Name& name  = kv.first;
    AccessInfo&      info  = kv.second;
    uint64_t         delta = info.total - info.last;

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
  bool isLocal =  uri.find("internal://")   == 0 ||
                  uri.find("appFace://")    == 0 ||
                  uri.find("contentstore")  == 0;

  if (isLocal)
    continue;  

  face.sendData(*data);           //  - OK: Face::sendData(const Data&)
}
  NFD_LOG_INFO("ACCESS-REPORT sent entries=" << nonZero);
  scheduleNextReport();
}

void
CustomStrategy::receiveFogInstruction(const ndn::Data& inst)
{
  // 1) quick sanity: content must be a TLV sequence -----------------
  const ndn::Block& payload = inst.getContent();
  if (!payload.hasWire() || payload.type() != TLV_THETA_VECTOR) {
    NFD_LOG_WARN("FOG_INSTRUCTION malformed, ignore");
    return;
  }

  // 2) iterate over (Name, θ) pairs --------------------------------
  ndn::Block seq = payload;   // make a copy to parse
  seq.parse();

  for (const ndn::Block& pair : seq.elements()) {
    if (pair.type() != TLV_THETA_PAIR)
      continue;
    pair.parse();

    // pair layout: <Name><NNI-theta>
    auto  it   = pair.elements_begin();
    ndn::Name name(*it);                      ++it;
    uint64_t  thetaFixed = ndn::readNonNegativeInteger(*it);

    double theta = static_cast<double>(thetaFixed) / 10000.0; // e.g., 7342 → 0.7342
    theta = std::clamp(theta, 0.0, 1.0);                      // safety

    m_thetaCache[name] = theta;
    NFD_LOG_INFO("θ_cache updated " << name << " ← " << theta);
  }
}

} // namespace fw
} // namespace nfd


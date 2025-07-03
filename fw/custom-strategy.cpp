// custom-strategy.cpp -------------------------------------------------

#include "custom-strategy.hpp"

#include "NFD/daemon/common/logger.hpp"
#include <ndn-cxx/security/key-chain.hpp>

// ---------------------------------------------------------------------------
//  Module‑wide constants
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t TLV_ACCESS_DELTA  = 0xF0;
constexpr uint32_t TLV_ACCESS_VECTOR = 0xF1;
constexpr uint32_t TLV_THETA_VECTOR  = 0xF2;
constexpr uint32_t TLV_THETA_PAIR    = 0xF3;
} // anonymous namespace

// ---------------------------------------------------------------------------
//  Strategy registration boilerplate
// ---------------------------------------------------------------------------
NFD_LOG_INIT(CustomStrategy);

namespace nfd::fw {

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
  , m_slru(100, 100)          // 50 probation + 50 protected
  , m_rng(std::random_device{}())
  , m_uni(0.0, 1.0)
{
  this->setInstanceName(name);
  scheduleNextReport();
}

// ---------------------------------------------------------------------------
//  afterReceiveInterest – SLRU hit & upstream forwarding
// ---------------------------------------------------------------------------
void CustomStrategy::afterReceiveInterest(const ndn::Interest&          interest,
                                          const nfd::FaceEndpoint&      ingress,
                                          const std::shared_ptr<pit::Entry>& pitEntry)
{
  const ndn::Name& name = interest.getName();

  // 1. Serve from SLRU (cache hit)
  if (m_slru.contains(name)) {
    if (auto dataPtr = m_slru.fetch(name)) {
      NFD_LOG_INFO("SLRU-HIT   " << name);
      this->sendData(*dataPtr, ingress.face, pitEntry);
    }
    return; // no upstream forwarding
  }

  // 2. Record Interest for periodic report
  ++m_accessCounter[name].total;

  // 3. Default BestRoute handling (duplicate suppression + forwarding)
  BestRouteStrategy::afterReceiveInterest(interest, ingress, pitEntry);
}

// ---------------------------------------------------------------------------
//  beforeSatisfyInterest – CMS update & cache admission
// ---------------------------------------------------------------------------
void CustomStrategy::beforeSatisfyInterest(const ndn::Data&                   data,
                                           const nfd::FaceEndpoint&           ingress,
                                           const std::shared_ptr<pit::Entry>& pitEntry)
{
  const ndn::Name& name = data.getName();

  // Control plane packets from the fog controller are never cached/forwarded
  if (name.size() >= 2 && name.getSubName(0, 2) == ndn::Name("/fog/instruction")) {
    receiveFogInstruction(data);
    return;
  }

  // 1. Update frequency sketch
  m_cms.increment(name);
  NFD_LOG_INFO("CMS-INC " << name);

  // 2. Probabilistic cache admission (θ_cache)
  double theta = m_defaultTheta;
  if (auto it = m_thetaCache.find(name); it != m_thetaCache.end())
    theta = it->second;

  if (m_uni(m_rng) < theta) {
    uint64_t estNew = m_cms.estimate(name);
    auto     dataPtr = std::make_shared<ndn::Data>(data);

    if (!m_slru.isFull()) {
      m_slru.insert(name, dataPtr);
      NFD_LOG_INFO("SLRU-ADMIT " << name);
    }
    else {
      ndn::Name victim    = m_slru.selectVictim();
      uint64_t  estVictim = m_cms.estimate(victim);

      NFD_LOG_INFO("CMS-COMP victim=" << victim << " est=" << estVictim
                    << " | new=" << name << " est=" << estNew);

      if (estVictim < estNew) {
        m_slru.insert(name, dataPtr);
        NFD_LOG_INFO("SLRU-ADMIT " << name);
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
//  Periodic access report
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


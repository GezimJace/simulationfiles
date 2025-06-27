// custom-strategy.hpp
// ===========================
#pragma once

#include "NFD/daemon/fw/strategy.hpp" 
#include <ndn-cxx/util/time.hpp>
#include <ns3/nstime.h>
#include <ns3/simulator.h>
#include <unordered_map>
#include "fw/best-route-strategy.hpp"
#include "cms.hpp"
#include "slru.hpp"
#include <random>

namespace nfd {
namespace fw {

class CustomStrategy : public nfd::fw::BestRouteStrategy
{

private:
  struct AccessInfo {
    uint64_t total    = 0;   // ever-seen interests
    uint64_t last = 0;   // snapshot used by NodeReportApp later
  };

  std::unordered_map<ndn::Name, AccessInfo> m_accessCounter;

public:
  static const ndn::Name STRATEGY_NAME;
  static const ndn::Name& getStrategyName();

  explicit CustomStrategy(Forwarder& forwarder, const ndn::Name& name);

  void afterReceiveInterest(const ndn::Interest&     interest,
                            const FaceEndpoint& ingress,
                            const std::shared_ptr<pit::Entry>& pitEntry) override;
 
void beforeSatisfyInterest(const ndn::Data& data,
                           const nfd::FaceEndpoint& ingress,
                           const std::shared_ptr<nfd::pit::Entry>& pitEntry) override;


private:
  // ---- step-3 state --------------------------------------------
  CountMinSketch           m_cms;
  SlruCache                m_slru;
  double                   m_thetaForward = 0.2;         // unused for now
  std::mt19937_64          m_rng;
  std::uniform_real_distribution<double> m_uni;

  // ── θ_cache table & defaults ───────────────────────────────────
  std::unordered_map<ndn::Name,double> m_thetaCache;      // per-content θ
  double                                m_defaultTheta = 0.5;  // fallback

  // ── periodic reporting (already added earlier) ────────────────
  ns3::Time   m_reportInterval{ns3::Seconds(10)};
  ns3::EventId m_reportEvent;
  void scheduleNextReport();
  void sendAccessReport();

  // ── fog-instruction handling  (NEW) ───────────────────────────
  void receiveFogInstruction(const ndn::Data& inst);

  // application-specific TLV codes for the instruction payload
  static constexpr uint32_t TLV_THETA_PAIR   = 0xF2;      // (Name, θ) pair
  static constexpr uint32_t TLV_THETA_VECTOR = 0xF3;      // sequence of pairs
};
} // namespace fw
} // namespace nfd


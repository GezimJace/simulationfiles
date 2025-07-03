/* grid-push-offpath.cc ---------------------------------------------------
 * 3×3 router mesh plus one stub “off-path” node per router.
 * --------------------------------------------------------------------- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/ndnSIM-module.h"

#include "ns3/ndnSIM/NFD/daemon/fw/forwarder.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/unsolicited-data-policy.hpp"

using namespace ns3;

/* --------------------------------------------------------------------- */
/* Helper: accept unsolicited Data on every net-device face              */
/* --------------------------------------------------------------------- */
static void
EnableAdmitNetworkUnsolicitedData()
{
  using namespace ns3::ndn;
  using nfd::fw::UnsolicitedDataPolicy;

  for (auto it = NodeList::Begin(); it != NodeList::End(); ++it) {
    Ptr<L3Protocol> l3 = (*it)->GetObject<L3Protocol>();
    if (!l3)
      continue;

    std::shared_ptr<nfd::Forwarder> fw = l3->getForwarder();
    fw->setUnsolicitedDataPolicy(
        UnsolicitedDataPolicy::create("admit-network"));
  }
}

/* --------------------------------------------------------------------- */
namespace ns3 {

int
main(int argc, char* argv[])
{
  /* ------------- CLI -------------------------------------------------- */
  uint32_t gridSize = 3;     // router grid N×N
  double   simTime  = 32;    // seconds
  double   thetaFwd = 0.2;   // θ_forward (still stored but unused)
  uint32_t nContents = 300;
  double   q = 0.7;
  double   freq = 200;

  CommandLine cmd;
  cmd.AddValue("gridSize", "router grid dimension", gridSize);
  cmd.AddValue("simTime",  "simulation time (s)",   simTime);
  cmd.AddValue("thetaForward", "θ_forward",         thetaFwd);
  cmd.Parse(argc, argv);

  /* ------------- node containers ------------------------------------- */
  NodeContainer routers;
  routers.Create(gridSize * gridSize);

  Ptr<Node> consumer = CreateObject<Node>();
  Ptr<Node> producer = CreateObject<Node>();

  NodeContainer stubs;
  stubs.Create(routers.GetN());                // one stub per router

  /* ------------- point-to-point links -------------------------------- */
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  p2p.SetChannelAttribute("Delay",   StringValue("10ms"));

  auto idx = [gridSize](uint32_t r, uint32_t c) { return r * gridSize + c; };

  for (uint32_t r = 0; r < gridSize; ++r) {
    for (uint32_t c = 0; c < gridSize; ++c) {
      if (c + 1 < gridSize)                    // East
        p2p.Install(routers.Get(idx(r,c)), routers.Get(idx(r,c+1)));
      if (r + 1 < gridSize)                    // South
        p2p.Install(routers.Get(idx(r,c)), routers.Get(idx(r+1,c)));
    }
  }

  p2p.Install(consumer, routers.Get(idx(0,0)));                   // NW
  p2p.Install(routers.Get(idx(gridSize-1, gridSize-1)), producer);// SE

  for (uint32_t i = 0; i < routers.GetN(); ++i)
    p2p.Install(routers.Get(i), stubs.Get(i));                    // router↔stub

  /* ------------- mobility: constant positions ------------------------ */
  const double spacing = 80.0;

  MobilityHelper mob;
  mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");

  // Routers
  Ptr<ListPositionAllocator> posRouters = CreateObject<ListPositionAllocator>();
  for (uint32_t r = 0; r < gridSize; ++r)
    for (uint32_t c = 0; c < gridSize; ++c)
      posRouters->Add(Vector(c*spacing, r*spacing, 0));
  mob.SetPositionAllocator(posRouters);
  mob.Install(routers);

  // Stubs (offset)
  Ptr<ListPositionAllocator> posStubs = CreateObject<ListPositionAllocator>();
  for (uint32_t r = 0; r < gridSize; ++r)
    for (uint32_t c = 0; c < gridSize; ++c)
      posStubs->Add(Vector(c*spacing + spacing*0.5,
                           r*spacing + spacing*0.2, 0));
  mob.SetPositionAllocator(posStubs);
  mob.Install(stubs);

  // Consumer & Producer
  Ptr<ListPositionAllocator> posCP = CreateObject<ListPositionAllocator>();
  posCP->Add(Vector(-spacing, 0, 0));                       // consumer
  posCP->Add(Vector(gridSize*spacing, gridSize*spacing, 0));// producer
  mob.SetPositionAllocator(posCP);
  mob.Install(NodeContainer(consumer, producer));

  /* ------------- NDN stack ------------------------------------------- */
  ndn::StackHelper ndn;
  ndn.setCsSize(100);
  ndn.setPolicy("nfd::cs::priority_fifo");
  ndn.Install(routers);
  ndn.Install(stubs);
  ndn.Install(consumer);
  ndn.Install(producer);

  EnableAdmitNetworkUnsolicitedData();
  LogComponentEnableAll(LOG_PREFIX_TIME);

  ndn::StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/custom");

  /* ------------- global routing -------------------------------------- */
  ndn::GlobalRoutingHelper gr;
  gr.InstallAll();
  gr.AddOrigins("/video", producer);
  ndn::GlobalRoutingHelper::CalculateRoutes();

  /* ------------- consumer & producer apps ---------------------------- */
  ndn::AppHelper consumerH("ns3::ndn::ConsumerZipfMandelbrot");
  consumerH.SetPrefix("/video");
  consumerH.SetAttribute("Frequency",        DoubleValue(freq));
  consumerH.SetAttribute("NumberOfContents", UintegerValue(nContents));
  consumerH.SetAttribute("q",                DoubleValue(q));
  consumerH.Install(consumer);

  ndn::AppHelper producerH("ns3::ndn::Producer");
  producerH.SetPrefix("/video");
  producerH.SetAttribute("PayloadSize", StringValue("1024"));
  producerH.Install(producer);

  /* ------------- NetAnim --------------------------------------------- */
  AnimationInterface anim("grid.xml");

  anim.UpdateNodeDescription(consumer, "Consumer");
  anim.UpdateNodeColor(consumer, 0,255,0);            // green

  anim.UpdateNodeDescription(producer, "Producer");
  anim.UpdateNodeColor(producer, 0,0,255);            // blue

  for (uint32_t r = 0; r < gridSize; ++r) {
    for (uint32_t c = 0; c < gridSize; ++c) {
      Ptr<Node> router = routers.Get(idx(r,c));
      Ptr<Node> stub   = stubs.Get(idx(r,c));

      std::string rc = std::to_string(r) + std::to_string(c);

      anim.UpdateNodeDescription(router, "R" + rc);
      anim.UpdateNodeColor(router, 255,0,0);          // red

      anim.UpdateNodeDescription(stub, "S" + rc);
      anim.UpdateNodeColor(stub, 255,140,140);        // light red
    }
  }

  /* ------------- run -------------------------------------------------- */
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

} // namespace ns3

int main(int argc, char* argv[]) { return ns3::main(argc, argv); }


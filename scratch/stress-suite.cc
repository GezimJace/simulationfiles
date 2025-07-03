/* stress-cache.cc ----------------------------------------------------------
 * Stress-test for CMS + SLRU (no neighbour push, no fog).
 * 5×5 router grid, heavy Zipf workload, central bottleneck links.
 * ------------------------------------------------------------------------- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/ndnSIM-module.h"

using namespace ns3;

/* --------------------------------------------------------------------- */
int
main(int argc, char* argv[])
{
  /* --- CLI ----------------------------------------------------------- */
  uint32_t gridSize   = 5;
  double   simTime    = 40;
  uint32_t catalogue  = 50000;
  double   consumerQ  = 1.0;
  double   freq       = 1000;           // Interests/s
  uint32_t csSize     = 120;

  CommandLine cmd;
  cmd.AddValue("simTime",   "simulation time (s)",        simTime);
  cmd.Parse(argc, argv);

  LogComponentEnableAll(LOG_PREFIX_TIME);

  /* --- node containers ---------------------------------------------- */
  NodeContainer routers;
  routers.Create(gridSize * gridSize);

  Ptr<Node> consumer = CreateObject<Node>();
  Ptr<Node> producer = CreateObject<Node>();

  auto idx = [=](uint32_t r, uint32_t c){ return r*gridSize + c; };

  /* --- point-to-point helpers --------------------------------------- */
  PointToPointHelper fast;
  fast.SetDeviceAttribute("DataRate", StringValue("20Mbps"));
  fast.SetChannelAttribute("Delay",   StringValue("5ms"));

  PointToPointHelper slow = fast;
  slow.SetDeviceAttribute("DataRate", StringValue("2Mbps"));   // bottleneck

  /* --- wire the grid -------------------------------------------------- */
  for (uint32_t r = 0; r < gridSize; ++r) {
    for (uint32_t c = 0; c < gridSize; ++c) {
      if (c+1 < gridSize) { // East link
        (c == gridSize/2 ? slow : fast)
          .Install(routers.Get(idx(r,c)), routers.Get(idx(r,c+1)));
      }
      if (r+1 < gridSize) { // South link
        (r == gridSize/2 ? slow : fast)
          .Install(routers.Get(idx(r,c)), routers.Get(idx(r+1,c)));
      }
    }
  }

  /* --- attach consumer & producer ----------------------------------- */
  fast.Install(consumer, routers.Get(idx(0,0)));                     // NW
  fast.Install(producer, routers.Get(idx(gridSize-1, gridSize-1)));  // SE

  /* --- mobility: fixed grid ----------------------------------------- */
  MobilityHelper mob;
  mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
  const double s = 60.0;  // spacing
  for (uint32_t r = 0; r < gridSize; ++r)
    for (uint32_t c = 0; c < gridSize; ++c)
      pos->Add(Vector(c*s, r*s, 0));
  pos->Add(Vector(-s, 0, 0));                       // consumer
  pos->Add(Vector(gridSize*s, gridSize*s, 0));      // producer
  mob.SetPositionAllocator(pos);
  mob.Install(NodeContainer(routers, consumer, producer));

/* --- NDN stack ----------------------------------------------------- */
ns3::ndn::StackHelper ndn;
ndn.setCsSize(csSize);
ndn.setPolicy("nfd::cs::priority_fifo");
ndn.InstallAll();

ns3::ndn::StrategyChoiceHelper::InstallAll(
        "/", "/localhost/nfd/strategy/custom");

/* --- routing ------------------------------------------------------- */
ns3::ndn::GlobalRoutingHelper gr; gr.InstallAll();
gr.AddOrigins("/video", producer);
ns3::ndn::GlobalRoutingHelper::CalculateRoutes();

/* --- consumer app -------------------------------------------------- */
ns3::ndn::AppHelper cH("ns3::ndn::ConsumerZipfMandelbrot");
cH.SetPrefix("/video");
cH.SetAttribute("NumberOfContents", UintegerValue(catalogue));
cH.SetAttribute("Frequency",        DoubleValue(freq));
cH.SetAttribute("q",                DoubleValue(consumerQ));
ApplicationContainer cApp = cH.Install(consumer);
cApp.Start(Seconds(1.0));                      // ← start 1 s
cApp.Stop (Seconds(simTime - 1));              // ← run until end

/* --- producer app -------------------------------------------------- */
ns3::ndn::AppHelper pH("ns3::ndn::Producer");
pH.SetPrefix("/video");
pH.SetAttribute("PayloadSize", StringValue("1200"));
ApplicationContainer pApp = pH.Install(producer);
pApp.Start(Seconds(0.5));                      // producer up first
pApp.Stop (Seconds(simTime));

/* --- optional tracers ---------------------------------------------- */
ns3::ndn::L3RateTracer::InstallAll("rate.csv", Seconds(0.5));



  /* --- NetAnim (optional) ------------------------------------------- */
  AnimationInterface anim("grid-cache.xml");
  anim.UpdateNodeDescription(consumer, "Consumer");
  anim.UpdateNodeDescription(producer, "Producer");

  /* --- run ----------------------------------------------------------- */
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

/* stress-cache-multi.cc ----------------------------------------------------
 * CMS + SLRU stress-test (no neighbour-push, no fog).
 * 5×5 router grid, four consumers (NW, NE, SW, SE), heavy Zipf workload,
 * central bottleneck links.  Every router sees traffic.
 * ----------------------------------------------------------------------- */

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
  /* ---------------- CLI & defaults ---------------------------------- */
  uint32_t gridSize  = 5;      // 5×5 routers
  double   simTime   = 40;     // seconds
  uint32_t catalogue = 50000;  // objects
  double   q         = 1.0;    // Zipf exponent
  double   freq      = 1000;   // Interests / s  (per consumer!)
  uint32_t csSize    = 120;

  CommandLine cmd;
  cmd.AddValue("simTime", "simulation time", simTime);
  cmd.Parse(argc, argv);

  LogComponentEnableAll(LOG_PREFIX_TIME);

  /* ---------------- node containers --------------------------------- */
  NodeContainer routers;
  routers.Create(gridSize * gridSize);

  NodeContainer consumers;                         // 4 corner consumers
  for (int i = 0; i < 4; ++i)
    consumers.Add(CreateObject<Node>());

  Ptr<Node> producer = CreateObject<Node>();

  auto idx = [=](uint32_t r, uint32_t c){ return r*gridSize + c; };

  /* ---------------- point-to-point helpers --------------------------- */
  PointToPointHelper fast, slow;
  fast.SetDeviceAttribute("DataRate", StringValue("20Mbps"));
  fast.SetChannelAttribute("Delay",   StringValue("5ms"));

  slow = fast;
  slow.SetDeviceAttribute("DataRate", StringValue("2Mbps"));   // bottleneck

  /* wire the grid */
  for (uint32_t r = 0; r < gridSize; ++r)
    for (uint32_t c = 0; c < gridSize; ++c) {
      if (c + 1 < gridSize)   // East link
        (c == gridSize/2 ? slow : fast)
          .Install(routers.Get(idx(r,c)), routers.Get(idx(r,c+1)));
      if (r + 1 < gridSize)   // South link
        (r == gridSize/2 ? slow : fast)
          .Install(routers.Get(idx(r,c)), routers.Get(idx(r+1,c)));
    }

  /* attach four consumers */
  fast.Install(consumers.Get(0), routers.Get(idx(0,0)));                   // NW
  fast.Install(consumers.Get(1), routers.Get(idx(0,gridSize-1)));          // NE
  fast.Install(consumers.Get(2), routers.Get(idx(gridSize-1,0)));          // SW
  fast.Install(consumers.Get(3), routers.Get(idx(gridSize-1,gridSize-1))); // SE

  /* attach producer (SE router) */
  fast.Install(producer, routers.Get(idx(gridSize-1, gridSize-1)));

  /* ---------------- mobility (fixed grid) --------------------------- */
  MobilityHelper mob;
  mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
  const double spacing = 60.0;

  /* routers */
  for (uint32_t r = 0; r < gridSize; ++r)
    for (uint32_t c = 0; c < gridSize; ++c)
      pos->Add(Vector(c*spacing, r*spacing, 0));

  /* consumers (slightly outside each corner) */
  pos->Add(Vector(-spacing, 0, 0));                            // NW
  pos->Add(Vector(gridSize*spacing, 0, 0));                    // NE
  pos->Add(Vector(-spacing, gridSize*spacing, 0));             // SW
  pos->Add(Vector(gridSize*spacing, gridSize*spacing, 0));     // SE

  /* producer (furthest SE) */
  pos->Add(Vector(gridSize*spacing + spacing, gridSize*spacing + spacing, 0));

  mob.SetPositionAllocator(pos);
  mob.Install(NodeContainer(routers, consumers, producer));

  /* ---------------- NDN stack --------------------------------------- */
  ns3::ndn::StackHelper ndn;
  ndn.setCsSize(csSize);
  ndn.setPolicy("nfd::cs::priority_fifo");
  ndn.InstallAll();

  ns3::ndn::StrategyChoiceHelper::InstallAll(
        "/", "/localhost/nfd/strategy/custom");

  /* ---------------- routing ----------------------------------------- */
  ns3::ndn::GlobalRoutingHelper gr;
  gr.InstallAll();
  gr.AddOrigins("/video", producer);
  ns3::ndn::GlobalRoutingHelper::CalculateRoutes();

  /* ---------------- apps -------------------------------------------- */
  ns3::ndn::AppHelper cH("ns3::ndn::ConsumerZipfMandelbrot");
  cH.SetPrefix("/video");
  cH.SetAttribute("NumberOfContents", UintegerValue(catalogue));
  cH.SetAttribute("Frequency",        DoubleValue(freq));
  cH.SetAttribute("q",                DoubleValue(q));

  for (auto n : consumers) {
    auto app = cH.Install(n);
    app.Start(Seconds(1.0));
    app.Stop (Seconds(simTime - 1));
  }

  ns3::ndn::AppHelper pH("ns3::ndn::Producer");
  pH.SetPrefix("/video");
  pH.SetAttribute("PayloadSize", StringValue("1200"));
  auto pApp = pH.Install(producer);
  pApp.Start(Seconds(0.5));
  pApp.Stop (Seconds(simTime));

  /* ---------------- tracers ----------------------------------------- */
  ns3::ndn::L3RateTracer::InstallAll("rate.csv", Seconds(0.5));

  /* ---------------- NetAnim (optional) ------------------------------ */
  AnimationInterface anim("grid-multi-cache.xml");
  anim.UpdateNodeDescription(producer, "Producer");
  anim.UpdateNodeColor(producer, 0,0,255);

  const char* lbl[4] = { "ConsNW", "ConsNE", "ConsSW", "ConsSE" };
  for (uint32_t i = 0; i < 4; ++i) {
    anim.UpdateNodeDescription(consumers.Get(i), lbl[i]);
    anim.UpdateNodeColor(consumers.Get(i), 0,255,0);
  }
  for (uint32_t i = 0; i < routers.GetN(); ++i)
    anim.UpdateNodeDescription(routers.Get(i), std::to_string(i));

  /* ---------------- run --------------------------------------------- */
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}



/* stress-cache-multi2.cc ----------------------------------------------------
 * 5×5 router grid, four corner consumers, TWO producers (/video, /sensor).
 * Heavy Zipf workload exercises CMS + SLRU everywhere (no neighbour-push,
 * no fog).  Central links throttled to 2 Mbps.
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
  /* ---- parameters --------------------------------------------------- */
  uint32_t gridSize   = 5;
  double   simTime    = 20;
  uint32_t catalogue  = 10000;
  double   zipfQ      = 1.2;
  double   freqPerApp = 500;     // Interests/s per prefix per consumer
  uint32_t csSize     = 1;

  CommandLine cmd;
  cmd.AddValue("simTime", "simulation time (s)", simTime);
  cmd.Parse(argc, argv);

  LogComponentEnableAll(LOG_PREFIX_TIME);

  /* ---- create nodes ------------------------------------------------- */
  NodeContainer routers; routers.Create(gridSize * gridSize);
  NodeContainer consumers; consumers.Create(4);        // NW NE SW SE
  Ptr<Node> prodVideo  = CreateObject<Node>();         // SE router
  Ptr<Node> prodSensor = CreateObject<Node>();         // NE router

  auto idx = [gridSize](uint32_t r, uint32_t c){ return r*gridSize + c; };

  /* ---- point-to-point helpers -------------------------------------- */
  PointToPointHelper fast;
  fast.SetDeviceAttribute("DataRate", StringValue("20Mbps"));
  fast.SetChannelAttribute("Delay",   StringValue("5ms"));

  PointToPointHelper slow = fast;
  slow.SetDeviceAttribute("DataRate", StringValue("2Mbps"));  // bottleneck

  /* ---- wire grid ---------------------------------------------------- */
  for (uint32_t r=0; r<gridSize; ++r)
    for (uint32_t c=0; c<gridSize; ++c) {
      if (c+1<gridSize)   // East
        (c==gridSize/2?slow:fast).Install(
            routers.Get(idx(r,c)), routers.Get(idx(r,c+1)));
      if (r+1<gridSize)   // South
        (r==gridSize/2?slow:fast).Install(
            routers.Get(idx(r,c)), routers.Get(idx(r+1,c)));
    }

  /* consumers to four corners */
  fast.Install(consumers.Get(0), routers.Get(idx(0,0)));               // NW
  fast.Install(consumers.Get(1), routers.Get(idx(0,gridSize-1)));      // NE
  fast.Install(consumers.Get(2), routers.Get(idx(gridSize-1,0)));      // SW
  fast.Install(consumers.Get(3), routers.Get(idx(gridSize-1,gridSize-1))); // SE

  /* producers attach */
  fast.Install(prodSensor, routers.Get(idx(0,gridSize-1)));            // NE
  fast.Install(prodVideo , routers.Get(idx(gridSize-1,gridSize-1)));   // SE

  /* ---- mobility ----------------------------------------------------- */
  const double spacing = 60.0;
  MobilityHelper mob;
  mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();

  for (uint32_t r=0;r<gridSize;++r)
    for (uint32_t c=0;c<gridSize;++c)
      pos->Add(Vector(c*spacing, r*spacing, 0));

  pos->Add(Vector(-spacing,0,0));                         // NW consumer
  pos->Add(Vector(gridSize*spacing,0,0));                 // NE
  pos->Add(Vector(-spacing,gridSize*spacing,0));          // SW
  pos->Add(Vector(gridSize*spacing,gridSize*spacing,0));  // SE

  pos->Add(Vector(gridSize*spacing, -spacing, 0));        // prodSensor
  pos->Add(Vector(gridSize*spacing+spacing,
                  gridSize*spacing+spacing, 0));          // prodVideo
  mob.SetPositionAllocator(pos);
  mob.Install(NodeContainer(routers, consumers, prodSensor, prodVideo));

  /* ---- NDN stack ---------------------------------------------------- */
  ns3::ndn::StackHelper ndn;
  ndn.setCsSize(csSize);
  ndn.setPolicy("nfd::cs::priority_fifo");
  ndn.InstallAll();

  ns3::ndn::StrategyChoiceHelper::InstallAll(
        "/", "/localhost/nfd/strategy/custom");

  /* ---- routing ------------------------------------------------------ */
  ns3::ndn::GlobalRoutingHelper gr; gr.InstallAll();
  gr.AddOrigins("/video" , prodVideo );
  gr.AddOrigins("/sensor", prodSensor);
  ns3::ndn::GlobalRoutingHelper::CalculateRoutes();

  /* ---- consumer apps (both prefixes) -------------------------------- */
  auto installConsumer = [&](Ptr<Node> n){
    ns3::ndn::AppHelper h("ns3::ndn::ConsumerZipfMandelbrot");
    h.SetAttribute("NumberOfContents", UintegerValue(catalogue));
    h.SetAttribute("Frequency",        DoubleValue(freqPerApp));
    h.SetAttribute("q",                DoubleValue(zipfQ));

    h.SetPrefix("/video");
    ApplicationContainer a1 = h.Install(n);
    a1.Start(Seconds(1.0));
    a1.Stop(Seconds(simTime-1));

    h.SetPrefix("/sensor");
    ApplicationContainer a2 = h.Install(n);
    a2.Start(Seconds(1.0));
    a2.Stop(Seconds(simTime-1));
  };
  for (auto n : consumers)
    installConsumer(n);

  /* ---- producer apps ------------------------------------------------ */
  ns3::ndn::AppHelper pV("ns3::ndn::Producer");
  pV.SetPrefix("/video"); pV.SetAttribute("PayloadSize", StringValue("1200"));
  ApplicationContainer pvApp = pV.Install(prodVideo);
  pvApp.Start(Seconds(0.5));

  ns3::ndn::AppHelper pS("ns3::ndn::Producer");
  pS.SetPrefix("/sensor"); pS.SetAttribute("PayloadSize", StringValue("1024"));
  ApplicationContainer psApp = pS.Install(prodSensor);
  psApp.Start(Seconds(0.5));

  /* ---- tracer ------------------------------------------------------- */
  ns3::ndn::L3RateTracer::InstallAll("rate.csv", Seconds(0.5));

  /* ---- NetAnim ------------------------------------------------------ */
  AnimationInterface anim("grid-multi2.xml");
  anim.UpdateNodeDescription(prodVideo , "Producer /video");
  anim.UpdateNodeDescription(prodSensor, "Producer /sensor");
  anim.UpdateNodeColor(prodVideo , 0,0,255);
  anim.UpdateNodeColor(prodSensor, 0,0,200);

  const char* lbl[4]={"ConsNW","ConsNE","ConsSW","ConsSE"};
  for(uint32_t i=0;i<4;++i){
    anim.UpdateNodeDescription(consumers.Get(i),lbl[i]);
    anim.UpdateNodeColor(consumers.Get(i),0,255,0);
  }

  /* ---- run ---------------------------------------------------------- */
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}


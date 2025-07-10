/* stress-cache-multi2.cc ----------------------------------------------------
 * 4×4 router grid, four corner consumers, TWO producers (/video, /sensor).
 * Heavy Zipf workload, CMS+SLRU in CustomStrategy.
 * Central links throttled to 2 Mbps.
 * WITH NS-3 energy framework + per-operation logical-energy coupling.
 * ------------------------------------------------------------------------- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/energy-module.h"                 // batteries
#include "ns3/simple-device-energy-model.h"    // constant-current model
#include "ns3/ndnSIM-module.h"

using namespace ns3;

/* --------------------------------------------------------------------- */
static void
PollEnergy (EnergySourceContainer* sources)
{
  for (uint32_t i = 0; i < sources->GetN (); ++i) {
    auto src   = DynamicCast<BasicEnergySource> (sources->Get (i));
    double rem = src->GetRemainingEnergy ();
    std::cout << Simulator::Now ().GetSeconds ()
              << "s  Node" << src->GetNode ()->GetId ()
              << "  " << rem << " J\n";
  }
  Simulator::Schedule (Seconds (1.0), &PollEnergy, sources);
}

/* --------------------------------------------------------------------- */
int
main (int argc, char* argv[])
{
  /* ---- parameters --------------------------------------------------- */
  uint32_t gridSize   = 4;        // routers per side
  double   simTime    = 20;       // seconds
  uint32_t catalogue  = 10000;
  double   zipfQ      = 1.2;
  double   freqPerApp = 500;      // Interests/s per prefix per consumer
  uint32_t csSize     = 1;        // router CS entries

  CommandLine cmd;
  cmd.AddValue ("simTime", "simulation time (s)", simTime);
  cmd.Parse (argc, argv);

  LogComponentEnableAll (LOG_PREFIX_TIME);

  /* ---- node containers ---------------------------------------------- */
  NodeContainer routers;   routers.Create (gridSize * gridSize);
  NodeContainer consumers; consumers.Create (4);      // NW NE SW SE
  Ptr<Node> prodVideo  = CreateObject<Node> ();       // SE router
  Ptr<Node> prodSensor = CreateObject<Node> ();       // NE router

  auto idx = [gridSize] (uint32_t r, uint32_t c) { return r * gridSize + c; };

  NodeContainer allNodes;
  allNodes.Add (routers);
  allNodes.Add (consumers);
  allNodes.Add (prodSensor);
  allNodes.Add (prodVideo);

  NetDeviceContainer allDevs;      // every P2P device ends up here

  /* ---- point-to-point helpers --------------------------------------- */
  PointToPointHelper fast;
  fast.SetDeviceAttribute ("DataRate", StringValue ("20Mbps"));
  fast.SetChannelAttribute ("Delay",   StringValue ("5ms"));

  PointToPointHelper slow = fast;
  slow.SetDeviceAttribute ("DataRate", StringValue ("2Mbps"));  // bottleneck

  /* ---- wire router grid --------------------------------------------- */
  for (uint32_t r = 0; r < gridSize; ++r)
    for (uint32_t c = 0; c < gridSize; ++c) {
      if (c + 1 < gridSize) {   // East link
        NetDeviceContainer d = (c == gridSize / 2 ? slow : fast)
            .Install (routers.Get (idx (r, c)), routers.Get (idx (r, c + 1)));
        allDevs.Add (d);
      }
      if (r + 1 < gridSize) {   // South link
        NetDeviceContainer d = (r == gridSize / 2 ? slow : fast)
            .Install (routers.Get (idx (r, c)), routers.Get (idx (r + 1, c)));
        allDevs.Add (d);
      }
    }

  /* ---- consumers at four corners ------------------------------------ */
  allDevs.Add (fast.Install (consumers.Get (0), routers.Get (idx (0, 0))));                 // NW
  allDevs.Add (fast.Install (consumers.Get (1), routers.Get (idx (0, gridSize - 1))));      // NE
  allDevs.Add (fast.Install (consumers.Get (2), routers.Get (idx (gridSize - 1, 0))));      // SW
  allDevs.Add (fast.Install (consumers.Get (3), routers.Get (idx (gridSize - 1, gridSize - 1)))); // SE

  /* ---- producers ---------------------------------------------------- */
  allDevs.Add (fast.Install (prodSensor, routers.Get (idx (0, gridSize - 1))));             // NE
  allDevs.Add (fast.Install (prodVideo , routers.Get (idx (gridSize - 1, gridSize - 1))));  // SE

  /* ---- mobility ----------------------------------------------------- */
  const double spacing = 60.0;
  MobilityHelper mob;
  mob.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator> ();

  for (uint32_t r = 0; r < gridSize; ++r)
    for (uint32_t c = 0; c < gridSize; ++c)
      pos->Add (Vector (c * spacing, r * spacing, 0));

  /* consumers */
  pos->Add (Vector (-spacing,                0,               0));            // NW
  pos->Add (Vector ( gridSize * spacing,     0,               0));            // NE
  pos->Add (Vector (-spacing,                gridSize * spacing, 0));         // SW
  pos->Add (Vector ( gridSize * spacing,     gridSize * spacing, 0));         // SE
  /* producers */
  pos->Add (Vector ( gridSize * spacing,    -spacing, 0));                    // prodSensor
  pos->Add (Vector ( gridSize * spacing + spacing,
                     gridSize * spacing + spacing, 0));                       // prodVideo
  mob.SetPositionAllocator (pos);
  mob.Install (NodeContainer (routers, consumers, prodSensor, prodVideo));

  /* ---- NDN stack ---------------------------------------------------- */
  ns3::ndn::StackHelper ndn;
  ndn.setCsSize (csSize);
  ndn.setPolicy ("nfd::cs::priority_fifo");
  ndn.InstallAll ();

  ns3::ndn::StrategyChoiceHelper::InstallAll ("/",
      "/localhost/nfd/strategy/custom");    // your CustomStrategy drains battery too

  /* ---------- ENERGY ------------------------------------------------- */
  // 1. battery per node
  BasicEnergySourceHelper batt;
  batt.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (3000)); // 3 kJ
  EnergySourceContainer sources = batt.Install (allNodes);

  // 2. constant-current model per NetDevice
  for (uint32_t i = 0; i < allDevs.GetN (); ++i) {
    Ptr<NetDevice> dev  = allDevs.Get (i);
    Ptr<BasicEnergySource> src = dev->GetNode ()->GetObject<BasicEnergySource> ();
    if (!src)
      continue;                         // safety (shouldn’t happen)

    auto model = CreateObject<SimpleDeviceEnergyModel> ();
    model->SetEnergySource (src);
    model->SetAttribute ("TxCurrentA", DoubleValue (0.005)); // 5 mA
    model->SetAttribute ("RxCurrentA", DoubleValue (0.005)); // 5 mA
    src->AppendDeviceEnergyModel (model);
    dev->AggregateObject (model);       // optional
  }

  /* start console ticker */
  PollEnergy (&sources);

  /* ---- routing ------------------------------------------------------ */
  ns3::ndn::GlobalRoutingHelper gr; gr.InstallAll ();
  gr.AddOrigins ("/video" , prodVideo );
  gr.AddOrigins ("/sensor", prodSensor);
  ns3::ndn::GlobalRoutingHelper::CalculateRoutes ();

  /* ---- consumer apps ----------------------------------------------- */
  auto installConsumer = [&] (Ptr<Node> n) {
    ns3::ndn::AppHelper h ("ns3::ndn::ConsumerZipfMandelbrot");
    h.SetAttribute ("NumberOfContents", UintegerValue (catalogue));
    h.SetAttribute ("Frequency",        DoubleValue (freqPerApp));
    h.SetAttribute ("q",                DoubleValue (zipfQ));

    h.SetPrefix ("/video");
    h.Install (n).Start (Seconds (1.0));

    h.SetPrefix ("/sensor");
    h.Install (n).Start (Seconds (1.0));
  };
  for (auto n : consumers)
    installConsumer (n);

  /* ---- producer apps ----------------------------------------------- */
  ns3::ndn::AppHelper pV ("ns3::ndn::Producer");
  pV.SetPrefix ("/video");
  pV.SetAttribute ("PayloadSize", StringValue ("1200"));
  pV.Install (prodVideo).Start (Seconds (0.5));

  ns3::ndn::AppHelper pS ("ns3::ndn::Producer");
  pS.SetPrefix ("/sensor");
  pS.SetAttribute ("PayloadSize", StringValue ("1024"));
  pS.Install (prodSensor).Start (Seconds (0.5));

  /* ---- L3 rate tracer ---------------------------------------------- */
  ns3::ndn::L3RateTracer::InstallAll ("rate.csv", Seconds (0.5));

  /* ---- NetAnim ------------------------------------------------------ */
  AnimationInterface anim ("grid-multi2.xml");
  anim.UpdateNodeDescription (prodVideo , "Producer /video");
  anim.UpdateNodeDescription (prodSensor, "Producer /sensor");
  anim.UpdateNodeColor       (prodVideo , 0, 0, 255);
  anim.UpdateNodeColor       (prodSensor, 0, 0, 200);

  const char* lbl[4] = {"ConsNW", "ConsNE", "ConsSW", "ConsSE"};
  for (uint32_t i = 0; i < 4; ++i) {
    anim.UpdateNodeDescription (consumers.Get (i), lbl[i]);
    anim.UpdateNodeColor       (consumers.Get (i), 0, 255, 0);
  }

  /* ---- run ---------------------------------------------------------- */
  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}


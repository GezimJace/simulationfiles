/* wifi.cc  –  4 × 4 wireless mesh running CMS+SLRU custom strategy
 * --------------------------------------------------------------- */

#include <iomanip>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/propagation-module.h"
#include "ns3/energy-module.h"
#include "ns3/netanim-module.h"
#include "ns3/ndnSIM-module.h"

using namespace ns3;

/* ---------------- scenario parameters ---------------------------- */
static const uint32_t N_ROUTERS = 16;          // must be a square number
static const uint32_t N_CONS    = 4;           // first N_CONS routers are consumers

static const double   CELL      = 40.0;        // grid step [m]
static const double   SIM_TIME  = 5.0;         // run time [s]

static const uint32_t CS_SIZE   = 50;          // per-node CS capacity [pkt]
static const uint32_t CATALOGUE = 10'000;
static const double   FREQ_APP  = 200.0;       // Interests /s
static const double   ZIPF_Q    = 1.2;

/* -------- battery monitor --------------------------------------- */
static void
PollEnergy (EnergySourceContainer* sources)
{
  for (auto it = sources->Begin (); it != sources->End (); ++it) {
    auto batt = DynamicCast<BasicEnergySource> (*it);
    std::cout << std::fixed << std::setprecision (1)
              << Simulator::Now ().GetSeconds () << " s  Node"
              << batt->GetNode ()->GetId () << "  "
              << batt->GetRemainingEnergy () << " J\n";
  }
  Simulator::Schedule (Seconds (1.0), &PollEnergy, sources);
}

/* ---------------- main ------------------------------------------- */
int
main (int argc, char** argv)
{
  double simTime = SIM_TIME;
  CommandLine cmd;
  cmd.AddValue ("simTime", "Simulation time [s]", simTime);
  cmd.Parse (argc, argv);

  ns3::PacketMetadata::Enable ();
  LogComponentEnableAll (LOG_PREFIX_TIME);

  /* --- create nodes --------------------------------------------- */
  NodeContainer routers;   routers.Create (N_ROUTERS);
  NodeContainer consumers;
  for (uint32_t i = 0; i < N_CONS; ++i)
    consumers.Add (routers.Get (i));

  NodeContainer producers; producers.Create (2);   // /video  /sensor

  /* --- place nodes ---------------------------------------------- */
  MobilityHelper mob;
  mob.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

  Ptr<GridPositionAllocator> grid = CreateObject<GridPositionAllocator> ();
  grid->SetDeltaX (CELL);
  grid->SetDeltaY (CELL);
  grid->SetMinX (0.0);
  grid->SetMinY (0.0);
  grid->SetLayoutType (GridPositionAllocator::ROW_FIRST);
  mob.SetPositionAllocator (grid);
  mob.Install (routers);

  Ptr<ListPositionAllocator> pa = CreateObject<ListPositionAllocator> ();
  pa->Add (Vector (-20, -20, 0));                 // /video producer
  pa->Add (Vector (3*CELL + 20, 3*CELL + 20, 0)); // /sensor producer
  mob.SetPositionAllocator (pa);
  mob.Install (producers);

  /* --- Wi-Fi channel + PHY -------------------------------------- */
  YansWifiChannelHelper chan;
  chan.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  chan.AddPropagationLoss  ("ns3::FriisPropagationLossModel");   // free-space

  YansWifiPhyHelper phy; phy.SetChannel (chan.Create ());
  phy.Set ("TxPowerStart", DoubleValue (20.0));   // 100 mW
  phy.Set ("TxPowerEnd",   DoubleValue (20.0));
  phy.Set ("RxGain",       DoubleValue (3.0));
  phy.Set ("RxSensitivity",  DoubleValue (-96.0));
  phy.Set ("CcaEdThreshold", DoubleValue (-99.0));

  WifiHelper wifi; wifi.SetStandard (WIFI_STANDARD_80211g);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("ErpOfdmRate12Mbps"));

  WifiMacHelper mac;  mac.SetType ("ns3::AdhocWifiMac");

  NetDeviceContainer devs = wifi.Install (phy, mac,
                                          NodeContainer (routers, producers));

  /* --- energy model --------------------------------------------- */
  BasicEnergySourceHelper batt;
  batt.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (3000.0)); // 3 kJ
  EnergySourceContainer sources = batt.Install (NodeContainer (routers,
                                                               producers));

  WifiRadioEnergyModelHelper radio;
  radio.Set ("TxCurrentA",   DoubleValue (0.038));
  radio.Set ("RxCurrentA",   DoubleValue (0.027));
  radio.Set ("IdleCurrentA", DoubleValue (0.018));
  radio.Install (devs, sources);

  /* --- NDN stack ------------------------------------------------- */
  ns3::ndn::StackHelper ndn;
  ndn.setCsSize (CS_SIZE);
  ndn.InstallAll ();

  // activate CMS+SLRU custom strategy everywhere
  ns3::ndn::StrategyChoiceHelper::InstallAll ("/",
      "/localhost/nfd/strategy/custom");

  /* --- routing --------------------------------------------------- */
  ns3::ndn::GlobalRoutingHelper gr; gr.InstallAll ();

  Ptr<Node> prodVideo  = producers.Get (0);
  Ptr<Node> prodSensor = producers.Get (1);
  gr.AddOrigins ("/video",  prodVideo);
  gr.AddOrigins ("/sensor", prodSensor);
  ns3::ndn::GlobalRoutingHelper::CalculateRoutes ();

  /* --- consumer applications ------------------------------------ */
  ns3::ndn::AppHelper cons ("ns3::ndn::ConsumerZipfMandelbrot");
  cons.SetAttribute ("NumberOfContents", UintegerValue (CATALOGUE));
  cons.SetAttribute ("Frequency",        DoubleValue (FREQ_APP));
  cons.SetAttribute ("q",                DoubleValue (ZIPF_Q));

  cons.SetPrefix ("/video");
  cons.Install (consumers).Start (Seconds (1.0));

  cons.SetPrefix ("/sensor");
  cons.Install (consumers).Start (Seconds (1.0));

  /* --- producer applications ------------------------------------ */
  ns3::ndn::AppHelper prod ("ns3::ndn::Producer");
  prod.SetAttribute ("PayloadSize", StringValue ("1200"));
  prod.SetPrefix ("/video");  prod.Install (prodVideo);
  prod.SetPrefix ("/sensor"); prod.Install (prodSensor);

  /* --- tracing --------------------------------------------------- */
  ns3::ndn::L3RateTracer::InstallAll ("rate.csv", Seconds (0.5));

  AnimationInterface anim ("mesh.xml");
  anim.EnablePacketMetadata ();
  anim.SetMaxPktsPerTraceFile (100000);

  anim.UpdateNodeDescription (prodVideo , "Producer /video");
  anim.UpdateNodeDescription (prodSensor, "Producer /sensor");
  anim.UpdateNodeColor       (prodVideo ,  0,   0, 255);
  anim.UpdateNodeColor       (prodSensor,  0,   0, 180);

  const char* lbl[4] = {"Cons0","Cons1","Cons2","Cons3"};
  for (uint32_t i = 0; i < N_CONS; ++i) {
    anim.UpdateNodeDescription (consumers.Get (i), lbl[i]);
    anim.UpdateNodeColor       (consumers.Get (i),  0, 255,   0);
  }

  /* --- battery read-out ----------------------------------------- */
  PollEnergy (&sources);

  /* --- run ------------------------------------------------------- */
  Simulator::Stop (Seconds (simTime));

  Simulator::Schedule (Seconds (simTime) - MicroSeconds (1),
    [&sources]() {
      std::cout << "\n=== FINAL ENERGY ===\n";
      for (auto it = sources.Begin (); it != sources.End (); ++it) {
        auto b = DynamicCast<BasicEnergySource> (*it);
        std::cout << "Node" << b->GetNode ()->GetId ()
                  << "  "  << b->GetRemainingEnergy () << " J\n";
      }
    });

  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}


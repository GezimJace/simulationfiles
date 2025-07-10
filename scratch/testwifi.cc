/* testwifi.cc  – 4 random nodes inside 50 m × 50 m
 * ndnSIM stack + custom CMS-SLRU strategy + Wi-Fi + energy + NetAnim
 * ----------------------------------------------------------------- */

#include <cstdlib>
#include <iomanip>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/propagation-module.h"
#include "ns3/energy-module.h"
#include "ns3/netanim-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/internet-module.h"

using namespace ns3;

/* ---------------- scenario knobs ---------------------------------- */
static const double   MAP_SIZE   = 30.0;   // square side [m]
static const double   SIM_TIME   = 21.0;    // run length [s]
static const uint32_t CS_SIZE    = 1;     // ContentStore [pkt]
static const uint32_t CATALOGUE  = 75;
static const double   CONS_RATE  = 5000;   // Interests /s

/* ---------------- battery logger ---------------------------------- */
static void
PollEnergy(EnergySourceContainer* srcs)
{
  // open-once, append thereafter
  static std::ofstream energyLog("metrics/scenario-node-energy.txt",
                                 std::ios::out | std::ios::trunc);

  for (auto it = srcs->Begin(); it != srcs->End(); ++it) {
    auto batt = DynamicCast<BasicEnergySource>(*it);
    energyLog << std::fixed << std::setprecision(1)
              << Simulator::Now().GetSeconds() << " s  Node"
              << batt->GetNode()->GetId() << "  "
              << batt->GetRemainingEnergy() << " J\n";
  }
  energyLog.flush();   // Make sure data is written to disk

  Simulator::Schedule(Seconds(1.0), &PollEnergy, srcs);
}

/* ---------------- main -------------------------------------------- */
int
main (int argc, char** argv)
{
  
  ns3::RngSeedManager::SetSeed (12345);  // set your own seed once
  ns3::RngSeedManager::SetRun  (4);      // or just vary the run number
  
  double simTime = SIM_TIME;
  CommandLine cmd;
  cmd.AddValue ("simTime", "Simulation time [s]", simTime);
  cmd.Parse (argc, argv);

  ns3::PacketMetadata::Enable ();
  //ns3::LogComponentEnable ("ndn-cxx.nfd.slru", static_cast<ns3::LogLevel>(ns3::LOG_LEVEL_INFO | ns3::LOG_PREFIX_TIME));
  //ns3::LogComponentEnable ("ndn-cxx.nfd.CustomStrategy", static_cast<ns3::LogLevel>(ns3::LOG_LEVEL_INFO | ns3::LOG_PREFIX_TIME));

//ns3::LogComponentEnable ("ndn-cxx.nfd.Forwarder", static_cast<ns3::LogLevel>(ns3::LOG_LEVEL_INFO | ns3::LOG_PREFIX_TIME));


  /* --- nodes ----------------------------------------------------- */
  NodeContainer nodes; nodes.Create (10);   // 0=consumer, 1=producer, 2-3=routers

  /* --- random placement inside 50 m × 50 m ----------------------- */
  MobilityHelper mob;
  mob.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mob.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
      "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=30.0]"),
      "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=30.0]"));
  mob.Install (nodes);

  /* --- Wi-Fi: 20 dBm, Friis, 11g 12 Mb/s ------------------------- */
  YansWifiChannelHelper chan;
  chan.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  chan.AddPropagationLoss  ("ns3::FriisPropagationLossModel");

  YansWifiPhyHelper phy; phy.SetChannel (chan.Create ());
  phy.Set ("TxPowerStart",   DoubleValue (20.0));
  phy.Set ("TxPowerEnd",     DoubleValue (20.0));
  phy.Set ("RxSensitivity",  DoubleValue (-96.0));
  phy.Set ("CcaEdThreshold", DoubleValue (-99.0));

  WifiHelper wifi; wifi.SetStandard (WIFI_STANDARD_80211g);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("ErpOfdmRate12Mbps"));
  WifiMacHelper mac; mac.SetType ("ns3::AdhocWifiMac");
  NetDeviceContainer devs = wifi.Install (phy, mac, nodes);

  InternetStackHelper ipStack;
  ipStack.Install (nodes);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.0.0.0", "255.255.255.0");
  ipv4.Assign (devs);


  /* --- energy ---------------------------------------------------- */
  BasicEnergySourceHelper batt;
  batt.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (1000));
  EnergySourceContainer sources = batt.Install (nodes);

  WifiRadioEnergyModelHelper radio;
  radio.Set ("TxCurrentA",   DoubleValue (0.038));
  radio.Set ("RxCurrentA",   DoubleValue (0.027));
  radio.Set ("IdleCurrentA", DoubleValue (0.018));
  radio.Install (devs, sources);

  /* --- ndnSIM stack + custom strategy ---------------------------- */
  ns3::ndn::StackHelper stack;
  stack.setCsSize (CS_SIZE);
  stack.InstallAll ();

  ns3::ndn::StrategyChoiceHelper::InstallAll (
      "/", "/localhost/nfd/strategy/custom");

  /* --- routing --------------------------------------------------- */
  ns3::ndn::GlobalRoutingHelper gr; gr.InstallAll ();
  gr.AddOrigins ("/prefix", nodes.Get (1));         // producer is Node-1
  ns3::ndn::GlobalRoutingHelper::CalculateRoutes ();

  /* --- consumer & producer apps --------------------------------- */
  ns3::ndn::AppHelper cons ("ns3::ndn::ConsumerZipfMandelbrot");
  cons.SetPrefix ("/prefix");
  cons.SetAttribute ("Frequency",        DoubleValue (CONS_RATE));
  cons.SetAttribute ("NumberOfContents", UintegerValue (CATALOGUE));
  cons.Install (nodes.Get (0));                     // Node-0

  ns3::ndn::AppHelper prod ("ns3::ndn::Producer");
  prod.SetPrefix ("/prefix");
  prod.SetAttribute ("PayloadSize", StringValue ("1024"));
  prod.Install (nodes.Get (1));                     // Node-1

  /* --- tracing --------------------------------------------------- */
  ns3::ndn::L3RateTracer::InstallAll ("metrics/rate.txt", Seconds (1.0));
  ns3::ndn::AppDelayTracer::InstallAll("metrics/app-delays.txt");
/*  
AnimationInterface anim ("mini.xml");
anim.EnablePacketMetadata ();
anim.SetMaxPktsPerTraceFile (20000);


anim.EnableWifiMacCounters (Seconds (0.0), Seconds (simTime), Seconds (0.01)); // MAC Tx arrows
anim.EnableWifiPhyCounters (Seconds (0.0), Seconds (simTime), Seconds (0.01)); // PHY circles
anim.EnablePacketMetadata ();             // store src/dst MAC in XML


  anim.UpdateNodeDescription (nodes.Get (0), "Consumer");
  anim.UpdateNodeColor       (nodes.Get (0),   0, 255,   0);
  anim.UpdateNodeDescription (nodes.Get (1), "Producer");
  anim.UpdateNodeColor       (nodes.Get (1),   0,   0, 255);
*/

  /* --- battery printout ----------------------------------------- */
  PollEnergy (&sources);

  /* --- run ------------------------------------------------------- */
  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}


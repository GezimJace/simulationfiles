/* testwifi.cc  – scalable Wi‑Fi/NDN scenario with optional mobility
 * -----------------------------------------------------------------
 *  • Single 802.11g BSS, Friis propagation
 *  • Multiple producers (first N_PRODUCERS nodes)
 *  • Remaining nodes act as Zipf‑Mandelbrot consumers
 *  • Optional RandomWaypoint mobility for consumer nodes (off by default)
 *  • Uses /localhost/nfd/strategy/custom exactly as provided
 */

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
static const double   MAP_SIZE    = 100.0;   // square side [m]
static const double   SIM_TIME    = 100.0;   // run length [s]
static const uint32_t CS_SIZE     = 1;      // ContentStore [pkt]
static const uint32_t CATALOGUE   = 75;     // #contents for Zipf
static const double   CONS_RATE   = 5000;   // Interests /s

/* scale parameters (override on command line) */
static uint32_t       N_NODES     = 20;     // total Wi-Fi nodes
static uint32_t       N_PRODUCERS = 4;      // first nodes act as producers

/* mobility parameters (override on command line) */
static bool   ENABLE_MOBILITY = true;
static double MIN_SPEED       = 0.5;   // [m/s]
static double MAX_SPEED       = 1.5;   // [m/s]
static double PAUSE_TIME      = 0.0;   // [s]

/* -------- NetAnim (visualisation) ------------------------------- */
//static bool      ENABLE_ANIM   = true;        // off by default
//static std::string ANIM_FILE   = "anim.xml";   // output file
//static uint32_t  ANIM_PKT_MAX  = 20000;        // packets per trace chunk


/* ---------------- battery logger ---------------------------------- */
static void
PollEnergy(EnergySourceContainer* srcs)
{
  static std::ofstream log("metrics/scenario-node-energy.txt",
                           std::ios::out | std::ios::trunc);

  for (auto it = srcs->Begin(); it != srcs->End(); ++it) {
    Ptr<BasicEnergySource> batt = DynamicCast<BasicEnergySource>(*it);
    log << std::fixed << std::setprecision(1)
        << Simulator::Now().GetSeconds() << " Node"
        << batt->GetNode()->GetId() << ' '
        << batt->GetRemainingEnergy() << " J\n";
  }
  log.flush();
  Simulator::Schedule(Seconds(1.0), &PollEnergy, srcs);
}

/* ---------------- main -------------------------------------------- */
int
main(int argc, char** argv)
{
  /* deterministic RNG unless you vary --run */
  RngSeedManager::SetSeed(12345);
  RngSeedManager::SetRun (4);

  /* command-line parameters */
  double simTime = SIM_TIME;
  CommandLine cmd;
  cmd.AddValue("simTime",     "Simulation time [s]",           simTime);
  cmd.AddValue("nNodes",      "Total number of Wi-Fi nodes",   N_NODES);
  cmd.AddValue("nProducers",  "Number of producer nodes",      N_PRODUCERS);
  cmd.AddValue("enableMobility","Enable RandomWaypoint for consumers", ENABLE_MOBILITY);
  cmd.AddValue("minSpeed",    "Min speed [m/s]",               MIN_SPEED);
  cmd.AddValue("maxSpeed",    "Max speed [m/s]",               MAX_SPEED);
  cmd.AddValue("pauseTime",   "Pause time [s]",                PAUSE_TIME);
  //cmd.AddValue("enableAnim",   "Enable NetAnim trace",           ENABLE_ANIM);
  //cmd.AddValue("animFile",     "NetAnim output XML filename",    ANIM_FILE);
  //cmd.AddValue("animPktLimit", "Max packets per trace chunk",    ANIM_PKT_MAX);

  cmd.Parse(argc, argv);


 //ns3::LogComponentEnable ("ndn-cxx.nfd.slru", static_cast<ns3::LogLevel>(ns3::LOG_LEVEL_INFO | ns3::LOG_PREFIX_TIME));
 //ns3::LogComponentEnable ("ndn-cxx.nfd.CustomStrategy", static_cast<ns3::LogLevel>(ns3::LOG_LEVEL_INFO | ns3::LOG_PREFIX_TIME));
 //ns3::LogComponentEnable ("ndn-cxx.nfd.Forwarder", static_cast<ns3::LogLevel>(ns3::LOG_LEVEL_INFO | ns3::LOG_PREFIX_TIME));


  if (N_PRODUCERS >= N_NODES) {
    std::cerr << "ERROR: nProducers must be smaller than nNodes\n";
    return 1;
  }

  /* --- nodes ----------------------------------------------------- */
  NodeContainer nodes;
  nodes.Create(N_NODES);

  /* --- initial placement ---------------------------------------- */
  MobilityHelper staticMob;
  staticMob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  staticMob.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
      "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"),
      "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"));
  staticMob.Install(nodes);

  /* --- optional RandomWaypoint for consumers -------------------- */
  if (ENABLE_MOBILITY) {
    NodeContainer consumerNodes;
    for (uint32_t i = N_PRODUCERS; i < N_NODES; ++i)
      consumerNodes.Add(nodes.Get(i));

    MobilityHelper mobCons;
    mobCons.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"));
    mobCons.SetMobilityModel("ns3::RandomWaypointMobilityModel",
        "Speed", StringValue("ns3::UniformRandomVariable[Min=0.5|Max=1.5]"),
        "Pause", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"),
        "PositionAllocator", PointerValue(nullptr)); // allocator set above
    mobCons.Install(consumerNodes);
  }

  /* --- Wi-Fi ----------------------------------------------------- */
  YansWifiChannelHelper chan;
  chan.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  chan.AddPropagationLoss ("ns3::FriisPropagationLossModel");

  YansWifiPhyHelper phy;
  phy.SetChannel(chan.Create());
  phy.Set("TxPowerStart",   DoubleValue(20.0));
  phy.Set("TxPowerEnd",     DoubleValue(20.0));
  phy.Set("RxSensitivity",  DoubleValue(-96.0));
  phy.Set("CcaEdThreshold", DoubleValue(-99.0));

  WifiHelper wifi;
  wifi.SetStandard(WIFI_STANDARD_80211g);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue("ErpOfdmRate12Mbps"));

  WifiMacHelper mac;
  mac.SetType("ns3::AdhocWifiMac");

  NetDeviceContainer devs = wifi.Install(phy, mac, nodes);

  /* --- IP stack (optional) -------------------------------------- */
  InternetStackHelper ipStack;
  ipStack.Install(nodes);
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.0.0.0", "255.255.255.0");
  ipv4.Assign(devs);

  /* --- energy ---------------------------------------------------- */
  BasicEnergySourceHelper batt;
  batt.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(1000));
  EnergySourceContainer sources = batt.Install(nodes);

  WifiRadioEnergyModelHelper radio;
  radio.Set("TxCurrentA",   DoubleValue(0.038));
  radio.Set("RxCurrentA",   DoubleValue(0.027));
  radio.Set("IdleCurrentA", DoubleValue(0.018));
  radio.Install(devs, sources);

  /* --- ndnSIM stack + strategy ---------------------------------- */
  ns3::ndn::StackHelper stack;
  stack.setCsSize(CS_SIZE);
  stack.InstallAll();

  ns3::ndn::StrategyChoiceHelper::InstallAll(
      "/", "/localhost/nfd/strategy/custom");

  /* --- routing --------------------------------------------------- */
  ns3::ndn::GlobalRoutingHelper gr;
  gr.InstallAll();                   // add FIB entries later

  /* --- apps ------------------------------------------------------ */
  ns3::ndn::AppHelper prod("ns3::ndn::Producer");
  prod.SetPrefix("/prefix");
  prod.SetAttribute("PayloadSize", StringValue("1024"));

  ns3::ndn::AppHelper cons("ns3::ndn::ConsumerZipfMandelbrot");
  cons.SetPrefix("/prefix");
  cons.SetAttribute("Frequency",        DoubleValue(CONS_RATE));
  cons.SetAttribute("NumberOfContents", UintegerValue(CATALOGUE));

  /* install producers */
  for (uint32_t i = 0; i < N_PRODUCERS; ++i) {
    prod.Install(nodes.Get(i));
    gr.AddOrigins("/prefix", nodes.Get(i));
  }

  /* install consumers */
  for (uint32_t i = N_PRODUCERS; i < N_NODES; ++i) {
    cons.Install(nodes.Get(i));
  }

  ns3::ndn::GlobalRoutingHelper::CalculateRoutes();

  /* --- tracing --------------------------------------------------- */
  ns3::ndn::L3RateTracer::InstallAll("metrics/rate.txt", Seconds(1.0));
  ns3::ndn::AppDelayTracer::InstallAll("metrics/app-delays.txt");

/*  // --- NetAnim visualisation (optional) --------------------------- 
  if (ENABLE_ANIM) {
  AnimationInterface anim(ANIM_FILE);
  anim.EnablePacketMetadata();                 // show src/dst in GUI
  //anim.SetMaxPktsPerTraceFile(ANIM_PKT_MAX);   // split large runs

  // Optional cosmetics (keep or remove as you like)
  for (uint32_t i = 0; i < N_PRODUCERS; ++i) {
    anim.UpdateNodeDescription(nodes.Get(i), "Producer");
    anim.UpdateNodeColor      (nodes.Get(i),   0,   0, 255);  // blue
  }
  for (uint32_t i = N_PRODUCERS; i < N_NODES; ++i) {
    anim.UpdateNodeDescription(nodes.Get(i), "Consumer");
    anim.UpdateNodeColor      (nodes.Get(i),   0, 255,   0);  // green
  }
  }
*/


  /* battery log */
  PollEnergy(&sources);

  /* --- run ------------------------------------------------------- */
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}


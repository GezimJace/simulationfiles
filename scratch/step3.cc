/* slru-end2end.cc  ----------------------------------------------------------
 * One consumer ↔ one producer, Zipf workload, your custom SLRU strategy active.
 * The default NFD Content Store is disabled so every repeat Interest reaches
 * the strategy (and thus SLRU).
 * ------------------------------------------------------------------------- */

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/network-module.h"

namespace ns3 {

int
main(int argc, char* argv[])
{
  // ---------- parameters you can tweak on the command line ----------------
  uint32_t nContents = 120;    // catalogue size
  double   q         = 0.7;   // Zipf skew
  double   freq      = 200;   // Interests per second
  double   simTime   = 32;    // seconds
  CommandLine cmd;
  cmd.AddValue("nContents", "catalogue size", nContents);
  cmd.AddValue("q",         "Zipf exponent",   q);
  cmd.AddValue("freq",      "Interest rate",   freq);
  cmd.AddValue("simTime",   "simulation time", simTime);
  cmd.Parse(argc, argv);

  // ---------- minimal two-node topology -----------------------------------
  Ptr<Node> consumer = CreateObject<Node>();
  Ptr<Node> producer = CreateObject<Node>();
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue("10Mbps"));
  p2p.SetChannelAttribute("Delay",    StringValue("10ms"));
  p2p.Install(consumer, producer);

  // NDN stack with our custom strategy
  ns3::ndn::StackHelper ndnHelper;
  ndnHelper.setCsSize(100);
  ndnHelper.setPolicy("nfd::cs::priority_fifo");
  ndnHelper.InstallAll();
  ns3::ndn::StrategyChoiceHelper::InstallAll(
      "/", "/localhost/nfd/strategy/custom");

  // ---------- static FIB route --------------------------------------------
  ndn::FibHelper::AddRoute(consumer, "/video", producer, 0);

  // ---------- consumer -----------------------------------------------------
  ndn::AppHelper cons("ns3::ndn::ConsumerZipfMandelbrot");
  cons.SetPrefix("/video");
  cons.SetAttribute("Frequency",        DoubleValue(freq));
  cons.SetAttribute("NumberOfContents", UintegerValue(nContents));
  cons.SetAttribute("q",                DoubleValue(q));
  cons.Install(consumer);

  // ---------- producer -----------------------------------------------------
  ndn::AppHelper prod("ns3::ndn::Producer");
  prod.SetPrefix("/video");
  prod.SetAttribute("PayloadSize", StringValue("1024"));
  prod.Install(producer);

  // ---------- run ----------------------------------------------------------
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

} // namespace ns3

int
main(int argc, char* argv[])
{
  return ns3::main(argc, argv);
}


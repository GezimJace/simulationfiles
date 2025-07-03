/*  repeat-names.cc -----------------------------------------------------------
 *  One consumer repeatedly requests a small catalogue of /video/seq=*
 *  names according to a Zipf-Mandelbrot law (heavy hitters).               */

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

namespace ns3 {

/* ---------- Topology:   [Consumer] <---> [Producer] ---------------------- */

int
main(int argc, char* argv[])
{
  // ---------- Command-line knobs ------------------------------------------
  uint32_t     nContents   = 100;     // catalogue size (≤ SLRU capacity)
  double       q           = 0.7;    // Zipf exponent (0 = uniform)
  double       freq        = 500.0;  // Interests per second
  double       simTime     = 10.0;   // seconds
  CommandLine().AddValue("nContents", "Vocabulary size", nContents);
  CommandLine().AddValue("q",         "Zipf alpha parameter", q);
  CommandLine().AddValue("freq",      "Consumer rate (pkt/s)", freq);
  CommandLine().AddValue("simTime",   "Simulation time (s)", simTime);
  CommandLine().Parse(argc, argv);

  // ---------- Nodes --------------------------------------------------------
  Ptr<Node> consumer = CreateObject<Node>();
  Ptr<Node> producer = CreateObject<Node>();
  NodeContainer nodes(consumer, producer);

  // ---------- Link ---------------------------------------------------------
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue("10Mbps"));
  p2p.SetChannelAttribute("Delay",    StringValue("10ms"));
  p2p.Install(consumer, producer);

  // ---------- NDN stack + your strategy -----------------------------------
  ndn::StackHelper ndnHelper;
  ndnHelper.setCsSize(1);                               // bypass default CS
  ndnHelper.InstallAll();

  ndn::StrategyChoiceHelper::InstallAll(
      "/", "/localhost/nfd/strategy/custom");           // your strategy
  ndn::FibHelper::AddRoute(consumer, "/video", producer, 0);

  // ---------- Consumer (Zipf) ---------------------------------------------
  ndn::AppHelper consHelper("ns3::ndn::ConsumerZipfMandelbrot");
  consHelper.SetPrefix     ("/video");
  consHelper.SetAttribute  ("Frequency",         DoubleValue(freq));
  consHelper.SetAttribute  ("NumberOfContents",  UintegerValue(nContents));
  consHelper.SetAttribute  ("q",                 DoubleValue(q));
  consHelper.Install(consumer);

  // ---------- Producer -----------------------------------------------------
  ndn::AppHelper prodHelper("ns3::ndn::Producer");
  prodHelper.SetPrefix("/video");
  prodHelper.SetAttribute("PayloadSize", StringValue("1024"));
  prodHelper.Install(producer);

  // ---------- Run ----------------------------------------------------------
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


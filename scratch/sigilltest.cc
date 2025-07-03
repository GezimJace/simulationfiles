/* built-in-hit-test.cc  -----------------------------------------------
 * Demonstrates cache hits with the stock Best-Route strategy.
 * Two nodes, point-to-point link, ConsumerZipfMandelbrot repeatedly
 * requests 20 names under /video.  No custom code involved.
 * -------------------------------------------------------------------- */

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/network-module.h"


namespace ns3 {

int
main(int argc, char* argv[])
{
  // ----------- CLI parameters -----------------------------------------
  uint32_t nContents = 5;
  double   q         = 0.7;      // Zipf skew
  double   freq      = 200.0;    // Interests per second
  double   simTime   = 30.0;     // seconds
  CommandLine cmd;
  cmd.AddValue("nContents", "catalogue size", nContents);
  cmd.AddValue("q",         "Zipf exponent",  q);
  cmd.AddValue("freq",      "Interest rate",  freq);
  cmd.AddValue("simTime",   "simulation time", simTime);
  cmd.Parse(argc, argv);

  // ----------- topology ------------------------------------------------
  Ptr<Node> consumer = CreateObject<Node>();
  Ptr<Node> producer = CreateObject<Node>();
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue("10Mbps"));
  p2p.SetChannelAttribute("Delay",    StringValue("10ms"));
  p2p.Install(consumer, producer);

  // ----------- NDN stack ----------------------------------------------
  ndn::StackHelper ndnHelper;
  ndnHelper.setCsSize(100);
  ndnHelper.setPolicy("nfd::cs::lru");
  ndnHelper.InstallAll();

  // Strategy: built-in Best-Route (already default)
  ndn::StrategyChoiceHelper::InstallAll(
      "/", "/localhost/nfd/strategy/best-route");

  // ----------- routing -------------------------------------------------
  // simplest: add one static FIB entry
  ndn::FibHelper::AddRoute(consumer, "/video", producer, 0);

  // ----------- consumer -----------------------------------------------
  ndn::AppHelper consumerHelper("ns3::ndn::ConsumerZipfMandelbrot");
  consumerHelper.SetPrefix("/video");
  consumerHelper.SetAttribute("Frequency",        DoubleValue(freq));
  consumerHelper.SetAttribute("NumberOfContents", UintegerValue(nContents));
  consumerHelper.SetAttribute("q",                DoubleValue(q));
  consumerHelper.Install(consumer);

  // ----------- producer ------------------------------------------------
  ndn::AppHelper producerHelper("ns3::ndn::Producer");
  producerHelper.SetPrefix("/video");
  producerHelper.SetAttribute("PayloadSize", StringValue("1024"));
  producerHelper.Install(producer);

  //------------logs------------------------------------------------------
  
  ndn::CsTracer::InstallAll("cs-trace.log", Seconds(1.0));
  
  // ----------- run -----------------------------------------------------
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


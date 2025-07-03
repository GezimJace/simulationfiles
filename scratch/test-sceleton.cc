/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

using namespace ns3;

int
main(int argc, char* argv[])
{
  CommandLine cmd; cmd.Parse(argc, argv);

  // — 3-node chain: C--R--P —
  NodeContainer nodes; nodes.Create(3);       // 0:C 1:R 2:P
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  p2p.SetChannelAttribute("Delay",   StringValue("2ms"));
  p2p.Install(nodes.Get(0), nodes.Get(1));
  p2p.Install(nodes.Get(1), nodes.Get(2));

  // NDN stack with our custom strategy
  ns3::ndn::StackHelper ndnHelper;
  ndnHelper.setCsSize(1);
  ndnHelper.setPolicy("nfd::cs::priority_fifo");
  ndnHelper.InstallAll();
  ns3::ndn::StrategyChoiceHelper::InstallAll(
      "/", "/localhost/nfd/strategy/custom");

  // Global routing
  ns3::ndn::GlobalRoutingHelper gr; gr.InstallAll();

  // Producer
  ns3::ndn::AppHelper prod("ns3::ndn::Producer");
  prod.SetPrefix("/video");
  prod.SetAttribute("PayloadSize", StringValue("1200"));
  prod.Install(nodes.Get(2));
  gr.AddOrigins("/video", nodes.Get(2));

  // Consumer that RE-requests the **same 10 names** every 100 ms
  ns3::ndn::AppHelper cons("ns3::ndn::ConsumerCbr");
  cons.SetPrefix("/video");
  cons.SetAttribute("Frequency", StringValue("100"));      // 100 req/s
  cons.SetAttribute("Randomize", StringValue("none"));
  cons.Install(nodes.Get(0));

  ns3::ndn::GlobalRoutingHelper::CalculateRoutes();

  // Tracers
  double step = 0.25;
  ns3::ndn::L3RateTracer::InstallAll("rate.txt", Seconds(step));
  ns3::ndn::AppDelayTracer::InstallAll("delay.txt");

  Simulator::Stop(Seconds(30));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}



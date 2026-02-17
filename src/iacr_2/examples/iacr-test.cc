/*
 * IACR_2 Test - Interference-Aware Cooperative Routing
 * Demonstrates the complete paper implementation with interference metrics
 */

#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/core-module.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/net-device-container.h"
#include "ns3/network-module.h"
#include "ns3/node-container.h"
#include "ns3/on-off-application-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/simulator.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-mac-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-phy.h"

#include "ns3/iacr-helper.h"

#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("IacrTest2");

int
main(int argc, char* argv[])
{
    // Enable logging for key components
    LogComponentEnable("IacrRoutingProtocol2", LOG_LEVEL_INFO);
    LogComponentEnable("IacrHelper2", LOG_LEVEL_INFO);

    uint32_t nNodes = 6;
    double simTime = 20.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nNodes", "Number of nodes", nNodes);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.Parse(argc, argv);

    std::cout << "\n=== IACR_2 (Interference-Aware Cooperative Routing) Test ===" << std::endl;
    std::cout << "Nodes: " << nNodes << std::endl;
    std::cout << "Simulation Time: " << simTime << " seconds" << std::endl;
    std::cout << "Algorithm: IACR (Interference-Aware Cooperative Routing)" << std::endl;
    std::cout << "Paper: arXiv:2201.01520v1\n" << std::endl;

    // Create nodes
    NodeContainer nodes;
    nodes.Create(nNodes);

    // Create WiFi channel
    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(wifiChannel.Create());
    wifiPhy.Set("TxPowerStart", DoubleValue(20.0));
    wifiPhy.Set("TxPowerEnd", DoubleValue(20.0));

    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", 
                                  "DataMode", StringValue("DsssRate11Mbps"),
                                  "ControlMode", StringValue("DsssRate1Mbps"));

    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

    // Configure mobility - linear topology for predictable routing
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        posAlloc->Add(Vector(i * 100.0, 0.0, 0.0));  // Linear arrangement, 100m apart
    }

    mobility.SetPositionAllocator(posAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // Install internet stack with IACR
    InternetStackHelper internet;

    iacr::IacrHelper iacrHelper;
    Ipv4StaticRoutingHelper staticRouting;

    Ipv4ListRoutingHelper list;
    list.Add(staticRouting, 0);
    list.Add(iacrHelper, 10);

    internet.SetRoutingHelper(list);
    internet.Install(nodes);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    std::cout << "Node Positions and IP Addresses:" << std::endl;
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Node> node = nodes.Get(i);
        Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
        Vector pos = mobility->GetPosition();
        std::cout << "  Node " << i << ": IP=" << interfaces.GetAddress(i) 
                  << " Pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
    }
    std::cout << std::endl;

    // Create traffic between first and last node
    uint16_t port = 9;

    // Packet sink on last node
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(nodes.Get(nNodes - 1));
    sinkApp.Start(Seconds(1.0));
    sinkApp.Stop(Seconds(simTime));

    // OnOff sender on first node
    OnOffHelper onoff("ns3::UdpSocketFactory", 
                      InetSocketAddress(interfaces.GetAddress(nNodes - 1), port));
    onoff.SetConstantRate(DataRate("256kbps"));
    ApplicationContainer senderApp = onoff.Install(nodes.Get(0));
    senderApp.Start(Seconds(2.0));
    senderApp.Stop(Seconds(simTime - 1.0));

    Simulator::Stop(Seconds(simTime));

    std::cout << "Starting simulation..." << std::endl;
    Simulator::Run();

    std::cout << "\n=== Simulation Complete ===" << std::endl;
    std::cout << "Total simulation time: " << Simulator::Now().GetSeconds() << " seconds" << std::endl;

    Simulator::Destroy();

    return 0;
}

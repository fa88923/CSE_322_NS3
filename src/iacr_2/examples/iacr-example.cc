/*
 * IACR Example - Simple Network Test
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/double.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/net-device-container.h"
#include "ns3/network-module.h"
#include "ns3/node-container.h"
#include "ns3/on-off-application-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-mac-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-phy.h"

#include "ns3/iacr-helper.h"

#include <iostream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("IacrExample");

int
main(int argc, char* argv[])
{
    // LogComponentEnable("IacrRoutingProtocol", LOG_LEVEL_DEBUG);
    // LogComponentEnable("IacrPacket", LOG_LEVEL_DEBUG);

    // Create 5 nodes
    uint32_t nNodes = 5;
    NodeContainer nodes;
    nodes.Create(nNodes);

    // Create WiFi channel
    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(wifiChannel.Create());

    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                                  StringValue("DsssRate11Mbps"), "ControlMode",
                                  StringValue("DsssRate1Mbps"));

    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

    // Mobility
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator", "MinX", DoubleValue(0.0), "MinY",
                                   DoubleValue(0.0), "DeltaX", DoubleValue(100.0), "DeltaY",
                                   DoubleValue(100.0), "GridWidth", UintegerValue(3), "LayoutType",
                                   StringValue("RowFirst"));

    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // Install internet stack
    InternetStackHelper internet;
    internet.Install(nodes);

    // Install IACR on all nodes
    iacr::IacrHelper iacr;
    Ipv4StaticRoutingHelper staticRouting;

    Ipv4ListRoutingHelper list;
    list.Add(staticRouting, 0);
    list.Add(iacr, 10);

    internet.SetRoutingHelper(list);
    internet.Install(nodes);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign(devices);

    // Create sender and receiver
    uint16_t port = 9;

    // Packet sink
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(nodes.Get(nNodes - 1));
    sinkApp.Start(Seconds(1.0));
    sinkApp.Stop(Seconds(10.0));

    // OnOff application
    OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(i.GetAddress(nNodes - 1), port));
    onoff.SetConstantRate(DataRate("256kbps"));
    ApplicationContainer onoffApp = onoff.Install(nodes.Get(0));
    onoffApp.Start(Seconds(2.0));
    onoffApp.Stop(Seconds(9.0));

    Simulator::Stop(Seconds(10.0));

    std::cout << "Starting IACR Example Simulation..." << std::endl;
    std::cout << "Nodes: " << nNodes << std::endl;
    std::cout << "Sending traffic from node 0 to node " << (nNodes - 1) << std::endl;

    Simulator::Run();
    Simulator::Destroy();

    std::cout << "Simulation completed." << std::endl;

    return 0;
}

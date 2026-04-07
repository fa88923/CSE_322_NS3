#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/aodv-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/energy-module.h"
#include "ns3/sixlowpan-module.h"
#include <fstream>
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Wpan80215Mobile");

int
main(int argc, char* argv[])
{
  // ── CLI parameters (same structure as aodv_simulation.cc) ─
    uint32_t    nNodes         = 40;
    uint32_t    nFlows         = 20;
    uint32_t    pps            = 200;
    uint32_t    areaMultiplier = 3;
    uint32_t    seed           = 12345;
    double      simTime        = 60.0;
    uint32_t    pktSize        = 512;
    bool        verbose        = false;
    std::string sweep          = "nodes"; // "nodes","flows","pps","area"
    std::string channelType    = "lrwpan"; // "wifi" or "lrwpan"

    CommandLine cmd (__FILE__);
    cmd.AddValue ("channelType",    "Channel type (wifi or lrwpan)",    channelType);
    cmd.AddValue ("nNodes",         "Number of nodes",                 nNodes);
    cmd.AddValue ("nFlows",         "Number of UDP flows",             nFlows);
    cmd.AddValue ("pps",            "Packets per second per flow",     pps);
    cmd.AddValue ("areaMultiplier", "Area multiplier k (side=k*Txr)",  areaMultiplier);
    cmd.AddValue ("seed",           "RNG seed",                        seed);
    cmd.AddValue ("simTime",        "Total simulation time (s)",       simTime);
    cmd.AddValue ("pktSize",        "Application packet size (bytes)", pktSize);
    cmd.AddValue ("verbose",        "Enable ns-3 logging",             verbose);
    cmd.AddValue ("sweep",          "Which param is varying",          sweep);
    cmd.Parse (argc, argv);

    const double txRange   = 200.0;
    double       areaSide  = static_cast<double>(areaMultiplier) * txRange;
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpRenoJ::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(pktSize));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(4));
    Config::SetDefault("ns3::TcpSocket::SndBufSize",
                       UintegerValue(1 << 21));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",
                       UintegerValue(1 << 21));
    Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold",
                       UintegerValue(64 * pktSize));

    // ── Nodes ─────────────────────────────────────────────────────────────────
    NodeContainer nodes;
    nodes.Create(nNodes);

    // ── 802.15.4 (LR-WPAN) setup ─────────────────────────────────────────────
    LrWpanHelper lrWpanHelper;
    NetDeviceContainer lrwpanDevices = lrWpanHelper.Install(nodes);
    lrWpanHelper.CreateAssociatedPan(lrwpanDevices, 0);

        Ptr<LogDistancePropagationLossModel> lossModel =
        CreateObject<LogDistancePropagationLossModel> ();
    lossModel->SetAttribute ("Exponent",           DoubleValue (3.0));
    lossModel->SetAttribute ("ReferenceDistance",  DoubleValue (1.0));

    // Retrieve the single shared channel that LrWpanHelper created and attach
    // the propagation models to it.
    Ptr<SpectrumChannel> chan = lrWpanHelper.GetChannel ();
    chan->AddPropagationLossModel  (lossModel);

    // ── SixLowPan (IPv6 over 802.15.4) ───────────────────────────────────────
    SixLowPanHelper sixlowpan;
    NetDeviceContainer devices = sixlowpan.Install(lrwpanDevices);
    MobilityHelper mobility;

    mobility.SetPositionAllocator (
        "ns3::RandomRectanglePositionAllocator",
        "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=" +
                           std::to_string (areaSide) + "]"),
        "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=" +
                           std::to_string (areaSide) + "]")
    );
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (nodes);

    // ── Internet stack with AODV ──────────────────────────────────────────────
    AodvHelper aodv;
    aodv.Set ("EnableHello",    BooleanValue (true));
    aodv.Set ("HelloInterval",  TimeValue (Seconds (0.2)));
    InternetStackHelper internet;
    internet.SetRoutingHelper(aodv);
    internet.Install(nodes);

    // ── IPv6 addresses ────────────────────────────────────────────────────────
    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:db8::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer interfaces = ipv6.Assign(devices);
    interfaces.SetForwarding(0, true);
    interfaces.SetDefaultRouteInAllNodes(0);

    // ── Energy model ──────────────────────────────────────────────────────────
    BasicEnergySourceHelper energySourceHelper;
    energySourceHelper.Set("BasicEnergySourceInitialEnergyJ",
                           DoubleValue(100.0));
    energy::EnergySourceContainer energySources = energySourceHelper.Install(nodes);

    // ── Applications ──────────────────────────────────────────────────────────
    uint16_t port        = 9;
    uint32_t actualFlows = std::min(nFlows, nNodes / 2);

    ApplicationContainer sinkApps, sourceApps;

    for (uint32_t i = 0; i < actualFlows; i++)
    {
        uint32_t sinkIdx   = nNodes - 1 - (i % (nNodes / 2));
        uint32_t sourceIdx = i % (nNodes / 2);

        // Sink
        PacketSinkHelper sinkHelper(
            "ns3::TcpSocketFactory",
            Inet6SocketAddress(Ipv6Address::GetAny(), port + i));
        sinkApps.Add(sinkHelper.Install(nodes.Get(sinkIdx)));

        // Source
        OnOffHelper source(
            "ns3::TcpSocketFactory",
            Inet6SocketAddress(interfaces.GetAddress(sinkIdx, 1), port + i));
        source.SetConstantRate(DataRate(nPktPerSec * pktSize * 8));
        source.SetAttribute("PacketSize", UintegerValue(pktSize));
        sourceApps.Add(source.Install(nodes.Get(sourceIdx)));
    }

    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(simTime));
    sourceApps.Start(Seconds(2.0));  // extra delay for AODV + mobility settle
    sourceApps.Stop(Seconds(simTime));

    // ── Flow Monitor ──────────────────────────────────────────────────────────
    FlowMonitorHelper flowMonHelper;
    Ptr<FlowMonitor> flowMonitor = flowMonHelper.InstallAll();

    // ── Run ───────────────────────────────────────────────────────────────────
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ── Collect metrics ───────────────────────────────────────────────────────
    flowMonitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

    double   totalThroughput = 0.0;
    double   totalDelay      = 0.0;
    uint64_t totalTxPkts     = 0;
    uint64_t totalRxPkts     = 0;
    uint32_t flowCount       = 0;

    for (auto& flow : stats)
    {
        totalTxPkts += flow.second.txPackets;

        if (flow.second.rxPackets == 0) continue;

        double duration = flow.second.timeLastRxPacket.GetSeconds()
                        - flow.second.timeFirstTxPacket.GetSeconds();
        double tput     = (duration > 0) ?
                          flow.second.rxBytes * 8.0 / duration / 1000.0 : 0;
        double delay    = flow.second.delaySum.GetSeconds() /
                          flow.second.rxPackets * 1000.0;

        totalThroughput += tput;
        totalDelay      += delay;
        totalRxPkts     += flow.second.rxPackets;
        flowCount++;
    }

    double avgThroughput = (flowCount > 0) ? totalThroughput / flowCount : 0;
    double avgDelay      = (flowCount > 0) ? totalDelay      / flowCount : 0;
    double pdr           = (totalTxPkts > 0) ?
                           (double)totalRxPkts / totalTxPkts * 100.0 : 0;
    double dropRatio     = 100.0 - pdr;

    // ── Energy ────────────────────────────────────────────────────────────────
    double totalEnergy = 0.0;
    for (uint32_t i = 0; i < energySources.GetN(); i++)
    {
        Ptr<energy::BasicEnergySource> src =
            DynamicCast<energy::BasicEnergySource>(energySources.Get(i));
        totalEnergy += 100.0 - src->GetRemainingEnergy();
    }
    double avgEnergy = totalEnergy / nNodes;

    // ── Print ─────────────────────────────────────────────────────────────────
    std::cout << "\n=== 802.15.4 Mobile Results ===" << std::endl;
    std::cout << "Sweep        : " << sweep        << std::endl;
    std::cout << "Nodes        : " << nNodes       << std::endl;
    std::cout << "Flows        : " << actualFlows  << std::endl;
    std::cout << "PktPerSec    : " << nPktPerSec   << std::endl;
    std::cout << "Speed        : " << speed << " m/s" << std::endl;
    std::cout << "Throughput   : " << avgThroughput << " kbps" << std::endl;
    std::cout << "Delay        : " << avgDelay      << " ms"   << std::endl;
    std::cout << "PDR          : " << pdr           << " %"    << std::endl;
    std::cout << "Drop Ratio   : " << dropRatio     << " %"    << std::endl;
    std::cout << "Avg Energy   : " << avgEnergy     << " J"    << std::endl;

    // ── Save to dat file ──────────────────────────────────────────────────────
    std::string outFile = "results-wpan-mobile-" + sweep + ".dat";
    std::ofstream out(outFile, std::ios::app);
    if (out.is_open())
    {
        double xVal = 0;
        if      (sweep == "nodes") xVal = nNodes;
        else if (sweep == "flows") xVal = nFlows;
        else if (sweep == "pps")   xVal = nPktPerSec;
        else if (sweep == "speed") xVal = speed;

        out << xVal          << "\t"
            << avgThroughput << "\t"
            << avgDelay      << "\t"
            << pdr           << "\t"
            << dropRatio     << "\t"
            << avgEnergy     << "\n";
        out.close();
    }

    Simulator::Destroy();
    return 0;
}

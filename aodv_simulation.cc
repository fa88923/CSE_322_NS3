/**
 * wpan-static-renoj.cc
 *
 * Static 802.15.4 (LR-WPAN) AODV simulation.
 *
 * Derived from wpan-mobile-renoj.cc with the following changes:
 *  - Random Waypoint mobility replaced with static ConstantPositionMobilityModel
 *    (nodes placed uniformly at random inside a square whose side = areaMultiplier
 *     * txRange, matching aodv_simulation.cc exactly).
 *  - TCP/RenoJ removed; applications now use UDP OnOff + PacketSink (same as
 *    aodv_simulation.cc lrwpan branch).
 *  - Command-line argument structure matches aodv_simulation.cc:
 *      --nNodes, --nFlows, --pps, --areaMultiplier, --seed, --simTime, --pktSize
 *    The old --speed / --sweep / --nPktPerSec arguments are gone.
 *  - AODV HelloInterval and EnableHello set identically to aodv_simulation.cc.
 *  - LrWpanHelper constructed with (false) to enable the log-distance propagation
 *    channel, and a LogDistancePropagationLossModel + ConstantSpeedPropagation-
 *    DelayModel are attached — matching the channel setup in aodv_simulation.cc.
 *  - Energy calculation uses the custom PHY-state-callback method from
 *    aodv_simulation.cc (CC2420 current values; no BasicEnergySource needed).
 *  - All metric calculations (throughput, delay, PDR, drop ratio, per-node
 *    throughput) are kept as-is from wpan-mobile-renoj.cc.
 *  - Output label changed to "802.15.4 Static Results".
 *  - .dat output file renamed to "results-wpan-static-<sweep>.dat"; the sweep
 *    key set is now "nodes","flows","pps","area" (speed removed).
 *
 * Build (from ns-3.45 root):
 *   cp wpan-static-renoj.cc scratch/
 *   ./ns3 run "scratch/wpan-static-renoj --nNodes=40 --nFlows=20 --pps=200 --areaMultiplier=3"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/aodv-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/spectrum-channel.h"

#include <fstream>
#include <cmath>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WpanStatic");

// ============================================================
//  Custom 802.15.4 energy tracker
//
//  Mirrors the implementation in aodv_simulation.cc (lrwpan branch).
//  We hook the LrWpanPhy "TrxStateValue" trace and integrate
//  current * voltage * dt for each PHY state interval.
//  Values from CC2420 datasheet (the reference 802.15.4 chip):
//    TX_ON / BUSY_TX : 17.4 mA
//    RX_ON / BUSY_RX : 18.8 mA
//    TRX_OFF (idle)  :  0.426 mA
//  Supply voltage    : 3.0 V
// ============================================================
struct LrWpanNodeEnergy
{
    double                      consumedJ  = 0.0;
    double                      lastTime   = 0.0;
    lrwpan::PhyEnumeration      lastState  = lrwpan::IEEE_802_15_4_PHY_TRX_OFF;
};

static const double LRWPAN_VOLTAGE = 3.0;
static const double LRWPAN_TX_A    = 17.4e-3;
static const double LRWPAN_RX_A    = 18.8e-3;
static const double LRWPAN_IDLE_A  =  0.426e-3;

static std::vector<LrWpanNodeEnergy> g_lrwpanEnergy;

static double LrWpanCurrentA (lrwpan::PhyEnumeration s)
{
    switch (s)
    {
    case lrwpan::IEEE_802_15_4_PHY_TX_ON:
    case lrwpan::IEEE_802_15_4_PHY_BUSY_TX:  return LRWPAN_TX_A;
    case lrwpan::IEEE_802_15_4_PHY_RX_ON:
    case lrwpan::IEEE_802_15_4_PHY_BUSY_RX:  return LRWPAN_RX_A;
    default:                                   return LRWPAN_IDLE_A;
    }
}

void LrWpanStateChangeCb (uint32_t idx,
                           lrwpan::PhyEnumeration oldState,
                           lrwpan::PhyEnumeration newState)
{
    double now = Simulator::Now ().GetSeconds ();
    double dt  = now - g_lrwpanEnergy[idx].lastTime;
    if (dt > 0.0)
        g_lrwpanEnergy[idx].consumedJ +=
            LrWpanCurrentA (oldState) * LRWPAN_VOLTAGE * dt;
    g_lrwpanEnergy[idx].lastTime  = now;
    g_lrwpanEnergy[idx].lastState = newState;
}

static void FlushLrWpanEnergy (uint32_t nNodes)
{
    double now = Simulator::Now ().GetSeconds ();
    for (uint32_t i = 0; i < nNodes; i++)
    {
        double dt = now - g_lrwpanEnergy[i].lastTime;
        if (dt > 0.0)
            g_lrwpanEnergy[i].consumedJ +=
                LrWpanCurrentA (g_lrwpanEnergy[i].lastState) * LRWPAN_VOLTAGE * dt;
        g_lrwpanEnergy[i].lastTime = now;
    }
}

// ============================================================
//  main
// ============================================================
int main (int argc, char *argv[])
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

    NS_ABORT_MSG_IF (nFlows > nNodes,
        "nFlows (" << nFlows << ") cannot exceed nNodes (" << nNodes << ")");
    NS_ABORT_MSG_IF (simTime < 10.0,
        "simTime must be >= 10 s");

    RngSeedManager::SetSeed (1);
    RngSeedManager::SetRun  (seed);

    // 802.15.4 nominal Tx range = 40 m (same as aodv_simulation.cc)
    const double txRange   = 40.0;
    double       areaSide  = static_cast<double>(areaMultiplier) * txRange;
    double       dataStart = 15.0;   // extra AODV convergence time for LR-WPAN
    double       dataStop  = simTime - 5.0;

    NS_LOG_UNCOND ("=== 802.15.4 Static AODV Simulation ===");
    NS_LOG_UNCOND ("nNodes    : " << nNodes);
    NS_LOG_UNCOND ("nFlows    : " << nFlows);
    NS_LOG_UNCOND ("PPS       : " << pps);
    NS_LOG_UNCOND ("AreaMult  : " << areaMultiplier << "  (" << areaSide << " m side)");
    NS_LOG_UNCOND ("PktSize   : " << pktSize << " bytes");
    NS_LOG_UNCOND ("SimTime   : " << simTime << " s");

    // ── Nodes ─────────────────────────────────────────────────
    NodeContainer nodes;
    nodes.Create (nNodes);

    // ── 802.15.4 (LR-WPAN) setup ──────────────────────────────
    // Pass false to LrWpanHelper so it creates a channel that we can
    // attach custom propagation models to (same approach as aodv_simulation.cc).
    LrWpanHelper lrWpanHelper (false);
    NetDeviceContainer lrwpanDevices = lrWpanHelper.Install (nodes);
    
    // Assign unique short addresses manually (ad-hoc mesh, not PAN coordinator)
    for (uint32_t i = 0; i < nNodes; i++)
    {
        Ptr<lrwpan::LrWpanNetDevice> dev =
            DynamicCast<lrwpan::LrWpanNetDevice> (lrwpanDevices.Get (i));
        dev->GetMac ()->SetShortAddress (Mac16Address (static_cast<uint16_t>(i + 1)));
        dev->GetMac ()->SetPanId (5);  // same PAN ID for channel sharing
    }

    // Attach the same propagation models used in aodv_simulation.cc:
    //   - ConstantSpeedPropagationDelayModel
    //   - LogDistancePropagationLossModel (exponent=3, ref=1 m)
    Ptr<LogDistancePropagationLossModel> lossModel =
        CreateObject<LogDistancePropagationLossModel> ();
    lossModel->SetAttribute ("Exponent",           DoubleValue (3.0));
    lossModel->SetAttribute ("ReferenceDistance",  DoubleValue (1.0));

    // Retrieve the single shared channel that LrWpanHelper created and attach
    // the propagation models to it.
    Ptr<SpectrumChannel> chan = lrWpanHelper.GetChannel ();
    chan->AddPropagationLossModel  (lossModel);
    // DelayModel is already set by LrWpanHelper, so don't set it again

    // ── SixLowPan (IPv6 over 802.15.4) ────────────────────────
    SixLowPanHelper sixlowpan;
    NetDeviceContainer devices = sixlowpan.Install (lrwpanDevices);

    // ── Static mobility: random placement, no movement ────────
    // Matches aodv_simulation.cc: RandomRectanglePositionAllocator +
    // ConstantPositionMobilityModel.
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

    // ── Internet stack + AODV ─────────────────────────────────
    // HelloInterval and EnableHello set identically to aodv_simulation.cc.
    AodvHelper aodv;
    aodv.Set ("EnableHello",    BooleanValue (true));
    aodv.Set ("HelloInterval",  TimeValue (Seconds (0.2)));
    InternetStackHelper internet;
    internet.SetRoutingHelper (aodv);
    internet.Install (nodes);

    // ── IPv6 addresses ─────────────────────────────────────────
    Ipv6AddressHelper ipv6;
    ipv6.SetBase (Ipv6Address ("2001:db8::"), Ipv6Prefix (64));
    Ipv6InterfaceContainer interfaces = ipv6.Assign (devices);
    for (uint32_t i = 0; i < nNodes; ++i)
        interfaces.SetForwarding (i, true);

    // ── Energy: custom PHY-state callback (from aodv_simulation.cc) ──
    g_lrwpanEnergy.resize (nNodes);
    for (uint32_t i = 0; i < nNodes; i++)
    {
        Ptr<lrwpan::LrWpanNetDevice> dev =
            DynamicCast<lrwpan::LrWpanNetDevice> (lrwpanDevices.Get (i));
        dev->GetPhy ()->TraceConnectWithoutContext (
            "TrxStateValue",
            MakeBoundCallback (&LrWpanStateChangeCb, i));
        g_lrwpanEnergy[i] = {};
    }

    // ── Applications: UDP OnOff + PacketSink ──────────────────
    // Flow-pair assignment kept from wpan-mobile-renoj.cc.
    uint16_t port        = 9;
    uint32_t actualFlows = std::min (nFlows, nNodes / 2);

    // Constant-rate string: pktSize * 8 * pps bps
    double dataRateBps = static_cast<double>(pktSize) * 8.0 *
                         static_cast<double>(pps);
    std::ostringstream drStr;
    drStr << std::fixed << std::setprecision (0) << dataRateBps << "bps";

    double dataDuration = dataStop - dataStart;

    ApplicationContainer sinkApps, sourceApps;

    for (uint32_t i = 0; i < actualFlows; i++)
    {
        uint32_t sinkIdx   = nNodes - 1 - (i % (nNodes / 2));
        uint32_t sourceIdx = i % (nNodes / 2);
        uint16_t flowPort  = static_cast<uint16_t>(port + i);

        Ipv6Address dstAddr = interfaces.GetAddress (sinkIdx, 1);

        // Sink
        PacketSinkHelper sinkHelper (
            "ns3::UdpSocketFactory",
            Inet6SocketAddress (Ipv6Address::GetAny (), flowPort));
        ApplicationContainer sa = sinkHelper.Install (nodes.Get (sinkIdx));
        sa.Start (Seconds (0.0));
        sa.Stop  (Seconds (simTime));
        sinkApps.Add (sa);

        // Source: OnOff CBR over UDP
        OnOffHelper source (
            "ns3::UdpSocketFactory",
            Inet6SocketAddress (dstAddr, flowPort));
        source.SetConstantRate (DataRate (drStr.str ()), pktSize);
        source.SetAttribute ("OnTime",
            StringValue ("ns3::ConstantRandomVariable[Constant=" +
                         std::to_string (dataDuration - 1.0) + "]"));
        source.SetAttribute ("OffTime",
            StringValue ("ns3::ConstantRandomVariable[Constant=0.001]"));

        ApplicationContainer oa = source.Install (nodes.Get (sourceIdx));
        oa.Start (Seconds (dataStart));
        oa.Stop  (Seconds (dataStop));
        sourceApps.Add (oa);
    }

    // ── Flow Monitor ──────────────────────────────────────────
    FlowMonitorHelper flowMonHelper;
    Ptr<FlowMonitor>  flowMonitor = flowMonHelper.InstallAll ();

    // ── Run ───────────────────────────────────────────────────
    Simulator::Stop (Seconds (simTime));
    NS_LOG_UNCOND ("Starting simulation...");
    Simulator::Run ();

    // Flush energy accumulators at simulation end
    FlushLrWpanEnergy (nNodes);

    // ── Collect metrics (kept from wpan-mobile-renoj.cc) ──────
    flowMonitor->CheckForLostPackets ();
    Ptr<Ipv6FlowClassifier> classifier =
        DynamicCast<Ipv6FlowClassifier> (flowMonHelper.GetClassifier6 ());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats ();

    double   totalThroughput = 0.0;
    double   totalDelay      = 0.0;
    uint64_t totalTxPkts     = 0;
    uint64_t totalRxPkts     = 0;
    uint32_t flowCount       = 0;

    for (auto &flow : stats)
    {
        Ipv6FlowClassifier::FiveTuple t = classifier->FindFlow (flow.first);
        // Skip non-UDP flows (ICMPv6, NDP, etc.)
        if (t.protocol != 17) continue;  // 17 = UDP

        totalTxPkts += flow.second.txPackets;

        if (flow.second.rxPackets == 0) continue;

        double duration = flow.second.timeLastRxPacket.GetSeconds ()
                        - flow.second.timeFirstTxPacket.GetSeconds ();
        double tput     = (duration > 0)
                          ? flow.second.rxBytes * 8.0 / duration / 1000.0 : 0;
        double delay    = flow.second.delaySum.GetSeconds () /
                          flow.second.rxPackets * 1000.0;

        totalThroughput += tput;
        totalDelay      += delay;
        totalRxPkts     += flow.second.rxPackets;
        flowCount++;
    }

    double avgThroughput = (flowCount > 0) ? totalThroughput / flowCount : 0;
    double avgDelay      = (flowCount > 0) ? totalDelay      / flowCount : 0;
    double pdr           = (totalTxPkts > 0)
                           ? (double)totalRxPkts / totalTxPkts * 100.0 : 0;
    double dropRatio     = 100.0 - pdr;

    // ── Energy (custom PHY-state method from aodv_simulation.cc) ──
    double totalEnergy = 0.0;
    for (uint32_t i = 0; i < nNodes; i++)
        totalEnergy += g_lrwpanEnergy[i].consumedJ;
    double avgEnergy = totalEnergy / static_cast<double>(nNodes);

    // ── Print ─────────────────────────────────────────────────
    std::cout << "\n=== 802.15.4 Static Results ===" << std::endl;
    std::cout << "Sweep        : " << sweep        << std::endl;
    std::cout << "Nodes        : " << nNodes       << std::endl;
    std::cout << "Flows        : " << actualFlows  << std::endl;
    std::cout << "PPS          : " << pps          << std::endl;
    std::cout << "AreaMult     : " << areaMultiplier << "  (" << areaSide << " m side)" << std::endl;
    std::cout << "PktSize      : " << pktSize      << " bytes" << std::endl;
    std::cout << "Throughput   : " << avgThroughput << " kbps" << std::endl;
    std::cout << "Delay        : " << avgDelay      << " ms"   << std::endl;
    std::cout << "PDR          : " << pdr           << " %"    << std::endl;
    std::cout << "Drop Ratio   : " << dropRatio     << " %"    << std::endl;
    std::cout << "Total Energy : " << totalEnergy   << " J"    << std::endl;
    std::cout << "Avg Energy   : " << avgEnergy     << " J"    << std::endl;

    // ── Output CSV row for the sweep script ────────────────────
    // Format: CSV,channelType,nNodes,nFlows,pps,areaMultiplier,pktSize,
    //         throughput_kbps,delay_s,pdr,dropRatio,totalEnergy_J,avgNodeEnergy_J,avgPerNodeTput_kbps
    double perNodeTput = (nFlows > 0) ? (avgThroughput / nFlows) : 0;
    std::cout << "CSV," << channelType << "," << nNodes << "," << nFlows << ","
              << pps << "," << areaMultiplier << "," << pktSize << ","
              << avgThroughput << "," << (avgDelay / 1000.0) << "," << (pdr / 100.0) << ","
              << (dropRatio / 100.0) << "," << totalEnergy << "," << avgEnergy << ","
              << perNodeTput << std::endl;

    // ── Save to dat file ──────────────────────────────────────
    std::string outFile = "results-wpan-static-" + sweep + ".dat";
    std::ofstream out (outFile, std::ios::app);
    if (out.is_open ())
    {
        double xVal = 0;
        if      (sweep == "nodes") xVal = nNodes;
        else if (sweep == "flows") xVal = nFlows;
        else if (sweep == "pps")   xVal = pps;
        else if (sweep == "area")  xVal = areaMultiplier;

        out << xVal          << "\t"
            << avgThroughput << "\t"
            << avgDelay      << "\t"
            << pdr           << "\t"
            << dropRatio     << "\t"
            << avgEnergy     << "\n";
        out.close ();
    }

    Simulator::Destroy ();
    return 0;
}
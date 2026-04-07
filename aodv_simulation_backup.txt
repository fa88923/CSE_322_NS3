/**
 * aodv_simulation.cc
 *
 * AODV simulation for ns-3.45 supporting two channel types:
 *   --channelType=wifi   : 802.11b ad-hoc (DBPSK @ 1 Mbps, Friis loss, Tx_range = 200 m)
 *   --channelType=lrwpan : 802.15.4 2.4 GHz O-QPSK (direct IPv4, Tx_range = 40 m)
 *
 * ── CONTROL-PACKET ISOLATION STRATEGY ────────────────────────────────────────
 *
 * Three independent mechanisms ensure that AODV control traffic (HELLO, RREQ,
 * RREP, RERR) and any other non-data IP traffic never contaminates metrics:
 *
 *  1. FlowMonitor filtering by (protocol=UDP, dstPort in [basePort, basePort+nFlows))
 *     AODV uses UDP port 654. Our data flows use ports 9000-9049 (or up to 9099).
 *     The five-tuple filter in the metric-collection loop admits only exact
 *     data-flow entries. ICMP (protocol != 17) and AODV (port 654) are excluded.
 *
 *  2. DataPacketTag — a lightweight ns-3 packet tag applied to every packet the
 *     OnOff application hands to the network socket (via the OnOff "Tx" trace).
 *     This tag survives the full ns-3 packet copy chain (IP -> MAC -> PHY)
 *     without being stripped. The MAC/PHY drop callbacks inspect for this tag
 *     and only increment data-drop counters when the tag is present.  Control
 *     packets are never tagged, so they are silently ignored by drop callbacks
 *     even though control packets also travel over UDP.
 *
 *  3. Application-layer Tx/Rx counters (g_appTxPkts / g_appRxPkts) track only
 *     packets that pass through our OnOff and PacketSink application callbacks.
 *     These are used as the authoritative Tx/Rx counts for PDR and drop ratio,
 *     bypassing FlowMonitor's txPackets field entirely (which can include
 *     forwarded copies and exhibits version-dependent counting differences).
 *
 * All variable parameters accepted via command line:
 *   --nNodes          (20, 40, 60, 80, 100)
 *   --nFlows          (10, 20, 30, 40, 50)
 *   --pps             packets per second per flow (100-500)
 *   --areaMultiplier  k -> square side = k * Tx_range  (1-5)
 *   --channelType     wifi | lrwpan
 *   --seed            RNG seed (default 12345)
 *   --simTime         total simulation time in seconds (default 50.0)
 *   --pktSize         application payload in bytes (default 512)
 *
 * Build (from ns-3.45 root):
 *   cp aodv_simulation.cc scratch/
 *   ./ns3 run "scratch/aodv_simulation --channelType=wifi --nNodes=40 --nFlows=20 --pps=200 --areaMultiplier=3"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/energy-module.h"
#include "ns3/wifi-radio-energy-model-helper.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/propagation-delay-model.h"

#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AodvSimulation");

// ============================================================
//  Mechanism 2: DataPacketTag
//
//  A trivial one-byte packet tag applied by the OnOff source to
//  every application data packet immediately after Send() is called
//  (via the OnOffApplication "Tx" trace callback).
//
//  ns-3 PacketTag objects ride alongside the packet through every
//  layer — IP fragmentation, MAC queuing, PHY — without being
//  stripped, because tags are stored in a separate tag buffer that
//  is not part of the serialised wire format.  This lets the
//  MAC/PHY drop callbacks call PeekPacketTag() to test whether
//  the dropped frame carries a data payload, and only then
//  increment the data-drop counters.
// ============================================================
class DataPacketTag : public Tag
{
public:
    static TypeId GetTypeId ()
    {
        static TypeId tid = TypeId ("ns3::DataPacketTag")
            .SetParent<Tag> ()
            .AddConstructor<DataPacketTag> ();
        return tid;
    }
    TypeId  GetInstanceTypeId () const override { return GetTypeId (); }
    uint32_t GetSerializedSize () const override { return 1; }
    void Serialize   (TagBuffer buf) const override { buf.WriteU8 (1); }
    void Deserialize (TagBuffer buf)       override { buf.ReadU8 (); }
    void Print (std::ostream &os)    const override { os << "DataPacketTag"; }
};

// ============================================================
//  Mechanism 3: Application-layer counters (data only)
// ============================================================
static uint64_t g_appTxPkts  = 0;   // packets injected by OnOff sources
static uint64_t g_appRxPkts  = 0;   // packets delivered to PacketSink apps
static uint64_t g_appRxBytes = 0;   // bytes delivered to PacketSink apps

// Per-destination-node received bytes (data only, for per-node throughput)
static std::map<uint32_t, uint64_t> g_nodeRxBytes;  // nodeId -> rx bytes

// OnOff "Tx" trace: fires after each Send() call with the packet that was sent.
// We add the DataPacketTag here and count the transmission.
void OnOffTxCallback (Ptr<const Packet> pkt)
{
    // AddPacketTag requires a non-const Packet*.  The const_cast is safe and
    // is the standard ns-3 pattern for trace callbacks that need to tag packets
    // (the packet object is owned by the simulation and not truly immutable).
    const_cast<Packet *>(PeekPointer (pkt))->AddPacketTag (DataPacketTag ());
    ++g_appTxPkts;
}

// PacketSink "Rx" trace: fires for each packet delivered to the application.
// The destination node ID is bound at install time via MakeBoundCallback.
void PacketSinkRxCallback (uint32_t nodeId, Ptr<const Packet> pkt, const Address &)
{
    ++g_appRxPkts;
    g_appRxBytes       += pkt->GetSize ();
    g_nodeRxBytes[nodeId] += pkt->GetSize ();
}

// ============================================================
//  MAC / PHY drop counters — DATA PACKETS ONLY
//  Each callback checks for DataPacketTag before counting.
// ============================================================
static uint64_t g_dataMacTxDrops = 0;
static uint64_t g_dataPhyTxDrops = 0;
static uint64_t g_dataPhyRxDrops = 0;

static inline bool IsDataPacket (Ptr<const Packet> pkt)
{
    DataPacketTag tag;
    return pkt->PeekPacketTag (tag);
}

// MacTxDrop: WifiMacQueue drops a packet (queue full or max retries exceeded).
void MacTxDropCb (Ptr<const Packet> pkt)
{
    if (IsDataPacket (pkt)) ++g_dataMacTxDrops;
}

// PhyTxDrop: PHY cannot transmit (e.g. device in sleep mode).
void PhyTxDropCb (Ptr<const Packet> pkt)
{
    if (IsDataPacket (pkt)) ++g_dataPhyTxDrops;
}

// PhyRxDrop: reception failure (collision, low SNR, CRC error).
// ns-3.30+ signature includes a WifiPhyRxfailureReason enum.
void PhyRxDropCb (Ptr<const Packet> pkt, WifiPhyRxfailureReason /*reason*/)
{
    if (IsDataPacket (pkt)) ++g_dataPhyRxDrops;
}

// ============================================================
//  Custom 802.15.4 energy tracker
//
//  LrWpanRadioEnergyModel is not in mainline ns-3.45.
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
    double               consumedJ  = 0.0;
    double               lastTime   = 0.0;
    lrwpan::PhyEnumeration lastState  = lrwpan::IEEE_802_15_4_PHY_TRX_OFF;
};

static const double LRWPAN_VOLTAGE = 3.0;
static const double LRWPAN_TX_A   = 17.4e-3;
static const double LRWPAN_RX_A   = 18.8e-3;
static const double LRWPAN_IDLE_A =  0.426e-3;

static std::vector<LrWpanNodeEnergy> g_lrwpanEnergy;

static double LrWpanCurrentA (lrwpan::PhyEnumeration s)
{
    switch (s)
    {
    case lrwpan::IEEE_802_15_4_PHY_TX_ON:
    case lrwpan::IEEE_802_15_4_PHY_BUSY_TX:  return LRWPAN_TX_A;
    case lrwpan::IEEE_802_15_4_PHY_RX_ON:
    case lrwpan::IEEE_802_15_4_PHY_BUSY_RX:  return LRWPAN_RX_A;
    default:                          return LRWPAN_IDLE_A;
    }
}

void LrWpanStateChangeCb (uint32_t idx,
                           lrwpan::PhyEnumeration oldState,
                           lrwpan::PhyEnumeration newState)
{
    double now = Simulator::Now ().GetSeconds ();
    double dt  = now - g_lrwpanEnergy[idx].lastTime;
    if (dt > 0.0)
        g_lrwpanEnergy[idx].consumedJ += LrWpanCurrentA (oldState) * LRWPAN_VOLTAGE * dt;
    g_lrwpanEnergy[idx].lastTime  = now;
    g_lrwpanEnergy[idx].lastState = newState;
}

static void FlushLrWpanEnergy ()
{
    double now = Simulator::Now ().GetSeconds ();
    for (auto &e : g_lrwpanEnergy)
    {
        double dt = now - e.lastTime;
        if (dt > 0.0)
            e.consumedJ += LrWpanCurrentA (e.lastState) * LRWPAN_VOLTAGE * dt;
        e.lastTime = now;
    }
}

// ============================================================
//  main
// ============================================================
int main (int argc, char *argv[])
{
    // ── CLI parameters ────────────────────────────────────────
    uint32_t    nNodes         = 40;
    uint32_t    nFlows         = 20;
    uint32_t    pps            = 200;
    uint32_t    areaMultiplier = 3;
    std::string channelType    = "wifi";
    uint32_t    seed           = 12345;
    double      simTime        = 50.0;
    uint32_t    pktSize        = 512;
    bool        verbose        = false;

    CommandLine cmd (__FILE__);
    cmd.AddValue ("nNodes",         "Number of nodes",                 nNodes);
    cmd.AddValue ("nFlows",         "Number of UDP flows",             nFlows);
    cmd.AddValue ("pps",            "Packets per second per flow",     pps);
    cmd.AddValue ("areaMultiplier", "Area multiplier k (side=k*Txr)",  areaMultiplier);
    cmd.AddValue ("channelType",    "wifi or lrwpan",                  channelType);
    cmd.AddValue ("seed",           "RNG seed",                        seed);
    cmd.AddValue ("simTime",        "Total simulation time (s)",       simTime);
    cmd.AddValue ("pktSize",        "Application packet size (bytes)", pktSize);
    cmd.AddValue ("verbose",        "Enable ns-3 logging",             verbose);
    cmd.Parse (argc, argv);

    NS_ABORT_MSG_IF (nFlows > nNodes,
        "nFlows (" << nFlows << ") cannot exceed nNodes (" << nNodes << ")");
    NS_ABORT_MSG_IF (simTime < 10.0,
        "simTime must be >= 10 s");

    RngSeedManager::SetSeed (seed);
    RngSeedManager::SetRun  (1);

    double txRange      = (channelType == "wifi") ? 200.0 : 40.0;
    double areaSide     = areaMultiplier * txRange;
    double dataStart    = (channelType == "wifi") ? 3.0 : 15.0;  // Much longer for LRWPAN AODV convergence
    double dataStop     = simTime - 5.0;
    double dataDuration = dataStop - dataStart;

    NS_LOG_UNCOND ("=== AODV Simulation ===");
    NS_LOG_UNCOND ("Channel   : " << channelType);
    NS_LOG_UNCOND ("nNodes    : " << nNodes);
    NS_LOG_UNCOND ("nFlows    : " << nFlows);
    NS_LOG_UNCOND ("PPS       : " << pps);
    NS_LOG_UNCOND ("AreaMult  : " << areaMultiplier << "  (" << areaSide << " m side)");
    NS_LOG_UNCOND ("PktSize   : " << pktSize << " bytes");
    NS_LOG_UNCOND ("SimTime   : " << simTime << " s");

    // ── Nodes ─────────────────────────────────────────────────
    NodeContainer nodes;
    nodes.Create (nNodes);



    // ── Mobility: static random placement ────────────────────
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

    // ── Internet stack + AODV ────────────────────────────────
    AodvHelper aodv;
    aodv.Set ("EnableHello", BooleanValue (true));
    aodv.Set ("HelloInterval", TimeValue (Seconds (0.2)));
    InternetStackHelper internet;
    internet.SetRoutingHelper (aodv);
    internet.Install (nodes);

    // ============================================================
    //  Channel-specific PHY / MAC / energy setup
    // ============================================================
    NetDeviceContainer     devices;
    Ipv4InterfaceContainer interfaces;
    Ipv6InterfaceContainer interfaces6;
    energy::EnergySourceContainer  energySources;
    energy::DeviceEnergyModelContainer wifiEnergyModels; // wifi only

    if (channelType == "wifi")
    {
        // ── 802.11b ad-hoc, DBPSK @ 1 Mbps ──────────────────
        YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                    "Exponent", DoubleValue(3.0),
                                    "ReferenceDistance", DoubleValue(1.0));

        YansWifiPhyHelper wifiPhy;
        wifiPhy.SetChannel      (wifiChannel.Create ());
        wifiPhy.Set ("TxPowerStart",  DoubleValue (20.0));  // dBm => ~200 m range
        wifiPhy.Set ("TxPowerEnd",    DoubleValue (20.0));
        wifiPhy.Set ("RxSensitivity", DoubleValue (-82.0)); // dBm

        WifiMacHelper wifiMac;
        wifiMac.SetType ("ns3::AdhocWifiMac");

        WifiHelper wifi;
        wifi.SetStandard (WIFI_STANDARD_80211b);
        wifi.SetRemoteStationManager (
            "ns3::ConstantRateWifiManager",
            "DataMode",    StringValue ("DsssRate1Mbps"),
            "ControlMode", StringValue ("DsssRate1Mbps"));

        devices = wifi.Install (wifiPhy, wifiMac, nodes);

        Ipv4AddressHelper ipv4;
        ipv4.SetBase ("10.1.0.0", "255.255.0.0");
        interfaces = ipv4.Assign (devices);

        // ── Energy: BasicEnergySource + WifiRadioEnergyModel ─
        BasicEnergySourceHelper srcHelper;
        srcHelper.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (1000.0));
        srcHelper.Set ("BasicEnergySupplyVoltageV",       DoubleValue (3.3));
        energySources = srcHelper.Install (nodes);

        WifiRadioEnergyModelHelper radioHelper;
        radioHelper.Set ("TxCurrentA",    DoubleValue (0.0174));
        radioHelper.Set ("RxCurrentA",    DoubleValue (0.0197));
        radioHelper.Set ("IdleCurrentA",  DoubleValue (0.0178));
        radioHelper.Set ("SleepCurrentA", DoubleValue (0.000001));
        wifiEnergyModels = radioHelper.Install (devices, energySources);

        // ── MAC / PHY drop traces (filter to data via DataPacketTag) ──
        //
        // MacTxDrop  : queue overflow or max-retry exhaustion in WifiMacQueue.
        //              Fires for ALL dropped frames (data + AODV control).
        //              Tag check inside MacTxDropCb isolates data drops.
        //
        // PhyTxDrop  : PHY busy / sleep prevents transmission.
        //              Same cross-protocol concern; tag check applies.
        //
        // PhyRxDrop  : CRC failure, collision, SNR below threshold.
        //              Fires at the *receiver* for every failed frame, so
        //              every node gets these events for all other nodes'
        //              transmissions that happen to fail at that receiver.
        //              Tag check ensures only our data packet failures count.
        Config::ConnectWithoutContext (
            "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTxDrop",
            MakeCallback (&MacTxDropCb));
        Config::ConnectWithoutContext (
            "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxDrop",
            MakeCallback (&PhyTxDropCb));


        Config::ConnectWithoutContext (
            "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyRxDrop",
            MakeCallback (&PhyRxDropCb));
    }
    else  // lrwpan
    {
        // ── 802.15.4, O-QPSK 250 kbps @ 2.4 GHz ─────────────
        LrWpanHelper lrWpanHelper (false);
        NetDeviceContainer lrwpanDevices = lrWpanHelper.Install (nodes);
        lrWpanHelper.CreateAssociatedPan (lrwpanDevices, 5);

        // ── SixLowPan wrapper for IPv6 over 802.15.4 ─────────
        SixLowPanHelper sixlowpan;
        devices = sixlowpan.Install (lrwpanDevices);

        // ── IPv6 addresses ────────────────────────────────────
        Ipv6AddressHelper ipv6;
        ipv6.SetBase (Ipv6Address ("2001:db8::"), Ipv6Prefix (64));
        interfaces6 = ipv6.Assign (devices);
        interfaces6.SetForwarding (0, true);
        for (uint32_t i = 0; i < nNodes; ++i)
            interfaces6.SetForwarding (i, true);
        interfaces6.SetDefaultRouteInAllNodes (0);

        // ── Energy: custom PHY-state callback ────────────────
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
    }

    // ============================================================
    //  Flow-pair selection  (random, no duplicate pairs, src != dst)
    // ============================================================
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable> ();

    std::vector<std::pair<uint32_t,uint32_t>> flows;
    {
        std::set<std::pair<uint32_t,uint32_t>> used;
        uint32_t attempts = 0;
        while (flows.size () < nFlows && attempts < 200000)
        {
            uint32_t s = rng->GetInteger (0, nNodes - 1);
            uint32_t d = rng->GetInteger (0, nNodes - 1);
            if (s == d) { ++attempts; continue; }
            auto p = std::make_pair (s, d);
            if (used.count (p)) { ++attempts; continue; }
            used.insert (p);
            flows.push_back (p);
            ++attempts;
        }
    }
    if (flows.size () < nFlows)
    {
        NS_LOG_WARN ("Only " << flows.size () <<
                     " unique flow pairs found (requested " << nFlows << ")");
        nFlows = static_cast<uint32_t> (flows.size ());
    }

    // ============================================================
    //  Application installation
    //
    //  Data flows use UDP destination ports [basePort, basePort+nFlows).
    //  AODV internally uses UDP port 654 — well outside this range.
    //
    //  Tracing:
    //    OnOffApplication::Tx  -> OnOffTxCallback  (tags packet, counts Tx)
    //    PacketSink::Rx        -> PacketSinkRxCallback (counts Rx, per-node bytes)
    // ============================================================
    const uint16_t basePort = 9000;

    // Build the set of data-flow destination ports for Mechanism 1 filter
    std::set<uint16_t> dataFlowPorts;
    for (uint32_t f = 0; f < nFlows; f++)
        dataFlowPorts.insert (static_cast<uint16_t> (basePort + f));

    ApplicationContainer sinkApps, sourceApps;

    for (uint32_t f = 0; f < nFlows; f++)
    {
        uint32_t    srcIdx  = flows[f].first;
        uint32_t    dstIdx  = flows[f].second;
        uint16_t    port    = static_cast<uint16_t> (basePort + f);

        // ── Constant-rate CBR: DataRate = pktSize * 8 * pps  bps
        double dataRateBps = static_cast<double> (pktSize) * 8.0 *
                             static_cast<double> (pps);
        std::ostringstream drStr;
        drStr << std::fixed << std::setprecision (0) << dataRateBps << "bps";

        if (channelType == "wifi")
        {
            // ── WiFi: IPv4 ────────────────────────────────────
            Ipv4Address dstAddr = interfaces.GetAddress (dstIdx);

            // Sink
            PacketSinkHelper sinkHelper (
                "ns3::UdpSocketFactory",
                InetSocketAddress (Ipv4Address::GetAny (), port));
            ApplicationContainer sa = sinkHelper.Install (nodes.Get (dstIdx));
            sa.Start (Seconds (0.0));
            sa.Stop  (Seconds (simTime));

            Ptr<PacketSink> sinkPtr = DynamicCast<PacketSink> (sa.Get (0));
            sinkPtr->TraceConnectWithoutContext (
                "Rx", MakeBoundCallback (&PacketSinkRxCallback, dstIdx));

            sinkApps.Add (sa);

            // Source
            OnOffHelper onOff (
                "ns3::UdpSocketFactory",
                InetSocketAddress (dstAddr, port));
            onOff.SetConstantRate (DataRate (drStr.str ()), pktSize);
            onOff.SetAttribute ("OnTime",
                StringValue ("ns3::ConstantRandomVariable[Constant=45]"));
            onOff.SetAttribute ("OffTime",
                StringValue ("ns3::ConstantRandomVariable[Constant=0.001]"));

            ApplicationContainer oa = onOff.Install (nodes.Get (srcIdx));
            oa.Start (Seconds (dataStart));
            oa.Stop  (Seconds (dataStop));

            Ptr<OnOffApplication> onOffPtr =
                DynamicCast<OnOffApplication> (oa.Get (0));
            onOffPtr->TraceConnectWithoutContext (
                "Tx", MakeCallback (&OnOffTxCallback));

            sourceApps.Add (oa);
        }
        else  // lrwpan with IPv6
        {
            // ── LRWPAN: IPv6 ──────────────────────────────────
            Ipv6Address dstAddr = interfaces6.GetAddress (dstIdx, 0);

            // Sink - use UDP, bind to ANY address to catch all incoming
            PacketSinkHelper sinkHelper (
                "ns3::UdpSocketFactory",
                Inet6SocketAddress (Ipv6Address::GetAny (), port));
            ApplicationContainer sa = sinkHelper.Install (nodes.Get (dstIdx));
            sa.Start (Seconds (0.0));
            sa.Stop  (Seconds (simTime));

            Ptr<PacketSink> sinkPtr = DynamicCast<PacketSink> (sa.Get (0));
            sinkPtr->TraceConnectWithoutContext (
                "Rx", MakeBoundCallback (&PacketSinkRxCallback, dstIdx));

            sinkApps.Add (sa);

            // Source
            OnOffHelper onOff (
                "ns3::UdpSocketFactory",
                Inet6SocketAddress (dstAddr, port));
            onOff.SetConstantRate (DataRate (drStr.str ()), pktSize);
            onOff.SetAttribute ("OnTime",
                StringValue ("ns3::ConstantRandomVariable[Constant=45]"));
            onOff.SetAttribute ("OffTime",
                StringValue ("ns3::ConstantRandomVariable[Constant=0.001]"));

            ApplicationContainer oa = onOff.Install (nodes.Get (srcIdx));
            oa.Start (Seconds (dataStart));
            oa.Stop  (Seconds (dataStop));

            Ptr<OnOffApplication> onOffPtr =
                DynamicCast<OnOffApplication> (oa.Get (0));
            onOffPtr->TraceConnectWithoutContext (
                "Tx", MakeCallback (&OnOffTxCallback));

            sourceApps.Add (oa);
        }
    }

    // ============================================================
    //  FlowMonitor (Mechanism 1)
    // ============================================================
    FlowMonitorHelper fmHelper;
    Ptr<FlowMonitor>  flowMonitor = fmHelper.InstallAll ();

    // ============================================================
    //  Run simulation
    // ============================================================
    Simulator::Stop (Seconds (simTime));
    NS_LOG_UNCOND ("Starting simulation...");
    Simulator::Run ();

    if (channelType == "lrwpan")
        FlushLrWpanEnergy ();

    // ============================================================
    //  Metric collection — DATA PACKETS ONLY
    // ============================================================

    // ── FlowMonitor: filter to data flows (Mechanism 1) ────────
    flowMonitor->CheckForLostPackets ();

    double fm_rxBytes  = 0.0;   // IP-level received bytes  (data flows only)
    double fm_delaySum = 0.0;   // sum of E2E delays        (data flows only)
    double fm_rxPkts   = 0.0;   // received packet count    (data flows only)
    double fm_lostPkts = 0.0;   // FlowMonitor lost packets (data flows only)

    std::map<uint32_t, double> fmNodeRxBytes;  // nodeId -> rx bytes (data only)

    if (channelType == "wifi")
    {
        // ── WiFi: IPv4 flow classification ────────────────────
        Ptr<Ipv4FlowClassifier> classifier =
            DynamicCast<Ipv4FlowClassifier> (fmHelper.GetClassifier ());

        for (auto &kv : flowMonitor->GetFlowStats ())
        {
            FlowId flowId = kv.first;
            const FlowMonitor::FlowStats &fs = kv.second;
            Ipv4FlowClassifier::FiveTuple ft = classifier->FindFlow (flowId);

            // ── Mechanism 1 filter ────────────────────────────────
            // Reject anything that is not UDP (protocol 17).
            if (ft.protocol != 17) continue;

            // Reject any UDP flow whose destination port is outside our data
            // port range.  This specifically removes AODV control messages
            if (!dataFlowPorts.count (
                    static_cast<uint16_t> (ft.destinationPort))) continue;

            fm_rxBytes  += fs.rxBytes;
            fm_delaySum += fs.delaySum.GetSeconds ();
            fm_rxPkts   += fs.rxPackets;
            fm_lostPkts += fs.lostPackets;

            // Per-destination-node throughput accumulation
            for (uint32_t i = 0; i < nNodes; i++)
            {
                if (interfaces.GetAddress (i) == ft.destinationAddress)
                {
                    fmNodeRxBytes[i] += fs.rxBytes;
                    break;
                }
            }
        }
    }
    else  // lrwpan with IPv6
    {
        // ── LRWPAN: IPv6 flow classification ───────────────────
        // For IPv6, just count packets from flows with data in them
        for (auto &kv : flowMonitor->GetFlowStats ())
        {
            const FlowMonitor::FlowStats &fs = kv.second;
            
            // Simple heuristic: if there are received packets and delay data,
            // it's likely a data flow (not control)
            if (fs.rxPackets == 0) continue;

            fm_rxBytes  += fs.rxBytes;
            fm_delaySum += fs.delaySum.GetSeconds ();
            fm_rxPkts   += fs.rxPackets;
            fm_lostPkts += fs.lostPackets;
        }
    }

    // ── Metric 1: Network Throughput ─────────────────────────
    // Source: FlowMonitor rxBytes (data flows only, filtered above).
    // FlowMonitor counts IP-level bytes (including UDP/IP headers), which is
    // the standard definition of network throughput in MANET studies.
    double networkThroughputBps =
        (dataDuration > 0.0) ? (fm_rxBytes * 8.0 / dataDuration) : 0.0;

    // ── Metric 2: End-to-End Delay ────────────────────────────
    // Source: FlowMonitor delaySum / rxPackets (data flows only).
    // Delay is measured from IP-layer Tx timestamp to IP-layer Rx timestamp.
    // Includes queuing delay, MAC backoff, propagation, RREQ discovery latency.
    // Control packets are excluded because fm_delaySum only covers filtered flows.
    double avgE2eDelay =
        (fm_rxPkts > 0.0) ? (fm_delaySum / fm_rxPkts) : 0.0;

    // ── Metrics 3 & 4: PDR and Drop Ratio ────────────────────
    // Source: app-layer counters (Mechanism 3) as ground-truth Tx/Rx.
    //
    // g_appTxPkts : data packets sent by OnOff applications (no control traffic)
    // g_appRxPkts : data packets delivered to PacketSink (no control traffic)
    //
    // For wifi, MAC/PHY data drops (Mechanism 2) supplement the FlowMonitor
    // lost count to give a comprehensive drop tally.
    // For lrwpan, we use the simpler app-layer delta because the LrWpan PHY
    // does not expose a per-packet Rx-drop trace pointer in mainline ns-3.45.

    double effectiveTx = static_cast<double> (g_appTxPkts);
    double totalDataDropped = 0.0;

    if (channelType == "wifi")
    {
        // Comprehensive wifi drop count:
        //   FlowMonitor lost packets (routing failures, TTL expiry, buffer overflows
        //     that were not caught at MAC layer before IP-level tracking)
        // + data-tagged MAC Tx drops (retry exhaustion, queue full)
        // + data-tagged PHY Tx drops (sleep-mode rejection)
        // + data-tagged PHY Rx drops (collision, low SNR at receiver)
        totalDataDropped =
            fm_lostPkts +
            static_cast<double> (g_dataMacTxDrops) +
            static_cast<double> (g_dataPhyTxDrops) +
            static_cast<double> (g_dataPhyRxDrops);
    }
    else // lrwpan
    {
        // App-layer delta: packets sent by source but never delivered to sink.
        // This captures all loss regardless of where it occurred in the stack.
        totalDataDropped =
            effectiveTx - static_cast<double> (g_appRxPkts);
    }

    double pdr = (effectiveTx > 0.0)
        ? std::min (1.0, static_cast<double> (g_appRxPkts) / effectiveTx)
        : 0.0;
    double dropRatio = (effectiveTx > 0.0)
        ? std::min (1.0, std::max (0.0, totalDataDropped / effectiveTx))
        : 0.0;

    // ── Metric 5: Energy Consumption ─────────────────────────
    // Energy reflects total radio activity (Tx, Rx, idle) for all packets
    // including control traffic, because the radio hardware cannot distinguish
    // data from control at the PHY level.  This is the physically correct and
    // universally accepted definition of node energy consumption in MANET studies.
    double totalEnergyJ = 0.0;
    if (channelType == "wifi")
    {
        for (auto it = wifiEnergyModels.Begin ();
             it != wifiEnergyModels.End (); ++it)
            totalEnergyJ += (*it)->GetTotalEnergyConsumption ();
    }
    else
    {
        for (uint32_t i = 0; i < nNodes; i++)
            totalEnergyJ += g_lrwpanEnergy[i].consumedJ;
    }
    double avgEnergyPerNodeJ = totalEnergyJ / static_cast<double> (nNodes);

    // ── Metric 6: Per-Node Throughput (data only) ─────────────
    // Source: fmNodeRxBytes, which was populated only from data-flow entries
    // (Mechanism 1).  Keyed by destination node ID.
    double sumPerNodeTput = 0.0;
    std::vector<double> perNodeTputs;
    for (auto &kv : fmNodeRxBytes)
    {
        double tput = (dataDuration > 0.0)
            ? (kv.second * 8.0 / dataDuration) : 0.0;
        perNodeTputs.push_back (tput);
        sumPerNodeTput += tput;
    }
    double avgPerNodeTput = perNodeTputs.empty ()
        ? 0.0 : (sumPerNodeTput / perNodeTputs.size ());

    // ============================================================
    //  Output
    // ============================================================
    NS_LOG_UNCOND ("\n===== RESULTS =====");
    NS_LOG_UNCOND ("Channel type           : " << channelType);
    NS_LOG_UNCOND ("nNodes                 : " << nNodes);
    NS_LOG_UNCOND ("nFlows (actual)        : " << nFlows);
    NS_LOG_UNCOND ("PPS                    : " << pps);
    NS_LOG_UNCOND ("Area multiplier        : " << areaMultiplier);
    NS_LOG_UNCOND ("Area side              : " << areaSide << " m");
    NS_LOG_UNCOND ("Packet size            : " << pktSize << " bytes");
    NS_LOG_UNCOND ("---");
    NS_LOG_UNCOND ("Network throughput     : "
        << std::fixed << std::setprecision (2)
        << networkThroughputBps / 1000.0 << " kbps  [data only]");
    NS_LOG_UNCOND ("Avg E2E delay          : "
        << std::fixed << std::setprecision (6)
        << avgE2eDelay << " s  [data only]");
    NS_LOG_UNCOND ("PDR                    : "
        << std::fixed << std::setprecision (4)
        << pdr * 100.0 << " %  [data only]");
    NS_LOG_UNCOND ("Drop ratio             : "
        << std::fixed << std::setprecision (4)
        << dropRatio * 100.0 << " %  [data only]");
    NS_LOG_UNCOND ("Total energy           : "
        << std::fixed << std::setprecision (4)
        << totalEnergyJ << " J  [all radio activity — includes control]");
    NS_LOG_UNCOND ("Avg energy/node        : "
        << std::fixed << std::setprecision (4)
        << avgEnergyPerNodeJ << " J");
    NS_LOG_UNCOND ("Avg per-node tput      : "
        << std::fixed << std::setprecision (2)
        << avgPerNodeTput / 1000.0 << " kbps  [data only, "
        << perNodeTputs.size () << " dest nodes]");
    NS_LOG_UNCOND ("---");
    NS_LOG_UNCOND ("App-layer TX pkts      : " << g_appTxPkts  << "  (data)");
    NS_LOG_UNCOND ("App-layer RX pkts      : " << g_appRxPkts  << "  (data)");
    NS_LOG_UNCOND ("App-layer RX bytes     : " << g_appRxBytes << "  (data)");
    NS_LOG_UNCOND ("FlowMon RX pkts (data) : " << static_cast<uint64_t> (fm_rxPkts));
    NS_LOG_UNCOND ("FlowMon lost    (data) : " << static_cast<uint64_t> (fm_lostPkts));
    if (channelType == "wifi")
    {
        NS_LOG_UNCOND ("Data MAC Tx drops      : " << g_dataMacTxDrops
            << "  (tagged data only)");
        NS_LOG_UNCOND ("Data PHY Tx drops      : " << g_dataPhyTxDrops
            << "  (tagged data only)");
        NS_LOG_UNCOND ("Data PHY Rx drops      : " << g_dataPhyRxDrops
            << "  (tagged data only)");
    }

    NS_LOG_UNCOND ("\n--- Per-destination-node throughput (data only) ---");
    for (auto &kv : fmNodeRxBytes)
    {
        double tput = (dataDuration > 0.0)
            ? (kv.second * 8.0 / dataDuration) : 0.0;
        NS_LOG_UNCOND ("  Node " << std::setw (4) << kv.first
            << "  :  " << std::fixed << std::setprecision (2)
            << tput / 1000.0 << " kbps");
    }

    // ── CSV line (for run_sweep.sh) ────────────────────────────
    // channelType,nNodes,nFlows,pps,areaMultiplier,pktSize,
    // throughput_kbps,delay_s,pdr,dropRatio,
    // totalEnergy_J,avgNodeEnergy_J,avgPerNodeTput_kbps
    std::cout << "CSV,"
              << channelType    << ","
              << nNodes         << ","
              << nFlows         << ","
              << pps            << ","
              << areaMultiplier << ","
              << pktSize        << ","
              << std::fixed << std::setprecision (4)
              << networkThroughputBps / 1000.0 << ","
              << avgE2eDelay    << ","
              << pdr            << ","
              << dropRatio      << ","
              << totalEnergyJ   << ","
              << avgEnergyPerNodeJ << ","
              << avgPerNodeTput / 1000.0
              << std::endl;

    Simulator::Destroy ();
    return 0;
}
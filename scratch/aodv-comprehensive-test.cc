#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/log.h"
#include <fstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include "ns3/ipv4-header.h"
#include "ns3/udp-header.h"
#include "ns3/llc-snap-header.h"
#include "ns3/wifi-mac-header.h"
#include <set>
#include <map>
#include <sstream>
#include "ns3/energy-module.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/wifi-mpdu.h"
#include <unordered_map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AodvComprehensiveTest");

// ============================================================================
// GLOBAL STATISTICS TRACKING
// ============================================================================

// Packet statistics
uint32_t g_totalPacketsSent = 0;
uint32_t g_totalPacketsReceived = 0;
uint32_t g_totalBytesSent = 0;
uint32_t g_totalBytesReceived = 0;

// Timing statistics
double g_simulationStartTime = 0.0;
double g_simulationEndTime = 0.0;
std::vector<double> g_packetDelays;           // Individual packet delays
std::map<uint32_t, Time> g_packetSentTimes;   // Track when each packet was sent

// Route discovery statistics
uint32_t g_totalRreqSent = 0;
uint32_t g_totalRrepSent = 0;
uint32_t g_totalRerrSent = 0;
std::vector<double> g_routeDiscoveryDelays;   // Time to establish each route
std::map<std::pair<Ipv4Address, Ipv4Address>, Time> g_routeRequestTimes;

// Route stability
uint32_t g_routeChanges = 0;
uint32_t g_linkBreaks = 0;

// Hop count tracking
std::vector<uint32_t> g_hopCounts;

// ============ SINR-BASED OUTAGE TRACKING ============
uint32_t g_phyRxAttempts = 0;   // Total successful preamble detections (PhyRxBegin)
uint32_t g_sinrDropCount = 0;   // Packets dropped due to SINR < threshold (PREAMBLE_DETECT_FAILURE)

// ============ SINR-BASED OUTAGE TRACKING ============
uint32_t g_intendedPhyRxBegins = 0;   // successful preamble detections for tracked packets at intended receiver
uint32_t g_intendedPhyRxDrops = 0;    // failed preamble detections for tracked packets at intended receiver

// Track the UDP destination ports used by the application flows we create.
// This lets us count only actual data packets, not AODV control or other traffic.
std::set<uint16_t> g_trackedFlowPorts;

// Cache: full PHY trace context -> MAC address of that receiving WifiNetDevice
std::map<std::string, Mac48Address> g_contextToMacCache;

// ============ IC-REP PACKET TRACKING ============
struct IcRepPacket
{
    Ipv4Address source;
    Ipv4Address destination;
    double receivedPower;
    double interference;
    Time timestamp;
};

std::vector<IcRepPacket> g_icRepPacketsSent;     // All IC-REP packets transmitted
std::vector<IcRepPacket> g_icRepPacketsRecv;     // All IC-REP packets received
std::map<Ipv4Address, uint32_t> g_icRepCountBySender;   // Count by sender
std::map<Ipv4Address, double> g_avgInterferenceByNode;  // Avg interference by node

// ============ ICP ENTRY TRACKING ============
struct IcpEntrySnapshot
{
    Ipv4Address nodeAddr;
    Ipv4Address neighborAddr;
    double createdInterference;      // I_c^j - interference node creates at neighbor
    double receivedInterference;     // I_j - total interference node receives
    double aggregateInterference;    // I_aggr
    Time timestamp;
};

std::vector<IcpEntrySnapshot> g_icpEntryUpdates;  // All ICP entry updates
// ================================================

// ============================================================================
// DROP TRACKING FOR ACTUAL APPLICATION DATA ONLY
// ============================================================================

struct DropStat
{
    uint32_t count = 0;
};

std::map<std::string, DropStat> g_dropStats;

void
IncrementDropStat(const std::string& key)
{
    g_dropStats[key].count++;
}

// Output files
std::ofstream csvFile;

// Packet sequence tracking
uint32_t g_nextPacketSeqNum = 0;

// ============================================================================
// CALLBACK FUNCTIONS
// ============================================================================

// Application-level TX callback
void TxCallback(std::string context, Ptr<const Packet> packet)
{
    g_totalPacketsSent++;
    g_totalBytesSent += packet->GetSize();
    
    // Record send time for delay calculation
    g_packetSentTimes[packet->GetUid()] = Simulator::Now();
}

// Application-level RX callback
void RxCallback(std::string context, Ptr<const Packet> packet, const Address &address)
{
    g_totalPacketsReceived++;
    g_totalBytesReceived += packet->GetSize();
    
    // Calculate delay if we have the send time
    auto it = g_packetSentTimes.find(packet->GetUid());
    if (it != g_packetSentTimes.end())
    {
        double delay = (Simulator::Now() - it->second).GetSeconds();
        g_packetDelays.push_back(delay);
        
        // Remove from map to save memory
        g_packetSentTimes.erase(it);
    }
}

// AODV control packet callbacks
void SendRreqCallback(const aodv::RreqHeader &rreqHeader, Ipv4Address destination)
{
    g_totalRreqSent++;
    
    // Track route discovery start time
    Ipv4Address origin = rreqHeader.GetOrigin();
    auto key = std::make_pair(origin, destination);
    
    if (g_routeRequestTimes.find(key) == g_routeRequestTimes.end())
    {
        g_routeRequestTimes[key] = Simulator::Now();
    }
}

void RecvRrepCallback(Ipv4Address destination)
{
    g_totalRrepSent++;
}

void SendRerrCallback(const aodv::RerrHeader &rerrHeader)
{
    g_totalRerrSent++;
    g_linkBreaks++;
}

void RouteDiscoveryTimeCallback(Time time)
{
    double discoveryDelay = time.GetSeconds();
    g_routeDiscoveryDelays.push_back(discoveryDelay);
}

// Track routing table check event
EventId g_routingCheckEventId;

// Periodic check for routing table updates
void CheckRoutingTableUpdates(NodeContainer nodes)
{
    // Routing table tracing disabled - only CSV output needed
}

// Routing table entry callback - tracks IPv4 route table updates
void RoutingTableEntryCallback(std::string context, const Ipv4RoutingTableEntry &route)
{
    // Extract node ID from context string (format: /NodeList/X/...)
    uint32_t nodeId = 0;
    size_t nodeListPos = context.find("/NodeList/");
    if (nodeListPos != std::string::npos)
    {
        size_t endPos = context.find("/", nodeListPos + 10);
        std::string nodeStr = context.substr(nodeListPos + 10, endPos - (nodeListPos + 10));
        nodeId = std::stoi(nodeStr);
    }
    
    Ipv4Address dest = route.GetDest();
    Ipv4Address gateway = route.GetGateway();
    uint32_t interface = route.GetInterface();
    
}

// IPv4 receive trace (to measure hops from TTL value at receiver)
// Hop count calculation is now done via FlowMonitor after simulation ends
// See CalculateHopCount() function below

// ============================================================================
// STATISTICS CALCULATION FUNCTIONS
// ============================================================================

double CalculateMean(const std::vector<double> &values)
{
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (double v : values) sum += v;
    return sum / values.size();
}

double CalculateStdDev(const std::vector<double> &values, double mean)
{
    if (values.size() < 2) return 0.0;
    double sumSquaredDiff = 0.0;
    for (double v : values)
    {
        double diff = v - mean;
        sumSquaredDiff += diff * diff;
    }
    return std::sqrt(sumSquaredDiff / (values.size() - 1));
}

double CalculateJitter(const std::vector<double> &delays)
{
    if (delays.size() < 2) return 0.0;
    
    std::vector<double> delayVariations;
    for (size_t i = 1; i < delays.size(); i++)
    {
        delayVariations.push_back(std::abs(delays[i] - delays[i-1]));
    }
    
    return CalculateMean(delayVariations);
}

double CalculatePercentile(std::vector<double> values, double percentile)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(percentile * values.size());
    if (index >= values.size()) index = values.size() - 1;
    return values[index];
}

// IC-REP Sent callback
void IcRepSentCallback(Ipv4Address source, Ipv4Address destination, double receivedPower, double interference)
{
    IcRepPacket pkt;
    pkt.source = source;
    pkt.destination = destination;
    pkt.receivedPower = receivedPower;
    pkt.interference = interference;
    pkt.timestamp = Simulator::Now();
    
    g_icRepPacketsSent.push_back(pkt);
    g_icRepCountBySender[source]++;
}

// IC-REP Received callback
void IcRepRecvCallback(Ipv4Address source, Ipv4Address destination, double receivedPower, double interference)
{
    IcRepPacket pkt;
    pkt.source = source;
    pkt.destination = destination;
    pkt.receivedPower = receivedPower;
    pkt.interference = interference;
    pkt.timestamp = Simulator::Now();
    
    g_icRepPacketsRecv.push_back(pkt);
}

// ICP Entry Update callback
void IcpEntryUpdateCallback(Ipv4Address nodeAddr, Ipv4Address neighborAddr, 
                            double createdInterference, double receivedInterference, 
                            double aggregateInterference)
{
    IcpEntrySnapshot snapshot;
    snapshot.nodeAddr = nodeAddr;
    snapshot.neighborAddr = neighborAddr;
    snapshot.createdInterference = createdInterference;
    snapshot.receivedInterference = receivedInterference;
    snapshot.aggregateInterference = aggregateInterference;
    snapshot.timestamp = Simulator::Now();
    
    g_icpEntryUpdates.push_back(snapshot);
}

///////OutageProbabilityCalculationHelper functions to determine if a PHY RX event corresponds to a tracked application data packet at the intended receiver
bool
ExtractNodeAndDeviceFromContext(const std::string& context, uint32_t& nodeId, uint32_t& deviceId)
{
    std::size_t nodePos = context.find("/NodeList/");
    if (nodePos == std::string::npos)
    {
        return false;
    }

    std::size_t nodeStart = nodePos + std::string("/NodeList/").size();
    std::size_t nodeEnd = context.find("/", nodeStart);
    if (nodeEnd == std::string::npos)
    {
        return false;
    }

    std::size_t devPos = context.find("/DeviceList/");
    if (devPos == std::string::npos)
    {
        return false;
    }

    std::size_t devStart = devPos + std::string("/DeviceList/").size();
    std::size_t devEnd = context.find("/", devStart);
    if (devEnd == std::string::npos)
    {
        return false;
    }

    try
    {
        nodeId = static_cast<uint32_t>(std::stoul(context.substr(nodeStart, nodeEnd - nodeStart)));
        deviceId = static_cast<uint32_t>(std::stoul(context.substr(devStart, devEnd - devStart)));
    }
    catch (...)
    {
        return false;
    }

    return true;
}

bool
GetReceiverMacFromContext(const std::string& context, Mac48Address& rxMac)
{
    auto cached = g_contextToMacCache.find(context);
    if (cached != g_contextToMacCache.end())
    {
        rxMac = cached->second;
        return true;
    }

    uint32_t nodeId = 0;
    uint32_t deviceId = 0;
    if (!ExtractNodeAndDeviceFromContext(context, nodeId, deviceId))
    {
        return false;
    }

    Ptr<Node> node = NodeList::GetNode(nodeId);
    if (!node || deviceId >= node->GetNDevices())
    {
        return false;
    }

    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(deviceId));
    if (!wifiDev || !wifiDev->GetMac())
    {
        return false;
    }

    rxMac = Mac48Address::ConvertFrom(wifiDev->GetMac()->GetAddress());
    g_contextToMacCache[context] = rxMac;
    return true;
}

bool
IsTrackedApplicationDataForThisReceiver(const std::string& context, Ptr<const Packet> packet)
{
    Ptr<Packet> p = packet->Copy();

    WifiMacHeader wifiHdr;
    if (!p->PeekHeader(wifiHdr))
    {
        return false;
    }

    // Count only unicast data frames
    if (!wifiHdr.IsData() || wifiHdr.GetAddr1().IsGroup())
    {
        return false;
    }

    // Verify that THIS receiver's PHY is the actual intended receiver
    Mac48Address rxMac;
    if (!GetReceiverMacFromContext(context, rxMac))
    {
        return false;
    }

    Mac48Address intendedRx = Mac48Address::ConvertFrom(wifiHdr.GetAddr1());
    if (intendedRx != rxMac)
    {
        return false;
    }

    // Strip 802.11 MAC header
    p->RemoveHeader(wifiHdr);

    // Strip LLC/SNAP
    LlcSnapHeader llc;
    if (!p->RemoveHeader(llc))
    {
        return false;
    }

    // Parse IPv4
    Ipv4Header ipHdr;
    if (!p->RemoveHeader(ipHdr))
    {
        return false;
    }

    // Count only UDP application packets
    if (ipHdr.GetProtocol() != 17)
    {
        return false;
    }

    UdpHeader udpHdr;
    if (!p->PeekHeader(udpHdr))
    {
        return false;
    }

    // Only count our flow destination ports
    if (g_trackedFlowPorts.find(udpHdr.GetDestinationPort()) == g_trackedFlowPorts.end())
    {
        return false;
    }

    return true;
}

bool
ExtractTrackedUdpInfoFromIpv4Packet(Ptr<const Packet> packet,
                                    Ipv4Address& src,
                                    Ipv4Address& dst,
                                    uint16_t& dport)
{
    Ptr<Packet> p = packet->Copy();

    Ipv4Header ipHdr;
    if (!p->RemoveHeader(ipHdr))
    {
        return false;
    }

    if (ipHdr.GetProtocol() != 17) // UDP only
    {
        return false;
    }

    UdpHeader udpHdr;
    if (!p->PeekHeader(udpHdr))
    {
        return false;
    }

    dport = udpHdr.GetDestinationPort();
    if (g_trackedFlowPorts.find(dport) == g_trackedFlowPorts.end())
    {
        return false;
    }

    src = ipHdr.GetSource();
    dst = ipHdr.GetDestination();
    return true;
}

bool
ExtractTrackedUdpInfoFromMpdu(Ptr<const WifiMpdu> mpdu,
                              Ipv4Address& src,
                              Ipv4Address& dst,
                              uint16_t& dport)
{
    if (!mpdu)
    {
        return false;
    }

    const WifiMacHeader& wifiHdr = mpdu->GetHeader();

    if (!wifiHdr.IsData() || wifiHdr.GetAddr1().IsGroup())
    {
        return false;
    }

    Ptr<Packet> p = mpdu->GetPacket()->Copy();

    LlcSnapHeader llc;
    if (!p->RemoveHeader(llc))
    {
        return false;
    }

    Ipv4Header ipHdr;
    if (!p->RemoveHeader(ipHdr))
    {
        return false;
    }

    if (ipHdr.GetProtocol() != 17)
    {
        return false;
    }

    UdpHeader udpHdr;
    if (!p->PeekHeader(udpHdr))
    {
        return false;
    }

    dport = udpHdr.GetDestinationPort();
    if (g_trackedFlowPorts.find(dport) == g_trackedFlowPorts.end())
    {
        return false;
    }

    src = ipHdr.GetSource();
    dst = ipHdr.GetDestination();
    return true;
}

// Physical layer RX begin callback - counts successful preamble detections
// only for tracked application data at the actual intended receiver
void
PhyRxBeginCallback(std::string context, Ptr<const Packet> packet, RxPowerWattPerChannelBand rxPowersW)
{
    if (IsTrackedApplicationDataForThisReceiver(context, packet))
    {
        g_intendedPhyRxBegins++;
    }
}

// Physical layer RX drop callback - counts failed preamble detections
// only for tracked application data at the actual intended receiver
void
PhyRxDropCallback(std::string context, Ptr<const Packet> packet, WifiPhyRxfailureReason reason)
{
    if (!IsTrackedApplicationDataForThisReceiver(context, packet))
    {
        return;
    }

    g_intendedPhyRxDrops++;

    Ptr<Packet> p = packet->Copy();

    WifiMacHeader wifiHdr;
    if (!p->RemoveHeader(wifiHdr))
    {
        return;
    }

    LlcSnapHeader llc;
    if (!p->RemoveHeader(llc))
    {
        return;
    }

    Ipv4Header ipHdr;
    if (!p->RemoveHeader(ipHdr))
    {
        return;
    }

    UdpHeader udpHdr;
    if (!p->PeekHeader(udpHdr))
    {
        return;
    }

    uint16_t dport = udpHdr.GetDestinationPort();
    Ipv4Address src = ipHdr.GetSource();
    Ipv4Address dst = ipHdr.GetDestination();

    // std::string reasonStr = PhyDropReasonToString(reason);
    // IncrementDropStat("PHY:" + reasonStr);
    // PrintTrackedDropToConsole("PHY", reasonStr, packet, src, dst, dport);
}

void
MacDroppedMpduCallback(WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu)
{
    Ipv4Address src, dst;
    uint16_t dport = 0;

    if (!ExtractTrackedUdpInfoFromMpdu(mpdu, src, dst, dport))
    {
        return;
    }

    Ptr<const Packet> packet = mpdu->GetPacket();
}

void
Ipv4DropCallback(std::string context,
                 const Ipv4Header& header,
                 Ptr<const Packet> packet,
                 Ipv4L3Protocol::DropReason reason,
                 Ptr<Ipv4> ipv4,
                 uint32_t interface)
{
    Ipv4Address src = header.GetSource();
    Ipv4Address dst = header.GetDestination();
    uint16_t dport = 0;
    
    // Try to extract UDP destination port from the packet
    Ptr<Packet> p = packet->Copy();
    UdpHeader udpHdr;
    bool hasUdp = p->PeekHeader(udpHdr);
    
    if (hasUdp)
    {
        dport = udpHdr.GetDestinationPort();
        // Only track if it's in our flow port range
        if (g_trackedFlowPorts.find(dport) == g_trackedFlowPorts.end())
        {
            return;
        }
    }
    else
    {
        // If we can't extract UDP header, skip (not UDP traffic)
        return;
    }

    // std::string reasonStr = Ipv4DropReasonToString(reason);
    // IncrementDropStat("IP:" + reasonStr);
    // PrintTrackedDropToConsole("IP", reasonStr, packet, src, dst, dport);
}

void
AodvAppDataDropCallback(Ptr<const Packet> packet,
                        const Ipv4Header& header,
                        std::string stage,
                        std::string reason)
{
    Ipv4Address src = header.GetSource();
    Ipv4Address dst = header.GetDestination();
    uint16_t dport = 0;

    // Try to extract UDP destination port
    Ptr<Packet> p = packet->Copy();
    UdpHeader udpHdr;
    if (!p->RemoveHeader(udpHdr))
    {
        return;
    }
    
    dport = udpHdr.GetDestinationPort();
    
    // Only track if it's in our flow port range
    if (g_trackedFlowPorts.find(dport) == g_trackedFlowPorts.end())
    {
        return;
    }

    IncrementDropStat(stage + ":" + reason);
    // PrintTrackedDropToConsole(stage, reason, packet, src, dst, dport);
}

// ============================================================================
// MAIN SIMULATION
// ============================================================================

int main(int argc, char *argv[])
{
    // Simulation parameters
    uint32_t nNodes = 30;
    double areaSize = 300.0;
    double simTime = 13.0;
    double dataStart = 3.0;
    uint32_t packetSize = 512;
    double packetInterval = 0.1;
    uint32_t nFlows = 5;
    std::string phyMode = "DsssRate1Mbps";
    double txPower = 20.0;  // dBm
    uint32_t rngRun = 1;    // Random seed run number

    double initialEnergyJ = 100.0;     // initial battery energy per node
    double txCurrentA = 0.38;          // radio TX current
    double rxCurrentA = 0.313;         // radio RX current
    double idleCurrentA = 0.273;       // radio idle current
    double sleepCurrentA = 0.033;      // radio sleep current
    double ccaBusyCurrentA = 0.273;    // radio CCA busy current
    double switchingCurrentA = 0.273;  // radio switching current
        
    // Command line
    CommandLine cmd;
    cmd.AddValue("nNodes", "Number of nodes", nNodes);
    cmd.AddValue("areaSize", "Size of square area (meters)", areaSize);
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("nFlows", "Number of concurrent flows", nFlows);
    cmd.AddValue("txPower", "Transmission power (dBm)", txPower);
    cmd.AddValue("RngRun", "Random run number for seed", rngRun);
    cmd.Parse(argc, argv);
    
    // Set the random seed immediately after parsing
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(rngRun);
    

    // Open output files
    csvFile.open("aodv-comprehensive-results.csv", std::ios::app);
    
    // Create nodes
    NodeContainer nodes;
    nodes.Create(nNodes);
    
    // WiFi setup
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    
    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                    "Exponent", DoubleValue(3.0),
                                    "ReferenceDistance", DoubleValue(1.0));
    wifiPhy.SetChannel(wifiChannel.Create());
    wifiPhy.Set("TxPowerStart", DoubleValue(txPower));
    wifiPhy.Set("TxPowerEnd", DoubleValue(txPower));
    
    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                   "DataMode", StringValue(phyMode),
                                   "ControlMode", StringValue(phyMode));
    
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

      // ====================================================================
    // ENERGY FRAMEWORK INSTALLATION
    // ====================================================================
    BasicEnergySourceHelper basicSourceHelper;
    basicSourceHelper.Set("BasicEnergySourceInitialEnergyJ",
                          DoubleValue(initialEnergyJ));

    ns3::energy::EnergySourceContainer energySources = basicSourceHelper.Install(nodes);

    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(txCurrentA));
    radioEnergyHelper.Set("RxCurrentA", DoubleValue(rxCurrentA));

    ns3::energy::DeviceEnergyModelContainer deviceEnergyModels =
        radioEnergyHelper.Install(devices, energySources);
    
    // Mobility
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                   "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaSize) + "]"),
                                   "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaSize) + "]"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    
    // Internet stack with AODV
    AodvHelper aodv;
    aodv.Set("HelloInterval", TimeValue(Seconds(0.2)));
    
    InternetStackHelper internet;
    internet.SetRoutingHelper(aodv);
    internet.Install(nodes);
    
    // IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);
    
    // Create flows
    ApplicationContainer serverApps;
    ApplicationContainer clientApps;
    
    for (uint32_t flow = 0; flow < nFlows; ++flow)
    {
        uint32_t srcIdx = flow % nNodes;
        uint32_t dstIdx = (flow + nNodes/2) % nNodes;
        if (srcIdx == dstIdx) dstIdx = (dstIdx + 1) % nNodes;
        
        uint16_t port = 9000 + flow;
        g_trackedFlowPorts.insert(port);
        
        // Server
        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                     InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(dstIdx));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simTime));
        serverApps.Add(sinkApp);
        
        // Client
        OnOffHelper onoff("ns3::UdpSocketFactory",
                          InetSocketAddress(interfaces.GetAddress(dstIdx), port));
        onoff.SetAttribute("PacketSize", UintegerValue(packetSize));
        onoff.SetAttribute("DataRate", StringValue(std::to_string(packetSize * 8 / packetInterval) + "bps"));
        onoff.SetConstantRate(DataRate(packetSize * 8 / packetInterval));
        
        ApplicationContainer clientApp = onoff.Install(nodes.Get(srcIdx));
        clientApp.Start(Seconds(dataStart));
        clientApp.Stop(Seconds(simTime - 0.5));
        clientApps.Add(clientApp);
    }
    
    // Connect callbacks
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::PacketSink/Rx",
                    MakeCallback(&RxCallback));
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::OnOffApplication/Tx",
                    MakeCallback(&TxCallback));
    
    // Connect AODV-specific traces (SendRreq trace source not available in standard AODV)
    // Config::Connect("/NodeList/*/$ns3::aodv::RoutingProtocol/SendRreq",
    //                 MakeCallback(&SendRreqCallback));
    
    // Hop count and energy will be calculated from FlowMonitor after simulation
    
    // Connect physical layer traces for SINR-based outage probability
    // PhyRxBegin: counts successful preamble detections (RX attempts that succeeded preamble stage)
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxBegin",
                    MakeCallback(&PhyRxBeginCallback));
    
    // PhyRxDrop: counts failed preamble detections (RX attempts that failed SINR check)
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxDrop",
                    MakeCallback(&PhyRxDropCallback));
    
    // IPv4 drop trace
    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Drop",
                    MakeCallback(&Ipv4DropCallback));
    
    // Note: AODV routing table trace will be dumped at end of simulation
    // See DumpAodvRoutingTables() function call after simulation.Run()
    
    // FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    
    // Connect trace sources for each node
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Node> node = nodes.Get(i);

        // Connect MAC dropped-MPDU trace
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(0));
        if (wifiDev && wifiDev->GetMac())
        {
            wifiDev->GetMac()->TraceConnectWithoutContext("DroppedMpdu",
                MakeCallback(&MacDroppedMpduCallback));
        }

        Ptr<Ipv4RoutingProtocol> routing = node->GetObject<Ipv4>()->GetRoutingProtocol();
        Ptr<aodv::RoutingProtocol> aodvRouting = DynamicCast<aodv::RoutingProtocol>(routing);

        if (aodvRouting)
        {
            // Connect IC-REP trace sources
            aodvRouting->TraceConnectWithoutContext("IcRepSent",
                MakeCallback(&IcRepSentCallback));
            aodvRouting->TraceConnectWithoutContext("IcRepRecv",
                MakeCallback(&IcRepRecvCallback));

            // Connect ICP entry update trace source
            aodvRouting->TraceConnectWithoutContext("IcpEntryUpdate",
                MakeCallback(&IcpEntryUpdateCallback));

            // NEW: application-data-only AODV routing / queue drop trace
            aodvRouting->TraceConnectWithoutContext("AppDataDrop",
                MakeCallback(&AodvAppDataDropCallback));
        }
    }
    
    g_simulationStartTime = dataStart;
    g_simulationEndTime = simTime;
    
    // Run
    Simulator::Stop(Seconds(simTime));
    
    // Schedule periodic routing table checks to capture updates
    g_routingCheckEventId = Simulator::Schedule(Seconds(0.1), &CheckRoutingTableUpdates, nodes);
    
    Simulator::Run();

    
    // ========================================================================
    // CALCULATE COMPREHENSIVE STATISTICS
    // ========================================================================
    
    // Basic packet statistics
    double pdr = (g_totalPacketsSent > 0) ? 
        (double)g_totalPacketsReceived / g_totalPacketsSent : 0.0;
    double normalizedThroughput = pdr;
    
    uint32_t g_totalTrackedRxAttempts = g_intendedPhyRxBegins + g_intendedPhyRxDrops;
    double outageProbability = (g_totalTrackedRxAttempts > 0) ?
    static_cast<double>(g_intendedPhyRxDrops) / g_totalTrackedRxAttempts : 0.0;
    
    // Throughput
    double dataTime = simTime - dataStart;
    double throughputKbps = (g_totalBytesReceived * 8.0) / (dataTime * 1000.0);
    double goodputKbps = throughputKbps * pdr;
    
    // Latency statistics (COMPREHENSIVE)
    double avgDelay = CalculateMean(g_packetDelays);
    double stdDevDelay = CalculateStdDev(g_packetDelays, avgDelay);
    double minDelay = g_packetDelays.empty() ? 0.0 : 
        *std::min_element(g_packetDelays.begin(), g_packetDelays.end());
    double maxDelay = g_packetDelays.empty() ? 0.0 : 
        *std::max_element(g_packetDelays.begin(), g_packetDelays.end());
    double medianDelay = CalculatePercentile(g_packetDelays, 0.5);
    double p95Delay = CalculatePercentile(g_packetDelays, 0.95);
    double p99Delay = CalculatePercentile(g_packetDelays, 0.99);
    double jitter = CalculateJitter(g_packetDelays);
    
    // Route discovery latency
    double avgRouteDiscovery = CalculateMean(g_routeDiscoveryDelays);
    double stdDevRouteDiscovery = CalculateStdDev(g_routeDiscoveryDelays, avgRouteDiscovery);
    
    // Control overhead
    uint32_t totalControlPackets = g_totalRreqSent + g_totalRrepSent + g_totalRerrSent;
    double controlOverheadRatio = (g_totalPacketsSent > 0) ?
        (double)totalControlPackets / g_totalPacketsSent : 0.0;
    
    // FlowMonitor detailed statistics
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
    // Hop count statistics - calculated from FlowMonitor timesForwarded
    // timesForwarded counts intermediate forwarding events only
    // Add 1 per received packet to account for the final delivery hop
    double avgHopCount = 0.0;
    double totalHops = 0.0;
    uint32_t hopSamples = 0;
    
    for (auto& i : stats)
    {
        if (i.second.rxPackets > 0)
        {
            // Each RX packet represents 1 final hop + timesForwarded intermediate hops
            totalHops += i.second.timesForwarded + i.second.rxPackets;
            hopSamples += i.second.rxPackets;
        }
    }
    
    avgHopCount = (hopSamples > 0) ? totalHops / hopSamples : 0.0;
    
    // Energy estimation from FlowMonitor per-flow data
    // E = P_t * t for TX and P_r * t for RX
    double txPowerWatts = std::pow(10.0, txPower / 10.0) / 1000.0;
    double rxPowerWatts = txPowerWatts * 0.5;  // RX power typically ~50% of TX power
    double totalEnergyMillijoules = 0.0;

    // Energy for transmitted and received packets
    for (auto& i : stats)
    {
        if (i.second.txPackets > 0)
        {
            double flowTxTime = i.second.txPackets * packetSize * 8.0 / 1000000.0;
            double flowTxEnergy = flowTxTime * txPowerWatts * 1000.0;
            totalEnergyMillijoules += flowTxEnergy;
        }
        
        if (i.second.rxPackets > 0)
        {
            double flowRxTime = i.second.rxPackets * packetSize * 8.0 / 1000000.0;
            double flowRxEnergy = flowRxTime * rxPowerWatts * 1000.0;
            totalEnergyMillijoules += flowRxEnergy;
        }
    }
    
    double energyMillijoules = totalEnergyMillijoules;
    double energyPerNode = energyMillijoules / nNodes;
    
    // Energy consumption from ns-3 Energy Framework
    double totalInitialEnergyJ = nNodes * initialEnergyJ;
    double totalRemainingEnergyJ = 0.0;
    double totalConsumedEnergyJ = 0.0;
    double totalConsumedEnergyMillijoules = 0.0;
    double avgConsumedEnergyPerNodeMillijoules = 0.0;

    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<ns3::energy::BasicEnergySource> source = DynamicCast<ns3::energy::BasicEnergySource>(energySources.Get(i));
        if (!source)
        {
            continue;
        }

        double remainingJ = source->GetRemainingEnergy();
        double consumedJ = initialEnergyJ - remainingJ;
        if (consumedJ < 0.0)
        {
            consumedJ = 0.0;
        }

        totalRemainingEnergyJ += remainingJ;
        totalConsumedEnergyJ += consumedJ;
    }

    outageProbability = 1-normalizedThroughput;

    totalConsumedEnergyMillijoules = totalConsumedEnergyJ * 1000.0;
    avgConsumedEnergyPerNodeMillijoules =
        (nNodes > 0) ? totalConsumedEnergyMillijoules / nNodes : 0.0;

    energyMillijoules = totalConsumedEnergyMillijoules;
    energyPerNode = avgConsumedEnergyPerNodeMillijoules;
    
    // ========================================================================
    // WRITE CSV RESULTS
    // ========================================================================
    
    // Format: nNodes,nFlows,Seed,NormalizedThroughput,OutageProbability,
    //         AvgDelayMs,AvgEnergyMJ,TotalEnergyMJ
    
    csvFile << nNodes << ","
            << nFlows << ","
            << rngRun << "," 
            << normalizedThroughput << "," 
            << outageProbability << ","
            << (avgDelay * 1000.0) << ","
            << (energyMillijoules / nFlows) << ","
            << energyMillijoules
            << std::endl;
    
    csvFile.close();
    
    Simulator::Destroy();
    return 0;
}
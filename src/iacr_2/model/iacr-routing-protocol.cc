/*
 * IACR Routing Protocol Implementation
 * Based on arXiv:2201.01520v1
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "iacr-routing-protocol.h"

#include "ns3/double.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/mobility-model.h"
#include "ns3/node-list.h"
#include "ns3/node.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <cmath>
#include <limits>

namespace ns3
{
namespace iacr
{

NS_LOG_COMPONENT_DEFINE("IacrRoutingProtocol2");
NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

TypeId
RoutingProtocol::GetTypeId()
{
    static TypeId tid = TypeId("ns3::iacr::RoutingProtocol2")
                            .SetParent<Ipv4RoutingProtocol>()
                            .SetGroupName("Iacr")
                            .AddConstructor<RoutingProtocol>();
    return tid;
}

RoutingProtocol::RoutingProtocol()
    : m_delta(0.1),
      m_pathLossExponent(2.0),
      m_transmitPower(20.0),
      m_referenceDistance(1.0),
      m_infoCollectionTimeout(MilliSeconds(100)),
      m_helloInterval(MilliSeconds(1000)),
      m_helloCounter(0),
      m_icreqSeqNumber(0)
{
    NS_LOG_FUNCTION(this);
}

RoutingProtocol::~RoutingProtocol()
{
    NS_LOG_FUNCTION(this);
}

double
RoutingProtocol::GetDistanceToNode(uint32_t nodeId)
{
    // Get distance to another node using mobility models
    Ptr<Node> thisNode = GetObject<Node>();
    Ptr<Node> otherNode = NodeList::GetNode(nodeId);

    if (!thisNode || !otherNode)
    {
        return 0.0;
    }

    Ptr<MobilityModel> thisMobility = thisNode->GetObject<MobilityModel>();
    Ptr<MobilityModel> otherMobility = otherNode->GetObject<MobilityModel>();

    if (!thisMobility || !otherMobility)
    {
        return 0.0;
    }

    return thisMobility->GetDistanceFrom(otherMobility);
}

double
RoutingProtocol::CalculatePathLoss(double distance)
{
    // Equation 1: L = 20*log10(f) + 20*log10(d/c) + 20*log10(4π/λ)
    // Simplified as: L = Pt - Pr = 20*log10(d) + constant
    NS_LOG_FUNCTION(this << distance);

    if (distance <= 0.0)
    {
        distance = m_referenceDistance;
    }

    // Path loss in dB = 20 * log10(distance) + 20*log10(f) - 20*log10(c) + ...
    // For simplicity, use free space path loss model
    double pathLoss =
        m_transmitPower - 20 * std::log10(distance / m_referenceDistance) -
        20 * m_pathLossExponent * std::log10(1.0);  // Simplified, normalized

    return pathLoss;
}

double
RoutingProtocol::CalculateReceivedInterference(Ipv4Address receiver)
{
    // Equation 2: I_j = sum of P_i * L_{ij} for all transmitting nodes i
    NS_LOG_FUNCTION(this << receiver);

    double totalInterference = 0.0;

    // Iterate over all neighbors and sum their interference contributions
    for (auto& entry : m_neighborTable)
    {
        totalInterference += entry.second.createdInterference;
        NS_LOG_DEBUG("Neighbor " << entry.first << " contributes " 
                                  << entry.second.createdInterference);
    }

    NS_LOG_DEBUG("Total received interference at " << receiver << ": " << totalInterference);
    return totalInterference;
}

double
RoutingProtocol::CalculatePathMetric(double aggregateInterference, double pathDelay)
{
    // Equation 6: M(p) = I_aggr^p / D_p
    // M(p) is the path metric, inversely proportional to delay
    NS_LOG_FUNCTION(this << aggregateInterference << pathDelay);

    if (pathDelay <= 0.0)
    {
        pathDelay = 1.0;
    }

    // Normalize interference and delay for metric computation
    double metric = (aggregateInterference + m_delta) / pathDelay;

    return metric;
}

double
RoutingProtocol::CalculateCreatedInterference(double metric, double neighborDistance)
{
    // Equation 7: I_c^j = M(p) * d(v,j)
    // Created interference at neighbor j due to this node's transmission
    NS_LOG_FUNCTION(this << metric << neighborDistance);

    double createdInterference = metric * neighborDistance;

    return createdInterference;
}

void
RoutingProtocol::UpdateNeighborInterference(Ipv4Address neighbor,
                                             double createdInterference,
                                             double receivedInterference)
{
    NS_LOG_FUNCTION(this << neighbor << createdInterference << receivedInterference);

    auto it = m_neighborTable.find(neighbor);
    if (it == m_neighborTable.end())
    {
        NeighborInfo info;
        info.address = neighbor;
        info.createdInterference = createdInterference;
        info.receivedInterference = receivedInterference;
        info.aggregateInterference = createdInterference + receivedInterference;
        info.lastUpdate = Simulator::Now();

        m_neighborTable[neighbor] = info;
        NS_LOG_INFO("Added neighbor " << neighbor << " created_interf=" << createdInterference
                                      << " received_interf=" << receivedInterference);
    }
    else
    {
        // Update interference values with exponential moving average
        const double alpha = 0.5;  // Smoothing factor
        double oldCreated = it->second.createdInterference;
        double oldReceived = it->second.receivedInterference;

        it->second.createdInterference =
            alpha * createdInterference + (1.0 - alpha) * oldCreated;
        it->second.receivedInterference =
            alpha * receivedInterference + (1.0 - alpha) * oldReceived;
        it->second.aggregateInterference =
            it->second.createdInterference + it->second.receivedInterference;
        it->second.lastUpdate = Simulator::Now();

        NS_LOG_DEBUG("Updated neighbor " << neighbor 
                                        << " created: " << oldCreated << " -> " 
                                        << it->second.createdInterference
                                        << " received: " << oldReceived << " -> "
                                        << it->second.receivedInterference);
    }
}

double
RoutingProtocol::GetAggregateInterference(Ipv4Address sender)
{
    NS_LOG_FUNCTION(this << sender);

    auto it = m_neighborTable.find(sender);
    if (it != m_neighborTable.end())
    {
        return it->second.aggregateInterference;
    }

    return 0.0;
}

void
RoutingProtocol::InformationCollectionPhase(Ipv4Address dest)
{
    // Algorithm 1: Information Collection Phase
    // Send ICREQ to gather interference information from neighbors
    NS_LOG_FUNCTION(this << dest);
    NS_LOG_INFO("Starting information collection phase for destination " << dest);

    m_icreqSeqNumber++;

    IcreqEntry entry;
    entry.requestId = m_icreqSeqNumber;
    entry.destination = dest;
    entry.timestamp = Simulator::Now();

    m_pendingIcreq[m_icreqSeqNumber] = entry;

    NS_LOG_DEBUG("Initiated ICREQ " << m_icreqSeqNumber << " for destination " << dest);
}

void
RoutingProtocol::RecvIcreq(Ptr<Packet> p, Ipv4Address sender)
{
    NS_LOG_FUNCTION(this << p << sender);

    Ptr<Packet> packet = p->Copy();
    IcreqHeader icreq;
    packet->RemoveHeader(icreq);

    NS_LOG_DEBUG("Received ICREQ from " << sender << " with metric " << icreq.GetPathMetric());

    // Update neighbor information based on ICREQ
    double receivedMetric = icreq.GetPathMetric();
    UpdateNeighborInterference(sender, receivedMetric, 0.0);
}

void
RoutingProtocol::RecvIcrep(Ptr<Packet> p, Ipv4Address sender)
{
    NS_LOG_FUNCTION(this << p << sender);

    Ptr<Packet> packet = p->Copy();
    IcrepHeader icrep;
    packet->RemoveHeader(icrep);

    NS_LOG_DEBUG("Received ICREP from " << sender << " with metric " << icrep.GetPathMetric());

    uint32_t reqId = icrep.GetRequestId();
    auto it = m_pendingIcreq.find(reqId);
    if (it != m_pendingIcreq.end())
    {
        it->second.receivedReplies.push_back(sender);
        NS_LOG_DEBUG("Recorded reply from " << sender << " for request " << reqId);
    }
}

void
RoutingProtocol::RecvHello(Ptr<Packet> p, Ipv4Address sender)
{
    NS_LOG_FUNCTION(this << p << sender);

    Ptr<Packet> packet = p->Copy();
    HelloHeader hello;
    packet->RemoveHeader(hello);

    NS_LOG_DEBUG("Received HELLO from " << sender
                                       << " created=" << hello.GetCreatedInterference()
                                       << " received=" << hello.GetReceivedInterference());

    UpdateNeighborInterference(sender,
                               hello.GetCreatedInterference(),
                               hello.GetReceivedInterference());
}

void
RoutingProtocol::SendHello()
{
    NS_LOG_FUNCTION(this);

    // Send periodic Hello message with interference information
    m_helloCounter++;

    double totalReceivedInterference = CalculateReceivedInterference(Ipv4Address("0.0.0.0"));
    double metric = CalculatePathMetric(totalReceivedInterference, 1.0);
    double createdInterference =
        CalculateCreatedInterference(metric, m_referenceDistance);

    NS_LOG_INFO("Sending HELLO beacon #" << m_helloCounter 
                                         << " metric=" << metric
                                         << " created_interf=" << createdInterference);

    // Create and send Hello packet (would go to neighbors)
    // This is a simplified version - full implementation would broadcast via UDP
}

bool
RoutingProtocol::RecvPacket(Ptr<Packet> p, const Ipv4Header& header)
{
    NS_LOG_FUNCTION(this << p << header);

    // Check for IACR-specific packet types
    Ptr<Packet> packet = p->Copy();

    uint8_t typeField = 0;
    packet->CopyData(&typeField, 1);

    // Handle based on packet type
    switch (typeField)
    {
    case IACRTYPE_ICREQ:
        NS_LOG_DEBUG("Received ICREQ packet");
        break;
    case IACRTYPE_ICREP:
        NS_LOG_DEBUG("Received ICREP packet");
        break;
    case IACRTYPE_HELLO:
        NS_LOG_DEBUG("Received HELLO packet");
        break;
    default:
        // Not an IACR packet, let parent routing protocol handle it
        return false;
    }

    return true;
}

Ipv4Address
RoutingProtocol::SelectBestNextHop(const std::vector<Ipv4Address>& candidates)
{
    NS_LOG_FUNCTION(this);

    if (candidates.empty())
    {
        return Ipv4Address();
    }

    if (candidates.size() == 1)
    {
        return candidates[0];
    }

    // Select neighbor with minimum aggregate interference
    Ipv4Address bestNeighbor = candidates[0];
    double bestMetric = std::numeric_limits<double>::max();

    for (const auto& candidate : candidates)
    {
        auto it = m_neighborTable.find(candidate);
        if (it != m_neighborTable.end())
        {
            double metric = it->second.aggregateInterference;
            NS_LOG_DEBUG("Candidate " << candidate << " has metric " << metric);

            if (metric < bestMetric)
            {
                bestMetric = metric;
                bestNeighbor = candidate;
            }
        }
        else
        {
            // Unknown neighbor, use it as fallback
            NS_LOG_DEBUG("Candidate " << candidate << " not in neighbor table");
            if (bestMetric == std::numeric_limits<double>::max())
            {
                bestNeighbor = candidate;
                bestMetric = 0.0;
            }
        }
    }

    NS_LOG_INFO("Selected best next hop " << bestNeighbor << " with metric " << bestMetric);
    return bestNeighbor;
}

void
RoutingProtocol::PurgeStaleNeighbors()
{
    NS_LOG_FUNCTION(this);

    Time now = Simulator::Now();
    Time neighborTimeout = MilliSeconds(3000);  // 3 second timeout

    auto it = m_neighborTable.begin();
    while (it != m_neighborTable.end())
    {
        if (now - it->second.lastUpdate > neighborTimeout)
        {
            NS_LOG_INFO("Purging stale neighbor " << it->first);
            it = m_neighborTable.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace iacr
} // namespace ns3

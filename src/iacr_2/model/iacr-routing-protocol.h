/*
 * IACR (Interference Aware Cooperative Routing) Protocol
 * Based on arXiv:2201.01520v1
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef IACRPROTOCOL_H
#define IACRPROTOCOL_H

#include "iacr-packet.h"

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/double.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/socket.h"

#include <map>
#include <vector>

namespace ns3
{
namespace iacr
{

class RoutingProtocol : public Ipv4RoutingProtocol
{
  public:
    
    static TypeId GetTypeId();

    RoutingProtocol();
    ~RoutingProtocol() override;

   // double CalculatePathLoss(double distance);

    double CalculateReceivedPower(double transmitPower, double distance);

    double CalculateAggregateInterference(const std::vector<NeighborInfo>& neighbors);

    double CalculateReceivedInterference(Ipv4Address receiver);

    double CalculatePathMetric(double aggregateInterference, double pathDelay);

    double CalculateCreatedInterference(double metric, double neighborDistance);

    void UpdateNeighborInterference(Ipv4Address neighbor,
                                     double createdInterference,
                                     double receivedInterference);

    double GetAggregateInterference(Ipv4Address sender);

    double GetDistanceToNode(uint32_t nodeId);

    Ipv4Address SelectBestNextHop(const std::vector<Ipv4Address>& candidates);

    void PurgeStaleNeighbors();

    void SetDeltaParameter(double delta)
    {
        m_delta = delta;
    }
    double GetDeltaParameter() const
    {
        return m_delta;
    }

    void SetPathLossExponent(double alpha)
    {
        m_pathLossExponent = alpha;
    }

  protected:

    void InformationCollectionPhase(Ipv4Address dest);

    void ReceiveIcreq(Ptr<Packet> p, Ipv4Address sender);

    void ReceiveIcrep(Ptr<Packet> p, Ipv4Address sender);

    void ReceiveRreq(Ptr<Packet> p, Ipv4Address sender);
       
    void ReceiveRrep(Ptr<Packet> p, Ipv4Address sender); 

    bool ReceivePacket(Ptr<Packet> p, const Ipv4Header& header);


  private:
    // Neighbor interference information
    struct NeighborInfo
    {
        Ipv4Address address;
        double createdInterference;  //!< I_c^j
        double receivedInterference; //!< I_j
        double aggregateInterference; //!< I_aggr^j
        Time lastUpdate;
    };

    std::map<Ipv4Address, NeighborInfo> m_neighborTable;

    // IACR parameters
    double m_delta;                 //!< Delta parameter for metric calculation
    double m_pathLossExponent;      //!< Path loss exponent (alpha)
    double m_transmitPower;         //!< Transmit power in dBm
    double m_referenceDistance;     //!< Reference distance (1m)
    Time m_infoCollectionTimeout;   //!< Timeout for information collection phase
    Time m_helloInterval;           //!< Interval for sending Hello messages
    uint32_t m_helloCounter;

    // Information collection request tracking
    struct IcreqEntry
    {
        uint32_t requestId;
        Ipv4Address destination;
        Time timestamp;
        std::vector<Ipv4Address> receivedReplies;
    };
    std::map<uint32_t, IcreqEntry> m_pendingIcreq;
    uint32_t m_icreqSeqNumber;
};

} // namespace iacr
} // namespace ns3

#endif 

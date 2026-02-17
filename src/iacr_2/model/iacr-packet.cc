/*
 * IACR Packet Implementation
 * Based on arXiv:2201.01520v1
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "iacr-packet.h"

#include "ns3/address-utils.h"
#include "ns3/log.h"
#include "ns3/type-id.h"

namespace ns3
{
namespace iacr
{

NS_LOG_COMPONENT_DEFINE("IacrPacket2");

// ===================== IcreqHeader =====================

IcreqHeader::IcreqHeader()
    : created_power(0.0),
      transmitterID("0.0.0.0"),
      destinationID("0.0.0.0"),
      sequenceNo(0)
{
    NS_LOG_FUNCTION(this);
}

IcreqHeader::~IcreqHeader()
{
    NS_LOG_FUNCTION(this);
}

TypeId
IcreqHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::iacr::IcreqHeader")
                            .SetParent<Header>()
                            .SetGroupName("Iacr")
                            .AddConstructor<IcreqHeader>();
    return tid;
}

TypeId
IcreqHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
IcreqHeader::GetSerializedSize() const
{
    return 8 + 4 + 4 + 4;  // created_power(8) + transmitterID(4) + destinationID(4) + sequenceNo(4)
}

void
IcreqHeader::Serialize(Buffer::Iterator start) const
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;

    i.WriteHtonU64((uint64_t)(created_power * 1e6));
    WriteTo(i, transmitterID);
    WriteTo(i, destinationID);
    i.WriteHtonU32(sequenceNo);
}

uint32_t
IcreqHeader::Deserialize(Buffer::Iterator start)
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;

    created_power = i.ReadNtohU64() / 1e6;
    ReadFrom(i, transmitterID);
    ReadFrom(i, destinationID);
    sequenceNo = i.ReadNtohU32();

    return GetSerializedSize();
}

void
IcreqHeader::Print(std::ostream& os) const
{
    os << "ICREQ: created_power=" << created_power << " from=" << transmitterID
       << " to=" << destinationID << " seq=" << sequenceNo;
}

// ===================== IcrepHeader =====================

IcrepHeader::IcrepHeader()
    : received_power(0.0),
      received_inference(0.0),
      transmitterID("0.0.0.0"),
      sourceID("0.0.0.0"),
      requestID(0)
{
    NS_LOG_FUNCTION(this);
}

IcrepHeader::~IcrepHeader()
{
    NS_LOG_FUNCTION(this);
}

TypeId
IcrepHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::iacr::IcrepHeader")
                            .SetParent<Header>()
                            .SetGroupName("Iacr")
                            .AddConstructor<IcrepHeader>();
    return tid;
}

TypeId
IcrepHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
IcrepHeader::GetSerializedSize() const
{
    return 8 + 8 + 4 + 4 + 4;  // received_power(8) + received_interference(8) + transmitterID(4) + sourceID(4) + requestID(4)
}

void
IcrepHeader::Serialize(Buffer::Iterator start) const
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;

    i.WriteHtonU64((uint64_t)(received_power * 1e6));
    i.WriteHtonU64((uint64_t)(received_inference * 1e6));
    WriteTo(i, transmitterID);
    WriteTo(i, sourceID);
    i.WriteHtonU32(requestID);
}

uint32_t
IcrepHeader::Deserialize(Buffer::Iterator start)
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;

    received_power = i.ReadNtohU64() / 1e6;
    received_inference = i.ReadNtohU64() / 1e6;
    ReadFrom(i, transmitterID);
    ReadFrom(i, sourceID);
    requestID = i.ReadNtohU32();

    return GetSerializedSize();
}

void
IcrepHeader::Print(std::ostream& os) const
{
    os << "ICREP: received_power=" << received_power << " interference=" << received_inference
       << " from=" << transmitterID << " to=" << sourceID << " req_id=" << requestID;
}

// ===================== RreqHeader =====================

RreqHeader::RreqHeader()
    : min_m(0.0),
      sourceID("0.0.0.0"),
      destinationID("0.0.0.0"),
      transmitterID("0.0.0.0"),
      nextHop("0.0.0.0"),
      received_power(0.0),
      received_inference(0.0),
      requestID(0),
      sequenceNo(0)
{
    NS_LOG_FUNCTION(this);
}

RreqHeader::~RreqHeader()
{
    NS_LOG_FUNCTION(this);
}

TypeId
RreqHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::iacr::RreqHeader")
                            .SetParent<Header>()
                            .SetGroupName("Iacr")
                            .AddConstructor<RreqHeader>();
    return tid;
}

TypeId
RreqHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
RreqHeader::GetSerializedSize() const
{
    return 8 + 4 + 4 + 4 + 4 + 8 + 8 + 4 + 4;  
    // min_m(8) + sourceID(4) + destID(4) + transmitterID(4) + nextHop(4) + 
    // received_power(8) + received_interference(8) + requestID(4) + sequenceNo(4)
}

void
RreqHeader::Serialize(Buffer::Iterator start) const
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;

    i.WriteHtonU64((uint64_t)(min_m * 1e6));
    WriteTo(i, sourceID);
    WriteTo(i, destinationID);
    WriteTo(i, transmitterID);
    WriteTo(i, nextHop);
    i.WriteHtonU64((uint64_t)(received_power * 1e6));
    i.WriteHtonU64((uint64_t)(received_inference * 1e6));
    i.WriteHtonU32(requestID);
    i.WriteHtonU32(sequenceNo);
}

uint32_t
RreqHeader::Deserialize(Buffer::Iterator start)
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;

    min_m = i.ReadNtohU64() / 1e6;
    ReadFrom(i, sourceID);
    ReadFrom(i, destinationID);
    ReadFrom(i, transmitterID);
    ReadFrom(i, nextHop);
    received_power = i.ReadNtohU64() / 1e6;
    received_inference = i.ReadNtohU64() / 1e6;
    requestID = i.ReadNtohU32();
    sequenceNo = i.ReadNtohU32();

    return GetSerializedSize();
}

void
RreqHeader::Print(std::ostream& os) const
{
    os << "RREQ: min_m=" << min_m << " src=" << sourceID << " dst=" << destinationID
       << " tx=" << transmitterID << " next=" << nextHop << " power=" << received_power
       << " interf=" << received_inference << " req_id=" << requestID << " seq=" << sequenceNo;
}

// ===================== RrepHeader =====================

RrepHeader::RrepHeader()
    : min_m(0.0),
      sourceID("0.0.0.0"),
      transmitterID("0.0.0.0"),
      nextHop("0.0.0.0")
{
    NS_LOG_FUNCTION(this);
}

RrepHeader::~RrepHeader()
{
    NS_LOG_FUNCTION(this);
}

TypeId
RrepHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::iacr::RrepHeader")
                            .SetParent<Header>()
                            .SetGroupName("Iacr")
                            .AddConstructor<RrepHeader>();
    return tid;
}

TypeId
RrepHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
RrepHeader::GetSerializedSize() const
{
    return 8 + 4 + 4 + 4;  // min_m(8) + sourceID(4) + transmitterID(4) + nextHop(4)
}

void
RrepHeader::Serialize(Buffer::Iterator start) const
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;

    i.WriteHtonU64((uint64_t)(min_m * 1e6));
    WriteTo(i, sourceID);
    WriteTo(i, transmitterID);
    WriteTo(i, nextHop);
}

uint32_t
RrepHeader::Deserialize(Buffer::Iterator start)
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;

    min_m = i.ReadNtohU64() / 1e6;
    ReadFrom(i, sourceID);
    ReadFrom(i, transmitterID);
    ReadFrom(i, nextHop);

    return GetSerializedSize();
}

void
RrepHeader::Print(std::ostream& os) const
{
    os << "RREP: min_m=" << min_m << " src=" << sourceID << " tx=" << transmitterID
       << " next=" << nextHop;
}

} // namespace iacr
} // namespace ns3

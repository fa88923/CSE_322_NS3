/*
 * Copyright (c) 2009 IITP RAS
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Based on
 *      NS-2 AODV model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 *
 *      AODV-UU implementation by Erik Nordström of Uppsala University
 *      https://web.archive.org/web/20100527072022/http://core.it.uu.se/core/index.php/AODV-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */
#include "aodv-packet.h"

#include "ns3/address-utils.h"
#include "ns3/packet.h"

namespace ns3
{
namespace aodv
{

NS_OBJECT_ENSURE_REGISTERED(TypeHeader);

TypeHeader::TypeHeader(MessageType t)
    : m_type(t),
      m_valid(true)
{
}

TypeId
TypeHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aodv::TypeHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aodv")
                            .AddConstructor<TypeHeader>();
    return tid;
}

TypeId
TypeHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
TypeHeader::GetSerializedSize() const
{
    return 1;
}

void
TypeHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8((uint8_t)m_type);
}

uint32_t
TypeHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    uint8_t type = i.ReadU8();
    m_valid = true;
    switch (type)
    {
    case AODVTYPE_RREQ:
    case AODVTYPE_RREP:
    case AODVTYPE_RERR:
    case AODVTYPE_RREP_ACK:
    case AODVTYPE_ICREP: {
        m_type = (MessageType)type;
        break;
    }
    default:
        m_valid = false;
    }
    uint32_t dist = i.GetDistanceFrom(start);
    NS_ASSERT(dist == GetSerializedSize());
    return dist;
}

void
TypeHeader::Print(std::ostream& os) const
{
    switch (m_type)
    {
    case AODVTYPE_RREQ: {
        os << "RREQ";
        break;
    }
    case AODVTYPE_RREP: {
        os << "RREP";
        break;
    }
    case AODVTYPE_RERR: {
        os << "RERR";
        break;
    }
    case AODVTYPE_RREP_ACK: {
        os << "RREP_ACK";
        break;
    }
    default:
        os << "UNKNOWN_TYPE";
    }
}

bool
TypeHeader::operator==(const TypeHeader& o) const
{
    return (m_type == o.m_type && m_valid == o.m_valid);
}

std::ostream&
operator<<(std::ostream& os, const TypeHeader& h)
{
    h.Print(os);
    return os;
}



//-----------------------------------------------------------------------------
// IC REQUEST
//-----------------------------------------------------------------------------

NS_OBJECT_ENSURE_REGISTERED(IcReqHeader);

IcReqHeader::IcReqHeader(Ipv4Address transmitter, double txPower)
    : m_transmitter(transmitter),
      m_txPower(txPower)
{
}

TypeId
IcReqHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aodv::IcReqHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aodv")
                            .AddConstructor<IcReqHeader>();
    return tid;
}

TypeId
IcReqHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
IcReqHeader::GetSerializedSize() const
{
    return 4 + 8; // IPv4 (4) + double (8)
}

void
IcReqHeader::Serialize(Buffer::Iterator i) const
{
    WriteTo(i, m_transmitter);
    i.WriteHtonU64((uint64_t)(m_txPower * 1e6));
}

uint32_t
IcReqHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;

    ReadFrom(i, m_transmitter);
    m_txPower = i.ReadNtohU64() / 1e6;

    uint32_t dist = i.GetDistanceFrom(start);
    NS_ASSERT(dist == GetSerializedSize());
    return dist;
}

void
IcReqHeader::Print(std::ostream& os) const
{
    os << "ICREQ: transmitter=" << m_transmitter
       << " txPower=" << m_txPower;
}

void
IcReqHeader::SetTransmitter(Ipv4Address addr)
{
    m_transmitter = addr;
}

void
IcReqHeader::SetTxPower(double power)
{
    m_txPower = power;
}

Ipv4Address
IcReqHeader::GetTransmitter() const
{
    return m_transmitter;
}

double
IcReqHeader::GetTxPower() const
{
    return m_txPower;
}


//-----------------------------------------------------------------------------
// IC REPLY
//-----------------------------------------------------------------------------

NS_OBJECT_ENSURE_REGISTERED(IcRepHeader);

IcRepHeader::IcRepHeader(Ipv4Address transmitter,
                         double pr,
                         double ir)
    : m_transmitter(transmitter),
      m_pr(pr),
      m_ir(ir)
{
}

TypeId
IcRepHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aodv::IcRepHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aodv")
                            .AddConstructor<IcRepHeader>();
    return tid;
}

TypeId
IcRepHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
IcRepHeader::GetSerializedSize() const
{
    return 4 + 8 + 8; // IPv4 + double + double
}

void
IcRepHeader::Serialize(Buffer::Iterator i) const
{
    WriteTo(i, m_transmitter);
    // Multiply by 1e18 to preserve precision when converting to uint64_t
    uint64_t prEncoded = (uint64_t)(m_pr * 1e18);
    uint64_t irEncoded = (uint64_t)(m_ir * 1e18);
    i.WriteHtonU64(prEncoded);
    i.WriteHtonU64(irEncoded);
}

uint32_t
IcRepHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;

    ReadFrom(i, m_transmitter);
    uint64_t prEncoded = i.ReadNtohU64();
    uint64_t irEncoded = i.ReadNtohU64();
    // Divide by 1e18 to recover the original values
    m_pr = prEncoded / 1e18;
    m_ir = irEncoded / 1e18;

    uint32_t dist = i.GetDistanceFrom(start);
    NS_ASSERT(dist == GetSerializedSize());
    return dist;
}

void
IcRepHeader::Print(std::ostream& os) const
{
    os << "ICREP: transmitter=" << m_transmitter
       << " Pr=" << m_pr
       << " Ir=" << m_ir;
}

void
IcRepHeader::SetTransmitter(Ipv4Address addr)
{
    m_transmitter = addr;
}

void
IcRepHeader::SetReceivedPower(double power)
{
    m_pr = power;
}

void
IcRepHeader::SetInterference(double interference)
{
    m_ir = interference;
}

Ipv4Address
IcRepHeader::GetTransmitter() const
{
    return m_transmitter;
}

double
IcRepHeader::GetReceivedPower() const
{
    return m_pr;
}

double
IcRepHeader::GetInterference() const
{
    return m_ir;
}



//-----------------------------------------------------------------------------
// RREQ
//-----------------------------------------------------------------------------
RreqHeader::RreqHeader(uint8_t flags,
                       uint8_t reserved,
                       uint8_t hopCount,
                       uint32_t requestID,
                       Ipv4Address dst,
                       uint32_t dstSeqNo,
                       Ipv4Address origin,
                       uint32_t originSeqNo)
    : m_flags(flags),
      m_reserved(reserved),
      m_hopCount(hopCount),
      m_requestID(requestID),
      m_dst(dst),
      m_dstSeqNo(dstSeqNo),
      m_origin(origin),
      m_originSeqNo(originSeqNo)
{
}

NS_OBJECT_ENSURE_REGISTERED(RreqHeader);

TypeId
RreqHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aodv::RreqHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aodv")
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
    return 23 + 8 + 8;
}

void
RreqHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(m_flags);
    i.WriteU8(m_reserved);
    i.WriteU8(m_hopCount);
    i.WriteHtonU32(m_requestID);
    WriteTo(i, m_dst);
    i.WriteHtonU32(m_dstSeqNo);
    WriteTo(i, m_origin);
    i.WriteHtonU32(m_originSeqNo);
    uint64_t encodedMetric = (uint64_t)(m_metric * 1e18);
    i.WriteHtonU64(encodedMetric);
    uint64_t encodedPrevMetric = (uint64_t)(m_prevMetric * 1e18);
    i.WriteHtonU64(encodedPrevMetric);
}

uint32_t
RreqHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    m_flags = i.ReadU8();
    m_reserved = i.ReadU8();
    m_hopCount = i.ReadU8();
    m_requestID = i.ReadNtohU32();
    ReadFrom(i, m_dst);
    m_dstSeqNo = i.ReadNtohU32();
    ReadFrom(i, m_origin);
    m_originSeqNo = i.ReadNtohU32();
    
    uint64_t encodedMetric = i.ReadNtohU64();
    m_metric = encodedMetric / 1e18;
    uint64_t encodedPrevMetric = i.ReadNtohU64();
    m_prevMetric = encodedPrevMetric / 1e18;

    uint32_t dist = i.GetDistanceFrom(start);
    NS_ASSERT(dist == GetSerializedSize());
    return dist;
}

void
RreqHeader::Print(std::ostream& os) const
{
    os << "RREQ ID " << m_requestID;
    os << " destination: ipv4 " << m_dst << " sequence number " << m_dstSeqNo;
    os << " source: ipv4 " << m_origin << " sequence number " << m_originSeqNo;
    os << " metric " << m_metric;
    os << " flags: Gratuitous RREP " << (*this).GetGratuitousRrep() << " Destination only "
       << (*this).GetDestinationOnly() << " Unknown sequence number " << (*this).GetUnknownSeqno();
    os << " prevMetric " << m_prevMetric;
}

std::ostream&
operator<<(std::ostream& os, const RreqHeader& h)
{
    h.Print(os);
    return os;
}

void
RreqHeader::SetGratuitousRrep(bool f)
{
    if (f)
    {
        m_flags |= (1 << 5);
    }
    else
    {
        m_flags &= ~(1 << 5);
    }
}

bool
RreqHeader::GetGratuitousRrep() const
{
    return (m_flags & (1 << 5));
}

void
RreqHeader::SetDestinationOnly(bool f)
{
    if (f)
    {
        m_flags |= (1 << 4);
    }
    else
    {
        m_flags &= ~(1 << 4);
    }
}

bool
RreqHeader::GetDestinationOnly() const
{
    return (m_flags & (1 << 4));
}

void
RreqHeader::SetUnknownSeqno(bool f)
{
    if (f)
    {
        m_flags |= (1 << 3);
    }
    else
    {
        m_flags &= ~(1 << 3);
    }
}

bool
RreqHeader::GetUnknownSeqno() const
{
    return (m_flags & (1 << 3));
}

void
RreqHeader::SetMetric(double metric)
{
    m_metric = metric;
}

double
RreqHeader::GetMetric() const
{
    return m_metric;
}

bool
RreqHeader::operator==(const RreqHeader& o) const
{
    return (m_flags == o.m_flags && m_reserved == o.m_reserved && m_hopCount == o.m_hopCount &&
            m_requestID == o.m_requestID && m_dst == o.m_dst && m_dstSeqNo == o.m_dstSeqNo &&
            m_origin == o.m_origin && m_originSeqNo == o.m_originSeqNo && m_metric == o.m_metric && m_prevMetric == o.m_prevMetric);
}

//-----------------------------------------------------------------------------
// RREP
//-----------------------------------------------------------------------------

RrepHeader::RrepHeader(uint8_t prefixSize,
                       uint8_t hopCount,
                       Ipv4Address dst,
                       uint32_t dstSeqNo,
                       Ipv4Address origin,
                       Time lifeTime)
    : m_flags(0),
      m_prefixSize(prefixSize),
      m_hopCount(hopCount),
      m_dst(dst),
      m_dstSeqNo(dstSeqNo),
      m_origin(origin)
{
    m_lifeTime = uint32_t(lifeTime.GetMilliSeconds());
}

NS_OBJECT_ENSURE_REGISTERED(RrepHeader);

TypeId
RrepHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aodv::RrepHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aodv")
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
    return 19 + 8;
}

void
RrepHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(m_flags);
    i.WriteU8(m_prefixSize);
    i.WriteU8(m_hopCount);
    WriteTo(i, m_dst);
    i.WriteHtonU32(m_dstSeqNo);
    WriteTo(i, m_origin);
    i.WriteHtonU32(m_lifeTime);
    uint64_t encodedPrevMetric = (uint64_t)(m_prevMetric * 1e18);
    i.WriteHtonU64(encodedPrevMetric);
}

uint32_t
RrepHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;

    m_flags = i.ReadU8();
    m_prefixSize = i.ReadU8();
    m_hopCount = i.ReadU8();
    ReadFrom(i, m_dst);
    m_dstSeqNo = i.ReadNtohU32();
    ReadFrom(i, m_origin);
    m_lifeTime = i.ReadNtohU32();
    uint64_t encodedPrevMetric = i.ReadNtohU64();
    m_prevMetric = encodedPrevMetric / 1e18;

    uint32_t dist = i.GetDistanceFrom(start);
    NS_ASSERT(dist == GetSerializedSize());
    return dist;
}

void
RrepHeader::Print(std::ostream& os) const
{
    os << "destination: ipv4 " << m_dst << " sequence number " << m_dstSeqNo;
    if (m_prefixSize != 0)
    {
        os << " prefix size " << m_prefixSize;
    }
    os << " source ipv4 " << m_origin << " lifetime " << m_lifeTime
       << " acknowledgment required flag " << (*this).GetAckRequired();
    os << " prevMetric " << m_prevMetric;
}

void
RrepHeader::SetLifeTime(Time t)
{
    m_lifeTime = t.GetMilliSeconds();
}

Time
RrepHeader::GetLifeTime() const
{
    Time t(MilliSeconds(m_lifeTime));
    return t;
}

void
RrepHeader::SetAckRequired(bool f)
{
    if (f)
    {
        m_flags |= (1 << 6);
    }
    else
    {
        m_flags &= ~(1 << 6);
    }
}

bool
RrepHeader::GetAckRequired() const
{
    return (m_flags & (1 << 6));
}

void
RrepHeader::SetPrefixSize(uint8_t sz)
{
    m_prefixSize = sz;
}

uint8_t
RrepHeader::GetPrefixSize() const
{
    return m_prefixSize;
}

bool
RrepHeader::operator==(const RrepHeader& o) const
{
    return (m_flags == o.m_flags && m_prefixSize == o.m_prefixSize && m_hopCount == o.m_hopCount &&
            m_dst == o.m_dst && m_dstSeqNo == o.m_dstSeqNo && m_origin == o.m_origin &&
            m_lifeTime == o.m_lifeTime && m_prevMetric == o.m_prevMetric);
}

void
RrepHeader::SetHello(Ipv4Address origin, uint32_t srcSeqNo, Time lifetime)
{
    m_flags = 0;
    m_prefixSize = 0;
    m_hopCount = 0;
    m_dst = origin;
    m_dstSeqNo = srcSeqNo;
    m_origin = origin;
    m_lifeTime = lifetime.GetMilliSeconds();
}

std::ostream&
operator<<(std::ostream& os, const RrepHeader& h)
{
    h.Print(os);
    return os;
}

//-----------------------------------------------------------------------------
// RREP-ACK
//-----------------------------------------------------------------------------

RrepAckHeader::RrepAckHeader()
    : m_reserved(0)
{
}

NS_OBJECT_ENSURE_REGISTERED(RrepAckHeader);

TypeId
RrepAckHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aodv::RrepAckHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aodv")
                            .AddConstructor<RrepAckHeader>();
    return tid;
}

TypeId
RrepAckHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
RrepAckHeader::GetSerializedSize() const
{
    return 1;
}

void
RrepAckHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(m_reserved);
}

uint32_t
RrepAckHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    m_reserved = i.ReadU8();
    uint32_t dist = i.GetDistanceFrom(start);
    NS_ASSERT(dist == GetSerializedSize());
    return dist;
}

void
RrepAckHeader::Print(std::ostream& os) const
{
}

bool
RrepAckHeader::operator==(const RrepAckHeader& o) const
{
    return m_reserved == o.m_reserved;
}

std::ostream&
operator<<(std::ostream& os, const RrepAckHeader& h)
{
    h.Print(os);
    return os;
}

//-----------------------------------------------------------------------------
// RERR
//-----------------------------------------------------------------------------
RerrHeader::RerrHeader()
    : m_flag(0),
      m_reserved(0)
{
}

NS_OBJECT_ENSURE_REGISTERED(RerrHeader);

TypeId
RerrHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aodv::RerrHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aodv")
                            .AddConstructor<RerrHeader>();
    return tid;
}

TypeId
RerrHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
RerrHeader::GetSerializedSize() const
{
    return (3 + 8 * GetDestCount());
}

void
RerrHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(m_flag);
    i.WriteU8(m_reserved);
    i.WriteU8(GetDestCount());
    for (auto j = m_unreachableDstSeqNo.begin(); j != m_unreachableDstSeqNo.end(); ++j)
    {
        WriteTo(i, (*j).first);
        i.WriteHtonU32((*j).second);
    }
}

uint32_t
RerrHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    m_flag = i.ReadU8();
    m_reserved = i.ReadU8();
    uint8_t dest = i.ReadU8();
    m_unreachableDstSeqNo.clear();
    Ipv4Address address;
    uint32_t seqNo;
    for (uint8_t k = 0; k < dest; ++k)
    {
        ReadFrom(i, address);
        seqNo = i.ReadNtohU32();
        m_unreachableDstSeqNo.insert(std::make_pair(address, seqNo));
    }

    uint32_t dist = i.GetDistanceFrom(start);
    NS_ASSERT(dist == GetSerializedSize());
    return dist;
}

void
RerrHeader::Print(std::ostream& os) const
{
    os << "Unreachable destination (ipv4 address, seq. number):";
    for (auto j = m_unreachableDstSeqNo.begin(); j != m_unreachableDstSeqNo.end(); ++j)
    {
        os << (*j).first << ", " << (*j).second;
    }
    os << "No delete flag " << (*this).GetNoDelete();
}

void
RerrHeader::SetNoDelete(bool f)
{
    if (f)
    {
        m_flag |= (1 << 0);
    }
    else
    {
        m_flag &= ~(1 << 0);
    }
}

bool
RerrHeader::GetNoDelete() const
{
    return (m_flag & (1 << 0));
}

bool
RerrHeader::AddUnDestination(Ipv4Address dst, uint32_t seqNo)
{
    if (m_unreachableDstSeqNo.find(dst) != m_unreachableDstSeqNo.end())
    {
        return true;
    }

    NS_ASSERT(GetDestCount() < 255); // can't support more than 255 destinations in single RERR
    m_unreachableDstSeqNo.insert(std::make_pair(dst, seqNo));
    return true;
}

bool
RerrHeader::RemoveUnDestination(std::pair<Ipv4Address, uint32_t>& un)
{
    if (m_unreachableDstSeqNo.empty())
    {
        return false;
    }
    auto i = m_unreachableDstSeqNo.begin();
    un = *i;
    m_unreachableDstSeqNo.erase(i);
    return true;
}

void
RerrHeader::Clear()
{
    m_unreachableDstSeqNo.clear();
    m_flag = 0;
    m_reserved = 0;
}

bool
RerrHeader::operator==(const RerrHeader& o) const
{
    if (m_flag != o.m_flag || m_reserved != o.m_reserved || GetDestCount() != o.GetDestCount())
    {
        return false;
    }

    auto j = m_unreachableDstSeqNo.begin();
    auto k = o.m_unreachableDstSeqNo.begin();
    for (uint8_t i = 0; i < GetDestCount(); ++i)
    {
        if ((j->first != k->first) || (j->second != k->second))
        {
            return false;
        }

        j++;
        k++;
    }
    return true;
}

std::ostream&
operator<<(std::ostream& os, const RerrHeader& h)
{
    h.Print(os);
    return os;
}
} // namespace aodv
} // namespace ns3

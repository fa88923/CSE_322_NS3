
#ifndef IACRPACKET_H
#define IACRPACKET_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"

namespace ns3
{
namespace iacr
{

enum IACRMessageType
{
    IACRTYPE_ICREQ = 10,  
    IACRTYPE_ICREP = 11, 
    IACRTYPE_HELLO = 12   
};


class IcreqHeader : public Header
{
    double created_power;
    Ipv4Address transmitterID;
    Ipv4Address destinationID;
    uint32_t sequenceNo;    
     
  
    IcreqHeader();
    ~IcreqHeader() override;

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;


};


class IcrepHeader : public Header
{
    double received_power;
    double received_inference;
    Ipv4Address transmitterID;
    Ipv4Address sourceID;
    uint32_t requestID;    

    IcrepHeader();
    ~IcrepHeader() override;

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;


};


// Not completed yet
class RreqHeader : public Header
{
    double min_m;
    Ipv4Address sourceID;
    Ipv4Address destinationID;
    Ipv4Address transmitterID;
    Ipv4Address nextHop;

    double received_power;
    double received_inference;
    uint32_t requestID;    
    uint32_t sequenceNo;

    RreqHeader();
    ~RreqHeader() override;
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;


};


class RrepHeader : public Header
{
    double min_m;
    Ipv4Address sourceID;
    Ipv4Address transmitterID;
    Ipv4Address nextHop;

    RrepHeader();
    ~RrepHeader() override;

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;


};


} // namespace iacr
} // namespace ns3

#endif /* IACRPACKET_H */

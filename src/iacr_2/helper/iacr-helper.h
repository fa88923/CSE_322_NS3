/*
 * IACR Helper
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef IACRHELPER_H
#define IACRHELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"

namespace ns3
{
namespace iacr
{

class RoutingProtocol;

/**
 * @ingroup iacr
 * @brief Helper class to make installing IACR routing to nodes easier
 */
class IacrHelper : public Ipv4RoutingHelper
{
  public:
    IacrHelper();
    ~IacrHelper() override;

    /**
     * Create a new IACR RoutingProtocol instance and stack it on the transport
     * associated with the net device
     * @param node The node on which the routing protocol will run
     * @returns A newly-created routing protocol for the node
     */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    /**
     * Set IACR-specific attribute values
     * @param name the name of the attribute
     * @param value the value of the attribute
     */
    void Set(std::string name, const AttributeValue& value);

  private:
    ObjectFactory m_agentFactory; //!< ObjectFactory to create IACR instances
};

} // namespace iacr
} // namespace ns3

#endif /* IACRHELPER_H */

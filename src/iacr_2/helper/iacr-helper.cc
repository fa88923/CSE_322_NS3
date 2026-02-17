/*
 * IACR Helper Implementation
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "iacr-helper.h"

#include "../model/iacr-routing-protocol.h"

#include "ns3/ipv4-list-routing.h"
#include "ns3/log.h"

namespace ns3
{
namespace iacr
{

NS_LOG_COMPONENT_DEFINE("IacrHelper2");

IacrHelper::IacrHelper()
{
    NS_LOG_FUNCTION(this);
    m_agentFactory.SetTypeId("ns3::iacr::RoutingProtocol2");
}

IacrHelper::~IacrHelper()
{
    NS_LOG_FUNCTION(this);
}

Ptr<Ipv4RoutingProtocol>
IacrHelper::Create(Ptr<Node> node) const
{
    NS_LOG_FUNCTION(this << node);
    Ptr<RoutingProtocol> agent = m_agentFactory.Create<RoutingProtocol>();
    node->AggregateObject(agent);
    return agent;
}

void
IacrHelper::Set(std::string name, const AttributeValue& value)
{
    NS_LOG_FUNCTION(this << name);
    m_agentFactory.Set(name, value);
}

} // namespace iacr
} // namespace ns3

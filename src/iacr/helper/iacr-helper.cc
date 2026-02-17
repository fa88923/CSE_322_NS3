/*
 * Copyright (c) 2009 IITP RAS
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Pavel Boyko <boyko@iitp.ru>, written after OlsrHelper by Mathieu Lacage
 * <mathieu.lacage@sophia.inria.fr>
 */
#include "iacr-helper.h"

#include "ns3/iacr-routing-protocol.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/names.h"
#include "ns3/node-list.h"
#include "ns3/ptr.h"

namespace ns3
{

IacrHelper::IacrHelper()
    : Ipv4RoutingHelper()
{
    m_agentFactory.SetTypeId("ns3::iacr::RoutingProtocol");
}

IacrHelper*
IacrHelper::Copy() const
{
    return new IacrHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
IacrHelper::Create(Ptr<Node> node) const
{
    Ptr<iacr::RoutingProtocol> agent = m_agentFactory.Create<iacr::RoutingProtocol>();
    node->AggregateObject(agent);
    return agent;
}

void
IacrHelper::Set(std::string name, const AttributeValue& value)
{
    m_agentFactory.Set(name, value);
}

int64_t
IacrHelper::AssignStreams(NodeContainer c, int64_t stream)
{
    int64_t currentStream = stream;
    Ptr<Node> node;
    for (auto i = c.Begin(); i != c.End(); ++i)
    {
        node = (*i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        NS_ASSERT_MSG(ipv4, "Ipv4 not installed on node");
        Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol();
        NS_ASSERT_MSG(proto, "Ipv4 routing not installed on node");
        Ptr<iacr::RoutingProtocol> iacr = DynamicCast<iacr::RoutingProtocol>(proto);
        if (iacr)
        {
            currentStream += iacr->AssignStreams(currentStream);
            continue;
        }
        // Iacr may also be in a list
        Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(proto);
        if (list)
        {
            int16_t priority;
            Ptr<Ipv4RoutingProtocol> listProto;
            Ptr<iacr::RoutingProtocol> listIacr;
            for (uint32_t i = 0; i < list->GetNRoutingProtocols(); i++)
            {
                listProto = list->GetRoutingProtocol(i, priority);
                listIacr = DynamicCast<iacr::RoutingProtocol>(listProto);
                if (listIacr)
                {
                    currentStream += listIacr->AssignStreams(currentStream);
                    break;
                }
            }
        }
    }
    return (currentStream - stream);
}

} // namespace ns3

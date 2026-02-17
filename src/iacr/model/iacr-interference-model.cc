/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * IACR: Interference Aware Cooperative Routing
 * for Edge Computing-enabled 5G Networks
 *
 * iacr-interference-model.cc
 *
 * Implements the composite routing metric:
 *
 *   M(i→j) = w_r * [1 / (1 + SINR(j))]   <-- received interference cost
 *           + w_c * cooperationFactor(j)    <-- created interference cost
 *
 * SINR(j) = rxPower(i→j) / (interferenceAtJ + noiseAtJ)
 *
 * A link is only viable when SINR(j) >= SINR_threshold.
 * Routes are selected by minimising the accumulated path metric.
 */

#include "iacr-interference-model.h"

#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"
#include "ns3/mobility-model.h"
#include "ns3/node.h"

#include <cmath>
#include <limits>
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("IacrInterferenceModel");

namespace iacr {

// =========================================================================
//  Constants
// =========================================================================

const double IacrInterferenceModel::METRIC_INFINITY =
    std::numeric_limits<double>::max () / 2.0;

// Boltzmann constant [J/K]
static const double BOLTZMANN = 1.380649e-23;

// Standard temperature [K]
static const double TEMPERATURE_K = 290.0;

// =========================================================================
//  TypeId
// =========================================================================

NS_OBJECT_ENSURE_REGISTERED (IacrInterferenceModel);

TypeId
IacrInterferenceModel::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::iacr::IacrInterferenceModel")
        .SetParent<Object> ()
        .SetGroupName ("Iacr")
        .AddConstructor<IacrInterferenceModel> ()

        // SINR threshold attribute (stored/set in dB for user convenience)
        .AddAttribute ("SinrThreshold",
                       "Minimum acceptable SINR per link [dB]. "
                       "Links below this value are rejected during route selection.",
                       DoubleValue (10.0),
                       MakeDoubleAccessor (&IacrInterferenceModel::SetSinrThreshold,
                                          &IacrInterferenceModel::GetSinrThreshold),
                       MakeDoubleChecker<double> ())

        // Weight: received interference term
        .AddAttribute ("ReceivedInterferenceWeight",
                       "Weight w_r for the received-interference cost term [0,1]. "
                       "w_r + w_c must be <= 1.",
                       DoubleValue (0.5),
                       MakeDoubleAccessor (
                           &IacrInterferenceModel::SetReceivedInterferenceWeight,
                           &IacrInterferenceModel::GetReceivedInterferenceWeight),
                       MakeDoubleChecker<double> (0.0, 1.0))

        // Weight: created interference (cooperation) term
        .AddAttribute ("CreatedInterferenceWeight",
                       "Weight w_c for the created-interference / cooperation cost "
                       "term [0,1].  w_r + w_c must be <= 1.",
                       DoubleValue (0.5),
                       MakeDoubleAccessor (
                           &IacrInterferenceModel::SetCreatedInterferenceWeight,
                           &IacrInterferenceModel::GetCreatedInterferenceWeight),
                       MakeDoubleChecker<double> (0.0, 1.0))

        // Entry lifetime
        .AddAttribute ("EntryLifetime",
                       "How long a link record is considered fresh. "
                       "Stale entries are ignored for route selection.",
                       TimeValue (Seconds (10.0)),
                       MakeTimeAccessor (&IacrInterferenceModel::SetEntryLifetime,
                                         &IacrInterferenceModel::GetEntryLifetime),
                       MakeTimeChecker ())

        // Path-loss exponent
        .AddAttribute ("PathLossExponent",
                       "Log-distance path-loss exponent n. "
                       "Typical values: 2 (free space), 2.7 (urban 5G).",
                       DoubleValue (2.7),
                       MakeDoubleAccessor (&IacrInterferenceModel::SetPathLossExponent,
                                           &IacrInterferenceModel::GetPathLossExponent),
                       MakeDoubleChecker<double> (1.0, 6.0))

        // Path-loss constant K
        .AddAttribute ("PathLossConstant",
                       "Scalar constant K in Pr = Pt * K * d^(-n). "
                       "Absorbs antenna gains and reference-distance normalisation.",
                       DoubleValue (1e-3),
                       MakeDoubleAccessor (&IacrInterferenceModel::SetPathLossConstant,
                                           &IacrInterferenceModel::GetPathLossConstant),
                       MakeDoubleChecker<double> ())

        // Default Tx power
        .AddAttribute ("DefaultTxPower",
                       "Default transmit power used when estimating link quality "
                       "from distance only [dBm].",
                       DoubleValue (20.0),
                       MakeDoubleAccessor (&IacrInterferenceModel::SetDefaultTxPower,
                                           &IacrInterferenceModel::GetDefaultTxPower),
                       MakeDoubleChecker<double> ());

    return tid;
}

// =========================================================================
//  Constructor / Destructor
// =========================================================================

IacrInterferenceModel::IacrInterferenceModel ()
  : m_sinrThreshold      (DbToLinear (10.0)),  // 10 dB → linear ~10.0
    m_weightReceived      (0.5),
    m_weightCreated       (0.5),
    m_entryLifetime       (Seconds (10.0)),
    m_pathLossExponent    (2.7),
    m_pathLossConstant    (1e-3),
    m_defaultTxPower_dBm  (20.0),
    m_noiseFigure_dB      (7.0)
{
    NS_LOG_FUNCTION (this);
}

IacrInterferenceModel::~IacrInterferenceModel ()
{
    NS_LOG_FUNCTION (this);
}

void
IacrInterferenceModel::DoDispose (void)
{
    NS_LOG_FUNCTION (this);
    m_linkTable.clear ();
    Object::DoDispose ();
}

// =========================================================================
//  Attribute setters / getters
// =========================================================================

void
IacrInterferenceModel::SetSinrThreshold (double threshold_dB)
{
    NS_LOG_FUNCTION (this << threshold_dB);
    m_sinrThreshold = DbToLinear (threshold_dB);
}

double
IacrInterferenceModel::GetSinrThreshold (void) const
{
    return LinearToDb (m_sinrThreshold);
}

void
IacrInterferenceModel::SetReceivedInterferenceWeight (double w)
{
    NS_LOG_FUNCTION (this << w);
    NS_ASSERT_MSG (w >= 0.0 && w <= 1.0, "Weight must be in [0,1]");
    m_weightReceived = w;
}

double
IacrInterferenceModel::GetReceivedInterferenceWeight (void) const
{
    return m_weightReceived;
}

void
IacrInterferenceModel::SetCreatedInterferenceWeight (double w)
{
    NS_LOG_FUNCTION (this << w);
    NS_ASSERT_MSG (w >= 0.0 && w <= 1.0, "Weight must be in [0,1]");
    m_weightCreated = w;
}

double
IacrInterferenceModel::GetCreatedInterferenceWeight (void) const
{
    return m_weightCreated;
}

void
IacrInterferenceModel::SetEntryLifetime (Time lifetime)
{
    NS_LOG_FUNCTION (this << lifetime);
    m_entryLifetime = lifetime;
}

Time
IacrInterferenceModel::GetEntryLifetime (void) const
{
    return m_entryLifetime;
}

void
IacrInterferenceModel::SetPathLossExponent (double n)
{
    NS_LOG_FUNCTION (this << n);
    m_pathLossExponent = n;
}

double
IacrInterferenceModel::GetPathLossExponent (void) const
{
    return m_pathLossExponent;
}

void
IacrInterferenceModel::SetPathLossConstant (double K)
{
    NS_LOG_FUNCTION (this << K);
    m_pathLossConstant = K;
}

double
IacrInterferenceModel::GetPathLossConstant (void) const
{
    return m_pathLossConstant;
}

void
IacrInterferenceModel::SetDefaultTxPower (double txPower_dBm)
{
    NS_LOG_FUNCTION (this << txPower_dBm);
    m_defaultTxPower_dBm = txPower_dBm;
}

double
IacrInterferenceModel::GetDefaultTxPower (void) const
{
    return m_defaultTxPower_dBm;
}

// =========================================================================
//  Link table management
// =========================================================================

void
IacrInterferenceModel::UpdateLinkInfo (Ipv4Address  neighbor,
                                       double       rxPower_W,
                                       double       txPower_W,
                                       double       noisePower_W,
                                       double       interference_W,
                                       double       cooperationFactor)
{
    NS_LOG_FUNCTION (this << neighbor << rxPower_W << interference_W
                          << cooperationFactor);

    LinkInfo & info = m_linkTable[neighbor];

    info.neighborAddr      = neighbor;
    info.rxPower_W         = rxPower_W;
    info.txPower_W         = txPower_W;
    info.noisePower_W      = (noisePower_W > 0.0) ? noisePower_W
                                                   : ComputeNoisePower ();
    info.interference_W    = (interference_W >= 0.0) ? interference_W : 0.0;
    info.cooperationFactor = std::max (0.0, std::min (1.0, cooperationFactor));
    info.lastUpdate        = Simulator::Now ();
    info.valid             = true;

    // Recompute SINR
    double denominator = info.interference_W + info.noisePower_W;
    if (denominator <= 0.0)
    {
        denominator = info.noisePower_W;
    }
    info.sinr = (rxPower_W > 0.0) ? (rxPower_W / denominator) : 0.0;

    NS_LOG_DEBUG ("Updated link to " << neighbor
                  << "  SINR=" << LinearToDb (info.sinr) << " dB"
                  << "  CF="   << info.cooperationFactor);
}

void
IacrInterferenceModel::UpdateLinkInfoFromDistance (Ipv4Address neighbor,
                                                   double      txPower_W,
                                                   double      distance_m,
                                                   double      cooperationFactor)
{
    NS_LOG_FUNCTION (this << neighbor << distance_m);

    double rxPower_W    = EstimateRxPower (txPower_W, distance_m);
    double noisePower_W = ComputeNoisePower ();

    // When no explicit interference measurement is available, assume the
    // interference is zero (best-case).  The routing protocol should prefer
    // to call UpdateLinkInfo() with real PHY measurements when possible.
    UpdateLinkInfo (neighbor, rxPower_W, txPower_W,
                    noisePower_W, 0.0, cooperationFactor);
}

void
IacrInterferenceModel::RemoveLinkInfo (Ipv4Address neighbor)
{
    NS_LOG_FUNCTION (this << neighbor);
    m_linkTable.erase (neighbor);
}

uint32_t
IacrInterferenceModel::PurgeStaleEntries (void)
{
    NS_LOG_FUNCTION (this);
    uint32_t removed = 0;
    Time now = Simulator::Now ();

    auto it = m_linkTable.begin ();
    while (it != m_linkTable.end ())
    {
        if ((now - it->second.lastUpdate) > m_entryLifetime)
        {
            NS_LOG_DEBUG ("Purging stale link entry: " << it->first);
            it = m_linkTable.erase (it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    return removed;
}

bool
IacrInterferenceModel::HasLinkInfo (Ipv4Address neighbor) const
{
    auto it = m_linkTable.find (neighbor);
    if (it == m_linkTable.end ())
    {
        return false;
    }
    Time age = Simulator::Now () - it->second.lastUpdate;
    return (it->second.valid && age <= m_entryLifetime);
}

bool
IacrInterferenceModel::GetLinkInfo (Ipv4Address neighbor, LinkInfo & info) const
{
    auto it = m_linkTable.find (neighbor);
    if (it == m_linkTable.end ())
    {
        return false;
    }
    Time age = Simulator::Now () - it->second.lastUpdate;
    if (!it->second.valid || age > m_entryLifetime)
    {
        return false;
    }
    info = it->second;
    return true;
}

// =========================================================================
//  Metric computation
// =========================================================================

double
IacrInterferenceModel::ComputeLinkMetric (Ipv4Address neighbor) const
{
    NS_LOG_FUNCTION (this << neighbor);

    LinkInfo info;
    if (!GetLinkInfo (neighbor, info))
    {
        NS_LOG_DEBUG ("No link info for " << neighbor << " → returning INFINITY");
        return METRIC_INFINITY;
    }

    // Gate on SINR threshold — link is not viable
    if (info.sinr < m_sinrThreshold)
    {
        NS_LOG_DEBUG ("Link to " << neighbor
                      << " SINR=" << LinearToDb (info.sinr) << " dB"
                      << " below threshold " << LinearToDb (m_sinrThreshold)
                      << " dB → INFINITY");
        return METRIC_INFINITY;
    }

    // -------------------------------------------------------------------
    // Received-interference cost term
    //   f_r(j) = 1 / (1 + SINR_linear(j))
    //
    // When SINR → ∞  : f_r → 0  (perfect channel, low cost)
    // When SINR → 0  : f_r → 1  (totally jammed, high cost)
    // -------------------------------------------------------------------
    double receivedCost = 1.0 / (1.0 + info.sinr);

    // -------------------------------------------------------------------
    // Created-interference (cooperation) cost term
    //   f_c(j) = cooperationFactor(j)  ∈ [0, 1]
    //
    // cooperationFactor = 0 : node j creates zero interference → ideal relay
    // cooperationFactor = 1 : node j creates maximum interference → worst relay
    // -------------------------------------------------------------------
    double createdCost = info.cooperationFactor;

    // -------------------------------------------------------------------
    // Composite IACR metric
    //   M(i→j) = w_r * f_r(j)  +  w_c * f_c(j)
    // -------------------------------------------------------------------
    double metric = m_weightReceived * receivedCost
                  + m_weightCreated  * createdCost;

    NS_LOG_DEBUG ("Link metric to " << neighbor
                  << "  SINR=" << LinearToDb (info.sinr) << " dB"
                  << "  f_r=" << receivedCost
                  << "  f_c=" << createdCost
                  << "  M="   << metric);

    return metric;
}

bool
IacrInterferenceModel::IsLinkViable (Ipv4Address neighbor) const
{
    LinkInfo info;
    if (!GetLinkInfo (neighbor, info))
    {
        return false;
    }
    return (info.sinr >= m_sinrThreshold);
}

double
IacrInterferenceModel::AccumulatePathMetric (double currentPathMetric,
                                             double hopMetric) const
{
    // Guard against overflow / infinity propagation
    if (currentPathMetric >= METRIC_INFINITY || hopMetric >= METRIC_INFINITY)
    {
        return METRIC_INFINITY;
    }
    return currentPathMetric + hopMetric;
}

bool
IacrInterferenceModel::IsBetterPath (double metricA, double metricB) const
{
    // Lower accumulated metric = better path
    return (metricA < metricB);
}

// =========================================================================
//  Local cooperation-factor computation
// =========================================================================

double
IacrInterferenceModel::ComputeLocalCooperationFactor (void) const
{
    NS_LOG_FUNCTION (this);

    if (m_linkTable.empty ())
    {
        return 0.0;  // No neighbours → creates no interference
    }

    // Sum the ratio of (power I would deliver to each neighbour k) /
    // (that neighbour's interference + noise floor).
    // A high ratio means this node would heavily interfere with neighbour k.
    double txPower_W = DbmToWatts (m_defaultTxPower_dBm);
    double sumRatio  = 0.0;
    uint32_t N       = 0;

    Time now = Simulator::Now ();

    for (auto const & kv : m_linkTable)
    {
        const LinkInfo & info = kv.second;

        // Skip stale entries
        if (!info.valid || (now - info.lastUpdate) > m_entryLifetime)
        {
            continue;
        }

        double denom = info.interference_W + info.noisePower_W;
        if (denom <= 0.0)
        {
            denom = info.noisePower_W;
        }

        // Interference power that this node would create at neighbour k
        // is approximated as the RX power that this node's transmission
        // would deliver to k.  We reuse the same path-loss model: since
        // info.rxPower_W was computed for the k→this direction, and we
        // assume channel reciprocity, it also approximates this→k.
        double createdInterference = info.rxPower_W;

        sumRatio += (createdInterference / denom);
        ++N;
    }

    if (N == 0)
    {
        return 0.0;
    }

    double avgRatio = sumRatio / static_cast<double> (N);

    // Normalise: saturate at 1.0.
    // A node whose average created-interference/noise ratio equals 1 is
    // already introducing interference equal to the noise floor for all
    // neighbours — treat that as maximum (CF = 1).
    // Scale linearly; cap at 1.
    double cf = std::min (1.0, avgRatio);

    NS_LOG_DEBUG ("Local cooperation factor = " << cf
                  << " (averaged over " << N << " neighbours)");
    return cf;
}

// =========================================================================
//  SINR accessors
// =========================================================================

double
IacrInterferenceModel::GetSinrDb (Ipv4Address neighbor) const
{
    LinkInfo info;
    if (!GetLinkInfo (neighbor, info))
    {
        return -std::numeric_limits<double>::infinity ();
    }
    return LinearToDb (info.sinr);
}

double
IacrInterferenceModel::GetSinrLinear (Ipv4Address neighbor) const
{
    LinkInfo info;
    if (!GetLinkInfo (neighbor, info))
    {
        return 0.0;
    }
    return info.sinr;
}

// =========================================================================
//  Internal helpers
// =========================================================================

double
IacrInterferenceModel::DbToLinear (double dB)
{
    return std::pow (10.0, dB / 10.0);
}

double
IacrInterferenceModel::LinearToDb (double linear)
{
    if (linear <= 0.0)
    {
        return -std::numeric_limits<double>::infinity ();
    }
    return 10.0 * std::log10 (linear);
}

double
IacrInterferenceModel::DbmToWatts (double dBm)
{
    return 1e-3 * std::pow (10.0, dBm / 10.0);
}

double
IacrInterferenceModel::WattsToDbm (double watts)
{
    if (watts <= 0.0)
    {
        return -std::numeric_limits<double>::infinity ();
    }
    return 10.0 * std::log10 (watts / 1e-3);
}

double
IacrInterferenceModel::EstimateRxPower (double txPower_W, double distance_m) const
{
    if (distance_m <= 0.0)
    {
        return txPower_W;  // Same location: no path loss
    }
    // Pr = Pt * K * d^(-n)
    return txPower_W * m_pathLossConstant
           * std::pow (distance_m, -m_pathLossExponent);
}

double
IacrInterferenceModel::ComputeNoisePower (double bandwidth_Hz) const
{
    // Thermal noise floor: N = k * T * B * NoiseFigure
    double noiseFigureLinear = DbToLinear (m_noiseFigure_dB);
    return BOLTZMANN * TEMPERATURE_K * bandwidth_Hz * noiseFigureLinear;
}

} // namespace iacr
} // namespace ns3

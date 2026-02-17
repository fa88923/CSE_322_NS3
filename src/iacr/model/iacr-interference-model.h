/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * IACR: Interference Aware Cooperative Routing
 * for Edge Computing-enabled 5G Networks
 *
 * Implements the composite routing metric defined in:
 * "Interference Aware Cooperative Routing for Edge Computing-enabled
 *  5G Networks", IEEE Sensors Journal, 2022.
 *
 * The metric M(path) is a function of two terms:
 *
 *   M(i→j) = w_r * f_received(i,j) + w_c * f_created(i,j)
 *
 * where:
 *   f_received(i,j) = received interference penalty at node j from
 *                     all active transmitters except i (SINR-based).
 *                     A link is only valid when SINR(i→j) >= SINR_threshold.
 *
 *   f_created(i,j)  = cooperation factor of node j: the normalised
 *                     interference that j creates for its neighbours
 *                     when forwarding packets (lower is better / more
 *                     cooperative).
 *
 *   w_r, w_c        = configurable weights (default 0.5 each, must sum ≤ 1).
 *
 * Usage in iacr-routing-protocol:
 *   1. Create one IacrInterferenceModel per node (aggregated on the Node).
 *   2. Call UpdateLinkInfo() whenever a HELLO/RREQ is heard from a neighbour.
 *   3. Call ComputeLinkMetric() to score a candidate next-hop.
 *   4. Call IsLinkViable() to reject links whose SINR is below threshold.
 *   5. Accumulate metric values with AccumulatePathMetric() as a RREQ
 *      traverses hops; select the path with the lowest accumulated metric.
 */

#ifndef IACR_INTERFERENCE_MODEL_H
#define IACR_INTERFERENCE_MODEL_H

#include "ns3/object.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/traced-value.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/wifi-phy.h"
#include "ns3/mobility-model.h"

#include <map>
#include <vector>

namespace ns3 {
namespace iacr {

// =========================================================================
//  Data structures
// =========================================================================

/**
 * \brief Per-link PHY/interference snapshot.
 *
 * One of these records is maintained for every known neighbour.
 * Values are refreshed each time a HELLO or RREQ is received from
 * that neighbour (see IacrInterferenceModel::UpdateLinkInfo).
 */
struct LinkInfo
{
    Ipv4Address  neighborAddr;   ///< IP address of the neighbour node
    double       rxPower_W;      ///< Last measured received signal power [Watts]
    double       txPower_W;      ///< Neighbour's declared Tx power [Watts]
    double       noisePower_W;   ///< Thermal noise power at this receiver [Watts]
    double       interference_W; ///< Aggregate interference from *other* transmitters [Watts]
    double       sinr;           ///< Computed SINR = rxPower / (interference + noise)
    double       cooperationFactor; ///< Normalised created-interference score [0,1]; lower = more cooperative
    Time         lastUpdate;     ///< Simulation time of last refresh
    bool         valid;          ///< True when the record holds fresh data

    LinkInfo ()
      : rxPower_W (0.0),
        txPower_W (0.0),
        noisePower_W (1e-13),   // ~-100 dBm default thermal noise
        interference_W (0.0),
        sinr (0.0),
        cooperationFactor (1.0),
        lastUpdate (Seconds (0.0)),
        valid (false)
    {}
};

// =========================================================================
//  IacrInterferenceModel
// =========================================================================

/**
 * \ingroup iacr
 * \brief Interference-Aware Cooperative Routing metric engine.
 *
 * This class is the single point of truth for all interference and
 * cooperation calculations needed by the IACR routing protocol.
 * It is designed to be aggregated onto an ns3::Node and accessed by
 * IacrRoutingProtocol via GetObject<IacrInterferenceModel>().
 *
 * \par Metric definition
 * For a candidate link i → j the composite metric is:
 *
 *   M(i→j) = w_r * ReceivedInterferenceCost(j)
 *           + w_c * CreatedInterferenceCost(j)
 *
 * where
 *   ReceivedInterferenceCost(j) = 1 / (1 + SINR(j))
 *       (maps SINR ∈ [0,∞) → cost ∈ (0,1]; lower SINR = higher cost)
 *
 *   CreatedInterferenceCost(j)  = cooperationFactor(j)
 *       (pre-computed normalised score carried in HELLO/RREQ; [0,1])
 *
 * A link is considered *viable* only when SINR(j) >= m_sinrThreshold.
 * The path metric accumulated across hops should be minimised during
 * route selection.
 */
class IacrInterferenceModel : public Object
{
public:
    // -----------------------------------------------------------------
    //  ns-3 TypeId / lifecycle
    // -----------------------------------------------------------------

    /**
     * \brief Get the TypeId for this class.
     * \return The TypeId for IacrInterferenceModel.
     */
    static TypeId GetTypeId (void);

    IacrInterferenceModel ();
    virtual ~IacrInterferenceModel ();

    // -----------------------------------------------------------------
    //  Configuration (also exposed as ns-3 Attributes)
    // -----------------------------------------------------------------

    /**
     * \brief Set the SINR threshold below which a link is considered broken.
     * \param threshold_dB  SINR threshold in dB (default 10 dB).
     */
    void SetSinrThreshold (double threshold_dB);

    /**
     * \brief Get the configured SINR threshold in dB.
     */
    double GetSinrThreshold (void) const;

    /**
     * \brief Set the weight applied to the received-interference cost term.
     * \param w  Weight in [0,1].  w_r + w_c should be <= 1.
     */
    void SetReceivedInterferenceWeight (double w);

    /**
     * \brief Get the received-interference weight.
     */
    double GetReceivedInterferenceWeight (void) const;

    /**
     * \brief Set the weight applied to the created-interference (cooperation) term.
     * \param w  Weight in [0,1].  w_r + w_c should be <= 1.
     */
    void SetCreatedInterferenceWeight (double w);

    /**
     * \brief Get the created-interference weight.
     */
    double GetCreatedInterferenceWeight (void) const;

    /**
     * \brief Set the entry lifetime for link records.
     * \param lifetime  Entries older than this are considered stale.
     */
    void SetEntryLifetime (Time lifetime);

    /**
     * \brief Get the entry lifetime.
     */
    Time GetEntryLifetime (void) const;

    // -----------------------------------------------------------------
    //  Link table management
    // -----------------------------------------------------------------

    /**
     * \brief Update (or insert) the link-info record for a neighbour.
     *
     * Called by IacrRoutingProtocol whenever it receives a HELLO or RREQ
     * carrying PHY measurements from the sender.
     *
     * \param neighbor        IPv4 address of the transmitting neighbour.
     * \param rxPower_W       Received signal power [W] as measured here.
     * \param txPower_W       Transmit power [W] declared by the neighbour.
     * \param noisePower_W    Thermal noise floor [W] at this node.
     * \param interference_W  Aggregate interference [W] from all *other*
     *                        concurrent transmitters observed at this node.
     * \param cooperationFactor  Normalised created-interference score [0,1]
     *                           as declared by the neighbour in its beacon.
     */
    void UpdateLinkInfo (Ipv4Address neighbor,
                         double rxPower_W,
                         double txPower_W,
                         double noisePower_W,
                         double interference_W,
                         double cooperationFactor);

    /**
     * \brief Convenience overload: derive rxPower from path loss model.
     *
     * When explicit power measurements are unavailable (e.g. in simpler
     * simulation setups), the received power is estimated using the
     * log-distance path loss model:
     *   Pr [W] = Pt * K * d^(-n)
     * where K is a path-loss constant, d is the inter-node distance, and
     * n is the path-loss exponent.
     *
     * \param neighbor          IPv4 address of the neighbour.
     * \param txPower_W         Transmit power [W] of the neighbour.
     * \param distance_m        Euclidean distance to the neighbour [m].
     * \param cooperationFactor Normalised cooperation score of the neighbour.
     */
    void UpdateLinkInfoFromDistance (Ipv4Address neighbor,
                                     double txPower_W,
                                     double distance_m,
                                     double cooperationFactor);

    /**
     * \brief Remove a neighbour's link record (e.g. on link break detection).
     * \param neighbor  IPv4 address of the neighbour to purge.
     */
    void RemoveLinkInfo (Ipv4Address neighbor);

    /**
     * \brief Remove all stale link records older than GetEntryLifetime().
     * \return Number of records removed.
     */
    uint32_t PurgeStaleEntries (void);

    /**
     * \brief Check whether a link record exists and is fresh.
     * \param neighbor  IPv4 address.
     * \return True if the record exists and is not stale.
     */
    bool HasLinkInfo (Ipv4Address neighbor) const;

    /**
     * \brief Retrieve a copy of the link record for a neighbour.
     * \param neighbor  IPv4 address.
     * \param info [out]  Populated on success.
     * \return True if a fresh record was found.
     */
    bool GetLinkInfo (Ipv4Address neighbor, LinkInfo & info) const;

    // -----------------------------------------------------------------
    //  Metric computation
    // -----------------------------------------------------------------

    /**
     * \brief Compute the IACR composite link metric for a candidate next-hop.
     *
     * M(i→j) = w_r * ReceivedInterferenceCost(j)
     *         + w_c * CreatedInterferenceCost(j)
     *
     * ReceivedInterferenceCost(j) = 1 / (1 + SINR_linear(j))
     * CreatedInterferenceCost(j)  = cooperationFactor(j)   (already in [0,1])
     *
     * \param neighbor  IPv4 address of the candidate next-hop.
     * \return Metric value in (0, 1].  Returns infinity (very large double)
     *         if no link info is available or the link is not viable.
     */
    double ComputeLinkMetric (Ipv4Address neighbor) const;

    /**
     * \brief Check whether a link meets the minimum SINR requirement.
     *
     * \param neighbor  IPv4 address of the candidate next-hop.
     * \return True if SINR(neighbor) >= m_sinrThreshold (linear).
     *         Returns false if no link info exists for this neighbor.
     */
    bool IsLinkViable (Ipv4Address neighbor) const;

    /**
     * \brief Accumulate a per-hop metric into a running path metric.
     *
     * The path metric is the *sum* of per-link metrics along the route
     * (consistent with how hop count is accumulated in standard AODV).
     * Route selection should pick the path with the *lowest* accumulated
     * path metric.
     *
     * \param currentPathMetric  Running path metric so far.
     * \param hopMetric          Metric of the next hop to add.
     * \return Updated (accumulated) path metric.
     */
    double AccumulatePathMetric (double currentPathMetric,
                                 double hopMetric) const;

    /**
     * \brief Compare two accumulated path metrics.
     *
     * \param metricA  Metric of path A.
     * \param metricB  Metric of path B.
     * \return True if path A is *better* (lower metric) than path B.
     */
    bool IsBetterPath (double metricA, double metricB) const;

    // -----------------------------------------------------------------
    //  Local cooperation-factor computation
    // -----------------------------------------------------------------

    /**
     * \brief Compute *this* node's cooperation factor to advertise in beacons.
     *
     * The cooperation factor quantifies how much interference this node
     * creates for its current neighbours when it forwards a packet.
     * It is computed as:
     *
     *   CF = (1/N) * sum_{k in neighbours} [ Pt * K * d(this,k)^(-n)
     *                                        / (I_k + N_k) ]
     *
     * normalised to [0, 1] where 1 means maximum interference (least
     * cooperative) and 0 means zero interference (fully cooperative).
     *
     * \return Cooperation factor in [0, 1].
     */
    double ComputeLocalCooperationFactor (void) const;

    /**
     * \brief Return the SINR for a specific neighbour in dB.
     * \param neighbor  IPv4 address of the neighbour.
     * \return SINR in dB, or -infinity if no record exists.
     */
    double GetSinrDb (Ipv4Address neighbor) const;

    /**
     * \brief Return the SINR for a specific neighbour in linear scale.
     * \param neighbor  IPv4 address of the neighbour.
     * \return SINR (linear), or 0.0 if no record exists.
     */
    double GetSinrLinear (Ipv4Address neighbor) const;

    // -----------------------------------------------------------------
    //  Physical layer parameters (path-loss model)
    // -----------------------------------------------------------------

    /**
     * \brief Set the path-loss exponent n (default 2.7 for urban 5G).
     */
    void SetPathLossExponent (double n);

    /**
     * \brief Get the path-loss exponent.
     */
    double GetPathLossExponent (void) const;

    /**
     * \brief Set the path-loss constant K (unit-less, default 1e-3).
     *
     * In the simplified log-distance model, Pr = Pt * K * d^(-n).
     * K encapsulates antenna gains, frequency-dependent free-space loss,
     * and a reference-distance normalisation.
     */
    void SetPathLossConstant (double K);

    /**
     * \brief Get the path-loss constant K.
     */
    double GetPathLossConstant (void) const;

    /**
     * \brief Set the default transmit power used for path-loss estimates.
     * \param txPower_dBm  Transmit power in dBm (default 20 dBm).
     */
    void SetDefaultTxPower (double txPower_dBm);

    /**
     * \brief Get the default transmit power in dBm.
     */
    double GetDefaultTxPower (void) const;

protected:
    virtual void DoDispose (void) override;

private:
    // -----------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------

    /** Convert dB value to linear ratio. */
    static double DbToLinear (double dB);

    /** Convert linear ratio to dB. */
    static double LinearToDb (double linear);

    /** Convert dBm to Watts. */
    static double DbmToWatts (double dBm);

    /** Convert Watts to dBm. */
    static double WattsToDbm (double watts);

    /**
     * \brief Estimate received power [W] using simplified log-distance model.
     * \param txPower_W   Transmit power in Watts.
     * \param distance_m  Distance in metres.
     */
    double EstimateRxPower (double txPower_W, double distance_m) const;

    /**
     * \brief Compute thermal noise power [W] given bandwidth.
     * \param bandwidth_Hz  Channel bandwidth in Hz (default 100 MHz for 5G NR).
     * \return Noise power in Watts (kTB * noise_figure).
     */
    double ComputeNoisePower (double bandwidth_Hz = 100e6) const;

    // -----------------------------------------------------------------
    //  Member variables
    // -----------------------------------------------------------------

    /// Map from neighbour IP → link snapshot record
    std::map<Ipv4Address, LinkInfo> m_linkTable;

    /// SINR threshold (linear scale) below which a link is declared broken
    double m_sinrThreshold;

    /// Weight for received-interference cost term
    double m_weightReceived;

    /// Weight for created-interference (cooperation) cost term
    double m_weightCreated;

    /// Stale-entry lifetime
    Time m_entryLifetime;

    /// Log-distance path-loss exponent
    double m_pathLossExponent;

    /// Log-distance path-loss constant K
    double m_pathLossConstant;

    /// Default Tx power [dBm] used in path-loss estimates
    double m_defaultTxPower_dBm;

    /// Receiver noise figure [dB] (default 7 dB, typical 5G NR device)
    double m_noiseFigure_dB;

    /// A very large metric value representing an unusable / unknown path
    static const double METRIC_INFINITY;
};

} // namespace iacr
} // namespace ns3

#endif /* IACR_INTERFERENCE_MODEL_H */

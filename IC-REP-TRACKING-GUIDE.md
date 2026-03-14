# IC-REP Packet Tracking and ICP Entry Monitoring Guide

## Overview

The enhanced AODV comprehensive test now includes comprehensive tracking of IC-REP (Information Collection Reply) packets and ICP (Information Collection Protocol) entry updates. This monitoring system allows you to analyze interference levels and protocol behavior at both the packet and neighbor-relationship levels.

## Features Added

### 1. **Trace Sources in AODV Routing Protocol**

Three new trace sources have been added to `/src/aodv/model/aodv-routing-protocol.h`:

- **`m_icRepSentTrace`** - Fired when an IC-REP packet is sent
  - Parameters: `source_node`, `dest_node`, `received_power (W)`, `interference (W)`
  
- **`m_icRepRecvTrace`** - Fired when an IC-REP packet is received
  - Parameters: `source_node`, `dest_node`, `received_power (W)`, `interference (W)`
  
- **`m_icpEntryUpdateTrace`** - Fired when an ICP entry is updated
  - Parameters: `node_addr`, `neighbor_addr`, `created_interference (W)`, `received_interference (W)`, `aggregate_interference (W)`

### 2. **Data Structures for Tracking**

Two new structures in `aodv-comprehensive-test.cc`:

```cpp
struct IcRepPacket
{
    Ipv4Address source;
    Ipv4Address destination;
    double receivedPower;
    double interference;
    Time timestamp;
};

struct IcpEntrySnapshot
{
    Ipv4Address nodeAddr;
    Ipv4Address neighborAddr;
    double createdInterference;      // I_c^j
    double receivedInterference;     // I_j
    double aggregateInterference;    // I_aggr
    Time timestamp;
};
```

### 3. **Output Files**

The test now generates three additional log files:

#### a. **Detailed Log** (`aodv-comprehensive-<nNodes>nodes-detailed.txt`)

Comprehensive section: **IC-REP AND ICP ANALYSIS**

Includes:
- Total IC-REP packets sent and received
- Average received power and interference levels
- IC-REP packet counts by sender node
- Total ICP entry updates
- Average interference metrics (I_c, I_j, I_aggr)
- ICP updates breakdown by node
- ICP updates breakdown by node-neighbor pair

#### b. **IC-REP Log** (`aodv-icrep-<nNodes>nodes.log`)

Real-time log of all IC-REP packets:
```
Format: Time(s) | Event Type | From IP | To IP | RxPower(W) | Interference(W)
```

Example entries (when IC-REP packets are transmitted):
```
0.5s: [IC-REP SENT] From: 10.0.0.1 To: 10.0.0.2 RxPower: 0.00125 W Interference: 0.00095 W
0.51s: [IC-REP RECV] From: 10.0.0.1 To: 10.0.0.2 RxPower: 0.00125 W Interference: 0.00095 W
```

#### c. **ICP Entry Log** (`aodv-icp-entries-<nNodes>nodes.log`)

Real-time log of ICP table updates:
```
Format: Time(s) | Node | Neighbor | I_c(W) | I_j(W) | I_aggr(W)
```

Where:
- **I_c**: Interference created by this node at the neighbor
- **I_j**: Total interference received by this node
- **I_aggr**: Aggregate interference at this node

Example entries:
```
0.52s: [ICP UPDATE] Node: 10.0.0.2 Neighbor: 10.0.0.1 I_c: 0.00125 W I_j: 0.00095 W I_aggr: 0.00220 W
```

## Using the IC-REP Tracking

### Running the Test

```bash
cd /home/asus/ns-3-dev
./ns3 run "scratch/aodv-comprehensive-test --nNodes=30 --simTime=20"
```

### Parameters

- `--nNodes=N` - Number of nodes (default: 30)
- `--simTime=T` - Simulation time in seconds (default: 30)
- `--areaSize=S` - Area size in meters (default: 300)
- `--nFlows=F` - Number of concurrent flows (default: 5)
- `--txPower=P` - TX power in dBm (default: 20)

### Output Files Generated

For a run with 30 nodes:

1. `aodv-comprehensive-30nodes-detailed.txt` - Main detailed results
2. `aodv-comprehensive-30nodes-latency.csv` - Per-packet latency data
3. `aodv-icrep-30nodes.log` - All IC-REP packets (NEW)
4. `aodv-icp-entries-30nodes.log` - All ICP entry updates (NEW)
5. `aodv-comprehensive-results.csv` - Summary statistics

## Analyzing IC-REP Packets

### 1. Count IC-REP Activity

```bash
# Count total IC-REP packets sent
grep "\[IC-REP SENT\]" aodv-icrep-30nodes.log | wc -l

# Count IC-REP packets received
grep "\[IC-REP RECV\]" aodv-icrep-30nodes.log | wc -l
```

### 2. Analyze Interference Levels

```bash
# Extract and analyze interference data
grep "\[IC-REP" aodv-icrep-30nodes.log | awk -F'[: ]' '{print $(NF-1)}' | sort -n | uniq -c
```

### 3. Identify High-Interference Neighborhoods

```bash
# Find nodes sending IC-REP packets with high interference
grep "Interference:" aodv-icrep-30nodes.log | awk '{print $(NF-4), $(NF-3), $(NF-1), $NF}' | sort -k3 -rn | head -20
```

## Analyzing ICP Entry Updates

### 1. Track Interference Evolution

```bash
# See how interference changes over time for a specific node pair
grep "10.0.0.2.*10.0.0.1" aodv-icp-entries-30nodes.log
```

### 2. Calculate Average Interference per Neighbor

```bash
# Extract I_c values and compute statistics
grep "\[ICP UPDATE\]" aodv-icp-entries-30nodes.log | awk -F'I_c: ' '{print $2}' | awk '{print $1}' | \
  awk '{sum+=$1; count++; if(NR==1 || $1>max) max=$1; if(NR==1 || $1<min) min=$1} 
        END {print "Avg:", sum/count, "Min:", min, "Max:", max}'
```

### 3. Identify Most Active Neighbors

```bash
# Count ICP updates per neighbor pair
grep "\[ICP UPDATE\]" aodv-icp-entries-30nodes.log | awk -F'Neighbor: ' '{print $2}' | awk '{print $1}' | sort | uniq -c | sort -rn
```

## Understanding Interference Metrics

The IC-REP tracking provides three key interference measurements:

### In IC-REP Packets:
- **ReceivedPower**: Power received by the sender at its own location (indicates how much interference this node creates at others)
- **Interference**: Total interference experienced by the sending node

### In ICP Entry Updates:
- **I_c (Created Interference)**: How much interference this node creates at a specific neighbor
  - High values indicate this node is a major source of interference for that neighbor
  
- **I_j (Received Interference)**: Total interference received by this node
  - High values indicate a congested environment
  
- **I_aggr (Aggregate Interference)**: Overall aggregate interference level
  - Used for routing decisions in interference-aware protocols

## Troubleshooting

### No IC-REP Packets Captured

This is normal in the default AODV configuration if:
- IC-REQ/IC-REP mechanism is not actively triggered
- Nodes haven't established enough routes with neighbors
- Simulation time is too short

To increase IC-REP activity:
- Increase `--simTime` parameter
- Increase `--nNodes` parameter
- Increase `--nFlows` parameter for more traffic

### Empty Log Files

If IC-REP and ICP log files contain only headers:
1. Check that trace sources are properly connected in the test
2. Verify that the AODV SendIcReply() and RecvIcReply() methods are being called
3. Check the detailed log for trace connection confirmations

## Implementation Details

### Files Modified

1. **`src/aodv/model/aodv-routing-protocol.h`**
   - Added three `TracedCallback` members
   - Added method documentation

2. **`src/aodv/model/aodv-routing-protocol.cc`**
   - Added trace fire calls in `SendIcReply()` method
   - Added trace fire calls in `RecvIcReply()` method

3. **`scratch/aodv-comprehensive-test.cc`**
   - Added `IcRepPacket` and `IcpEntrySnapshot` structures
   - Added global tracking vectors and maps
   - Added callback functions for trace sources
   - Added trace connection setup code
   - Added detailed statistics output

### Callback Signatures

```cpp
// IC-REP Sent callback
void IcRepSentCallback(Ipv4Address source, Ipv4Address destination, 
                       double receivedPower, double interference);

// IC-REP Received callback
void IcRepRecvCallback(Ipv4Address source, Ipv4Address destination, 
                       double receivedPower, double interference);

// ICP Entry Update callback
void IcpEntryUpdateCallback(Ipv4Address nodeAddr, Ipv4Address neighborAddr,
                            double createdInterference, double receivedInterference,
                            double aggregateInterference);
```

## Next Steps

### Extending IC-REP Tracking

1. **Custom Filters**: Modify callbacks to filter by time range, node address, or interference threshold
2. **Export to CSV**: Convert log data to CSV for analysis in spreadsheet/plotting tools
3. **Real-time Visualization**: Integrate with visualization tools to show interference heatmaps
4. **Machine Learning**: Feed interference data into ML models for anomaly detection

### Enhancing the Protocol

1. **Interference-based Routing**: Use ICP data to make better routing decisions
2. **Link Quality Estimation**: Combine IC-REP data with existing metrics
3. **Congestion Avoidance**: Use interference levels to detect and avoid congested paths

## References

- AODV RFC 3561: https://tools.ietf.org/html/rfc3561
- NS-3 AODV Module: https://www.nsnam.org/docs/
- Interference-Aware Routing: Research papers on IACR and interference metrics

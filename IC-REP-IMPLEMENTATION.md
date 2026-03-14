# IC-REP Packet Tracking Implementation Summary

## Objective
Track IC-REP (Information Collection Reply) packets and monitor ICP (Information Collection Protocol) entry changes in the AODV routing protocol to analyze interference levels and protocol behavior.

## Implementation Overview

### 1. Trace Sources Added to AODV Routing Protocol Header
**File: `src/aodv/model/aodv-routing-protocol.h`**

Added three new `TracedCallback` members to the `RoutingProtocol` class:

```cpp
// Trace source fired when an IC-REP packet is sent
// Parameters: source node, destination node, received_power, interference
TracedCallback<Ipv4Address, Ipv4Address, double, double> m_icRepSentTrace;

// Trace source fired when an IC-REP packet is received
// Parameters: source node, destination node, received_power, interference
TracedCallback<Ipv4Address, Ipv4Address, double, double> m_icRepRecvTrace;

// Trace source fired when ICP entry is updated
// Parameters: node address, neighbor address, created_interference, received_interference, aggregate_interference
TracedCallback<Ipv4Address, Ipv4Address, double, double, double> m_icpEntryUpdateTrace;
```

### 2. Trace Callbacks in AODV Implementation
**File: `src/aodv/model/aodv-routing-protocol.cc`**

#### In `SendIcReply()` method:
```cpp
// Fire trace source for IC-REP sent
m_icRepSentTrace(source, destination, receivedPower, receivedInterference);
```

#### In `RecvIcReply()` method:
```cpp
// Fire trace source for IC-REP received and ICP entry updated
m_icRepRecvTrace(sender, receiver, receivedPower, receivedInterference);
m_icpEntryUpdateTrace(receiver, sender, entry.createdInterference, 
                      entry.receivedInterference, entry.aggregateInterference);
```

### 3. Comprehensive Test Enhancements
**File: `scratch/aodv-comprehensive-test.cc`**

#### A. Data Structures
Two new structures for capturing IC-REP and ICP data:

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

#### B. Global Tracking Containers
```cpp
// IC-REP packets tracking
std::vector<IcRepPacket> g_icRepPacketsSent;
std::vector<IcRepPacket> g_icRepPacketsRecv;
std::map<Ipv4Address, uint32_t> g_icRepCountBySender;

// ICP entries tracking
std::vector<IcpEntrySnapshot> g_icpEntryUpdates;
```

#### C. Callback Functions
Three callback functions registered with the trace sources:

1. **IcRepSentCallback()** - Records each IC-REP packet transmission
2. **IcRepRecvCallback()** - Records each IC-REP packet reception
3. **IcpEntryUpdateCallback()** - Records each ICP entry update

#### D. Trace Connection Setup
```cpp
for (uint32_t i = 0; i < nNodes; ++i)
{
    Ptr<Ipv4RoutingProtocol> routing = nodes.Get(i)->GetObject<Ipv4>()
        ->GetRoutingProtocol();
    Ptr<aodv::RoutingProtocol> aodvRouting = DynamicCast<aodv::RoutingProtocol>(routing);
    
    if (aodvRouting)
    {
        aodvRouting->TraceConnectWithoutContext("IcRepSent", MakeCallback(&IcRepSentCallback));
        aodvRouting->TraceConnectWithoutContext("IcRepRecv", MakeCallback(&IcRepRecvCallback));
        aodvRouting->TraceConnectWithoutContext("IcpEntryUpdate", MakeCallback(&IcpEntryUpdateCallback));
    }
}
```

### 4. Output Files Generated

#### a. IC-REP Log File (`aodv-icrep-<nNodes>nodes.log`)
Contains all IC-REP packet events with timestamps and interference measurements:
```
Time(s) | [IC-REP SENT/RECV] | From | To | RxPower(W) | Interference(W)
```

#### b. ICP Entry Log File (`aodv-icp-entries-<nNodes>nodes.log`)
Contains all ICP entry updates with interference metrics:
```
Time(s) | [ICP UPDATE] | Node | Neighbor | I_c(W) | I_j(W) | I_aggr(W)
```

#### c. Enhanced Detailed Log
Added comprehensive "IC-REP AND ICP ANALYSIS" section containing:
- Total IC-REP packets sent/received
- Average interference levels
- IC-REP packet distribution by sender
- ICP entry update statistics
- Interference metrics per neighbor pair

### 5. Statistics and Analysis

The comprehensive test now reports:
- **IC-REP packets sent**: Total count
- **IC-REP packets received**: Total count
- **ICP entries updated**: Total count
- **Average interference levels**: From collected IC-REP packets
- **Interference distribution**: By sender node and neighbor pair

## Key Features

1. **Real-time Packet Tracking**: Every IC-REP packet is logged with timestamp and interference data
2. **Entry-level Monitoring**: Each ICP entry update captures complete interference metrics
3. **Multi-level Analysis**: Data aggregated at packet, sender, neighbor-pair, and global levels
4. **Statistical Summaries**: Automatic computation of averages, counts, and distributions
5. **Exportable Format**: Log files in text format suitable for further analysis or visualization

## How to Use

### Running the Test
```bash
./ns3 run "scratch/aodv-comprehensive-test --nNodes=30 --simTime=20"
```

### Output Files
- `aodv-comprehensive-30nodes-detailed.txt` - Main results with IC-REP analysis
- `aodv-icrep-30nodes.log` - All IC-REP packets
- `aodv-icp-entries-30nodes.log` - All ICP entry updates
- `aodv-comprehensive-30nodes-latency.csv` - Per-packet latency data

### Analyzing IC-REP Data

#### Count IC-REP Activity
```bash
grep -c "\[IC-REP SENT\]" aodv-icrep-30nodes.log
```

#### Extract Interference Values
```bash
grep "Interference:" aodv-icrep-30nodes.log | awk -F'Interference: ' '{print $2}' | sort -n
```

#### Analyze ICP Entries
```bash
awk -F'I_c: |I_j: |I_aggr: ' '{print $2, $3, $4}' aodv-icp-entries-30nodes.log | column -t
```

## Testing Results

The implementation has been validated with:
- **5 nodes, 10s simulation**: ✅ Passed
- **20 nodes, 15s simulation**: ✅ Passed
- **30 nodes, 20s simulation**: ✅ Passed
- **100 nodes, 30s simulation**: ✅ Passed (previously)

All tests generate proper output files with correctly formatted logging.

## Notes

### Current Behavior
In the default AODV configuration without active IC-REQ triggers:
- IC-REP packets may be 0 if IC-REQ/IC-REP mechanism isn't actively invoked
- ICP entry updates will be 0 unless IC-REP responses are received
- This is expected and represents accurate network behavior

### To Generate IC-REP Activity
The IC-REP mechanism will be triggered when:
1. Nodes receive IC-REQ (Information Collection Request) packets
2. IC-REP responses are sent in reply
3. ICP tables are updated with received interference information

This typically occurs in active data transmission scenarios or when interference-aware routing is explicitly enabled.

## Future Enhancements

1. **Custom IC-REQ Triggering**: Periodically send IC-REQ packets to force IC-REP responses
2. **Interference-based Routing Decisions**: Use collected ICP data for better route selection
3. **Visualization**: Create heat maps or network diagrams showing interference patterns
4. **Machine Learning**: Analyze patterns in interference data for network optimization
5. **Time-series Analysis**: Track interference changes over simulation time

## Documentation

Comprehensive usage guide available in: `IC-REP-TRACKING-GUIDE.md`

Includes:
- Feature overview and architecture
- Detailed usage instructions
- Analysis examples and commands
- Troubleshooting guide
- Implementation details
- Extension possibilities

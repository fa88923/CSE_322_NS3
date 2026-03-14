# AODV Routing Table Logging

## Overview

The AODV comprehensive test now automatically creates detailed routing table change logs whenever you run the simulation. This includes all route creation events, route updates, metric changes, and hop count information.

## Usage

### Method 1: Using the Bash Script (Recommended)

The easiest way to run the simulation with routing table logging is to use the provided bash script:

```bash
./run-with-routing-logs.sh <nNodes> [areaSize] [simTime]
```

**Examples:**

```bash
# Run with 5 nodes (default 300m area, 13s simulation time)
./run-with-routing-logs.sh 5

# Run with 30 nodes
./run-with-routing-logs.sh 30

# Run with 100 nodes
./run-with-routing-logs.sh 100

# Run with custom parameters
./run-with-routing-logs.sh 30 300 13
```

### Method 2: Using NS_LOG Environment Variable

You can also run the simulation directly with NS_LOG enabled:

```bash
NS_LOG="AodvRoutingTable=logic:AodvRoutingProtocol=info" \
  ./ns3 run "scratch/aodv-comprehensive-test --nNodes=30" 2>&1 | \
  tee aodv-routing-table-30nodes.log
```

### Method 3: Automatic Logging

The test file automatically opens a routing table log file. Just run the test normally:

```bash
./ns3 run "scratch/aodv-comprehensive-test --nNodes=30"
```

This will create: `aodv-routing-table-30nodes.log`

## Output Files

When you run the test, the following files are created:

| File | Description |
|------|-------------|
| `aodv-comprehensive-{N}nodes-detailed.txt` | Comprehensive test results with all metrics |
| `aodv-comprehensive-{N}nodes-latency.csv` | Per-packet latency data |
| `aodv-icrep-{N}nodes.log` | IC-REP packet information (power and interference) |
| `aodv-icp-entries-{N}nodes.log` | ICP entry updates (interference measurements) |
| **`aodv-routing-table-{N}nodes.log`** | **Routing table changes with metrics and hops** |
| `aodv-comprehensive-results.csv` | Summary statistics (appended) |

## Routing Table Log Format

### Route Creation Events

```
ROUTE CREATED: Dst=10.1.1.4 | NextHop=10.1.1.4 | Hops=1 | Metric=inf | SeqNo=0 | Lifetime=3s | Flag=VALID
```

**Fields:**
- `Dst`: Destination IP address
- `NextHop`: Next hop router IP
- `Hops`: Number of hops to destination
- `Metric`: Route metric (interference-based or hop count)
- `SeqNo`: Sequence number
- `Lifetime`: Route lifetime in seconds
- `Flag`: Route state (VALID, INVALID, IN_SEARCH)

### Route Update Events

```
ROUTE UPDATE: Dst=10.1.1.4 | OldMetric=inf | NewMetric=1.47762e-11 | OldHops=1 | NewHops=1 | OldNextHop=10.1.1.4 | NewNextHop=10.1.1.4 | NewSeqNo=0 | NewLifetime=3s
```

**Fields:**
- `OldMetric` / `NewMetric`: Metric before and after update
- `OldHops` / `NewHops`: Hop count before and after
- `OldNextHop` / `NewNextHop`: Next hop before and after
- Shows when routes improve or degrade

### Metric Updates from IC-REP

```
METRIC UPDATE from IC-REP: Neighbor 10.1.1.4 | OldMetric=inf | NewMetric=1.47762e-11 | CreatedInterf=2.95525e-11 | ReceivedInterf=0
```

**Fields:**
- `Neighbor`: The neighbor that was updated
- `OldMetric` / `NewMetric`: Metric values
- `CreatedInterf`: Interference this node creates at the neighbor (W)
- `ReceivedInterf`: Total interference received by the node (W)

## Analyzing the Logs

### Count unique metric values:

```bash
strings aodv-routing-table-100nodes.log | grep -o "Metric=[0-9.e-]*" | sort -u | wc -l
```

### Find all route creations:

```bash
strings aodv-routing-table-30nodes.log | grep "ROUTE CREATED"
```

### Find metric updates:

```bash
strings aodv-routing-table-30nodes.log | grep "ROUTE UPDATE"
```

### Find IC-REP triggered metric updates:

```bash
strings aodv-routing-table-30nodes.log | grep "METRIC UPDATE from IC-REP"
```

### Find all changes to a specific destination:

```bash
strings aodv-routing-table-30nodes.log | grep "Dst=10.1.1.50"
```

### Show metric progression for a destination:

```bash
strings aodv-routing-table-30nodes.log | grep "Dst=10.1.1.50" | grep -E "Metric|NewMetric"
```

## Log Content Example

```
AODV Routing Table Changes Log for 5 nodes
========================================
Routing Table Entry Creation and Updates
Metrics and hop counts logged for every route change
--------
Route to 10.1.1.1 not found
Route to 10.1.1.1 not found
ROUTE CREATED: Dst=10.1.1.1 | NextHop=10.1.1.1 | Hops=1 | Metric=inf | SeqNo=0 | Lifetime=3s | Flag=VALID
Route to 10.1.1.1 found
ROUTE UPDATE: Dst=10.1.1.1 | OldMetric=inf | NewMetric=0 | OldHops=1 | NewHops=1 | OldNextHop=10.1.1.1 | NewNextHop=10.1.1.1 | NewSeqNo=0 | NewLifetime=3s
METRIC UPDATE from IC-REP: Neighbor 10.1.1.4 | OldMetric=inf | NewMetric=1.47762e-11 | CreatedInterf=2.95525e-11 | ReceivedInterf=0
```

## Understanding Metrics

### Metric Values

- **inf** (infinity): Route doesn't exist or is invalid
- **0**: Initial metric before IC-REP data arrives
- **~1e-11 to 1e-09**: Interference-based metric calculated from IC-REP data
  - Calculated as: `0.5 * CreatedInterference + 0.5 * ReceivedInterference`
  - Lower metrics are better (less interference)
  - Higher variety indicates dynamic routing conditions

### Metric Changes

When you see a metric change from `inf → 0 → numerical_value`, this indicates:

1. Route created with unknown metric (inf)
2. IC-REP data received, metric becomes 0
3. IC-REP data received again with interference measurements, metric becomes numerical

## Tips

1. **Large files**: Routing table logs can be large (~400KB for 5 nodes). Use `strings` command and `grep` to filter
2. **Real-time analysis**: Use `tail -f aodv-routing-table-Nnodes.log` to watch in real-time
3. **Metric analysis**: Focus on "METRIC UPDATE from IC-REP" lines to see IC-REP effectiveness
4. **Hop count stability**: Look at whether NewHops changes frequently (route instability)
5. **Metric diversity**: More unique metric values = more dynamic routing decisions based on interference

## Related Files

- **IC-REP Logs**: `aodv-icrep-{N}nodes.log` - Raw IC-REP packet data
- **ICP Logs**: `aodv-icp-entries-{N}nodes.log` - Interference measurements by node
- **Detailed Results**: `aodv-comprehensive-{N}nodes-detailed.txt` - Complete performance metrics

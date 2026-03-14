#!/bin/bash

# Script to run aodv-comprehensive-test with CLEAN routing table logging
# This properly separates NS_LOG output from simulation output

if [ $# -eq 0 ]; then
    echo "Usage: $0 <nNodes> [areaSize] [simTime]"
    echo ""
    echo "Examples:"
    echo "  $0 5                    # Run with 5 nodes"
    echo "  $0 30                   # Run with 30 nodes"
    echo "  $0 100                  # Run with 100 nodes"
    exit 1
fi

NNODES=$1
AREA=${2:-300}
TIME=${3:-13}

echo "Running aodv-comprehensive-test with CLEAN routing table logging..."
echo "  Nodes: $NNODES"
echo "  Area: ${AREA}m x ${AREA}m"
echo "  Simulation time: ${TIME}s"
echo ""

# Create temp files for separation
ROUTING_LOG="aodv-routing-table-${NNODES}nodes-raw.log"
SIMULATION_OUTPUT="aodv-sim-output-${NNODES}nodes-temp.txt"

# Run the test with NS_LOG separated
# Redirect NS_LOG to stdout (file descriptor 1), simulation output to stderr (file descriptor 2)
# Then split them
NS_LOG="AodvRoutingTable=logic:AodvRoutingProtocol=info" \
    ./ns3 run "scratch/aodv-comprehensive-test --nNodes=$NNODES --areaSize=$AREA --simTime=$TIME" \
    2>"$SIMULATION_OUTPUT" 1>"$ROUTING_LOG"

# Filter the routing log to remove non-text content and keep only relevant lines
echo "Cleaning routing table log..."
strings "$ROUTING_LOG" | grep -E "ROUTE|METRIC|ICP Table" > "aodv-routing-table-${NNODES}nodes.log"

# Show stats
echo ""
echo "=========================================="
ORIGINAL_SIZE=$(du -h "$ROUTING_LOG" | cut -f1)
CLEANED_SIZE=$(du -h "aodv-routing-table-${NNODES}nodes.log" | cut -f1)
ORIGINAL_LINES=$(wc -l < "$ROUTING_LOG" 2>/dev/null || echo "N/A")
CLEANED_LINES=$(wc -l < "aodv-routing-table-${NNODES}nodes.log")

echo "Log Cleaning Results:"
echo "  Original: $ORIGINAL_SIZE ($ORIGINAL_LINES lines)"
echo "  Cleaned:  $CLEANED_SIZE ($CLEANED_LINES lines)"
echo "=========================================="

# Clean up raw log
rm -f "$ROUTING_LOG"

echo ""
echo "Simulation Results (from stdout):"
grep -E "RESULTS SUMMARY|Throughput|Outage|Delay|Jitter|Hop Count|IC-REP|Output files:" "$SIMULATION_OUTPUT" || true

echo ""
echo "Simulation complete!"
echo ""
echo "Output files created:"
echo "  - aodv-comprehensive-${NNODES}nodes-detailed.txt (detailed results)"
echo "  - aodv-comprehensive-${NNODES}nodes-latency.csv (latency data)"
echo "  - aodv-icrep-${NNODES}nodes.log (IC-REP packets)"
echo "  - aodv-icp-entries-${NNODES}nodes.log (ICP entries)"
echo "  - aodv-routing-table-${NNODES}nodes.log (ROUTING TABLE CHANGES - CLEAN TEXT)"
echo "  - aodv-comprehensive-results.csv (summary results)"
echo ""
echo "Clean routing log info:"
echo "  Size: $CLEANED_SIZE"
echo "  Lines: $CLEANED_LINES"
echo ""
echo "To analyze the routing log:"
echo "  - View all route creations:      grep 'ROUTE CREATED' aodv-routing-table-${NNODES}nodes.log"
echo "  - View all route updates:        grep 'ROUTE UPDATE' aodv-routing-table-${NNODES}nodes.log"
echo "  - View IC-REP metric updates:    grep 'METRIC UPDATE' aodv-routing-table-${NNODES}nodes.log"
echo "  - Count unique metrics:          grep -o 'Metric=[^ ]*' aodv-routing-table-${NNODES}nodes.log | sort -u | wc -l"
echo "  - View changes for dest X:       grep 'Dst=10.1.1.X' aodv-routing-table-${NNODES}nodes.log"

rm -f "$SIMULATION_OUTPUT"

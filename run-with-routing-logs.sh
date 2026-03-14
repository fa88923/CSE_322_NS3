#!/bin/bash

# Script to run aodv-comprehensive-test with routing table logging enabled
# This captures all AODV routing table changes to a log file

if [ $# -eq 0 ]; then
    echo "Usage: $0 <nNodes> [areaSize] [simTime]"
    echo ""
    echo "Examples:"
    echo "  $0 5                    # Run with 5 nodes"
    echo "  $0 30                   # Run with 30 nodes"
    echo "  $0 100                  # Run with 100 nodes"
    echo "  $0 30 300 13            # Run with 30 nodes, 300m area, 13s sim time"
    exit 1
fi

NNODES=$1
AREA=${2:-300}
TIME=${3:-13}

echo "Running aodv-comprehensive-test with routing table logging..."
echo "  Nodes: $NNODES"
echo "  Area: ${AREA}m x ${AREA}m"
echo "  Simulation time: ${TIME}s"
echo ""

# Run the test with NS_LOG enabled and capture all output
# AodvRoutingTable=logic for routing table changes
# AodvRoutingProtocol=info for metric updates from IC-REP
NS_LOG="AodvRoutingTable=logic:AodvRoutingProtocol=info" \
    ./ns3 run "scratch/aodv-comprehensive-test --nNodes=$NNODES --areaSize=$AREA --simTime=$TIME" \
    2>&1 | tee "aodv-routing-table-${NNODES}nodes.log"

echo ""
echo "Simulation complete!"
echo "Output files created:"
echo "  - aodv-comprehensive-${NNODES}nodes-detailed.txt (detailed results)"
echo "  - aodv-comprehensive-${NNODES}nodes-latency.csv (latency data)"
echo "  - aodv-icrep-${NNODES}nodes.log (IC-REP packets)"
echo "  - aodv-icp-entries-${NNODES}nodes.log (ICP entries)"
echo "  - aodv-routing-table-${NNODES}nodes.log (ROUTING TABLE CHANGES)"
echo "  - aodv-comprehensive-results.csv (summary results)"

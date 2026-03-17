#!/bin/bash

# Comprehensive AODV Performance Test Suite
# Tests all metrics including latency for baseline comparison

echo "=========================================="
echo "AODV COMPREHENSIVE PERFORMANCE TEST SUITE"
echo "=========================================="
echo ""
echo "This will measure:"
echo "  1. Normalized Throughput"
echo "  2. Outage Probability"
echo "  3. End-to-End Latency (avg, std dev, jitter, percentiles)"
echo "  4. Route Discovery Latency"
echo "  5. Hop Count Statistics"
echo "  6. Control Overhead"
echo "  7. Energy Consumption"
echo "  8. Route Stability"
echo ""

# Clear previous results
rm -f aodv-comprehensive-results.csv
rm -f aodv-comprehensive-*nodes-seed*-detailed.txt
rm -f aodv-comprehensive-*nodes-seed*-latency.csv

# Write CSV header
echo "nNodes,Seed,NormalizedThroughput,OutageProbability,AvgDelayMs,StdDevDelayMs,JitterMs,P95DelayMs,P99DelayMs,ThroughputKbps,EnergyMJ,AvgHopCount,ControlOverhead,RouteDiscoveryMs,LinkBreaks" > aodv-comprehensive-results.csv

# Node counts to test (matching paper)
#NODE_COUNTS=(100 120 140 160 180 200)

#NODE_COUNTS=(210 220  230 240 250)
# NODE_COUNTS=(100 120 140 160 180)
#NODE_COUNTS=(100 120 140 160 180 200 210 220 230 240 250)
FLOW_COUNTS=(25 30 35 40 45 50 55 60)
SEEDS=(1 2 3 4 5)

n=60
# Run simulation for each node count and each seed
for f in "${FLOW_COUNTS[@]}"
do
    for seed in "${SEEDS[@]}"
    do
        echo "=========================================="
        echo "Running simulation with $f flows, seed $seed..."
        echo "=========================================="
        
        # Capture NS_LOG output directly to a file (not to console)
        NS_LOG="AodvRoutingTable=logic:AodvRoutingProtocol=info" \
            ./ns3 run "scratch/aodv-comprehensive-test --nNodes=$n --simTime=13.0 --nFlows=$f --RngRun=$seed" \
            > /dev/null 2> "aodv-routing-table-${f}flows-seed${seed}-raw.log"
        
        # Clean the routing log to remove binary/corrupted content and write directly to final file
        if [ -f "aodv-routing-table-${f}flows-seed${seed}-raw.log" ]; then
            echo "Cleaning routing table log for $f flows, seed $seed..."
            strings "aodv-routing-table-${f}flows-seed${seed}-raw.log" | \
                grep -E "ROUTE|METRIC|ICP Table updated" > "aodv-routing-table-${f}flows-seed${seed}.log"
            rm -f "aodv-routing-table-${f}flows-seed${seed}-raw.log"
            
            CLEANED_LINES=$(wc -l < "aodv-routing-table-${f}flows-seed${seed}.log")
            echo "✓ Routing log cleaned: $CLEANED_LINES lines"
        fi
        
        if [ $? -eq 0 ]; then
            echo "✓ Simulation completed successfully for $f flows, seed $seed"
            echo ""
        else
            echo "✗ Simulation failed for $f flows, seed $seed"
            echo ""
            exit 1
        fi
        
        # Small delay between runs
        sleep 1
    done
done

echo "=========================================="
echo "ALL SIMULATIONS COMPLETE!"
echo "=========================================="
echo ""
echo "Results saved to:"
echo "  - aodv-comprehensive-results.csv"
echo "  - aodv-comprehensive-<N>nodes-seed<S>-detailed.txt"
echo "  - aodv-comprehensive-<N>nodes-seed<S>-latency.csv"
echo "  - aodv-routing-table-<N>nodes-seed<S>.log (clean routing table changes)"
echo "  - aodv-icrep-<N>nodes.log (IC-REP packets)"
echo "  - aodv-icp-entries-<N>nodes.log (ICP entries)"
echo ""
echo "To analyze routing logs:"
echo "  grep 'ROUTE CREATED' aodv-routing-table-<N>nodes-seed<S>.log"
echo "  grep 'ROUTE UPDATE' aodv-routing-table-<N>nodes-seed<S>.log"
echo "  grep 'METRIC UPDATE' aodv-routing-table-<N>nodes-seed<S>.log"
echo ""
echo "To analyze results:"
echo "  python3 plot-comprehensive-results.py"
echo ""
echo "To compare latency distributions:"
echo "  python3 analyze-latency.py"
echo ""

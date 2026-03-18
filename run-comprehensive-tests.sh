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
echo "nNodes,nFlows,Seed,NormalizedThroughput,OutageProbability,AvgDelayMs,AvgEnergyMJ,TotalEnergyMJ" > aodv-comprehensive-results.csv

# Node counts to test (matching paper)
#NODE_COUNTS=(100 120 140 160 180 200)

#NODE_COUNTS=(210 220  230 240 250)
# NODE_COUNTS=(100 120 140 160 180)
#NODE_COUNTS=(100 120 140 160 180 200 210 220 230 240 250)
FLOW_COUNTS=(25 30 35 40 45 50 55 60)
# FLOW_COUNTS=(60)
#SEEDS=(1 2 3 4 5)
SEEDS=(1)

n=60
# Run simulation for each node count and each seed
for f in "${FLOW_COUNTS[@]}"
do
    for seed in "${SEEDS[@]}"
    do
        echo "=========================================="
        echo "Running simulation with $f flows, seed $seed, nodes $f..."
        echo "=========================================="
        
        # Set up log filename with proper naming: nodes-flows-seed pattern
        LOG_FILE="aodv-comprehensive-${f}nodes-${f}flows-seed${seed}-logs.log"
        
        # Run simulation with AODV protocol logging enabled
        export NS_LOG="AodvRoutingProtocol=all:*=prefix_time:*=prefix_node:*=prefix_func:*=prefix_level"
        ./ns3 run "scratch/aodv-comprehensive-test --nNodes=$f --simTime=13.0 --nFlows=$f --RngRun=$seed" 2> "$LOG_FILE"
        EXIT_CODE=$?
        unset NS_LOG
        
        if [ $EXIT_CODE -eq 0 ]; then
            LINES=$(wc -l < "$LOG_FILE")
            echo "✓ Simulation completed successfully for $f flows, seed $seed"
            echo "  Log file: $LOG_FILE ($LINES lines)"
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
echo ""
echo "To enable detailed AODV protocol logging in future runs:"
echo "  export NS_LOG='AodvRoutingProtocol=all:Aodv=all:*=prefix_time:*=prefix_node:*=prefix_func'"
echo "  ./ns3 run 'scratch/aodv-comprehensive-test ...' 2> aodv-logs.log"
echo ""
echo "To analyze results:"
echo "  python3 plot-comprehensive-results.py"
echo ""
echo "To compare latency distributions:"
echo "  python3 analyze-latency.py"
echo ""

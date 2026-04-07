#!/usr/bin/env bash
# run_sweep.sh
#
# Runs the paired 5-point AODV simulation sweep in one invocation.
# Each sweep index defines one full configuration:
#   index 0 -> nNodes=20,  nFlows=10, pps=100, areaMultiplier=1
#   index 1 -> nNodes=40,  nFlows=20, pps=200, areaMultiplier=2
#   index 2 -> nNodes=60,  nFlows=30, pps=300, areaMultiplier=3
#   index 3 -> nNodes=80,  nFlows=40, pps=400, areaMultiplier=4
#   index 4 -> nNodes=100, nFlows=50, pps=500, areaMultiplier=5
#
# Usage:
#   bash run_sweep.sh [wifi|lrwpan|both]
#
# Output:
#   results/results_wifi.csv
#   results/results_lrwpan.csv
#
# Each CSV row format (matching the "CSV," prefix in the simulation output):
#   channelType,nNodes,nFlows,pps,areaMultiplier,pktSize,
#   throughput_kbps,delay_s,pdr,dropRatio,totalEnergy_J,avgNodeEnergy_J,avgPerNodeTput_kbps

set -euo pipefail

CHANNEL_ARG="${1:-both}"
NS3_BIN="./ns3"
SCRATCH="scratch/aodv_simulation"
OUTDIR="results"
mkdir -p "$OUTDIR"

# ---- Paired sweep values ----
NODE_VALS=(20 40 60 80 100)
FLOW_VALS=(10 20 30 40 50)
PPS_VALS=(100 200 300 400 500)
AREA_VALS=(1 2 3 4 5)

PKT_SIZE=64
SIM_TIME=50
SEED=12345

CSV_HEADER="channelType,nNodes,nFlows,pps,areaMultiplier,pktSize,throughput_kbps,delay_s,pdr,dropRatio,totalEnergy_J,avgNodeEnergy_J,avgPerNodeTput_kbps"

run_sim() {
    local ch=$1 nn=$2 nf=$3 pp=$4 am=$5

    local output
    local rc=0
    output=$("$NS3_BIN" run "$SCRATCH" -- \
        --channelType="$ch" \
        --nNodes="$nn" \
        --nFlows="$nf" \
        --pps="$pp" \
        --areaMultiplier="$am" \
        --pktSize="$PKT_SIZE" \
        --simTime="$SIM_TIME" \
        --seed="$SEED" \
        2>&1) || rc=$?

    local row
    row=$(printf '%s\n' "$output" | grep '^CSV,' | sed 's/^CSV,//' || true)
    if [ -n "$row" ]; then
        if [ "$rc" -ne 0 ]; then
            echo "Simulation returned exit code $rc after emitting CSV for channel=$ch nNodes=$nn nFlows=$nf pps=$pp areaMultiplier=$am" >&2
        fi
        printf '%s\n' "$row"
        return 0
    fi

    if [ "$rc" -ne 0 ]; then
        echo "Simulation failed for channel=$ch nNodes=$nn nFlows=$nf pps=$pp areaMultiplier=$am" >&2
        echo "$output" >&2
        return "$rc"
    fi

    echo "Simulation completed but no CSV row found for channel=$ch nNodes=$nn nFlows=$nf pps=$pp areaMultiplier=$am" >&2
    echo "$output" >&2
    return 1
}

init_csv() {
    local outfile=$1
    echo "$CSV_HEADER" > "$outfile"
}

run_channel() {
    local ch=$1
    local outfile="$OUTDIR/results_${ch}.csv"

    init_csv "$outfile"

    echo ">>> Channel: $ch"
    echo ""

    for i in "${!NODE_VALS[@]}"; do
        local nn="${NODE_VALS[$i]}"
        local nf="${FLOW_VALS[$i]}"
        local pp="${PPS_VALS[$i]}"
        local am="${AREA_VALS[$i]}"

        echo -n "  Sweep $((i + 1))/5 -> nNodes=$nn, nFlows=$nf, pps=$pp, areaMultiplier=$am ... "
        row=$(run_sim "$ch" "$nn" "$nf" "$pp" "$am")
        echo "$row" >> "$outfile"
        echo "done"
    done

    echo "  Saved: $outfile"
    echo ""
}

case "$CHANNEL_ARG" in
    wifi)
        run_channel "wifi"
        ;;
    lrwpan)
        run_channel "lrwpan"
        ;;
    both)
        run_channel "wifi"
        run_channel "lrwpan"
        ;;
    *)
        echo "Usage: bash run_sweep.sh [wifi|lrwpan|both]"
        exit 1
        ;;
esac

echo "=== Sweep complete ==="
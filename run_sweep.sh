#!/usr/bin/env bash


set -euo pipefail

CHANNEL_ARG="${1:-both}"
NS3_BIN="./ns3"
SCRATCH="scratch/aodv_simulation"
OUTDIR="results"
mkdir -p "$OUTDIR"

# ---- Sweep values ----
NODE_VALS=(20 40 60 80 100)
FLOW_VALS=(10 20 30 40 50)
PPS_VALS=(100 200 300 400 500)

# NODE_VALS=()
# FLOW_VALS=()
# PPS_VALS=()
AREA_VALS=(1 2 3 4 5)
PKT_SIZE=80
SIM_TIME=50
SEED=12345

# ---- Baseline values (held fixed when varying another parameter) ----
BASE_NODES=60
BASE_FLOWS=20
BASE_PPS=200
BASE_AREA=3

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

    if [ -z "$row" ]; then
        echo "Simulation completed but no CSV row found for channel=$ch nNodes=$nn nFlows=$nf pps=$pp areaMultiplier=$am" >&2
        echo "$output" >&2
        return 1
    fi
}

init_csv() {
    local outfile=$1
    echo "$CSV_HEADER" > "$outfile"
}

run_channel() {
    local ch=$1

    local nodes_file="$OUTDIR/results_${ch}_nodes.csv"
    local flows_file="$OUTDIR/results_${ch}_flows.csv"
    local pps_file="$OUTDIR/results_${ch}_pps.csv"
    local area_file="$OUTDIR/results_${ch}_area.csv"

    init_csv "$nodes_file"
    init_csv "$flows_file"
    init_csv "$pps_file"
    init_csv "$area_file"

    echo ">>> Channel: $ch"
    echo ""

    echo "  [1/4] Varying nNodes -> $nodes_file"
    for nn in "${NODE_VALS[@]}"; do
        echo -n "    nNodes=$nn ... "
        row=$(run_sim "$ch" "$nn" "$BASE_FLOWS" "$BASE_PPS" "$BASE_AREA")
        echo "$row" >> "$nodes_file"
        echo "done"
    done

    echo "  [2/4] Varying nFlows -> $flows_file"
    for nf in "${FLOW_VALS[@]}"; do
        if [ "$nf" -gt "$BASE_NODES" ]; then
            echo "    nFlows=$nf ... skipped (nFlows > BASE_NODES=$BASE_NODES)"
            continue
        fi
        echo -n "    nFlows=$nf ... "
        row=$(run_sim "$ch" "$BASE_NODES" "$nf" "$BASE_PPS" "$BASE_AREA")
        echo "$row" >> "$flows_file"
        echo "done"
    done

    echo "  [3/4] Varying PPS -> $pps_file"
    for pp in "${PPS_VALS[@]}"; do
        echo -n "    pps=$pp ... "
        row=$(run_sim "$ch" "$BASE_NODES" "$BASE_FLOWS" "$pp" "$BASE_AREA")
        echo "$row" >> "$pps_file"
        echo "done"
    done

    echo "  [4/4] Varying areaMultiplier -> $area_file"
    for am in "${AREA_VALS[@]}"; do
        echo -n "    areaMultiplier=$am ... "
        row=$(run_sim "$ch" "$BASE_NODES" "$BASE_FLOWS" "$BASE_PPS" "$am")
        echo "$row" >> "$area_file"
        echo "done"
    done

    echo "  Saved: $nodes_file"
    echo "  Saved: $flows_file"
    echo "  Saved: $pps_file"
    echo "  Saved: $area_file"
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
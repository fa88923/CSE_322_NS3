#!/usr/bin/env bash

set -euo pipefail

CHANNEL_ARG="${1:-lrwpan}"
NS3_BIN="./ns3"
SCRATCH="scratch/aodv_simulation"
OUTDIR="results"
mkdir -p "$OUTDIR"

# ---- Sweep values ----
NODE_VALS=(20 40 60 80 100)
FLOW_VALS=(10 20 30 40 50)
PPS_VALS=(100 200 300 400 500)
AREA_VALS=(1 2 3 4 5)

# ---- Baseline values (held fixed when varying another parameter) ----
BASE_NODES=60
BASE_FLOWS=20
BASE_PPS=200
BASE_AREA=3

CSV_HEADER="nNodes,nFlows,nPktPerSec,areaMultiplier,throughput_kbps,delay_ms,pdr_percent,dropRatio_percent,avgNodeEnergy_J"

run_sim() {
    local nn=$1 nf=$2 pp=$3 am=$4

    local output
    local rc=0
    output=$("$NS3_BIN" run "$SCRATCH" -- \
        --nNodes="$nn" \
        --nFlows="$nf" \
        --nPktPerSec="$pp" \
        --areaMultiplier="$am" \
        2>&1) || rc=$?

    # Extract metrics from human-readable output
    local throughput delay pdr dropRatio energy
    throughput=$(printf '%s\n' "$output" | grep "Throughput" | sed 's/.*: \([^ ]*\).*/\1/')
    delay=$(printf '%s\n' "$output" | grep "Delay" | sed 's/.*: \([^ ]*\).*/\1/')
    pdr=$(printf '%s\n' "$output" | grep "PDR" | sed 's/.*: \([^ ]*\).*/\1/')
    dropRatio=$(printf '%s\n' "$output" | grep "Drop Ratio" | sed 's/.*: \([^ ]*\).*/\1/')
    energy=$(printf '%s\n' "$output" | grep "Avg Energy" | sed 's/.*: \([^ ]*\).*/\1/')

    if [ -z "$throughput" ] || [ -z "$delay" ] || [ -z "$pdr" ]; then
        echo "Failed to parse output for nNodes=$nn nFlows=$nf nPktPerSec=$pp areaMultiplier=$am" >&2
        echo "$output" >&2
        return 1
    fi

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$nn" "$nf" "$pp" "$am" \
        "$throughput" "$delay" "$pdr" "$dropRatio" "$energy"
}

init_csv() {
    local outfile=$1
    echo "$CSV_HEADER" > "$outfile"
}

run_channel() {
    local nodes_file="$OUTDIR/results_lrwpan_nodes.csv"
    local flows_file="$OUTDIR/results_lrwpan_flows.csv"
    local pps_file="$OUTDIR/results_lrwpan_pps.csv"
    local area_file="$OUTDIR/results_lrwpan_area.csv"

    init_csv "$nodes_file"
    init_csv "$flows_file"
    init_csv "$pps_file"
    init_csv "$area_file"

    echo ">>> LR-WPAN Static Network Sweep"
    echo ""

    echo "  [1/4] Varying nNodes -> $nodes_file"
    for nn in "${NODE_VALS[@]}"; do
        echo -n "    nNodes=$nn ... "
        if row=$(run_sim "$nn" "$BASE_FLOWS" "$BASE_PPS" "$BASE_AREA"); then
            echo "$row" >> "$nodes_file"
            echo "done"
        else
            echo "failed"
            return 1
        fi
    done

    echo "  [2/4] Varying nFlows -> $flows_file"
    for nf in "${FLOW_VALS[@]}"; do
        if [ "$nf" -gt "$BASE_NODES" ]; then
            echo "    nFlows=$nf ... skipped (nFlows > BASE_NODES=$BASE_NODES)"
            continue
        fi
        echo -n "    nFlows=$nf ... "
        if row=$(run_sim "$BASE_NODES" "$nf" "$BASE_PPS" "$BASE_AREA"); then
            echo "$row" >> "$flows_file"
            echo "done"
        else
            echo "failed"
            return 1
        fi
    done

    echo "  [3/4] Varying nPktPerSec (PPS) -> $pps_file"
    for pp in "${PPS_VALS[@]}"; do
        echo -n "    pps=$pp ... "
        if row=$(run_sim "$BASE_NODES" "$BASE_FLOWS" "$pp" "$BASE_AREA"); then
            echo "$row" >> "$pps_file"
            echo "done"
        else
            echo "failed"
            return 1
        fi
    done

    echo "  [4/4] Varying areaMultiplier -> $area_file"
    for am in "${AREA_VALS[@]}"; do
        echo -n "    areaMultiplier=$am ... "
        if row=$(run_sim "$BASE_NODES" "$BASE_FLOWS" "$BASE_PPS" "$am"); then
            echo "$row" >> "$area_file"
            echo "done"
        else
            echo "failed"
            return 1
        fi
    done

    echo ""
    echo "  Saved: $nodes_file"
    echo "  Saved: $flows_file"
    echo "  Saved: $pps_file"
    echo "  Saved: $area_file"
    echo ""
}

case "$CHANNEL_ARG" in
    lrwpan)
        run_channel
        ;;
    *)
        echo "Usage: bash run_sweep.sh [lrwpan]"
        exit 1
        ;;
esac

echo "=== Sweep complete ==="
# AODV Protocol Logging Guide

## Overview

This guide explains how to enable detailed AODV routing protocol logging in the ns-3 simulations.

## Quick Start

To enable AODV protocol logging when running simulations:

```bash
export NS_LOG="AodvRoutingProtocol=all:*=prefix_time:*=prefix_node:*=prefix_func:*=prefix_level"
./ns3 run "scratch/aodv-comprehensive-test --nNodes=60 --simTime=13.0 --nFlows=25 --RngRun=1" 2> aodv-logs.log
```

## Log Output

- **stderr (2)**: Captured to the log file
- **stdout (1)**: Simulation console output (metrics, summaries)

## Log Format

Each log line includes:
- **Time**: Simulation time in seconds (with `prefix_time`)
- **Node**: Source node ID (with `prefix_node`)
- **Function**: Function name in AODV code (with `prefix_func`)
- **Component**: AODV component name (AodvRoutingProtocol, etc.)
- **Message**: The log message

### Example Log Lines

```
AodvRoutingProtocol:NotifyAddAddress(...) at +0.000000000s node 0
AodvRoutingProtocol:SendInitialRequest(...) at +3.100000000s node 5
AodvRoutingProtocol:RecvReply(...) at +3.105000000s node 3
```

## Available Components

- `AodvRoutingProtocol`: Main AODV routing protocol logic
- `Aodv`: Additional AODV sub-components

## Log Levels

- `all`: All log messages (most verbose)
- `debug`: Debug-level messages
- `info`: Informational messages
- `warn`: Warning messages
- `error`: Error messages

## Log Prefixes

- `prefix_time`: Include simulation time
- `prefix_node`: Include node ID
- `prefix_func`: Include function name
- `prefix_level`: Include log level

## Analyzing Logs

### Filter for route discoveries
```bash
grep "SendInitialRequest\|RecvReply" aodv-logs.log
```

### Filter for routing decisions
```bash
grep "RouteFound\|SendPacket" aodv-logs.log
```

### Count log entries by component
```bash
grep "AodvRoutingProtocol" aodv-logs.log | wc -l
```

### View logs for specific node
```bash
grep "node X" aodv-logs.log
```

## Performance Impact

⚠️ **Warning**: Enabling extensive logging significantly slows down simulations.
- Log file size: ~28 MB per 25-node, 2-second simulation
- Runtime overhead: Can increase simulation time by 2-5x

For production runs, use logging selectively or post-process results instead.

## Examples

### Minimal logging (route discoveries only)
```bash
export NS_LOG="AodvRoutingProtocol=info:*=prefix_time:*=prefix_node"
```

### Full protocol analysis
```bash
export NS_LOG="AodvRoutingProtocol=all:Aodv=all:*=prefix_time:*=prefix_node:*=prefix_func"
```

### Silent running (no logs)
```bash
unset NS_LOG
./ns3 run "scratch/aodv-comprehensive-test ..."
```


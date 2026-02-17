# IACR_2 Module - AODV-Based Implementation

IACR_2 is an Interference Aware Cooperative Routing protocol implementation for ns-3 that extends AODV with interference-aware metrics.

## Overview

Based on paper: arXiv:2201.01520v1 - "Interference Aware Cooperative Routing for Edge Computing-enabled 5G Networks"

This module implements:
- **Algorithm 1**: Information Collection Phase - gather interference information from neighbors
- **Algorithm 2**: Route Establishment Phase - select optimal paths based on interference metrics
- **Key Equations**:
  - Eq. 1: Path Loss Model
  - Eq. 2: Received Interference Calculation
  - Eq. 6: Path Metric (interference-aware)
  - Eq. 7: Created Interference

## Key Features

- **Inherits from AODV**: Reuses proven route discovery and maintenance mechanisms
- **Interference Metrics**: Tracks created and received interference per neighbor
- **Cooperative Routing**: Considers neighbor interference in route selection
- **Extensible Design**: Easy to modify interference calculations and parameters

## Architecture

### IACR Packet Types

1. **ICREQ** (Type 10): Information Collection Request
   - Gathers neighbor interference info during route discovery

2. **ICREP** (Type 11): Information Collection Reply
   - Response with interference metrics

3. **HELLO** (Type 12): Beacon message
   - Periodic neighbor advertisement with current interference

### Classes

- `RoutingProtocol`: Main routing protocol extending `aodv::RoutingProtocol`
- `IcreqHeader`: Information collection request header
- `IcrepHeader`: Information collection reply header
- `HelloHeader`: Hello beacon header
- `IacrHelper`: Helper class for easy protocol installation

## Usage

```cpp
#include "iacr-helper.h"

// Create helper
iacr::IacrHelper iacrHelper;

// Set IACR parameters
iacrHelper.Set("DeltaParameter", DoubleValue(0.1));

// Install on nodes
Ipv4ListRoutingHelper list;
list.Add(staticRouting, 0);
list.Add(iacrHelper, 10);

internet.SetRoutingHelper(list);
internet.Install(nodes);
```

## Compilation

```bash
cd /home/asus/ns-3-dev
./ns3 build
```

## Example

```bash
./ns3 run iacr-example
```

## Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| DeltaParameter | double | 0.1 | Metric smoothing parameter (Δ in Eq. 6) |
| PathLossExponent | double | 2.0 | Path loss model exponent |
| TransmitPower | double | 20.0 dBm | Node transmit power |
| HelloInterval | ms | 1000 | Hello beacon interval |

## Implementation Notes

- Built on AODV's proven route discovery mechanism
- Adds interference calculation layer on top of AODV
- Compatible with existing ns-3 routing infrastructure
- Minimal overhead compared to standalone IACR implementation

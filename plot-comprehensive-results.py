#!/usr/bin/env python3
"""
Comprehensive AODV Performance Analysis and Plotting
Generates all comparison graphs for AODV baseline
"""

import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
import sys
import os

def plot_comprehensive_results(csv_file):
    """Generate comprehensive performance plots"""
    
    if not os.path.exists(csv_file):
        print(f"Error: {csv_file} not found!")
        sys.exit(1)
    
    df = pd.read_csv(csv_file)
    print("Data loaded successfully")
    print(df)
    print()
    
    # Create comprehensive figure
    fig = plt.figure(figsize=(18, 12))
    gs = fig.add_gridspec(3, 3, hspace=0.3, wspace=0.3)
    
    fig.suptitle('AODV Comprehensive Performance Analysis (Baseline for Comparison)', 
                 fontsize=16, fontweight='bold')
    
    # ============================================================
    # ROW 1: Core Metrics from Paper
    # ============================================================
    
    # 1. Normalized Throughput (Paper Figure 2)
    ax1 = fig.add_subplot(gs[0, 0])
    ax1.plot(df['nNodes'], df['NormalizedThroughput'], 
             marker='s', color='red', linewidth=2, markersize=8, label='Standard AODV')
    ax1.set_xlabel('Number of nodes', fontsize=11)
    ax1.set_ylabel('Normalized Throughput (τ̂)', fontsize=11)
    ax1.set_title('Throughput (Paper Fig 2)', fontweight='bold')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    ax1.set_ylim([0, 0.8])
    
    # 2. Outage Probability (Paper Figure 3)
    ax2 = fig.add_subplot(gs[0, 1])
    ax2.plot(df['nNodes'], df['OutageProbability'], 
             marker='s', color='red', linewidth=2, markersize=8, label='Standard AODV')
    ax2.set_xlabel('Number of nodes', fontsize=11)
    ax2.set_ylabel('P(γ ≤ γth)', fontsize=11)
    ax2.set_title('Outage Probability (Paper Fig 3)', fontweight='bold')
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    ax2.set_ylim([0, 1.0])
    
    # 3. Energy Consumption (Paper Figure 5)
    ax3 = fig.add_subplot(gs[0, 2])
    ax3.plot(df['nNodes'], df['EnergyMJ'], 
             marker='s', color='red', linewidth=2, markersize=8, label='Standard AODV')
    ax3.set_xlabel('Number of nodes', fontsize=11)
    ax3.set_ylabel('Energy Consumption (mJ)', fontsize=11)
    ax3.set_title('Energy (Paper Fig 5)', fontweight='bold')
    ax3.grid(True, alpha=0.3)
    ax3.legend()
    
    # ============================================================
    # ROW 2: Latency Metrics (NEW - For IACR Comparison)
    # ============================================================
    
    # 4. Average End-to-End Delay
    ax4 = fig.add_subplot(gs[1, 0])
    ax4.plot(df['nNodes'], df['AvgDelayMs'], 
             marker='o', color='blue', linewidth=2, markersize=8, label='Avg Delay')
    ax4.fill_between(df['nNodes'], 
                     df['AvgDelayMs'] - df['StdDevDelayMs'], 
                     df['AvgDelayMs'] + df['StdDevDelayMs'], 
                     alpha=0.2, color='blue')
    ax4.set_xlabel('Number of nodes', fontsize=11)
    ax4.set_ylabel('Delay (ms)', fontsize=11)
    ax4.set_title('End-to-End Latency (Mean ± Std Dev)', fontweight='bold')
    ax4.grid(True, alpha=0.3)
    ax4.legend()
    
    # 5. Latency Percentiles
    ax5 = fig.add_subplot(gs[1, 1])
    ax5.plot(df['nNodes'], df['AvgDelayMs'], 
             marker='o', color='blue', linewidth=2, markersize=6, label='Mean')
    ax5.plot(df['nNodes'], df['P95DelayMs'], 
             marker='^', color='orange', linewidth=2, markersize=6, label='95th percentile')
    ax5.plot(df['nNodes'], df['P99DelayMs'], 
             marker='v', color='red', linewidth=2, markersize=6, label='99th percentile')
    ax5.set_xlabel('Number of nodes', fontsize=11)
    ax5.set_ylabel('Delay (ms)', fontsize=11)
    ax5.set_title('Latency Distribution', fontweight='bold')
    ax5.grid(True, alpha=0.3)
    ax5.legend()
    
    # 6. Jitter
    ax6 = fig.add_subplot(gs[1, 2])
    ax6.plot(df['nNodes'], df['JitterMs'], 
             marker='d', color='purple', linewidth=2, markersize=8, label='Jitter')
    ax6.set_xlabel('Number of nodes', fontsize=11)
    ax6.set_ylabel('Jitter (ms)', fontsize=11)
    ax6.set_title('Delay Variation (Jitter)', fontweight='bold')
    ax6.grid(True, alpha=0.3)
    ax6.legend()
    
    # ============================================================
    # ROW 3: Routing Metrics (NEW - For IACR-HC Comparison)
    # ============================================================
    
    # 7. Average Hop Count
    ax7 = fig.add_subplot(gs[2, 0])
    ax7.plot(df['nNodes'], df['AvgHopCount'], 
             marker='h', color='green', linewidth=2, markersize=8, label='Avg Hops')
    ax7.set_xlabel('Number of nodes', fontsize=11)
    ax7.set_ylabel('Average Hop Count', fontsize=11)
    ax7.set_title('Path Length (Hop Count)', fontweight='bold')
    ax7.grid(True, alpha=0.3)
    ax7.legend()
    
    # 8. Route Discovery Latency
    ax8 = fig.add_subplot(gs[2, 1])
    ax8.plot(df['nNodes'], df['RouteDiscoveryMs'], 
             marker='*', color='brown', linewidth=2, markersize=10, label='Discovery Time')
    ax8.set_xlabel('Number of nodes', fontsize=11)
    ax8.set_ylabel('Discovery Time (ms)', fontsize=11)
    ax8.set_title('Route Discovery Latency', fontweight='bold')
    ax8.grid(True, alpha=0.3)
    ax8.legend()
    
    # 9. Control Overhead
    ax9 = fig.add_subplot(gs[2, 2])
    ax9.plot(df['nNodes'], df['ControlOverhead'], 
             marker='s', color='gray', linewidth=2, markersize=8, label='Control Overhead')
    ax9.plot(df['nNodes'], df['LinkBreaks'] / df['nNodes'], 
             marker='x', color='red', linewidth=2, markersize=8, label='Link Breaks/Node')
    ax9.set_xlabel('Number of nodes', fontsize=11)
    ax9.set_ylabel('Ratio / Count', fontsize=11)
    ax9.set_title('Routing Overhead & Stability', fontweight='bold')
    ax9.grid(True, alpha=0.3)
    ax9.legend()
    
    plt.tight_layout()
    output_file = 'aodv-comprehensive-results.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"✓ Plot saved: {output_file}\n")
    plt.show()
    
    # ========================================================================
    # SUMMARY TABLES
    # ========================================================================
    
    print("\n" + "="*120)
    print("COMPREHENSIVE SUMMARY TABLE")
    print("="*120)
    print(f"{'Nodes':<6} {'Throughput':<11} {'Outage':<8} {'AvgDelay':<10} {'Jitter':<9} "
          f"{'AvgHops':<9} {'Discovery':<11} {'Control':<9} {'Energy':<10}")
    print(f"{'':6} {'(τ̂)':<11} {'(Pout)':<8} {'(ms)':<10} {'(ms)':<9} "
          f"{'':9} {'(ms)':<11} {'Overhead':<9} {'(mJ)':<10}")
    print("-"*120)
    
    for idx, row in df.iterrows():
        print(f"{int(row['nNodes']):<6} {row['NormalizedThroughput']:<11.4f} "
              f"{row['OutageProbability']:<8.4f} {row['AvgDelayMs']:<10.2f} "
              f"{row['JitterMs']:<9.2f} {row['AvgHopCount']:<9.2f} "
              f"{row['RouteDiscoveryMs']:<11.2f} {row['ControlOverhead']:<9.4f} "
              f"{row['EnergyMJ']:<10.2f}")
    
    print("="*120)
    print()
    
    # ========================================================================
    # EXPECTED TRENDS & VALIDATION
    # ========================================================================
    
    print("EXPECTED TRENDS (Standard AODV):")
    print("="*80)
    print("As node count INCREASES:")
    print("  ✓ Throughput should DECREASE (more interference)")
    print("  ✓ Outage should INCREASE (worse SINR)")
    print("  ✓ Delay should INCREASE (more contention)")
    print("  ✓ Jitter should INCREASE (variable congestion)")
    print("  ✓ Hop count should INCREASE slightly (longer paths due to breakage)")
    print("  ✓ Route discovery should INCREASE (more collisions)")
    print("  ✓ Control overhead should INCREASE (more RREQ retries)")
    print("  ✓ Energy should INCREASE (more transmissions)")
    print()
    
    # Check trends
    trends = {
        'Throughput decreasing': df['NormalizedThroughput'].is_monotonic_decreasing,
        'Outage increasing': df['OutageProbability'].is_monotonic_increasing,
        'Delay increasing': df['AvgDelayMs'].is_monotonic_increasing,
        'Energy increasing': df['EnergyMJ'].is_monotonic_increasing,
    }
    
    print("OBSERVED TRENDS:")
    print("="*80)
    for trend, observed in trends.items():
        status = "✓ YES" if observed else "✗ NO (check simulation)"
        print(f"  {trend:<30} {status}")
    print()
    
    # ========================================================================
    # COMPARISON BASELINE VALUES
    # ========================================================================
    
    print("="*120)
    print("BASELINE VALUES FOR COMPARISON")
    print("="*120)
    print("\nUse these values when comparing IACR and IACR-HC:")
    print()
    print("Expected Improvements with IACR (interference-aware):")
    print("  ✓ Throughput:         HIGHER (avoid high-interference paths)")
    print("  ✓ Outage:             LOWER (better SINR on selected routes)")
    print("  ✓ Energy:             LOWER (fewer retransmissions)")
    print("  ✗ Delay:              HIGHER (may use more hops to avoid interference)")
    print("  ✗ Route Discovery:    HIGHER (ICP phase adds overhead)")
    print("  ✗ Hop Count:          HIGHER (longer paths with lower interference)")
    print()
    print("Expected Improvements with IACR-HC (interference + hop count):")
    print("  ✓ Throughput:         HIGHER than AODV (but maybe < pure IACR)")
    print("  ✓ Delay:              LOWER than pure IACR (hop count penalty)")
    print("  ✓ Hop Count:          LOWER than pure IACR (balanced metric)")
    print("  ~ Energy:             MODERATE (trade-off between AODV and IACR)")
    print()
    print("="*120)

def analyze_latency_distribution(latency_files):
    """Analyze per-packet latency distributions"""
    
    print("\n" + "="*80)
    print("LATENCY DISTRIBUTION ANALYSIS")
    print("="*80)
    
    fig, axes = plt.subplots(2, 3, figsize=(15, 10))
    fig.suptitle('Latency Distribution per Node Count', fontsize=14, fontweight='bold')
    
    node_counts = [5, 10, 15, 20, 25, 30]
    
    for idx, n in enumerate(node_counts):
        row = idx // 3
        col = idx % 3
        ax = axes[row, col]
        
        latency_file = f'aodv-comprehensive-{n}nodes-latency.csv'
        
        if os.path.exists(latency_file):
            df_lat = pd.read_csv(latency_file)
            
            # Plot histogram
            ax.hist(df_lat['Delay(ms)'], bins=50, alpha=0.7, color='blue', edgecolor='black')
            ax.set_xlabel('Delay (ms)', fontsize=10)
            ax.set_ylabel('Frequency', fontsize=10)
            ax.set_title(f'{n} nodes (n={len(df_lat)})', fontweight='bold')
            ax.grid(True, alpha=0.3)
            
            # Add statistics
            mean_delay = df_lat['Delay(ms)'].mean()
            median_delay = df_lat['Delay(ms)'].median()
            p95_delay = df_lat['Delay(ms)'].quantile(0.95)
            
            textstr = f'Mean: {mean_delay:.2f}ms\nMedian: {median_delay:.2f}ms\n95th: {p95_delay:.2f}ms'
            ax.text(0.65, 0.95, textstr, transform=ax.transAxes, fontsize=8,
                   verticalalignment='top', bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
        else:
            ax.text(0.5, 0.5, 'Data not available', transform=ax.transAxes,
                   ha='center', va='center')
            ax.set_title(f'{n} nodes', fontweight='bold')
    
    plt.tight_layout()
    plt.savefig('aodv-latency-distributions.png', dpi=300, bbox_inches='tight')
    print("✓ Latency distribution plot saved: aodv-latency-distributions.png\n")
    plt.show()

if __name__ == '__main__':
    csv_file = 'aodv-comprehensive-results.csv'
    
    if len(sys.argv) > 1:
        csv_file = sys.argv[1]
    
    # Main comprehensive plots
    plot_comprehensive_results(csv_file)
    
    # Latency distribution analysis
    print("\nAnalyzing per-packet latency distributions...")
    analyze_latency_distribution([])
    
    print("\n" + "="*80)
    print("ANALYSIS COMPLETE!")
    print("="*80)
    print("\nGenerated files:")
    print("  1. aodv-comprehensive-results.png - 9-panel comprehensive view")
    print("  2. aodv-latency-distributions.png - Per-node latency histograms")
    print()
    print("Next steps:")
    print("  1. Implement IACR and run with same parameters")
    print("  2. Implement IACR-HC (with hop count penalty)")
    print("  3. Compare all three algorithms side-by-side")
    print("="*80)

#!/bin/bash
t=$(cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/np_ecn_marked_roce_packets)
echo "Marked ECN $t"

echo "CNP sent $(cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/np_cnp_sent)" 

echo "CNP ignored $(cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rp_cnp_ignored)"

echo "CNP handled $(cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rp_cnp_handled)"

echo "iCRC errors $(cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rx_icrc_encapsulated)"

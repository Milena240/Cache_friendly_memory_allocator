#!/bin/bash

RUNS=10

echo "-------------------------------------------------------"
echo "Starting Fast Benchmark ($RUNS runs)"
echo "-------------------------------------------------------"

# Function to extract average time
get_time() {
    perf stat -r $RUNS $1 2>&1 | grep "seconds time elapsed" | awk '{print $1}'
}

echo "Measuring Symmetric (Slow)..."
TIME_SLOW=$(get_time ./asymmetric)
echo "Average Time: $TIME_SLOW s"

echo "Measuring Symmetric Aligned (Fast)..."
TIME_FAST=$(get_time ./asymmetric_aligned)
echo "Average Time: $TIME_FAST s"

# Calculate the difference
IF_SPEEDUP=$(echo "$TIME_SLOW / $TIME_FAST" | bc -l)

echo "-------------------------------------------------------"
printf "The Aligned version is %.2fx faster than the Slow version.\n" $IF_SPEEDUP
echo "-------------------------------------------------------"

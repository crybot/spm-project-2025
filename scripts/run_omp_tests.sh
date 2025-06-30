#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 <input_file>"
	exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE=sorted.bin

echo "Input file: $INPUT_FILE"
for i in 5 6 7; do
	B=$((10**i))
	echo "Batch size: $B"
	for N in 1 2 4 6 8; do
		echo "Running on $N thread(s)"
		rm -f $OUTPUT_FILE
		srun --nodes=1 --ntasks=1 --cpus-per-task=$N --export=ALL,OMP_NUM_THREADS=$N \
			./build_make/apps/omp_single_node/omp_single_node --input $INPUT_FILE --output $OUTPUT_FILE --batch_size $B
	done
done

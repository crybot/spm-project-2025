#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 <input_file>"
	exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE=sorted.bin

echo "Input file: $INPUT_FILE"
for N in 2 4 6 7; do
	for M in 2 4 6 8; do
		echo "Running on $N node(s) with $M thread(s)"
		rm -f $OUTPUT_FILE
		srun --nodes=$N --ntasks-per-node=1 --cpus-per-task=$M --export=ALL,OMP_NUM_THREADS=$M --time=00:10:00 \
			--mpi=pmix ./build_make/apps/mpi_omp_multi_node/mpi_omp_multi_node --input $INPUT_FILE --output $OUTPUT_FILE
		# srun --nodes=$N --ntasks-per-node=1 --time=00:10:00 \
		# 	--mpi=pmix ./build_make/apps/mpi_omp_multi_node/mpi_omp_multi_node --input $INPUT_FILE --output $OUTPUT_FILE
	done
done

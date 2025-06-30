#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 <input_file>"
	exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE=sorted.bin

echo "Input file: $INPUT_FILE"
for N in 1 2 4 6 8 10 12 14 16; do
	echo "Running on $N thread(s)"
	rm -f $OUTPUT_FILE
	srun --nodes=1 --ntasks=1 --cpus-per-task=$N --export=ALL,OMP_NUM_THREADS=$N ./build_make/apps/ff_single_node/ff_single_node \
		--input $INPUT_FILE --output $OUTPUT_FILE --num_sorters $N --num_writers 1
done

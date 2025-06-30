#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 <input_file>"
	exit 1
fi

INPUT_FILE="$1"

echo "Input file: $INPUT_FILE"
echo "Running on a single thread"
srun --nodes=1 --ntasks=1 --cpus-per-task=1 ./build_make/apps/sequential_single_node/sequential_single_node --input $INPUT_FILE 

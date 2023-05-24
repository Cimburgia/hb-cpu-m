#! /bin/bash

# Check for entering a comment to run
if [ "$#" -ne 1 ]; then
    echo "Single Comment needed..."
    exit 1
fi

# Create and save new run folder
date_string=$(date +"%d_%m_%Y__%H:%M:%S")
new_dir="out/$date_string"
mkdir -p "$new_dir"

# Compile and execute program, save stdout to log file
echo "$1" > "$new_dir/log.txt"
make
./prog >> "$new_dir/log.txt"
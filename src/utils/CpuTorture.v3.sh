#!/bin/bash

cores=$(grep -c ^processor /proc/cpuinfo)
echo "Total Number of CPU Cores: $cores"

# Starting CPU Load
initLoad=15 # In percentage

# Step up CPU load successively over iterations
deltaLoadIncrease=5 # In percentage

# Simultaneous instances of stress-ng to hog the CPU
workers=4 

# Amount of stress run-time per CPU core
runtimePerCore=120 # In Seconds

# How long should each stress cycle last?
minRunTime=$((runtimePerCore * cores)) # In seconds

while [ true ]
do
    load=$((initLoad + (RANDOM % 2)))
    timeout=$((minRunTime + (RANDOM % 120))) # In seconds
    backoff=$(((timeout / cores) * 1000 * 1000)) # In microseconds

    if [ "$load" -gt "100" ];
    then
        initLoad=15
        echo "CPU Load has exceeded 100%.. Resetting the load to $((initLoad)) %.."
        continue
    fi 

    echo "Number of workers: $workers"
    echo "CPU Load per worker: $load %"
    echo "Runtime of each worker: $timeout seconds"
    echo "Backoff: $((backoff / (1000 * 1000))) seconds"

    # Method 1 : Spawn 'cores' # of stress-ng instances
    # Each instance has with 'workers' # of workers 
    for ((i = 1 ; i <= cores ; i++));
    do
        echo "Starting stress-ng instance# $((i)) with $((workers)) workers ..."
        stress-ng --cpu $workers --cpu-load $load --cpu-load-slice 0 --timeout $timeout --quiet -k &

        if [ "$i" -eq "$cores" ];
        then
            break
        fi
        
        # Spread the starting of workers across the time period 
        distribute=$(((timeout / cores) - (RANDOM % 10)))
        distribute=$((distribute / 2))
        echo "Sleep for $distribute seconds before starting next worker # $((i + 1)) .."

        sleep "$distribute"
    done

    min=-120
    max=120
    rand=$(((RANDOM % (max - min)) + min))

    coolOffPeriod=$(((timeout * 2) + rand)) # In seconds

    echo "Cool-off for {$((timeout * 2)) + ( $((rand)) ) = $coolOffPeriod} seconds before starting next iteration of stress..."
    sleep $coolOffPeriod

    # Step-up load for next iteration
    initLoad=$((load + deltaLoadIncrease))

    #workers=$((workers + 1))
done

echo "Goodbye.."

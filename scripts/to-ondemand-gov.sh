#!/bin/bash
for i in `seq 0 2 23`; do
	echo $i
	echo "ondemand" | sudo tee "/sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor"
done

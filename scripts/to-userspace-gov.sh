#!/bin/bash
for i in `seq 0 2 23`; do
	echo $i
	echo "userspace" | sudo tee "/sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor"
	echo "1000000" | tee "/sys/devices/system/cpu/cpu$i/cpufreq/scaling_setspeed"
done

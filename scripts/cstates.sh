if [ $# -eq 0 ]
then
	echo "You should use 'enable' or 'disable' as parameter"
	exit
fi

if [ $1 = "enable" ];
then
	for cpus in cpu0 cpu2 cpu4 cpu6 cpu8 cpu10 cpu12 cpu14 cpu16 cpu18 cpu20 cpu22
	do
		sudo echo "0" | sudo tee /sys/devices/system/cpu/${cpus}/cpuidle/state1/disable
		sudo echo "0" | sudo tee /sys/devices/system/cpu/${cpus}/cpuidle/state2/disable
		sudo echo "0" | sudo tee /sys/devices/system/cpu/${cpus}/cpuidle/state3/disable
	done
elif [ $1 = "disable" ];
then
	for cpus in cpu0 cpu2 cpu4 cpu6 cpu8 cpu10 cpu12 cpu14 cpu16 cpu18 cpu20 cpu22
	do
		sudo echo "1" | sudo tee /sys/devices/system/cpu/${cpus}/cpuidle/state1/disable
		sudo echo "1" | sudo tee /sys/devices/system/cpu/${cpus}/cpuidle/state2/disable
		sudo echo "1" | sudo tee /sys/devices/system/cpu/${cpus}/cpuidle/state3/disable
done
fi

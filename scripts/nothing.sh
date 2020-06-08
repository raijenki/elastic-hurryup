for frequencyStep in 1.0GHz 1.3GHz 1.7GHz 2.0GHz 2.3GHz 2.6GHz
do
	sudo cpupower frequency-set -f ${frequencyStep}
	for counter in 1 2 3
	do
		sudo ./init-es.sh &
		sleep 60
		echo "Running the $counter time for $frequencyStep"
		energyStart=$(cat /sys/class/powercap/intel-rapl/intel-rapl\:0/energy_uj)
		sleep 1800
		energyEnd=$(cat /sys/class/powercap/intel-rapl/intel-rapl\:0/energy_uj)
		consumption=$((energyEnd - energyStart))
		echo "$counter, $frequencyStep, $consumption" > /home/cc/elastic-hurryup/db/nothing/energy-nothing-${frequencyStep}-${counter}.txt
		sudo pkill java &
		sleep 15
	done
done

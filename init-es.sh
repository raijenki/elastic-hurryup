export ES_JAVA_OPTS="-noverify -agentpath:$HOME/hurryup-jvmti/bin/hurryup_jvmti.so" #Uncomment if running without HUP
#sudo chmod 666 /sys/devices/system/cpu/cpu*/cpufreq/scaling_setspeed
exec "$HOME/elastic-hurryup/build/server/bin/elasticsearch"

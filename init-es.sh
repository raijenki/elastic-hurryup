export ES_JAVA_OPTS="-agentpath:$HOME/hurryup-jvmti/bin/hurryup_jvmti.so" #Uncomment if running without HUP
exec "$HOME/elastic-hurryup/build/server/bin/elasticsearch"

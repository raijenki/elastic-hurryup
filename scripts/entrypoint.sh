#!/bin/bash

#Read Index Node's IP
export IP=localhost
export SEARCH_DRIVER=Ziphian.java
#Read local IP
export TERMS_FILE=terms_ordered
export RAMP_UP=60
export RAMP_DOWN=60
export STEADY_STATE=1200
export SCALE=30
export AGENT_ID=1
export HOST_IP=localhost
export AGENTS=$HOST_IP:1
export FABAN_HOME=/home/cc/elastic-hurryup/build/client
export FABAN_OUTPUT_DIR=/home/cc/elastic-hurryup/db
#Read client parameters
export ANT_HOME=/home/cc/elastic-hurryup/build/ant
export JAVA_VERSION="8"
#export JAVA_HOME=/home/cc/jdk7
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
export CLIENT_HEAP_SIZE=2g
export DRIVER_DIR=$FABAN_HOME/search
export POLICY_PATH=$DRIVER_DIR/config/security/driver.policy
export BENCHMARK_CONFIG=$FABAN_HOME/search/deploy/run.xml

#PREPARE
$FABAN_HOME/master/bin/startup.sh

#RUN
cd $FABAN_HOME/search \
		&& cp distributions/$SEARCH_DRIVER src/sample/searchdriver/SearchDriver.java


cd $FABAN_HOME/search \
		&& $ANT_HOME/bin/ant deploy 

cd $FABAN_HOME/search \
		&& sed -i "/<ipAddress1>/c\<ipAddress1>$IP</ipAddress1>" deploy/run.xml \
			&& sed -i "/<portNumber1>/c\<portNumber1>9200</portNumber1>" deploy/run.xml \
				&& sed -i "/<outputDir>/c\<outputDir>$FABAN_OUTPUT_DIR</outputDir>" deploy/run.xml \
					&& sed -i "/<termsFile>/c\<termsFile>$FABAN_HOME/search/src/sample/searchdriver/$TERMS_FILE</termsFile>" deploy/run.xml \
						&& sed -i "/<fa:scale>/c\<fa:scale>$SCALE</fa:scale>" deploy/run.xml \
							&& sed -i "/<agents>/c\<agents>$AGENTS</agents>" deploy/run.xml \
								&& sed -i "/<fa:rampUp>/c\<fa:rampUp>$RAMP_UP</fa:rampUp>" deploy/run.xml \
									&& sed -i "/<fa:rampDown>/c\<fa:rampDown>$RAMP_DOWN</fa:rampDown>" deploy/run.xml \
										&& sed -i "/<fa:steadyState>/c\<fa:steadyState>$STEADY_STATE</fa:steadyState>" deploy/run.xml

echo "Print= $AGENTS"

export CLASSPATH=$FABAN_HOME/lib/fabanagents.jar:$FABAN_HOME/lib/fabancommon.jar:$FABAN_HOME/lib/fabandriver.jar:$JAVA_HOME/lib/tools.jar:$FABAN_HOME/search/build/lib/search.jar

until $(curl --output /dev/null --silent --head --fail http://localhost:9200); do
	    printf '.'
	        sleep 5
	done

	#START Registry
	numactl --cpunodebind=1 $JAVA_HOME/bin/java -classpath $CLASSPATH -Djava.security.policy=$POLICY_PATH com.sun.faban.common.RegistryImpl &
	sleep 3s

	#START Agent
	numactl --cpunodebind=1 $JAVA_HOME/bin/java -classpath $CLASSPATH -Xmx$CLIENT_HEAP_SIZE -Xms$CLIENT_HEAP_SIZE -Djava.security.policy=$POLICY_PATH com.sun.faban.driver.engine.AgentImpl "SearchDriver" $AGENT_ID $HOST_IP &

	#START Master
	numactl --cpunodebind=1 $JAVA_HOME/bin/java -classpath $CLASSPATH -Xmx$CLIENT_HEAP_SIZE -Xms$CLIENT_HEAP_SIZE -Djava.security.policy=$POLICY_PATH -Dbenchmark.config=$BENCHMARK_CONFIG com.sun.faban.driver.engine.MasterImpl

	sleep 3s

	#Output summary
	#cat $FABAN_OUTPUT_DIR/1/summary.xml

set -e
# Exports
export FABAN_USER=faban
export INSTALL_PATH=/home/cc/elastic-hurryup/build # Change this

# Install dependencies (FROM Cloudsuite)
sudo apt-get update
sudo apt-get install -y --no-install-recommends telnet wget tar curl numactl
sudo rm -rf /var/lib/apt/lists/*
sudo groupadd -r $FABAN_USER
sudo useradd -r -g $FABAN_USER $FABAN_USER

# Download faban
mkdir -p ../downloads/
mkdir -p ../build/
cd ../downloads/
wget faban.org/downloads/faban-kit-latest.tar.gz # As of Apr 2020, ver 1.4.2
tar xvf faban-kit-latest.tar.gz
mv faban ../build/client

# Download ant
wget http://archive.apache.org/dist/ant/binaries/apache-ant-1.9.9-bin.tar.gz
tar xvf apache-ant-1.9.9-bin.tar.gz
mv apache-ant-1.9.9 ../build/ant

# Install Cloudsuite's Search
wget http://cloudsuite.ch/download/web_search/search.tar.gz
tar xvf search.tar.gz
mv search/ ../build/client/.

mkdir -p ../db/
cd ../install
# Uncoment this line for remote host
# sed -i 's/localhost:9200/0.0.0.0:9200/g' Ziphian.java
sed -i "s,/home/cc/elastic-hurryup/db/log.txt,$INSTALL_PATH/db/log.txt,g" Ziphian.java
cp Ziphian.java ../build/client/search/distributions/Ziphian.java
mkdir -p "$INSTALL_PATH/db"
cd ../build/client/search/
sed -i "/faban.home/c\\faban.home=$INSTALL_PATH/client" build.properties
sed -i "/ant.home/c\\ant.home=$INSTALL_PATH/ant" build.properties 
sed -i "/faban.url/c\\faban.url=http://localhost:9980/" build.properties # Change port if necessary

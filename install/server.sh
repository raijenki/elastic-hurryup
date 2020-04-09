export es=localhost:9200
export site=en.wikipedia.org
export index=enwiki
# The most recent from https://dumps.wikimedia.org/other/cirrussearch/
export dump=enwiki-20200330-cirrussearch-content.json.gz

# Install JDK8 for compatibility issues
sudo apt-get install openjdk-8-jdk numactl

# Download elasticsearc 6.5.4
mkdir ../build/
mkdir ../downloads/
cd ../downloads/
wget https://artifacts.elastic.co/downloads/elasticsearch/elasticsearch-6.5.4.tar.gz 

# Install and move
tar xvf elasticsearch-6.5.4.tar.gz
mv elasticsearch-6.5.4 ../build/server/
mv elasticsearch ../build/server/bin/.

#Add plugins which will allow us to index Wikipedia onto Elasticsearch
cd ../build/server/
./bin/elasticsearch-plugin install analysis-icu 
./bin/elasticsearch-plugin install org.wikimedia.search:extra:6.5.4

# Start Elasticsearch
./bin/elasticsearch &
sleep 10

# Create Index 'enwiki' for Wikipedia
# Code by https://www.elastic.co/pt/blog/loading-wikipedia
curl -H 'Content-Type: application/json' -s 'https://'$site'/w/api.php?action=cirrus-settings-dump&format=json&formatversion=2' |
	jq '{
		settings: {
		   index: {
		      analysis: .content.page.index.analysis, 
	    	      similarity: .content.page.index.similarity,
	     	   }
     		}
	     } ' |
curl -H 'Content-Type: application/json' -XPUT $es/$index?pretty -d @-
curl -H 'Content-Type: application/json' -s 'https://'$site'/w/api.php?action=cirrus-mapping-dump&format=json&formatversion=2' |	     
	 jq .content |
	 sed 's/"index_analyzer"/"analyzer"/' |
	 sed 's/"position_offset_gap"/"position_increment_gap"/' |
	 curl -H 'Content-Type: application/json' -XPUT $es/$index/_mapping/page?pretty -d @-


# Download English Wikipedia content (~30gb, may take some time)
cd ../../downloads/
wget https://dumps.wikimedia.org/other/cirrussearch/current/enwiki-20200330-cirrussearch-content.json.gz 
mkdir chunks
cd chunks
zcat ../$dump | split -a 10 -l 500 - $index & # split files in less chunks, may take a while

#Curl everything (overnight job)
for file in *; do
	echo -n "${file}:  "
	    took=$(curl -s -H 'Content-Type: application/x-ndjson' -XPOST $es/$index/_bulk?pretty --data-binary @$file |
		        grep took | cut -d':' -f 2 | cut -d',' -f 1)
	      printf '%7s\n' $took
	        [ "x$took" = "x" ] || rm $file
done
cd ../
rm -r chunks
cd ../
echo "Done"





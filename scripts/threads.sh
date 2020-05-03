espid=`jps | grep Elasticsearch | cut -f1 -d' '`

max_core=24

i=0
for p in `jstack $espid |  egrep '[[]search[]]' | sed -n 's/.*"\(.*\)".*nid=\([xa-f0-9]*\).*/\2/p'`;
	#for p in `jstack $pid |  egrep '[[]search[]]' | sed -n 's/.*"\(.*\)".*nid=\([xa-f0-9]*\).*/print \2/p' | xargs -0 python -c`; 
do
	 #echo $(($p))
	  echo "$(($p)) goes to core `expr $i % $max_core`"
	   taskset -cp `expr $i % $max_core` "$(($p))"
	    i=`expr $i + 2`
    done

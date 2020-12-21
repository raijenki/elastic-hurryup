# Frequency controller for Elasticsearch
## What is this?
This project ("Hurryup") is a frequency controller for dynamic (voltage) and frequency scaling processors (DFS/DVFS). In a nutshell, the scripts monitors the received queries on Elasticsearch and promotes only the heavy queries (which are over a user-defined threshold) to the highest available frequency. Other queries run in a mid-level frequency and the core gets at the lowest frequency when inactive. Results have shown that energy efficiency can be up to 28% when compared to Linux's Ondemand.

# Reproducibility
The original experiments used a Intel Xeon Gold 6126 machine (Skylake Architecture) with 192 GB of random access memory, Elasticsearch v6.5.4 and Faban v1.4.2. The processors includes two sockets of 12 physical DVFS cores each, where the server and client runs separately through numactl's cpunodebind. The energy driver 'acpi-cpufreq' must be installed and activated as it allows the energy controller to userspace. 'cpupower/cpufreqd' is the interface for changing the per-core frequencies.

The files 'install/server.sh' and 'install/client.sh' includes all the automated steps for installation. The 'install/config.sh' includes some pointers to necessary Linux's files. 

## Related Papers
To be published.

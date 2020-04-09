# Frequency controller for Elasticsearch
## What is this?
This project ("Hurryup") is a frequency controller for dynamic voltage and frequency scaling processors. In a nutshell, the scripts monitors the received queries on Elasticsearch and builds a model that uses the lowest frequency + lowest number of cores, which are deeply related to energy consumption, without losing the established deadline. By using this model, it increases/decreases frequencies on a per-core granulometry.

# Reproducibility
The original experiments used a Intel Xeon Gold 6126 machine (Skylake Architecture) with 192 GB of random access memory, Elasticsearch v6.5.4 and Faban v1.4.2. The processors includes two sockets of 12 physical DVFS cores each, where the server and client runs separately through numactl's cpunodebind. The energy driver 'acpi-cpufreq' must be installed and activated as it allows the energy controller to userspace. 'cpupower/cpufreqd' is the interface for changing the per-core frequencies.

The files 'install/server.sh' and 'install/client.sh' includes all the automated steps for installation. The 'install/config.sh' includes some pointers to necessary Linux's files. 

## Related Papers
To be published.

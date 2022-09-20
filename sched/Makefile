#JAVA_HOME ?= /usr/lib/jvm/default
#JAVA_HOME ?= /usr/lib/jvm/java-8-openjdk-armhf
#JAVA_HOME ?= /usr/lib/jvm/java-8-openjdk-amd64
JAVA_HOME ?= /home/denimorim/.sdkman/candidates/java/8.0.302-open

CXXFLAGS += -std=c++14 -O2 -pedantic -Wall -Wextra -Wno-unused-parameter
INCLUDE += -I $(JAVA_HOME)/include -I $(JAVA_HOME)/include/linux -I /home/denimorim/dev/undergrad-thesis-profiler/include

SRC_FILES = src/agent.cpp src/calltracer.cpp src/vm.cpp src/tls.cpp src/hurryup.cpp
AR_FILES = /home/denimorim/dev/undergrad-thesis-profiler/build/lib/libjvmtiprof.a /home/denimorim/dev/undergrad-thesis-profiler/build/jnif/libjnif.a

all: build

build:
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(INCLUDE) $(SRC_FILES) $(AR_FILES) -shared -fpic -o bin/hurryup_jvmti.so


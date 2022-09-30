#JAVA_HOME ?= /usr/lib/jvm/default
#JAVA_HOME ?= /usr/lib/jvm/java-8-openjdk-armhf
JAVA_HOME ?= /usr/lib/jvm/java-8-openjdk-amd64

CXXFLAGS += -std=c++14 -O2 -pedantic -Wall -Wextra -Wno-unused-parameter
INCLUDE += -I $(JAVA_HOME)/include -I $(JAVA_HOME)/include/linux -I /home/cc/jvmtiprof/include

SRC_FILES = src/agent.cpp src/calltracer.cpp src/vm.cpp src/tls.cpp src/hurryup.cpp
AR_FILES = /home/cc/jvmtiprof/build/lib/libjvmtiprof.a /home/cc/jvmtiprof/build/jnif/libjnif.a

all: build

build:
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(INCLUDE) $(SRC_FILES) $(AR_FILES) -shared -fpic -o bin/hurryup_jvmti.so


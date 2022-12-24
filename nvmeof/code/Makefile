all: client attacker


SPDK = $(HOME)/spdk/
SPDK_CONFIG_PATH = $(SPDK)/build/lib/pkgconfig/

# WE need to remove spdk_log from nano spdk/build/lib/pkgconfig/spdk_env_dpdk.pc

CFLAGS += -Wall -std=c++11 -I./ -I$(SPDK)/build/include -O2
SPDK_LIB = $(shell  PKG_CONFIG_PATH="$(SPDK_CONFIG_PATH)" pkg-config --libs spdk_nvme)
DPDK_LIB = $(shell  PKG_CONFIG_PATH="$(SPDK_CONFIG_PATH)" pkg-config --libs spdk_env_dpdk)
SYS_LIB = $(shell  PKG_CONFIG_PATH="$(SPDK_CONFIG_PATH)" pkg-config --libs --static spdk_syslibs)

LDFLAGS = -pthread -Wl,--whole-archive $(SPDK_LIB)  $(DPDK_LIB) -Wl,--no-whole-archive  $(SYS_LIB)

client: test.cpp
	g++ test.cpp  $(CFLAGS) $(LDFLAGS)  -o client 

attacker: attacker.cpp
	g++ attacker.cpp  -libverbs  -o attacker
	
clean:
	rm -f client 


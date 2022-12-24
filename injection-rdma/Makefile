APPS = attacker main attacker_bw

all: CFLAGS += -O2
all: ${APPS}


main: main.cpp
	g++ main.cpp --std=c++11 ${CFLAGS} -libverbs -o main
attacker: attacker.cpp
	g++ attacker.cpp --std=c++11 ${CFLAGS} -libverbs -o attacker
	
attacker_bw: attacker_bw.cpp
	g++ attacker_bw.cpp --std=c++11 ${CFLAGS} -libverbs -o attacker_bw
	
clean:
	$(foreach fname,${APPS}, rm -f ${fname})

debug: CFLAGS += -DDEBUG -g -O0
debug: ${APPS}

.DELETE_ON_ERROR:
.PHONY: all clean

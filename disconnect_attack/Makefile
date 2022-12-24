APPS =  client  server attacker


LDFLAGS =  -libverbs -lrdmacm 
CFLAGS += -Wall -std=c++11 -I./ -O2

all: ${APPS}

 
client: clean
	g++ client.cpp $(CFLAGS) $(LDFLAGS)   -o client

server: clean
	g++ server.cpp $(CFLAGS) $(LDFLAGS)   -o server

attacker: clean
	g++ attacker.cpp $(CFLAGS) $(LDFLAGS)   -o attacker

clean:
	$(foreach fname,${APPS}, rm -f ${fname})


.DELETE_ON_ERROR:
.PHONY: all clean

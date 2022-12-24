APPS = cnp

CFLAGS += -Wall -std=c++11 

all: CFLAGS += -O2
all: ${APPS}

cnp: clean
	g++ cnp.cpp ${CFLAGS} -lz -o cnp

clean:
	$(foreach fname,${APPS}, rm -f ${fname})

debug: CFLAGS += -DDEBUG -g -O0
debug: ${APPS}

.DELETE_ON_ERROR:
.PHONY: all clean

# Variables
SERVER_BINARY=server
MINER_BINARY=miner
SERVER_SOURCE=server.c
MINER_SOURCE=miner.c

# Default target
all: build

# Build binaries
build: $(SERVER_BINARY) $(MINER_BINARY)

$(SERVER_BINARY): $(SERVER_SOURCE)
	gcc -o $(SERVER_BINARY) $(SERVER_SOURCE) -lz

$(MINER_BINARY): $(MINER_SOURCE)
	gcc -o $(MINER_BINARY) $(MINER_SOURCE) -lz

# Clean up binaries
clean:
	rm -f $(SERVER_BINARY) $(MINER_BINARY)

.PHONY: all build clean


#!/bin/bash

# Stop the containers
docker stop mtacoin-server mtacoin-miner-1 mtacoin-miner-2 mtacoin-miner-3

# Remove the containers
docker rm mtacoin-server mtacoin-miner-1 mtacoin-miner-2 mtacoin-miner-3

echo "Containers stopped and removed."


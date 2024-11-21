#!/bin/bash

# Ensure /mnt/mta directory exists and has proper permissions
sudo mkdir -p /mnt/mta
sudo chmod 777 /mnt/mta

# Copy configuration file to the correct location
cp mtacoin.conf /mnt/mta/
sudo chmod 777 /mnt/mta/mtacoin.conf

# Pull the Docker images from Docker Hub
docker pull naorb12/linux-assigment3:server
docker pull naorb12/linux-assigment3:miner

# Run the server container
docker run -d --name mtacoin-server -v /mnt/mta:/mnt/mta naorb12/linux-assigment3:server

# Wait for the server to initialize
sleep 1

# Run multiple miner containers
for i in {1..3}; do
    docker run -d --name mtacoin-miner-$i -v /mnt/mta:/mnt/mta naorb12/linux-assigment3:miner
done

echo "Server and miner containers are running."


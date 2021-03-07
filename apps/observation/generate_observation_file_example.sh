#!/bin/bash

python3 ./create_observation_descriptor.py --outputfile=$HOME/xengine_output/staging/observation.json --synctime=1612923335 \
	--object_name=3C286 --antennas=2b,3c,4g --xdir=$HOME/xengine_output/staging --basename=ata_2021_03_07_10_03 --skyfreq=3e9 \
	--startchannel=1920 --integration-time=0.04
	

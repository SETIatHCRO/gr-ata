#!/bin/bash

cd $HOME/xengine_output/staging

python3 read_correlated_data.py --inputfile=$HOME/xengine_output/staging/observation_descriptor.json --outputfile=$HOME/xengine_output/staging/casa_2021_Feb_09_sync.uvfits


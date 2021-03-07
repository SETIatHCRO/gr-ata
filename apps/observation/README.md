## ATA GNURadio XEngine Observation Application

This application provides the tools necessary to take an observation with the SNAP controllers at the Allen Telescope Array and produce a valid UVFITS file suitable for processing in other applications.  This current setup handles synchronization and phasing in the following manner:

1. SNAP controllers are synchronized to start outputting data at the same time
2. A synchronizer block is used within the flowgraph to ensure all data streams are aligned based on the SNAP timestamps. (This has some limitations in the total number of antennas, potentially up to 10, that will be addressed in future work)
3. Phase synchronization is taken care of in postprocessing (processing the xengine output into a UVFITS file).  So integration times for a frame should be kept to a level that this approach will not produce incorrect results (a maximum integration of around 0.1 seconds is recommended for now)

# GNU Radio Requirements

Currently, all development work has been done based on GNU Radio 3.8 with a port to 3.9 coming soon.  So running the required flowgraphs does require a working GNU Radio installation.  It will also require the gr-clenabled (https://github.com/ghostop14/gr-clenabled) to be installed.  There is a maint-3.8 branch that matches with GNU Radio 3.8.  It will also require this gr-ata OOT module set to be installed.

# Pre-Run Information to Record
Prior to starting a run, the following information should be recorded:

- sync time: This is the SNAP start synchronization timestamp at which the observation was started. Format will look something like this: 1612923335
- object name: Designator of observed target. E.g. CAS-A would be 3C286
- antennas: List of antennas in order
- xdir: Directory where xengine output data and the observation file will be
- basename: The base name of the xengine files. Ex: ata_2021_03_07_10_03
- sky freq: The center frequency of the observation in Hz
- start channel: For example, if recording 256 channels, 2048 is the center channel, so the start channel would be 1920.
- integration time: Integration time in seconds. Note that each ATA time frame is 4 microseconds. So 10000 integration frames = 0.04 seconds integration time.

Also, look in the latest observation_descriptor_template.json (included here) to ensure that the appropriate physical delays for all of the antennas you are using are in the currently calculated list.  If not, you may need to do some additional work prior to using a particular antenna.

# X-Engine Correlation

The first step will be to coordinate with ATA operators to create a valid SNAP configuration file.  SNAPs should be configured to synchronize, and to output over a single ethernet connection (no round-robin).  Each snap should be configured in order to output over UDP to unique ports starting at 10000.  For instance, in a 3 -antenna setup, Antenna 2b->10000, 3c->10001, and 4g->10002.

The files in this application are configured specifically for 256 channels and a sky frequency of 3 GHz, however that can be changed in the accompanying flowgraphs.

Several flowgraphs are provided to record 3, 4, and 6 antennas.  Scaling these is simply a matter of changing the number of inputs and creating the additional copy/paste source blocks (changing the port numbers as appropriate).  Within these flowgraphs, there are a few parameters that may need to be adjusted:

1. output_file: The directory and file base name used to write the xengine output
2. sky_freq: The center channel's sky frequency
3. starting_channel: The first channel in the set sent to this xengine processor
4. num_channels: The number of channels sent from the SNAPs to this xengine processor
5. Integration Time: The OpenCL X-Engine block has a parameter that defines how many time frames should be integrated.  Each time frame is 4 microseconds, so an integration value here of 10000 would correspond to an integration time of 0.04 seconds.

With a 10000 frame integration and 256 channels, you can expect around 90 MB / min to be generated (rough ballpark).  It is highly recommended that NVMe drives or a high-throughput RAID setup be used to minimize delays created by disk writes.  NVMe's are the preferred approach due to their extremely high throughput.

# Post-Processing

Once an observation recording is finished, there are several python scripts to post-process the data into a UVFITS file.

## create_observation_descriptor.py
This python script will take the minimum parameters needed to create the observation descriptor file that is needed by read_correlated_data.py.  Its parameters are focused on the information noted to record above.

```
usage: create_observation_descriptor.py [-h] [--template TEMPLATE] --outputfile OUTPUTFILE --synctime SYNCTIME --object_name OBJECT_NAME
                                        --antennas ANTENNAS --xdir XDIR --basename BASENAME --skyfreq SKYFREQ --startchannel STARTCHANNEL
                                        --integration-time INTEGRATION_TIME

ATA Observation Descriptor Generator

optional arguments:
  -h, --help            show this help message and exit
  --template TEMPLATE   Template file to be used. If not provided, ./
  --outputfile OUTPUTFILE
                        File name to save the generated observation json to.
  --synctime SYNCTIME   This is the SNAP start synchronization timestamp at which the observation was started. Format will look something like
                        this: 1612923335
  --object_name OBJECT_NAME
                        Designator of observed target. E.g. Casa would be 3C286
  --antennas ANTENNAS   Comma-separated list of antennas in order (no spaces)
  --xdir XDIR           Directory where xengine output data and the observation file will be
  --basename BASENAME   The base name of the xengine files. Ex: ata_2021_03_07_10_03
  --skyfreq SKYFREQ     The center frequency of the observation in Hz
  --startchannel STARTCHANNEL
                        Number of the starting channel sent to the xengine
  --integration-time INTEGRATION_TIME
                        Integration time in seconds. Note that each ATA time frame is 4 microseconds. So 10000 integration frames = 0.04
                        seconds integration time
```

Note that this script does require the file observation_descriptor_template.json (included here) to be present as a baseline template.

An example of running this generator may look like:

```
python3 ./create_observation_descriptor.py --outputfile=$HOME/xengine_output/staging/observation.json --synctime=1612923335 \
	--object_name=3C286 --antennas=2b,3c,4g --xdir=$HOME/xengine_output/staging --basename=ata_2021_03_07_10_03 --skyfreq=3e9 \
	--startchannel=1920 --integration-time=0.04
```

There is also a generate_observation_file_example.sh script included here that can be used as a starting point/example.

## read_correlated_data.py

This script is the primary script to take the xengine output and generate the UVFITS file format.  It takes a JSON descriptor file that provides the necessary parameters and file locations to create the file.  This script does require the file antenna_coordinates_ecef.txt (included here) to be in the local directory.  This is an example of the descriptor file for a Cas-A observation:

```
{
	"observation_start": "2021-02-09 21:15:35 EST",
	"instrument": "SNAPs",
	"telescope_name": "ATA",
	"telescope_location": [40.8174, -121.472, 1043],
	"object_name": "3C461",
	"comments": "",
	"antenna_names": ["1f","2a","4g","1k","5c"],
	"antenna_delays": {
		"1a" : -149,
		"1c" : 0,
		"2a" : -347,
		"4g" : -904,
		"4j" : -984,
		"1f" : -171,
		"1h" : -178,
		"1k" : -260,
		"2b" : -233,
		"2h" : -244,
		"5c" : -1650
	},
	"polarizations": 2,
	"num_baselines": 15,
	"channel_width": 250000.0,
	"first_channel_center_freq": 2968000000.0,
	"integration_time_seconds": 0.04,
	"input_dir": "/home/user/xengine_output/staging",
	"observation_base_name": "casa_2021_Feb_9_sync",
	"output_dir": "/home/user/xengine_output/staging"
}
```

This script really only has 2 required parameters: the input JSON descriptor, and the output UVFITS file.  
```
usage: read_correlated_data.py [-h] --inputfile INPUTFILE --outputfile OUTPUTFILE [--check-uvfits]

X-Engine Data Extractor

optional arguments:
  -h, --help            show this help message and exit
  --inputfile INPUTFILE, -i INPUTFILE
                        Observation JSON descriptor file
  --outputfile OUTPUTFILE, -o OUTPUTFILE
                        UVFITS output file
  --check-uvfits, -c    This will enable validating UVFITS data on writing.
```

An example of running this script would be:

```
python3 read_correlated_data.py --inputfile=$HOME/xengine_output/staging/observation_descriptor.json --outputfile=$HOME/xengine_output/staging/casa_2021_Feb_09_sync.uvfits
```

There is a run_correlation_example.sh script included here that can be used as a starting point/example.

# Visualizing the Data

While using the program casa (https://casa.nrao.edu/) is beyond the scope of this help file, it should be noted that a working docker image is available where at least casa and casaviewer run correctly if you do not have a local installation or run into issues setting it up.  The docker can be pulled via the command:

```
docker pull ghostop14/casa:6.1.0
```

An example of running it would be:
```
docker run --rm -dit --network=host -v /etc/localtime:/etc/localtime -v /etc/timezone:/etc/timezone \
	-v $HOME:$HOME \
	-e DISPLAY=unix$DISPLAY \
	-v /tmp/.X11-unix:/tmp/.X11-unix \
	ghostop14/casa:6.1.0 \
	/bin/bash
```

You can then interact with the docker with either ```docker attach <dockerid>``` or ```docker exec -it <docker id> /bin/bash```.  The command 'casa' will start the interactive python casa framework, and casaviewer can also be launched.  See the casa documentation for any further guidance.

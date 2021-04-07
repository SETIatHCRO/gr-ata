# gr-ata - GNU Radio Blocks and Support Scripts for the Allen Telescope Array

gr-ata contains a number of GNU Radio blocks unique to the ATA

- **SNAP Source** - The SNAP source block is designed to work with the new SNAP data capture devices at each antenna.  At the time of writing, the latest format is v2.0.  The SNAP boards have several modes:

      + **Voltage Mode** - This is the most common mode of use.  The SNAP performs a PFB and FFT prior to sending the data over the network.   The SNAP boards put out IQ data in two polarizations (X/Y), each as 4-bit two's complement data in UDP packets consisting of 16 timing frames (at 4 microseconds per frame), covering 256 channels.  Each channel is 250 KHz wide.  Multiple 256-channel packets can be used to make up a total of 2048 channels.  The block supports UDP by specifying a port, supports multicast UDP, and also reading data from PCAP files captured with tools such as tcpdump.  The block can output the XY FFT vectors as packed 4-bit, 8-bit ichar, or full gr_complex.
     + **Spectrometer Mode** - This mode is a slower transmission mode.  Each packet contains the XX product, YY product, as well as the real part of XY*, and the imaginary part of XY*.
	
- **SNAP Synchronizer (V3)** - When working with multiple antennas, it is usually important to time-align the streams.  The SNAP boards provide timestamp information that is sent downstream from the SNAP Source as GNU Radio tags.  This block can aggregate multiple antennas that may not be at exactly the same timestamp and align them such that the output is time-aligned based on the SNAP timestamps.  Note that if you are going to do a full X-Engine correlator, [gr-clenabled](https://github.com/ghostop14/gr-clenabled) has a GPU-enabled X-Engine that has an option to perform this option for the ATA within that block.  This is the more efficient approach.  However, if you are developing your own applications based on the SNAP Source block, this synchronizer may be required.

## Prerequisites

In order to make gr-ata, you will first need to install [ATA-Utils](https://github.com/SETIatHCRO/ATA-Utils).  In order to install this module, you'll need to follow these steps:

```
git clone https://github.com/SETIatHCRO/ATA-Utils
cd ATA-Utils/pythonLibs
sudo pip3 install .
```

## Installing gr-ata

Install gr-ata by doing:

```
git clone https://github.com/SETIatHCRO/gr-ata
cd gr-ata
mkdir build
cd build
cmake ..
make
sudo make install
sudo ldconfig
```

## X-Engine

With gr-ata, it is possible to run a full FX correlation engine from within GNU Radio.  The pipeline takes data from the SNAP boards running in voltage mode via SNAP source blocks, and leverages [gr-clenabled](https://github.com/ghostop14/gr-clenabled)'s clXEngine block to perform GPU accelerated correlation.  The XEngine block has been used to do observations with 12 simultaneous antennas of 256 channels (both polarizations) in real-time, and has been timed to support up to 21 antennas on an NVIDIA 2080 GPU.  In the apps/observation/gr_flowgraphs directory is a sample flowgraph called ata_12ant_xcorr.grc that shows the flowgraph to correlate 12 antennas in realtime.

In order to more easily support reconfigurable observations, there is an application named ata_multi_ant.py in apps/observations/gr_flowgraphs.  This can be used from the command-line to control a variable number of antennas (configured from the command-line).  Observations at the ATA have been executed with this script such as this.  The one piece of information that would be required from the SNAP configuration will be the unix timestamp that they are all synchronized to start on (designated snap-sync).

### 3c84 5-Minute Calibration 
This example uses the timeout command to control a 5-minute runtime.  numactl -N 0 is used to lock in the run to just one NUMA node in a 2-CPU system.  If you have a system with just 1 CPU, or want to use the --enable-affinity option to balance across all nodes, this can be left off.

```
timeout 5m numactl -N 0 ./ata_multi_ant_xcorr.py --snap-sync=1616950797 --integration-frames=20000 --output-prefix=3c84 --object-name=3c84 --starting-channel=1920 --num-channels=256 --antenna-list=1a,1f,4g,5c,1c,2b,2h,1h,1k,4j,2a,3c --starting-chan-freq=2968000000.0 --output-directory=$HOME/xengine_output/staging 
```
 
### Cassiopeia A 2-Minutes Observing, 8-Minute Sleep
```
while true; do timeout 2m numactl -N 0 ./ata_multi_ant_xcorr.py --snap-sync=1616950797 --integration-frames=20000 --output-prefix=casa --object-name=3c461 --starting-channel=1920 --num-channels=256 --antenna-list=1a,1f,4g,5c,1c,2b,2h,1h,1k,4j,2a,3c --starting-chan-freq=2968000000.0 --output-directory=$HOME/xengine_output/staging; echo "[`date`] Sleeping..."; sleep 8m;done 
```

### ata_multi_ant_xcorr.py
The full list of options for ata_multi_ant_xcorr.py is:

```
usage: ata_multi_ant_xcorr.py [-h] --snap-sync SNAP_SYNC --object-name OBJECT_NAME --antenna-list ANTENNA_LIST --num-channels NUM_CHANNELS --starting-channel
                              STARTING_CHANNEL --starting-chan-freq STARTING_CHAN_FREQ [--channel-width CHANNEL_WIDTH] --integration-frames INTEGRATION_FRAMES
                              [--cpu-integration CPU_INTEGRATION] --output-directory OUTPUT_DIRECTORY [--output-prefix OUTPUT_PREFIX] [--base-port BASE_PORT] [--no-output]
                              [--enable-affinity]

ATA Multi-Antenna X-Engine

optional arguments:
  -h, --help            show this help message and exit
  --snap-sync SNAP_SYNC, -s SNAP_SYNC
                        The unix timestamp when the SNAPs started and were synchronized
  --object-name OBJECT_NAME, -o OBJECT_NAME
                        Name of viewing object. E.g. 3C84 or 3C461 for CasA
  --antenna-list ANTENNA_LIST, -a ANTENNA_LIST
                        Comma-separated list of antennas used (no spaces). This will be used to also define num_antennas.
  --num-channels NUM_CHANNELS, -c NUM_CHANNELS
                        Number of channels being received from SNAP (should be a multiple of 256)
  --starting-channel STARTING_CHANNEL, -t STARTING_CHANNEL
                        Starting channel number being received from the SNAP
  --starting-chan-freq STARTING_CHAN_FREQ, -f STARTING_CHAN_FREQ
                        Center frequency (in Hz) of the first channel (e.g. for 3 GHz sky freq and 256 channels, first channel would be 2968000000.0
  --channel-width CHANNEL_WIDTH, -w CHANNEL_WIDTH
                        [Optional] Channel width. For now for the ATA, this number should be 250000.0
  --integration-frames INTEGRATION_FRAMES, -i INTEGRATION_FRAMES
                        Number of Frames to integrate in the correlator. Note this should be a multiple of 16 to optimize the way the SNAP outputs frames (e.g. 10000,
                        20000, or 24000 but not 25000). Each frame is 4 microseconds so an integration of 10000 equates to a time of 0.04 seconds.
  --cpu-integration CPU_INTEGRATION
                        If set to a value > 1, each GPU correlated frame will be further accumulated in a CPU buffer. This allows for integrations beyond the amount of
                        memory available on a GPU. This figure is a multiplier, so if integration-time=20000, and cpu-integration=2, total integration time will be 40000
  --output-directory OUTPUT_DIRECTORY, -d OUTPUT_DIRECTORY
                        Directory path to where correlation outputs should be written. If set to the word 'none', no output will be generated (useful for performance
                        testing).
  --output-prefix OUTPUT_PREFIX, -p OUTPUT_PREFIX
                        If specified, this prefix will be prepended to the output files. Otherwise 'ata' will be used.
  --base-port BASE_PORT, -b BASE_PORT
                        The first UDP port number for the listeners. The first antenna will be assigned to this port and each subsequent antenna to the next number up (e.g.
                        10000, 10001, 10002,...)
  --no-output, -n       Used for performance tuning. Disables disk IO.
  --enable-affinity, -e
                        Enable CPU affiniity
```

### Output Format
The output format saved to disk from the XEngine is similar to xGPU's output.  It is a lower-triangular order laid out as a one-dimensional array.  So each baseline pair ``[1,1],[2,1],[2,2],[3,1],[3,2],[3,3],[4,1]...[n,n]`` is saved (the upper triangular half of the matrix above the autocorrelation line is just the complex conjugate.  So space is saved by not storing it as redundant information).  The data output can best be described as a matrix with the following dimensions: ``[t][baseline][frequency][npol^2]``.  
             
A JSON file is also output with it providing required observation data.  The following is an example from a Cassiopeia A observation:

```
{
"sync_timestamp":17648512,
"first_seq_num":17648768,
"object_name":"CasA",
"num_baselines":6,
"first_channel":1920,
"first_channel_center_freq":2968000000.000000,
"channels":256,
"channel_width":250000.000000,
"polarizations":2,
"antennas":3,
"antenna_names":["1f","4k","4g"],
"ntime":10000,
"samples_per_block":6144,
"bytes_per_block":49152,
"data_type":"cf32_le",
"data_format": "triangular order"
}
```

### Processing Output Data

The following python sample code shows a brief example of using the output correlated binary data and JSON file together to post-process the data. (Note this would require numpy and astropy to have been installed to run this code).


```
import numpy as np
import json
from astropy.coordinates import SkyCoord

x = np.fromfile(xengine_output_file, dtype = 'complex64')
with open(jsonfile) as f:
    metadata = json.load(f)
ants = metadata['antenna_names']
n_ants = len(ants)
n_baselines = n_ants * (n_ants + 1) // 2
n_pols = metadata['polarizations']
n_chans = metadata['channels']
chan_fs = metadata['channel_width']
n = n_baselines * n_pols * n_chans
x = x[:x.size//n*n].reshape((-1, n_chans, n_baselines, n_pols**2))
f_first = metadata['first_channel_center_freq']
f = n_chans/2*chan_fs + f_first
T = metadata['ntime'] / chan_fs
source = SkyCoord.from_name(metadata['object_name'])
```

Daniel Estivez also has a separate repository with scripts for interferometry at the ATA here: [ata_interferometry](https://github.com/daniestevez/ata_interferometry).  Specifically, there are a number of scripts in the postprocess directory that have been used in real pipelines to correct for fringe rotation (fringe_stop.py) and to create a UVFITS output file (xengine_to_uvfits.py) that can be used with common applications such as [casa](https://casa.nrao.edu/) that can handle UVFITS files.

### Optimizing Performance

From the SNAP Source block perspective, several things can dramatically affect performance.  First, it is recommended that CPU's be modern CPU's running at least at a base clock speed of 2.4 GHz.  However, successful testing with 12 antennas was executed on a Xeon 4216 CPU with a clock speed of 2.1 GHz with virtualization **enabled**.  While the network threads per SNAP block are not necesarily demanding, timing is critical to keep up with the network.  Both thread count and per-thread performance will matter.  In general with an X-Engine, you can calculate the number of spawned threads as 2*SNAPs + 2 [for the correlator] + 1 [primary thread].  So while some applications may benefit from disabling virtualization, this will benefit from keeping virtualization enabled.  The Xeon 4216 for instance has 16 cores, 32 virtual pipelines, so 12 antennas can run comfortably on one CPU.  Given the low per-thread usage though in the network blocks, it should be possible to "double up" on one CPU and not completely require exclusivity on CPU cores.

Another important optimization for the SNAP source block will be network tuning.  In sysctl.conf (or from the command-line for temporary settings), the following settings are recommended:

```
net.core.rmem_default=26214400 
net.core.rmem_max=104857600 
net.core.wmem_default=1024000 
net.core.wmem_max=104857600 
net.core.netdev_max_backlog=300000 
```

If these are put in sysctl.conf, issue the command ``sudo sysctl --system`` to apply the changes.  

They can also be temporarily applied from the command-line by prepending each with ``sudo sysctl -w``.

ethtool -g <interface> should also be used to look at the receive buffer settings.  If the current hardware settings are not set to the maximums, they should be increased.  The following shows an example where the hardware settings are not the maximum:

```
ethtool -g enp225s0f0
Ring parameters for enp225s0f0:
Pre-set maximums:
RX:		8192
RX Mini:	0
RX Jumbo:	0
TX:		8192
Current hardware settings:
RX:		1024
RX Mini:	0
RX Jumbo:	0
TX:		1024
```

The RX ring buffer can be increased with the command ``ethtool -G <interface> rx 8192``.  Note if the maximum is not 8192, change the setting accordingly.

If using a Mellanox network adapter, [libvma](https://github.com/Mellanox/libvma) can also be used.  This library can be pre-loaded without any code changes in the application to provide faster data transfers by skipping the kernel and going straight to user space for a dramatic performance improvement.  Using libvma simply requires LD_PRELOAD=libvma.so before the application on the command-line, such as:

```
LD_PRELOAD=libvma.so ./ata_multi_ant_xcorr.py --snap-sync=1616950797 --integration-frames=20000 --output-prefix=casa --object-name=3c461 --starting-channel=1920 --num-channels=256 --antenna-list=1a,1f,4g,5c,1c,2b,2h,1h,1k,4j,2a,3c --starting-chan-freq=2968000000.0 --output-directory=$HOME/xengine_output/staging
```

Since the SNAPs use UDP as their transport medium, some drops are to be expected.  However, a properly tuned system should have very few (or ideally none).  If you see a lot of dropped packets when you run the SNAP source, you will probably need to go back and determine what system components are not keeping up or need to be tuned.

Another important consideration will be the effects of NUMA in a multi-CPU system.  Generally each "node" (CPU) will have associated hardware such as memory, a GPU, and a network adapter.  Any time access from one node crosses to hardware on a different node, the cost of access is higher than local resources.  So it should be avoided, or at least minimized.  Measurements crossing an interconnect with an NVIDIA 3090 GPU showed a difference of 145 Gbps (different node) versus 195 Gbps (same node) and can have unexpected impacts on performance.

Disks are another very important consideration when using the X-Engine.  NVMe drives can generally write at around 3500 MB/s.  SSD drives may come down to 250 MB/s, and standard spinning drives could be around 100 MB/s.  RAID setups can also help, and there are a number of calculators that can help predict RAID performance based on drive type, RAID type, and number of drives.  However, faster is better.  Some of this can be offset by integration times if your data source can account for delays.  Longer integrations will result in fewer disk writes.  However, shorter integrations may be required to correct for delays in post-processing.

Lastly, once an X-Engine is running and synchronized, it will fill in any missing frames in its internal queues with zeros.  However, if the pipeline is not keeping up, these queues can eventually grow and consume the memory in the system.  Over short periods (2-5 minutes), this can let you oversubscribe the pipeline depending on how much memory you have.  But it is not recommended.  Generally, using a tool such as htop and  monitoring memory usage until you are sure your system is fast enough for your antenna configuration and can keep up is recommended.

## Supporting Apps and Information

There are a few support / testing apps in the apps subdirectory that could be of interest.

### Scripts
datetime_from_timestamp.py - Takes a Unix timestamp (e.g. SNAP sync timestamp) and prints the date/time in a readable format.

numa_list_nodes.py - Prints information about CPU-to-node numbers.

read_voltage_pcap_v2.py - Reads the SNAP v2.0 packets and can print out the header information including timestamps and packet starting channels.  A number of other features are available and can be seen from the --help option.

replay_pcap_to_mcast.py - Can replay a PCAP recording back out to a multicast group.  Note: Playback speed will be slower than realtime given the high packet rates.

replay_pcap_to_udp.py - Can replay a PCAP recording back out to a specific destination.  Note: Playback speed will be slower than realtime given the high packet rates.

### ATA Data Files
antenna_cordinets_ecef.txt - ATA telescope locations in ECEF coordinates.

antenna_cordinets_enu.txt - ATA telescope locations in a relative ENU coordinate system.

#!/usr/bin/env python3

import json
import argparse
from datetime import datetime
import pprint
pp = pprint.PrettyPrinter(indent=4)
from tzlocal import get_localzone
local_tz = get_localzone() 

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='ATA Observation Descriptor Generator')
    parser.add_argument('--template', type=str, help="Template file to be used.  If not provided, ./", required=False, default="./observation_descriptor_template.json")
    parser.add_argument('--outputfile', type=str, help="File name to save the generated observation json to.", required=True)
    parser.add_argument('--synctime', type=int, help="This is the SNAP start synchronization timestamp at which the observation was started.  Format will look something like this: 1612923335", required=True)
    parser.add_argument('--object_name', type=str, help="Designator of observed target.  E.g. Casa would be 3C286", required=True)
    parser.add_argument('--antennas', type=str, help="Comma-separated list of antennas in order (no spaces)", required=True)
    parser.add_argument('--xdir', type=str, help="Directory where xengine output data and the observation file will be", required=True)
    parser.add_argument('--basename', type=str, help="The base name of the xengine files.  Ex: ata_2021_03_07_10_03", required=True)
    parser.add_argument('--skyfreq', type=float, help="The center frequency of the observation in Hz", required=True)
    parser.add_argument('--startchannel', type=float, help="Number of the starting channel sent to the xengine", required=True)
    parser.add_argument('--channels', type=int, help="Number of channels in this observation", required=True)
    parser.add_argument('--integration-time', type=float, help="Integration time in seconds.  Note that each ATA time frame is 4 microseconds.  So 10000 integration frames = 0.04 seconds integration time", required=True)
    parser.add_argument('--ntime', type=int, help="Number of integration frames.  This is related to integration time as ntime * frame_time = integration_time", required=True)
    parser.add_argument('--disable-delay-correction',help="Disable delay correction and only phase", action='store_true', required=False)
     
    args = parser.parse_args()

    try:
        with  open(args.template) as f:
            obs_desc = json.load(f)
    except Exception as e:
        print("ERROR opening template file: " + str(e))
        exit(1)
        
    args.antennas.replace(" ", "")
    ant_list = args.antennas.split(",")
    
    num_ant = len(ant_list)
    if num_ant == 0:
        print("ERROR: Antennas must be provided.")
        exit(1)
        
    baselines = num_ant * (num_ant + 1) // 2

    # Time information
    obs_time = datetime.fromtimestamp(args.synctime, local_tz)
    obs_desc['observation_start'] = obs_time.strftime("%Y-%m-%d %H:%M:%S.%f")
    obs_desc['observation_start_timestamp'] = args.synctime
    obs_desc['snap_sync_time'] = args.synctime
    obs_desc['ntime'] = args.ntime
    
    # Object
    obs_desc['object_name'] = args.object_name

    # channels
    obs_desc['channels'] = args.channels
    
    # Antenna setup
    obs_desc['baselines'] = baselines
    obs_desc['antenna_names'] = ant_list
    
    # Sky frequency info
    first_chan_center = args.skyfreq - 250000*(2048-args.startchannel)
    obs_desc['first_channel_center_freq'] = first_chan_center

    # Observation xengine integration time.
    obs_desc['integration_time_seconds'] = args.integration_time
    
    # Delay correction
    obs_desc['correct_for_delay'] = not args.disable_delay_correction
    
    # File locations
    obs_desc['input_dir'] = args.xdir
    obs_desc['output_dir'] = args.xdir
    obs_desc['observation_base_name'] = args.basename
    
    # Output
    try:
        with  open(args.outputfile, "w") as f:
            json_str = pprint.pformat(obs_desc, indent=4)
            json_str = json_str.replace("'", '"')
            f.write(json_str)
    except Exception as e:
        print("ERROR writing file: " + str(e))
        exit(1)

    print("Done. File written to " + args.outputfile)
    

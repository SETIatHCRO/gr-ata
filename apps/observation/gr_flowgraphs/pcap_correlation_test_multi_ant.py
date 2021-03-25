#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: ATA New SNAP X-Engine
# Author: ghostop14
# GNU Radio version: 3.8.2.0

from gnuradio import gr
from gnuradio.filter import firdes
import sys
import signal
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
import ata
import clenabled
import multiprocessing

class snap_correlation_test_3ant_synch_in_xengine(gr.top_block):

    def __init__(self):
        gr.top_block.__init__(self, "ATA New SNAP X-Engine")

        ##################################################
        # Variables
        ##################################################
        self.starting_channel = starting_channel = clparam_starting_channel
        self.num_channels = num_channels = clparam_num_channels
        self.output_file = output_file = clparam_output_directory + '/casa_2021_jan_04_sync_v3_xeng'
        self.ending_channel = ending_channel = starting_channel+num_channels-1

        ##################################################
        # Blocks
        ##################################################
        self.clenabled_clXEngine_0 = clenabled.clXEngine(1,1,0,0,False, 6, 2, clparam_num_antennas, 1, starting_channel, num_channels, 
                                                                                            clparam_integration_frames, clparam_antenna_list, True,output_file,0,True, 
                                                                                            clparam_snap_sync, clparam_object_name, clparam_starting_chan_freq, clparam_channel_width, clparam_no_output)

        if clparam_enable_affinity:
            num_cores = multiprocessing.cpu_count()
            self.clenabled_clXEngine_0.set_processor_affinity([0, 1])

        self.antenna_list = []
        for i in range(0, clparam_num_antennas):
            if i == 0:
                input_file = '/home/sonata/casa_pcap/snap_2_ant_1f.pcap'
                input_port = clparam_base_port + i
            elif i == 1:
                input_file = '/home/sonata/casa_pcap/snap_7_ant_3c.pcap'
                input_port = clparam_base_port + i
            else:
                input_file = '/home/sonata/casa_pcap/snap_8_ant_4g.pcap'
                input_port = 10002
                
            new_ant = ata.snap_source(input_port, 1, True, False, False,starting_channel,ending_channel,3, input_file, False, True, '224.1.1.10',  False)
            if clparam_enable_affinity:
                if (i+3) < num_cores:
                    new_ant.set_processor_affinity([i+2, i+3])
            ##################################################
            # Connections
            ##################################################
            self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (new_ant, 'sync'))
            self.connect((new_ant, 0), (self.clenabled_clXEngine_0, i))
            self.antenna_list.append(new_ant)
        
    def get_starting_channel(self):
        return self.starting_channel

    def set_starting_channel(self, starting_channel):
        self.starting_channel = starting_channel
        self.set_ending_channel(self.starting_channel+self.num_channels-1)

    def get_num_channels(self):
        return self.num_channels

    def set_num_channels(self, num_channels):
        self.num_channels = num_channels
        self.set_ending_channel(self.starting_channel+self.num_channels-1)

    def get_ending_channel(self):
        return self.ending_channel

    def set_ending_channel(self, ending_channel):
        self.ending_channel = ending_channel





def main(top_block_cls=snap_correlation_test_3ant_synch_in_xengine, options=None):
    if gr.enable_realtime_scheduling() != gr.RT_OK:
        print("Error: failed to enable real-time scheduling.")
    tb = top_block_cls()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        sys.exit(0)

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    tb.start()

    tb.wait()


if __name__ == '__main__':
    parser = ArgumentParser(description='ATA Multi-Antenna X-Engine - PCAP Test App')
    parser.add_argument('--snap-sync', '-s', type=int, default='ata', help="The unix timestamp when the SNAPs started and were synchronized", required=True)
    parser.add_argument('--object-name', '-o', type=str, help="Name of viewing object.  E.g. 3C84 or 3C461 for CasA", required=True)
    parser.add_argument('--antenna-list', '-a', type=str, help="Comma-separated list of antennas used (no spaces).  This will be used to also define num_antennas.", required=True)
    parser.add_argument('--num-channels', '-c', type=int, help="Number of channels being received from SNAP (should be a multiple of 256)", required=True)
    parser.add_argument('--starting-channel', '-t', type=int, help="Starting channel number being received from the SNAP", required=True)
    parser.add_argument('--starting-chan-freq', '-f', type=float, help="Center frequency (in Hz) of the first channel (e.g. for 3 GHz sky freq and 256 channels, first channel would be 2968000000.0", required=True)
    parser.add_argument('--channel-width', '-w', type=float, default=250000.0,  help="[Optional] Channel width.  For now for the ATA, this number should be 250000.0", required=False)
    parser.add_argument('--integration-frames', '-i', type=int, help="Number of Frames to integrate in the correlator.  Note this should be a multiple of 16 to optimize the way the SNAP outputs frames (e.g. 10000, 20000, or 24000 but not 25000). Each frame is 4 microseconds so an integration of 10000 equates to a time of 0.04 seconds.", required=True)
    parser.add_argument('--output-directory', '-d', type=str, help="Directory path to where correlation outputs should be written. If set to the word 'none', no output will be generated (useful for performance testing).", required=True)
    parser.add_argument('--output-prefix', '-p', type=str, default='ata', help="If specified, this prefix will be prepended to the output files.  Otherwise 'ata' will be used.", required=False)
    parser.add_argument('--base-port', '-b', type=int, default=10000, help="The first UDP port number for the listeners.  The first antenna will be assigned to this port and each subsequent antenna to the next number up (e.g. 10000, 10001, 10002,...)", required=False)
    parser.add_argument('--no-output', '-n', help="Used for performance tuning.  Disables disk IO.", action='store_true', required=False)

    args = parser.parse_args()
    clparam_snap_sync = args.snap_sync
    clparam_object_name = args.object_name
    clparam_antenna_list = args.antenna_list.replace(' ', '').split(',')
    clparam_num_antennas = len(clparam_antenna_list)
    clparam_starting_channel = args.starting_channel
    clparam_starting_chan_freq = args.starting_chan_freq
    clparam_num_channels = args.num_channels
    clparam_integration_frames = args.integration_frames
    clparam_no_output = args.no_output
    clparam_output_directory = args.output_directory
    clparam_output_prefix = args.output_prefix
    clparam_channel_width = args.channel_width
    clparam_base_port = args.base_port
    
    clparam_enable_affinity = False
    
    if clparam_num_antennas < 2:
        print("ERROR: Please provide at least 2 antennas")
        exit(1)
        
    if (clparam_num_channels % 256) > 0 or clparam_num_channels < 256 or clparam_num_channels > 4096:
        print("ERROR: The number of channels must be a multiple of 256 from 256 to 4096")
        exit(1)
        
    if (clparam_integration_frames % 16) > 0:
        print("ERROR: The number of integration frames should be a multiple of 16 to optimize the SNAP->xengine pipeline")
        exit(1)

    if not os.path.exists(clparam_output_directory):
        print("ERROR: The specified output directory does not exist.")
        exit(1)
    
    main()

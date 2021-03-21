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
        self.clenabled_clXEngine_0 = clenabled.clXEngine(1,2,0,0,False, 6, 2, clparam_num_antennas, 1, starting_channel, num_channels, clparam_integration_frames, True,output_file,0,True)
        self.clenabled_clXEngine_0.set_processor_affinity([0, 1])
        
        self.antenna_list = []
        for i in range(0, clparam_num_antennas):
            if i == 0:
                input_file = '/home/sonata/casa_pcap/snap_2_ant_1f.pcap'
                input_port = 10000 + i
            elif i == 1:
                input_file = '/home/sonata/casa_pcap/snap_7_ant_3c.pcap'
                input_port = 10000 + i
            else:
                input_file = '/home/sonata/casa_pcap/snap_8_ant_4g.pcap'
                input_port = 10002
                
            new_ant = ata.snap_source(input_port, 1, True, False, False,starting_channel,ending_channel,3, input_file, False, True, '224.1.1.10')
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
    parser = ArgumentParser(description='X-Engine Data Extractor')
    parser.add_argument('--num-antennas', '-a', type=int, help="Number of antennas to use.  The first antenna will listen on UDP 10000, and each successive one will increment the listening port by 1.", required=True)
    parser.add_argument('--starting-channel', '-s', type=int, help="Starting channel number being received from the SNAP", required=True)
    parser.add_argument('--num-channels', '-c', type=int, help="Number of channels being received from SNAP (should be a multiple of 256)", required=True)
    parser.add_argument('--integration-frames', '-n', type=int, help="Number of Frames to integrate in the correlator.  Each frame is 4 microseconds.  So 4us * integration_frames=integration time.", required=True)
    parser.add_argument('--output-directory', '-o', type=str, help="Directory path to where correlation outputs should be written", required=True)
    
    args = parser.parse_args()
    clparam_starting_channel = args.starting_channel
    clparam_num_channels = args.num_channels
    clparam_integration_frames = args.integration_frames
    clparam_output_directory = args.output_directory
    clparam_num_antennas = args.num_antennas
    
    main()
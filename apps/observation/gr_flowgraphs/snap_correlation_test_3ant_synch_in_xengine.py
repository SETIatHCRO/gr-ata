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
        self.starting_channel = starting_channel = 1920
        self.num_channels = num_channels = 256
        self.ending_channel = ending_channel = starting_channel+num_channels-1

        ##################################################
        # Blocks
        ##################################################
        self.clenabled_clXEngine_0 = clenabled.clXEngine(1,1,0,0, False, 6, 2, 3, 1, starting_channel, num_channels, 1024, '1f,3c,4g'.replace(' ','').split(','), True,'/home/mpiscopo/xengine_output/2021_Jan_04/casa_2021_jan_04_sync_v3_xeng',0,True,0, 'CasA', 2968000000.0, 25000.0, False)
        self.clenabled_clXEngine_0.set_processor_affinity([0, 1])
        self.ata_snap_source_0_0_0 = ata.snap_source(10002, 1, True, False, False,starting_channel,ending_channel,3, '/home/sonata/casa_pcap/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0 = ata.snap_source(10001, 1, True, False, False,starting_channel,ending_channel,3, '/home/sonata/casa_pcap/snap_7_ant_3c.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0 = ata.snap_source(10000, 1, True, False, False,starting_channel,ending_channel,3, '/home/sonata/casa_pcap/snap_2_ant_1f.pcap', False, True, '224.1.1.10')



        ##################################################
        # Connections
        ##################################################
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0, 'sync'))
        self.connect((self.ata_snap_source_0, 0), (self.clenabled_clXEngine_0, 0))
        self.connect((self.ata_snap_source_0_0, 0), (self.clenabled_clXEngine_0, 1))
        self.connect((self.ata_snap_source_0_0_0, 0), (self.clenabled_clXEngine_0, 2))


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
    main()

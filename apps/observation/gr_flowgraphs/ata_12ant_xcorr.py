#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: ATA 12 Antenna XEngine
# Author: ghostop14
# GNU Radio version: 3.8.2.0

from datetime import datetime
from gnuradio import gr
from gnuradio.filter import firdes
import sys
import signal
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
import ata
import clenabled


class ata_12ant_xcorr(gr.top_block):

    def __init__(self):
        gr.top_block.__init__(self, "ATA 12 Antenna XEngine")

        ##################################################
        # Variables
        ##################################################
        self.now = now = datetime.now()
        self.starting_channel = starting_channel = 1920
        self.num_channels = num_channels = 256
        self.file_timestamp = file_timestamp = now.strftime("%Y_%m_%d_%H_%M")
        self.output_file = output_file = '/home/mpiscopo/xengine_output/staging/ata_' + file_timestamp
        self.ending_channel = ending_channel = starting_channel+num_channels-1

        ##################################################
        # Blocks
        ##################################################
        self.clenabled_clXEngine_0 = clenabled.clXEngine(1,1,0,0,False, 6, 2, 12, 1, starting_channel, num_channels, 10000, True,output_file,0,True)
        self.ata_snap_source_0_0_0_0_0_1_0_0_0_0_0 = ata.snap_source(10011, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0_0_0_1_0_0_0_0 = ata.snap_source(10010, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0_0_0_1_0_0_0 = ata.snap_source(10009, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0_0_0_1_0_0 = ata.snap_source(10008, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0_0_0_1_0 = ata.snap_source(10007, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0_0_0_1 = ata.snap_source(10006, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0_0_0_0 = ata.snap_source(10004, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0_0_0 = ata.snap_source(10005, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0_0 = ata.snap_source(10003, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0_0 = ata.snap_source(10002, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_8_ant_4g.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0_0 = ata.snap_source(10001, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_5_ant_2a.pcap', False, True, '224.1.1.10')
        self.ata_snap_source_0 = ata.snap_source(10000, 1, True, False, False,starting_channel,ending_channel,1, '/home/sonata/casa_pcap_feb9/snap_2_ant_1f.pcap', False, True, '224.1.1.10')



        ##################################################
        # Connections
        ##################################################
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0_0_1, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0_0_1_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0_0_1_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0_0_1_0_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0_0_1_0_0_0_0, 'sync'))
        self.msg_connect((self.clenabled_clXEngine_0, 'sync'), (self.ata_snap_source_0_0_0_0_0_1_0_0_0_0_0, 'sync'))
        self.connect((self.ata_snap_source_0, 0), (self.clenabled_clXEngine_0, 0))
        self.connect((self.ata_snap_source_0_0, 0), (self.clenabled_clXEngine_0, 1))
        self.connect((self.ata_snap_source_0_0_0, 0), (self.clenabled_clXEngine_0, 2))
        self.connect((self.ata_snap_source_0_0_0_0, 0), (self.clenabled_clXEngine_0, 3))
        self.connect((self.ata_snap_source_0_0_0_0_0, 0), (self.clenabled_clXEngine_0, 5))
        self.connect((self.ata_snap_source_0_0_0_0_0_0, 0), (self.clenabled_clXEngine_0, 4))
        self.connect((self.ata_snap_source_0_0_0_0_0_1, 0), (self.clenabled_clXEngine_0, 6))
        self.connect((self.ata_snap_source_0_0_0_0_0_1_0, 0), (self.clenabled_clXEngine_0, 7))
        self.connect((self.ata_snap_source_0_0_0_0_0_1_0_0, 0), (self.clenabled_clXEngine_0, 8))
        self.connect((self.ata_snap_source_0_0_0_0_0_1_0_0_0, 0), (self.clenabled_clXEngine_0, 9))
        self.connect((self.ata_snap_source_0_0_0_0_0_1_0_0_0_0, 0), (self.clenabled_clXEngine_0, 10))
        self.connect((self.ata_snap_source_0_0_0_0_0_1_0_0_0_0_0, 0), (self.clenabled_clXEngine_0, 11))


    def get_now(self):
        return self.now

    def set_now(self, now):
        self.now = now

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

    def get_file_timestamp(self):
        return self.file_timestamp

    def set_file_timestamp(self, file_timestamp):
        self.file_timestamp = file_timestamp
        self.set_output_file('/home/mpiscopo/xengine_output/staging/ata_' + self.file_timestamp)

    def get_output_file(self):
        return self.output_file

    def set_output_file(self, output_file):
        self.output_file = output_file

    def get_ending_channel(self):
        return self.ending_channel

    def set_ending_channel(self, ending_channel):
        self.ending_channel = ending_channel





def main(top_block_cls=ata_12ant_xcorr, options=None):
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

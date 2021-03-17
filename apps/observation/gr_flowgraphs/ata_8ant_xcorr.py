#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: ATA 8 Antenna XEngine
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


class ata_8ant_xcorr(gr.top_block):

    def __init__(self):
        gr.top_block.__init__(self, "ATA 8 Antenna XEngine")

        ##################################################
        # Variables
        ##################################################
        self.starting_channel = starting_channel = 1920
        self.num_channels = num_channels = 256
        self.now = now = datetime.now()
        self.ending_channel = ending_channel = starting_channel+num_channels-1
        self.snap_vector_len = snap_vector_len = int((ending_channel-starting_channel+1)*2)
        self.sky_freq = sky_freq = 3000e6
        self.file_timestamp = file_timestamp = now.strftime("%Y_%m_%d_%H_%M")
        self.channel_width = channel_width = 250e3
        self.center_channel = center_channel = starting_channel + num_channels/2
        self.start_freq = start_freq = sky_freq - (2048-starting_channel)*channel_width
        self.snap_samp_rate = snap_samp_rate = num_channels/4e-6
        self.snap_bandwidth = snap_bandwidth = channel_width*num_channels
        self.output_file = output_file = '/home/mpiscopo/xengine_output/staging/ata_' + file_timestamp
        self.half_channel_width = half_channel_width = channel_width/2.0
        self.gain = gain = 1
        self.data_vector_len = data_vector_len = int(snap_vector_len/2)
        self.center_freq = center_freq = sky_freq + (center_channel-2048)*channel_width

        ##################################################
        # Blocks
        ##################################################
        self.clenabled_clXEngine_0 = clenabled.clXEngine(1,1,0,0,False, 6, 2, 8, 1, starting_channel, num_channels, 10000, True,output_file,0,True)
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
        self.connect((self.ata_snap_source_0, 0), (self.clenabled_clXEngine_0, 0))
        self.connect((self.ata_snap_source_0_0, 0), (self.clenabled_clXEngine_0, 1))
        self.connect((self.ata_snap_source_0_0_0, 0), (self.clenabled_clXEngine_0, 2))
        self.connect((self.ata_snap_source_0_0_0_0, 0), (self.clenabled_clXEngine_0, 3))
        self.connect((self.ata_snap_source_0_0_0_0_0, 0), (self.clenabled_clXEngine_0, 5))
        self.connect((self.ata_snap_source_0_0_0_0_0_0, 0), (self.clenabled_clXEngine_0, 4))
        self.connect((self.ata_snap_source_0_0_0_0_0_1, 0), (self.clenabled_clXEngine_0, 6))
        self.connect((self.ata_snap_source_0_0_0_0_0_1_0, 0), (self.clenabled_clXEngine_0, 7))


    def get_starting_channel(self):
        return self.starting_channel

    def set_starting_channel(self, starting_channel):
        self.starting_channel = starting_channel
        self.set_center_channel(self.starting_channel + self.num_channels/2)
        self.set_ending_channel(self.starting_channel+self.num_channels-1)
        self.set_snap_vector_len(int((self.ending_channel-self.starting_channel+1)*2))
        self.set_start_freq(self.sky_freq - (2048-self.starting_channel)*self.channel_width)

    def get_num_channels(self):
        return self.num_channels

    def set_num_channels(self, num_channels):
        self.num_channels = num_channels
        self.set_center_channel(self.starting_channel + self.num_channels/2)
        self.set_ending_channel(self.starting_channel+self.num_channels-1)
        self.set_snap_bandwidth(self.channel_width*self.num_channels)
        self.set_snap_samp_rate(self.num_channels/4e-6)

    def get_now(self):
        return self.now

    def set_now(self, now):
        self.now = now

    def get_ending_channel(self):
        return self.ending_channel

    def set_ending_channel(self, ending_channel):
        self.ending_channel = ending_channel
        self.set_snap_vector_len(int((self.ending_channel-self.starting_channel+1)*2))

    def get_snap_vector_len(self):
        return self.snap_vector_len

    def set_snap_vector_len(self, snap_vector_len):
        self.snap_vector_len = snap_vector_len
        self.set_data_vector_len(int(self.snap_vector_len/2))

    def get_sky_freq(self):
        return self.sky_freq

    def set_sky_freq(self, sky_freq):
        self.sky_freq = sky_freq
        self.set_center_freq(self.sky_freq + (self.center_channel-2048)*self.channel_width)
        self.set_start_freq(self.sky_freq - (2048-self.starting_channel)*self.channel_width)

    def get_file_timestamp(self):
        return self.file_timestamp

    def set_file_timestamp(self, file_timestamp):
        self.file_timestamp = file_timestamp
        self.set_output_file('/home/mpiscopo/xengine_output/staging/ata_' + self.file_timestamp)

    def get_channel_width(self):
        return self.channel_width

    def set_channel_width(self, channel_width):
        self.channel_width = channel_width
        self.set_center_freq(self.sky_freq + (self.center_channel-2048)*self.channel_width)
        self.set_half_channel_width(self.channel_width/2.0)
        self.set_snap_bandwidth(self.channel_width*self.num_channels)
        self.set_start_freq(self.sky_freq - (2048-self.starting_channel)*self.channel_width)

    def get_center_channel(self):
        return self.center_channel

    def set_center_channel(self, center_channel):
        self.center_channel = center_channel
        self.set_center_freq(self.sky_freq + (self.center_channel-2048)*self.channel_width)

    def get_start_freq(self):
        return self.start_freq

    def set_start_freq(self, start_freq):
        self.start_freq = start_freq

    def get_snap_samp_rate(self):
        return self.snap_samp_rate

    def set_snap_samp_rate(self, snap_samp_rate):
        self.snap_samp_rate = snap_samp_rate

    def get_snap_bandwidth(self):
        return self.snap_bandwidth

    def set_snap_bandwidth(self, snap_bandwidth):
        self.snap_bandwidth = snap_bandwidth

    def get_output_file(self):
        return self.output_file

    def set_output_file(self, output_file):
        self.output_file = output_file

    def get_half_channel_width(self):
        return self.half_channel_width

    def set_half_channel_width(self, half_channel_width):
        self.half_channel_width = half_channel_width

    def get_gain(self):
        return self.gain

    def set_gain(self, gain):
        self.gain = gain

    def get_data_vector_len(self):
        return self.data_vector_len

    def set_data_vector_len(self, data_vector_len):
        self.data_vector_len = data_vector_len

    def get_center_freq(self):
        return self.center_freq

    def set_center_freq(self, center_freq):
        self.center_freq = center_freq





def main(top_block_cls=ata_8ant_xcorr, options=None):
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

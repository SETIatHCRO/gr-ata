#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2020 ewhite42.
#
# This is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# This software is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this software; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.
#

''' This script contains the functionality necessary to
    create a GNU Radio block to control subsets of the
    Allen Telescope Array.

    Ellie White 2 July 2020.
'''

import time
#import numpy
from gnuradio import gr
import pmt
from ATATools import ata_control as ac

obs_info = {}

class control(gr.basic_block):
    """
    This block contains the necessary functions to initialize
    observations with the ATA, and point and track a subset of
    the antennas on a given source if commanded to do so.
    """
    def __init__(self):
        gr.basic_block.__init__(self,
                                name="control",
                                in_sig=None,
                                out_sig=None)

        '''self.cfreq = cfreq #center frequency
        self.ant_list = [a.strip() for a in ant_list.split(',')] #list of antennas to observe with
        self.src_list = [s.strip() for s in src_list.split(',')] #list of source names
        self.dur_list = dur_list #list of scan durations, in seconds'''
        
        self.message_port_register_in(pmt.intern("command"))
        self.set_msg_handler(pmt.intern("command"), self.handle_msg)      
        
        '''print("obs_info: ", obs_info)
        
        print("Frequency: ", self.cfreq)
        print("Antennas: ", self.ant_list)
        print("Sources: ", self.src_list)
        print("Durations: ", self.dur_list)
        
        
        self.run()
        print("All done!")'''
        
    def handle_msg(self, msg):
        ''' message handler function'''
        print("in message handler")
        obs_info = pmt.to_python(msg)
        
        self.cfreq = obs_info['freq']
        self.ant_list = [a.strip() for a in obs_info['antennas_list'].split(',')]
        self.src_list = [s.strip() for s in obs_info["source_list"].split(',')] #
        self.dur_list = obs_info["durations_list"] #list of scan durations, in seconds

        self.begin()
        self.run()
        
    def begin(self):
        ''' initialize the observation '''
        #try to reserve the antennas you want to observe with;
        #if it doesn't work you need to release the antennas,
        #then re-reserve them.
        try:
            ac.reserve_antennas(self.ant_list)

        except RuntimeError:
            print("Antennas were not released after last run. Releasing and reserving.")
            ac.release_antennas(self.ant_list, False)
            ac.reserve_antennas(self.ant_list)

        #check if the LNAs are on -- if not, turn them on
        ac.try_on_lnas(self.ant_list)

        #run setup with Autotune
        ac.autotune(self.ant_list)

        #set the center frequency
        ac.set_freq(self.cfreq, self.ant_list)
        

    def point_and_track(self, src, dur):
        ''' Tells the antenna to point and track on a given target '''
        #create ephemeris file that tells the antenna
        #where it should be pointing at each timestamp
        ac.make_and_track_ephems(src, self.ant_list)

        #stay on source for given duration
        time.sleep(dur)

    def set_freq(self, new_freq):
        '''reset the center frequency '''
        ac. set_freq(new_freq, self.ant_list)

    def get_eq_coords(self):
        ''' return current RA and Dec coordinates '''
        curr_radec = ac.getRaDec(self.ant_list)
        return curr_radec

    def end_session(self):
        ''' release antennas at the end of a session '''
        ac.release_antennas(self.ant_list, True)

    def run(self):
        ''' this function runs the control script '''
        num = len(self.src_list)
        ra_dec = self.get_eq_coords()
        print(ra_dec)

        for i in range(num):
            self.point_and_track(self.src_list[i], self.dur_list[i])
            print(ra_dec)

        self.end_session()


    def forecast(self, noutput_items, ninput_items_required):
        #setup size of input_items[i] for work call
        for i in range(len(ninput_items_required)):
            ninput_items_required[i] = noutput_items

    def general_work(self, input_items, output_items):
        print("in work function")
        #output_items[0][:] = input_items[0]
        #consume(0, len(input_items[0]))        #self.consume_each(len(input_items[0]))
        #return len(output_items[0])
        
    '''def __del__(self):
        run this function to cleanly exit the session
        ac.release_antennas(self.ant_list, True)
        print("Antennas have been released. Session complete.")'''

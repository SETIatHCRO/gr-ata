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
    def __init__(self, username):
        gr.basic_block.__init__(self,
                                name="control",
                                in_sig=None,
                                out_sig=None)
        
        self.username = username
        alarm = ac.get_alarm()
        
        if alarm['user'] == self.username:
            self.message_port_register_in(pmt.intern("command"))
            self.set_msg_handler(pmt.intern("command"), self.handle_msg)
            
        else:
            raise Exception("Wrong username, you do not have permission to observe")

        
    def handle_msg(self, msg):
        ''' message handler function'''
        print("in message handler")
        self.obs_info = pmt.to_python(msg)
        
        self.cfreq = self.obs_info['freq']
        self.ant_list = [a.strip() for a in self.obs_info['antennas_list'].split(',')]
        self.src_list = [s.strip() for s in self.obs_info["source_list"].split(',')] #
        self.dur_list = self.obs_info["durations_list"] #list of scan durations, in seconds
        self.obs_type = self.obs_info["obs_type"]
        
        self.begin()
        self.run()
        self.end_session()
        self.work()
        
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

        #set the center frequency
        ac.set_freq(self.cfreq, self.ant_list)
        
        
    def run(self):
        ''' this function runs the control script '''
        
        if self.obs_type == "track":
            self.track()
        
        elif self.obs_type == "onoff":
            print("On-Off hasn't been implemented yet")
            
        else:
            print("Sorry, you didn't select an observation type. Ending session.")
        
    def track(self):
    
        ''' run a sequence of tracking observations '''
    
        self.coord_type = self.obs_info["coord_type"]

        #point at first source and autotune
        if self.coord_type == "id":
            ac.make_and_track_ephems(self.src_list[0], self.ant_list)
            
        elif self.coord_type == "azel":
            #insert call to ata control fn
            print("az-el not yet implemented")
            
        elif self.coord_type == "radec":
            #insert fn call
            print("radec not yet implemented")
            
        else:
            print("No coordinate type specified!")
            return
            
        #run setup with Autotune
        ac.autotune(self.ant_list)
        
        #track on the first source for a given duration
        time.sleep(self.dur_list[0])
        print("Done tracking on {0}".format(self.src_list[0]))
        
        #track on remaining sources
        
        num = len(self.src_list) - 1

        for i in range(num):
        
            if self.coord_type == "id":
                ac.make_and_track_ephems(self.src_list[i+1], self.ant_list)
            
            elif self.coord_type == "azel":
                #insert call to ata control fn
                print("azel not yet implemented")
            
            elif self.coord_type == "radec":
                #insert fn call
                print("radec not implemented")
            
            else:
                print("No coordinate type specified!")
                return
                
            time.sleep(self.dur_list[i+1])
            print("Done tracking on {0}".format(self.src_list[i+1]))

    def set_freq(self, new_freq):
        '''reset the center frequency '''
        ac.set_freq(new_freq, self.ant_list)

    def get_eq_coords(self):
        ''' return current RA and Dec coordinates '''
        curr_radec = ac.getRaDec(self.ant_list)
        return curr_radec
        
    def end_session(self):
        ''' release antennas at the end of a session '''
        ac.release_antennas(self.ant_list, True)
        print("Antennas have been released and stowed.")


    def work(self):
        '''run this function to cleanly exit the session'''
        
        print("Exiting session")
        return -1

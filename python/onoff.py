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

"""
This block runs an on-off observation with the Allen Array.
"""
import time
import threading as thr
import pmt
from gnuradio import gr

class onoff(gr.sync_block):
    """
    This block accepts input from the user in order to
    run an on-off observation with the Allen Array.
    """

    def __init__(self, cfreq, ant_list, dur, az_off, el_off):
        gr.sync_block.__init__(self,
                               name="onoff",
                               in_sig=None,
                               out_sig=None)

        self.message_port_register_out(pmt.intern("command"))
        self.message_port_register_in(pmt.intern("msg_in"))
        self.set_msg_handler(pmt.intern('msg_in'), self.handle_msg)

        self.dur = dur
        self.az_off = az_off
        self.el_off = el_off

        #set up dictionary of observing info which will be sent through
        #the message port

        obs_key = pmt.intern("obs_type")
        obs_val = pmt.intern("onoff")

        ant_key = pmt.intern("antennas_list")
        ant_val = pmt.intern(ant_list)

        freq_key = pmt.intern("freq")
        freq_val = pmt.from_double(cfreq)

        dur_key = pmt.intern("dur")
        dur_val = pmt.to_pmt(dur)

        self.azoff_key = pmt.intern("az_off")
        self.azoff_val = pmt.from_double(az_off)

        self.eloff_key = pmt.intern("el_off")
        self.eloff_val = pmt.from_double(el_off)

        command = pmt.make_dict()
        command = pmt.dict_add(command, ant_key, ant_val)
        command = pmt.dict_add(command, freq_key, freq_val)
        command = pmt.dict_add(command, dur_key, dur_val)
        command = pmt.dict_add(command, obs_key, obs_val)

        self.command = command

    def handle_msg(self, msg):

        ''' This function handles input from the QT GUI
        Message Edit Box.'''

        self.command = pmt.dict_delete(self.command, pmt.intern("source_id"))
        self.command = pmt.dict_delete(self.command, pmt.intern("ra"))
        self.command = pmt.dict_delete(self.command, pmt.intern("dec"))
        self.command = pmt.dict_delete(self.command, pmt.intern("az"))
        self.command = pmt.dict_delete(self.command, pmt.intern("el"))

        key = pmt.car(msg)
        val = pmt.cdr(msg)

        str_key = pmt.symbol_to_string(key)
        
        if str_key == "source_id":
            self.command = pmt.dict_add(self.command, key, val)
            self.command = pmt.dict_delete(self.command, self.azoff_key)

            self.command = pmt.dict_delete(self.command, self.eloff_key)
            thr.Thread(target=self.wait_duration).start()

        elif str_key == "azel":
            azel_pair = pmt.symbol_to_string(val).split(',')
            az = pmt.from_double(float(azel_pair[0].strip()))
            el = pmt.from_double(float(azel_pair[1].strip()))

            self.command = pmt.dict_add(self.command, pmt.intern("az"), az)
            self.command = pmt.dict_add(self.command, pmt.intern("el"), el)

            self.command = pmt.dict_delete(self.command, self.azoff_key)
            self.command = pmt.dict_delete(self.command, self.eloff_key)

            thr.Thread(target=self.wait_duration).start()

        elif str_key == "radec":
            radec_pair = pmt.symbol_to_string(val).split(',')
            ra = pmt.from_double(float(radec_pair[0].strip()))
            dec = pmt.from_double(float(radec_pair[1].strip()))

            self.command = pmt.dict_add(self.command, pmt.intern("ra"), ra)
            self.command = pmt.dict_add(self.command, pmt.intern("dec"), dec)

            self.command = pmt.dict_delete(self.command, self.azoff_key)
            self.command = pmt.dict_delete(self.command, self.eloff_key)

            thr.Thread(target=self.wait_duration).start()

        else: 
            print("Wrong value in left-hand space of Message Edit box.\n"\
                  "Must be either source_id, radec, or azel. Try again.\n")

    def set_source(self, src):

        ''' This function sets the source's
            identifier string '''

        src_key = pmt.intern("source_id")
        src_val = pmt.intern(src)
        self.command = pmt.dict_add(self.command, src_key, src_val)

    def set_src_radec(self, ra, dec):

        ''' This function sets the target source's
            right ascension and declination '''

        ra_key = pmt.intern("ra")
        ra_val = pmt.from_double(ra)

        dec_key = pmt.intern("dec")
        dec_val = pmt.from_double(dec)

        self.command = pmt.dict_add(self.command, ra_key, ra_val)
        self.command = pmt.dict_add(self.command, dec_key, dec_val)

    def set_src_azel(self, az, el):

        ''' This function sets the target source's
            azimuth and elevation '''

        az_key = pmt.intern("az")
        az_val = pmt.from_double(az)

        el_key = pmt.intern("el")
        el_val = pmt.from_double(el)

        self.command = pmt.dict_add(self.command, az_key, az_val)
        self.command = pmt.dict_add(self.command, el_key, el_val)

    def wait_duration(self):

        '''This function handles the sequencing
        of the on-off scan. '''

        #this command tells the control block to point
        # on-source for the given duration
        self.message_port_pub(pmt.intern("command"), self.command)
        time.sleep(self.dur)

        #this command will instruct the command block to point off
        #source by the given offsets
        self.command = pmt.dict_add(self.command, self.azoff_key, self.azoff_val)
        self.command = pmt.dict_add(self.command, self.eloff_key, self.eloff_val)

        self.message_port_pub(pmt.intern("command"), self.command)

        self.command = pmt.dict_delete(self.command, self.azoff_key)
        self.command = pmt.dict_delete(self.command, self.azoff_key)

    def start(self):
        ''' publish the observation info to the output message port '''

        thr.Thread(target=self.wait_duration).start()
        return super().start()

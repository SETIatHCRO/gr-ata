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
This block connects the antenna(s) specified to the
proper switch(es), then sets the appropriate attenuation
value(s) for the given antenna(s).
"""

from gnuradio import gr
from ATATools import ata_control as ac

class ifswitch(gr.sync_block):
    """
This block connects the antenna(s) specified to the
proper switch(es), then sets the appropriate attenuation
value(s) for the given antenna(s).
    """
    def __init__(self, ant1, ant2, ant3):
        gr.sync_block.__init__(self,
                               name="IF Switch",
                               in_sig=None,
                               out_sig=None)
        self.ant1 = ant1
        self.ant2 = ant2
        self.ant3 = ant3

        self.db1 = 0
        self.db2 = 0
        self.db3 = 0
        print("initializing if switch")

    def set_db1(self, db1):
        ''' set the attenuation for the switch1 antenna '''
        self.db1 = db1

    def set_ant2(self, db2):
        ''' set the attenuation for the switch2 antenna '''
        self.db2 = db2

    def set_ant3(self, db3):
        ''' set the attenuation for the switch3 antenna '''
        self.db3 = db3

    def start(self):

        print("if switch start")
        ant_list = []
        ant_pol_list = []
        db_list = []

        if self.ant1 != 'none':
            ant_list.append(self.ant1)
            ant1x = self.ant1+'x'
            ant1y = self.ant1+'y'
            ant_pol_list.append([ant1x, ant1y])
            if (self.db1 >= 0) and (self.db1 <= 31.5):
                db_list.append([self.db1, self.db1])
            else:
                raise Exception("Error: Gain for Switch 1 must be between 0 and 31.5 dB")

        if self.ant2 != 'none':
            ant_list.append(self.ant2)
            ant2x = self.ant2+'x'
            ant2y = self.ant2+'y'
            ant_pol_list.append([ant2x, ant2y])
            if (self.db2 >= 0) and (self.db2 <= 31.5):
                db_list.append([self.db2, self.db2])
            else:
                raise Exception("Error: Gain for Switch 2 must be between 0 and 31.5 dB")

        if self.ant3 != 'none':
            ant_list.append(self.ant3)
            ant3x = self.ant3+'x'
            ant3y = self.ant3+'y'
            ant_pol_list.append([ant3x, ant3y])
            if (self.db3 >= 0) and (self.db3 <= 31.5):
                db_list.append([self.db3, self.db3])
            else:
                raise Exception("Error: Gain for Switch 3 must be between 0 and 31.5 dB")

        if not (self.ant1 or self.ant2 or self.ant3):
            print("No antennas specified. Please select one or more antennas and try again.")

        ac.rf_switch_thread(ant_list)
        print("IF Switch has been set.")
        ac.set_atten_thread(ant_pol_list, db_list)
        print("IF Switch attenuation has been set.")

        return super().start() 

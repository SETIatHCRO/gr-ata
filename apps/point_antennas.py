#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from ATATools import ata_control, logger_defaults
from datetime import datetime
from argparse import ArgumentParser

def main():
    parser = ArgumentParser(description='ATA Antenna Controller')
    parser.add_argument('--frequency', '-f', type=int, default=3000, help="Frequency in MHz to tune the LOs to default is 3000 (e.g. 3000 for 3 GHz)", required=False)

    args = parser.parse_args()
    
    ants_lo_a = ["1c","2a","4j"]                   # Lo a
    ants_lo_b = ["1a", "1f", "4g", "5c"] # LO b
    ants_lo_c = ["2h","1k","1h"]
    ants_lo_d = ["2b","3c"]
    ant_list = ants_lo_a + ants_lo_b + ants_lo_c + ants_lo_d

    # Reserve antennas, and make sure to unreserve when code exists
    print("Reserving antennas " + str(ant_list))
    ata_control.reserve_antennas(ant_list)

    # Tune the RF over fiber power
    freq = args.frequency
    print("Autotuning to " + str(freq) + "...")
    ata_control.autotune(ant_list)
    ata_control.set_freq(freq, ants_lo_a, lo='a')
    ata_control.set_freq(freq, ants_lo_b, lo='b')
    ata_control.set_freq(freq, ants_lo_c, lo='c')
    ata_control.set_freq(freq, ants_lo_d, lo='d')

    # pick source and point dishes
    while True:
        cmd = input("'park' to exit or target name (e.g. casa or 3c84) ")
        
        if cmd == 'park':
            break
            
        source = cmd
        print("[" + str(datetime.now().strftime("%Y-%m-%d %H:%M:%S")) + "] Tracking to " + source)
        ata_control.make_and_track_source(source, ant_list)
        print("[" + str(datetime.now().strftime("%Y-%m-%d %H:%M:%S")) + "] Running.")
        _ = input("Press ENTER to change state")

    print("Releasing antennas...")
    ata_control.release_antennas(ant_list, True)
    print("Done.")

if __name__ == "__main__":
    main()


# Author: Paul Boven <p.boven@xs4all.nl>
# Copyright (c) 2019 CAMRAS, released under CC BY v4.0
# https://creativecommons.org/licenses/by/4.0/

# Note: this requires GNU Radio 3.8 (or higher)
# Required packages (Debian/Ubuntu): python3-zmq, python3-astropy

# To communicate with GNU Radio

'''Part of this code was adapted from a program written by Paul Boven of 
   Dwingeloo Observatory.

   Note -- please give RA in hour angle format and Dec in decimal
   degree format. (This is the ATA catalog default format).

   E. White, 11 Aug. 2020
'''

# this module will be imported in the into your flowgraph
from astropy import units as u
from astropy.time import Time
from astropy.coordinates import SkyCoord, EarthLocation, AltAz, get_sun
import time


def vlsr_correction(obs_timestamp, ra, dec):
    # hcro
    loc = EarthLocation.from_geodetic(lat=40.8172439*u.deg, \
                                      lon=-121.4698327*u.deg, \
                                      height=986.0*u.m)

    # Correction for the Sun's motion in our own Galaxy
    sun = get_sun(Time(obs_timestamp)).icrs
    psun = SkyCoord(ra="18:03:50.29", \
                    dec="+30:00:16.8", \
                    frame="icrs", unit=(u.hourangle, u.deg))
    vsun = -20.0 * u.km / u.s

    #Calculate the correction:
    T = Time('2010-03-26T15:16:33')

    # Source (Antenna) Direction. Make sure to transform to icrs
    SD = SkyCoord(ra=ra*u.hourangle, dec=dec*u.deg, frame='icrs', \
                  location = loc, obstime = T)

    # Calculate radial velocity and correct for Solar motion
    v = SD.cartesian.dot(psun.cartesian) * vsun - SD.radial_velocity_correction()

    return v.value


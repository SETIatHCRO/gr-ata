#!/usr/bin/env python3

import json
import numpy
from datetime import timedelta
# pip3 install julian if this is missing:
import julian
# import pprint
import argparse
import glob
from os import path
from pyuvdata import UVData
#from pyuvdata import utils as pyuv_utils
from astropy.time import Time, TimeDelta
from pathlib import Path
from dateutil import parser as dateparser
import pymap3d
from astropy.coordinates import SkyCoord, ITRS
from astropy import units as astro_units
import astropy.constants as const

def read_antenna_coordinates(filename=None):
    if filename == None:
        filename = 'antenna_coordinates_ecef.txt'
        
    try:
        f = open(filename, 'r')
    except Exception as e:
        print("ERROR opening antenna file: " + str(e))
        return None
        
    lines = f.readlines()
    # remove the header
    lines = lines[1:]
    antenna_details = {}
    for cur_line in lines:
        line_vals = cur_line.split(',')

        if len(line_vals) == 4:
            antenna_details[line_vals[0].upper()] = {"x": float(line_vals[1]), "y":float(line_vals[2]), "z":float(line_vals[3])}
            # antenna_details[line_vals[0].upper()] = {"N": float(line_vals[2]), "E":float(line_vals[1]), "U":float(line_vals[3])}
        else:
            print("Couldn't split line on commas: " + cur_line)
            
    if len(antenna_details.keys()) == 0:
        return None
    else:
        return antenna_details
    
def read_ecef_coordinates2(path=None):
    if path == None:
        path = 'antenna_coordinates_ecef.txt'
        
    with open(path) as f:
        l = f.readlines()[1:]
    
    ants = [ll.split(',')[0] for ll in l]
    ecef = numpy.empty((len(ants), 3))
    for j in range(3):
        ecef[:,j] = numpy.array([float(ll.split(',')[j+1]) for ll in l])
    return {a.lower() : e * astro_units.m for a, e in zip(ants, ecef)}
    
# This routine is translated from xGPU.
# It takes the triangular order Hermitian matrix and outputs the full NSTATION*NSTATION*NFREQ*NPOL*NPOL matrix.
def xgpuExtractMatrix(triangular_matrix,  NFREQUENCY, NSTATION, NPOL):
    full_matrix = numpy.zeros(NFREQUENCY*NSTATION*NSTATION*NPOL*NPOL).astype(numpy.complex64)
    for f in range(0, NFREQUENCY):
        for i in range(0, NSTATION):
            for j in range(0, i):
                k = f*(NSTATION+1)*(NSTATION/2) + i*(i+1)/2 + j
                for pol1 in range(0, NPOL):
                    for pol2 in range(0, NPOL):
                        index = int((k*NPOL+pol1)*NPOL+pol2)
                        full_matrix[(((f*NSTATION + i)*NSTATION + j)*NPOL + pol1)*NPOL+pol2] = triangular_matrix[index]
                        full_matrix[(((f*NSTATION + j)*NSTATION + i)*NPOL + pol2)*NPOL+pol1] =  triangular_matrix[index]
                        
    return full_matrix

# This was used in an attempt to multithread the reorder function.  Threading did not improve performance,
# but this is left in in case we want to use it in the future.
def reorderInnerLoop(data,  reordered_matrix, num_frequencies,  num_baselines, cur_baseline,  values_per_square):
    for cur_freq in range(0, num_frequencies):
        reorder_index = (num_frequencies * values_per_square) * cur_baseline + cur_freq * values_per_square
        original_index = (num_baselines * values_per_square) * cur_freq + cur_baseline * values_per_square
        for i in range(0, values_per_square):
            reordered_matrix[reorder_index + i] = data[original_index + i]
    
def reorderFreqPerBaseline(data,num_frequencies,  num_baselines, npol):
    # xGPU output is f0 [baselines 0..n], f1 [baselines 0..n]... fn[baselines 0..n]
    # The returned matrix is laid out as:
    # [baseline][frequency][npol^2]
    # Need to switch this to baseline0[frequencies 0..n],...
    reordered_matrix = numpy.zeros(len(data)).astype(numpy.complex64)
    
    values_per_square = npol*npol
    
    for cur_baseline in range(0, num_baselines):
        for cur_freq in range(0, num_frequencies):
            reorder_index = (num_frequencies * values_per_square) * cur_baseline + cur_freq * values_per_square
            original_index = (num_baselines * values_per_square) * cur_freq + cur_baseline * values_per_square
            for i in range(0, values_per_square):
                reordered_matrix[reorder_index + i] = data[original_index + i]

    # The returned matrix is laid out as:
    # [baseline][frequency][npol^2]
    return reordered_matrix
    
def stop_baseline(t0, cross, freq, source, baseline, T, ch_fs,
                      ant_coordinates, ant_delays_ns):
    ant1, ant2 = baseline.split('-')
    delay_offset = (ant_delays_ns[ant1] - ant_delays_ns[ant2]) * 1e-9
    baseline_itrs = ant_coordinates[ant1] - ant_coordinates[ant2]
    north_radec = [source.ra.deg, source.dec.deg + 90]
    if north_radec[1] > 90:
        north_radec[1] = 180 - north_radec[1]
        north_radec[0] = 180 + north_radec[0]
        
    north = SkyCoord(ra = north_radec[0]*astro_units.deg, dec = north_radec[1]*astro_units.deg)
    
    f_obs = freq * astro_units.Hz
    ts = t0 + TimeDelta(T, format = 'sec') * numpy.arange(cross.shape[0])
    source_itrs = source.transform_to(ITRS(obstime = Time(ts))).cartesian
    north_itrs = north.transform_to(ITRS(obstime = Time(ts))).cartesian
    east_itrs = north_itrs.cross(source_itrs)
    ww = source_itrs.xyz.T.dot(baseline_itrs)
    vv = north_itrs.xyz.T.dot(baseline_itrs)
    uu = east_itrs.xyz.T.dot(baseline_itrs)
    w_cycles = (ww/const.c*f_obs).to(1).value
    w_seconds = (ww/const.c).to(astro_units.s).value
    phase_corr = numpy.exp(-1j*2*numpy.pi*w_cycles)[:,numpy.newaxis,numpy.newaxis]
    nfft = cross.shape[1]
    ch_idx = numpy.arange(-nfft//2,nfft//2)[:,numpy.newaxis]
    delay_corr = numpy.exp(1j*2*numpy.pi*(delay_offset - w_seconds[:,numpy.newaxis,numpy.newaxis])*ch_idx*ch_fs)
    return (cross * phase_corr * delay_corr, 
            numpy.array([uu.value,vv.value,ww.value]))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='X-Engine Data Extractor')
    parser.add_argument('--inputfile', '-i', type=str, help="Observation JSON descriptor file", required=True)
    parser.add_argument('--outputfile', '-o', type=str, help="UVFITS output file", required=True)
    parser.add_argument('--check-uvfits', '-c', help="This will enable validating UVFITS data on writing.", action='store_true', required=False)
    
    args = parser.parse_args()
    
    # pp = pprint.PrettyPrinter(indent=4)

    # Load the top-level descriptor file
    descriptor_file = args.inputfile

    try:
        with  open(descriptor_file) as f:
            metadata = json.load(f)
    except Exception as e:
        print("ERROR: " + str(e))
        exit(1)
        
    # Validate the file looks correct
    expected_top_level_keys = ['num_baselines','instrument', 'telescope_name', 'telescope_location', 'object_name', 'antenna_names','channel_width','first_channel_center_freq','integration_time_seconds','polarizations','input_dir', 'observation_base_name', 'output_dir']
    expected_data_keys = ['first_seq_num','num_baselines', 'first_channel', 'channels', 'polarizations', 'antennas', 'ntime', 'samples_per_block']
    
    for expected_key in expected_top_level_keys:
        if expected_key not in metadata.keys():
            print("ERROR: Unable to find top-level key " + expected_key + " in " + descriptor_file)
            exit(1)
            
    if not path.exists(metadata['input_dir']):
        print("ERROR: The input directory " + metadata['input_dir'] + " does not exist.")
        exit(2)
        
    if not path.exists(metadata['output_dir']):
        print("ERROR: The output directory " + metadata['output_dir'] + " does not exist.")
        exit(2)
        
    # Get the files in the directory, and sort by date so they're in cronological order
    obs_file_list = glob.glob(metadata['input_dir'] + "/" + metadata['observation_base_name'] + "*.json")
    obs_file_list.sort(key=path.getmtime)
    
    if len(obs_file_list) == 0:
        print("ERROR: No files meeting the specification " + metadata['input_dir'] + "/" + metadata['observation_base_name'] + "*.json were found.")
        exit(2)

    # Set up top-level UVFITS information
    try:
        UV = UVData()
        num_antennas = len(metadata['antenna_names'])
        UV.Nants_data = num_antennas
        UV.Nants_telescope = num_antennas
        UV.Nbls = metadata['num_baselines']
        UV.Nblts = 0 # incremented with each observation file
        UV.Nfreqs = 0 # set with first observation file
        # Polarizations: since we have XX, XY, YX, and YY data, we have to set the number of
        # polarizations in the array to 4 (d_npol * d_npol with d_npol = 2)
        d_npol = metadata['polarizations']
        d_npol_sq = d_npol * d_npol
        UV.Npols = d_npol_sq
        UV.Nspws = 1  # Number of non-contiguous spectral windows.  1 is the only value supported, but it's not set by default.
        # UV.Ntimes incremented with observation files.
        UV.Ntimes = 0
        
        UV.ant_1_array = []
        UV.ant_2_array = []
        UV.antenna_names = metadata['antenna_names']
        UV.antenna_numbers = numpy.array(list(range(0,num_antennas)))
    except Exception as e:
        print("ERROR setting UVFITS object initial parameters: " + str(e))
        exit(10)

    uvw_provided = False
    if 'uvw_file' in metadata.keys():
        uvw_provided = True
        try:
            uvw_from_file = numpy.fromfile(metadata['uvw_file'], dtype = 'float64')
        except Exception as e:
            print("ERROR loading uvw file: " + str(e))
            exit(10)
        
    try:
        antenna_details = None
        
        UV.telescope_location_lat_lon_alt_degrees = numpy.array(metadata['telescope_location'])
        [telescope_x, telescope_y, telescope_z] = pymap3d.geodetic2ecef(metadata['telescope_location'][0],metadata['telescope_location'][1],metadata['telescope_location'][2])
        # [telescope_x, telescope_y, telescope_z] = pyuv_utils.XYZ_from_LatLonAlt(metadata['telescope_location'][0],metadata['telescope_location'][1],metadata['telescope_location'][2])
    except Exception as e:
        print("ERROR Converting telescope location to ITRF coordinates: " + str(e))
        exit(10)
        
    try:
        UV.antenna_names = metadata['antenna_names']
        if metadata['telescope_name'] != 'ATA':
            UV.antenna_positions = numpy.array( metadata['antenna_coord_relative_telescope_itrf_m'], dtype = 'float')
        else:
            if not antenna_details:
                antenna_details = read_antenna_coordinates()
                antenna_ecef_phasing_coordinates = read_ecef_coordinates2()
            
            if antenna_details is None:
                print("ERROR reading antenna file antenna_coordinates_ecef.txt")
                exit(1)
                
            ant_pos = []
            if not antenna_details:
                print("ERROR: no antenna details loaded.")
                exit(11)
                
            try:
                for cur_name in metadata['antenna_names']:
                    cur_ant = antenna_details[cur_name.upper()]
                    ant_pos.append([cur_ant['x'] - telescope_x, cur_ant['y'] - telescope_y, cur_ant['z'] - telescope_z])
            except Exception as e:
                print("ERROR parsing antenna names: " + str(e))
                print("Valid antennas are: " + str(antenna_details.keys()))
                exit(12)
                
            UV.antenna_positions = numpy.array(ant_pos, dtype = 'float')
            
        if metadata['telescope_name'] != 'ATA' and 'antenna_coord_relative_telescope_itrf_m' not in metadata.keys():
            print("ERROR: Telescope is not the ATA and 'antenna_coord_relative_telescope_itrf_m' was not provided.")
            exit(2)

        UV.channel_width = metadata['channel_width']
        UV.flex_spw = False # This controls whether or not individual channel mappings needs to be defined.
        
        # See https://github.com/RadioAstronomySoftwareGroup/pyuvdata/blob/76b43ba09228e500c811c5f80319810baaea611b/pyuvdata/uvdata/uvdata.py#L154
        # For a description of the polarization array.  This sequence -5...-8 specifies linear as defined here:
        # linear -5:-8 (XX, YY, XY, YX)
        UV.polarization_array = [-5, -6, -7, -8] 
        UV.spw_array = [0]
        
        UV.instrument = metadata['instrument']
        UV.telescope_name = metadata['telescope_name']
        UV.object_name = metadata['object_name']
        UV.history = ''
        UV.vis_units = 'UNCALIB'
        # UV._set_unphased()
        # UV.set_drift()
        UV.set_phased()
        
        coords = None
        try:
            coords = SkyCoord.from_name(UV.object_name)
            UV.phase_center_ra = coords.ra.rad
            UV.phase_center_dec = coords.dec.rad
        except Exception as e:
            if 'object_ra' in metadata.keys():
                UV.phase_center_ra_degrees = metadata['object_ra']
                UV.phase_center_dec_degrees = metadata['object_dec']
                
                coords = SkyCoord(ra=metadata['object_ra']*astro_units.degree, dec=metadata['object_dec']*astro_units.degree, frame='icrs')
            else:
                print("ERROR: Unable to get coordinates for " + metadata['object_name'] + ": " + str(e))
                exit(1)
            
        UV.phase_center_epoch = 2000.0
        obs_start = dateparser.parse(metadata['observation_start'])
        jullian_start = julian.to_jd(obs_start, fmt='jd')
        
        first_channel_center_freq = metadata['first_channel_center_freq']
        integration_time_seconds = metadata['integration_time_seconds']
        if 'baseline_uvw_vectors' in metadata.keys():
            baseline_vectors = numpy.asarray(metadata['baseline_uvw_vectors'])
        else:
            if not antenna_details:
                antenna_details = read_antenna_coordinates()
            
            if antenna_details is None:
                print("ERROR reading antenna file antenna_coordinates_ecef.txt")
                exit(1)

            baseline_vectors = numpy.zeros((UV.Nbls, 3), dtype=float)
            current_index = 0
            try:
                for i in range(0, len(UV.antenna_names)):
                    coord1 = antenna_details[UV.antenna_names[i].upper()]
                    for j in range(0, i+1):
                        coord2 = antenna_details[UV.antenna_names[j].upper()]
                        
                        # According to https://pyuvdata.readthedocs.io/en/latest/uvdata_parameters.html,
                        # uvw_array needs to be ant1-ant2 to be AIS/FITS compliant.  Myriad does ant2-ant1.
                        u = coord1["x"] - coord2["x"]
                        v = coord1["y"] - coord2["y"]
                        w = coord1["z"] - coord2["z"]
                        
                        baseline_vectors[current_index][0] = u
                        baseline_vectors[current_index][1] = v
                        baseline_vectors[current_index][2] = w
            except Exception as e:
                print("ERROR processing antenna names: " + str(e))
                exit(1)
            
        UV.baseline_array = []
        
    except Exception as e:
        print("ERROR building uvfits object..." + str(e))
        exit(10)
    
    # Loop through observation files
    for obs_descriptor_file in obs_file_list:
        try:
            with  open(obs_descriptor_file) as f:
                print("Loading " + obs_descriptor_file)
                obs_metadata = json.load(f)
        except Exception as e:
            print("ERROR parsing " + obs_descriptor_file + ": " + str(e))
            continue
            
        for expected_key in expected_data_keys:
            if expected_key not in obs_metadata.keys():
                print("ERROR: Unable to find observation key " + expected_key + " in " + obs_descriptor_file)
                exit(1)
                
        data_file = obs_descriptor_file.replace(".json", "")

        if not path.exists(data_file):
            print("ERROR: Unable to find data file " + data_file)
            continue

        try:
            f = open(data_file, 'rb')
        except Exception as e:
            print("Error opening " + data_file + ": " + str(e))
            continue
            
        d_num_inputs = obs_metadata['antennas']
        
        d_num_channels = obs_metadata['channels']
        d_num_baselines = obs_metadata['num_baselines']
        samples_per_block = obs_metadata['samples_per_block']
        bytes_per_block = obs_metadata['bytes_per_block']
        integrated_samples = obs_metadata['ntime']
        
        if UV.Nfreqs == 0:
            UV.Nfreqs = d_num_channels
            UV.freq_array = numpy.ndarray((1, d_num_channels), dtype=float)
            for i in range(0, d_num_channels):
                UV.freq_array[0][i] = first_channel_center_freq + i*UV.channel_width
                
        file_size = Path(data_file).stat().st_size
        
        num_time_blocks = file_size // bytes_per_block
        
        # Nblts is a UVFITS construct of number of time entries * num baselines
        Nblts = num_time_blocks * d_num_baselines 
        
        UV.Nblts += Nblts
        # Ant arrays are the indices of the first antennas in the baselines
        for t in range(0, num_time_blocks):
            for a1 in range(0, num_antennas):
                for a2 in range(0, num_antennas):
                    if a2 <= a1:
                        UV.ant_1_array.append(a1)
                        UV.ant_2_array.append(a2)
                
        if UV.integration_time is not None:
            UV.integration_time = UV.integration_time.concatenate((UV.integration_time, numpy.full((Nblts), integration_time_seconds, dtype=float)))
        else:
            # Create an array of size Nblts filled with the value integration_time_seconds
            UV.integration_time = numpy.full((Nblts), integration_time_seconds, dtype=float)
            
        cur_block = 0
        
        for i in range(0, num_time_blocks):
            UV.baseline_array = numpy.concatenate((UV.baseline_array, list(range(0, d_num_baselines)))).astype(dtype=int)
            
        print("Reading " + data_file + ":")
        print("Num stations: " + str(d_num_inputs))
        print("Num channels: " + str(d_num_channels))
        print("Num baselines: " + str(d_num_baselines))
        print("Num time blocks: " + str(num_time_blocks))
        print("samples per block: " + str(samples_per_block))
        
        # It may seem odd to have the polarization dimension be d_npol*d_npol, but this is the way
        # the libraries appear to handle it.  For XY polarizations, d_npol=2, so d_npol*d_npol=4
        # which gets you storage for XX,XY, YX, YY (4 slots).
        tmp_array = numpy.zeros((num_time_blocks* d_num_baselines, 1, d_num_channels,  d_npol_sq), dtype=numpy.complex64)
        
        if UV.flag_array is None:
            UV.flag_array = numpy.zeros((num_time_blocks* d_num_baselines, 1, d_num_channels,  d_npol_sq), dtype=bool)
        else:
            UV.flag_array = numpy.concatenate((UV.flag_array, numpy.zeros((num_time_blocks* d_num_baselines, 1, d_num_channels,  d_npol_sq), dtype=bool))).astype(dtype=bool)
            
        if UV.nsample_array is None:
            UV.nsample_array = numpy.full((num_time_blocks* d_num_baselines, 1, d_num_channels,  d_npol_sq), integrated_samples,  dtype=float)
        else:
            UV.nsample_array = numpy.concatenate((UV.flag_array, numpy.full((num_time_blocks* d_num_baselines, 1, d_num_channels,  d_npol_sq), integrated_samples,  dtype=float)))
            
        # Loop through the individual integrated blocks
        while True:
            try:
                cc_data_vector = numpy.fromfile(f, dtype=numpy.complex64, count=samples_per_block)
            except Exception as e:
                print("Exception.  Blocks read: " + str(cur_block))
                print(str(e))
                break
            
            if cc_data_vector.size < samples_per_block:
                # We really didn't read that last block, take it off the += 1
                print("Blocks read: " + str(cur_block))
                break
                
            reordered_matrix = reorderFreqPerBaseline(cc_data_vector, d_num_channels, d_num_baselines, d_npol)
            # Add to UV Output Here
            # -----------------------------------
            for cur_baseline in range(0, d_num_baselines):
                for cur_freq in range(0, d_num_channels):
                    # See https://pyuvdata.readthedocs.io/en/v2.1.2/uvdata.html
                    # reorder_pols for a discussion on pols.  CASA format has XX, XY, YX, YY.  AIPS has XX,YY,XY,YX
                    # Code below sets up for AIPS format (which is the default?): XX, YY, XY, YX
                    tmp_array[cur_block*d_num_baselines + cur_baseline][0][cur_freq][0] = reordered_matrix[cur_baseline*d_num_channels*d_npol_sq+cur_freq*d_npol_sq+0]
                    tmp_array[cur_block*d_num_baselines + cur_baseline][0][cur_freq][1] = reordered_matrix[cur_baseline*d_num_channels*d_npol_sq+cur_freq*d_npol_sq+3]
                    tmp_array[cur_block*d_num_baselines + cur_baseline][0][cur_freq][2] = reordered_matrix[cur_baseline*d_num_channels*d_npol_sq+cur_freq*d_npol_sq+1]
                    tmp_array[cur_block*d_num_baselines + cur_baseline][0][cur_freq][3] = reordered_matrix[cur_baseline*d_num_channels*d_npol_sq+cur_freq*d_npol_sq+2]
                      
            if UV.time_array is not None:
                cur_julian = julian.to_jd(obs_start + timedelta(seconds=cur_block*integration_time_seconds), fmt='jd')
                UV.time_array = numpy.concatenate((UV.time_array, numpy.full((d_num_baselines), cur_julian, dtype=float)))
            else:
                UV.time_array = numpy.full((d_num_baselines), jullian_start, dtype=float)
                
            UV.Ntimes = len(numpy.unique(UV.time_array))
            
            if not uvw_provided:
                if UV.uvw_array is not None:
                    UV.uvw_array = numpy.concatenate((UV.uvw_array, numpy.asarray(baseline_vectors)))
                else:
                    UV.uvw_array = numpy.asarray(baseline_vectors)
                
            cur_block += 1
            
        if UV.data_array is None:
            UV.data_array = tmp_array
        else:
            UV.data_array = numpy.concatenate((UV.data_array, tmp_array), axis=0)
            
        f.close()
        
    if 'antenna_delays' in metadata:
        # We need to phase the inputs
        delays = metadata['antenna_delays']
        n = d_num_baselines * d_npol * d_num_channels
        chan_fs = metadata['channel_width']
        f_first = metadata['first_channel_center_freq']
        f = d_num_channels/2*chan_fs + f_first
        t_sync = Time(obs_start)
        
        x = UV.data_array
        # This is swapped from Daniel's original.  The reordered UVFITS matrix indices
        # Run baselines, channels, pol rather than channel, baseline, pol
        x = x[:x.size//n*n].reshape((-1, d_num_baselines, d_num_channels, d_npol**2))
        t0 = t_sync + TimeDelta(obs_metadata['first_seq_num'] / chan_fs, format = 'sec')
        T = obs_metadata['ntime'] / chan_fs

        x_stop = numpy.empty_like(x)
        # axes for uvw are (time, baseline, uvw)
        uvw = numpy.zeros((x.shape[0], x.shape[1], 3), dtype = 'float64')

        ants = metadata['antenna_names']
        baselines = [f'{a}-{b}' for j,a in enumerate(ants) for b in ants[:j+1]]

        for j, baseline in enumerate(baselines):
            if baseline.split('-')[0] == baseline.split('-')[1]:
                # autocorrelation. no need to do anything
                x_stop[:,j, :] = x[:,j, :]
            else:
                stop = stop_baseline(t0, x[:, j, :], f,
                                     coords, baseline,
                                     T, chan_fs, antenna_ecef_phasing_coordinates, delays)
                x_stop[:,j, :] = stop[0]
                uvw[:,j,:] = stop[1].T
                
        UV.data_array = x_stop.flatten()
        UV.uvw_array = uvw.reshape(Nblts, 3)
            
    UV.Ntimes = len(numpy.unique(UV.time_array))
    
    # Fill in the lst_array values from the time array
    UV.set_lsts_from_time_array()
    
    # Finish making sure necessary arrays are numpy arrays
    UV.ant_1_array = numpy.array(UV.ant_1_array)
    UV.ant_2_array = numpy.array(UV.ant_2_array)
    
    if 'uvw_file' in metadata.keys():
        try:
            uvw_from_file = uvw_from_file.reshape(Nblts, 3)
            UV.uvw_array = uvw_from_file
        except Exception as e:
            print("ERROR reshaping uvw file: " + str(e))
            exit(10)
            
    successful_save = True
    
    print("Writing UVFITS file...")
    try:
        UV.write_uvfits(args.outputfile, spoof_nonessential=True,  write_lst=False, run_check=args.check_uvfits,  force_phase=True,  check_extra=False)
    except Exception as e:
        print("ERROR in write_uvfits call: " + str(e))
        successful_save = False
        
    if successful_save:
        print("Done.")
    

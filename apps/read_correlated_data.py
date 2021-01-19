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
from pathlib import Path
from dateutil import parser as dateparser

from astropy.coordinates import SkyCoord

def read_antenna_coordinates(filename=None):
    if filename == None:
        filename = 'antenna_coordinates.txt'
        
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

        if len(line_vals) == 3:
            antenna_details[line_vals[0].upper()] = {"N": float(line_vals[1]), "E":float(line_vals[2])}
        else:
            print("Couldn't split line on commas: " + cur_line)
            
    if len(antenna_details.keys()) == 0:
        return None
    else:
        return antenna_details
    
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
    # [frequency][baseline][npol^2]
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
    
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='X-Engine Data Extractor')
    parser.add_argument('--inputfile', '-i', type=str, help="Observation JSON descriptor file", required=True)
    parser.add_argument('--outputfile', '-o', type=str, help="UVFITS output file", required=True)
    
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
    expected_top_level_keys = ['antenna_coord_relative_telescopescope_itrf_m', 'num_baselines','instrument', 'telescope_name', 'telescope_location', 'object_name', 'antenna_names','channel_width','first_channel_center_freq','integration_time_seconds','polarizations','input_dir', 'observation_base_name', 'output_dir']
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
        UV.telescope_location_lat_lon_alt_degrees = numpy.array(metadata['telescope_location'])
        UV.instrument = metadata['instrument']
        UV.telescope_name = metadata['telescope_name']
        UV.object_name = metadata['object_name']
        UV.history = ''
        UV.vis_units = 'UNCALIB'
        # UV._set_unphased()
        UV.set_drift()
        
        try:
            coords = SkyCoord.from_name(UV.object_name)
            UV.phase_center_ra = coords.ra.rad
            UV.phase_center_dec = coords.dec.rad
        except Exception as e:
            if 'object_ra' in metadata.keys():
                UV.phase_center_ra = metadata['object_ra']
                UV.phase_center_dec = metadata['object_dec']
            else:
                print("ERROR: Unable to get coordinates for " + metadata['object_name'] + ": " + str(e))
                exit(1)
            
        UV.phase_center_epoch = 2000.0
        obs_start = dateparser.parse(metadata['observation_start'])
        jullian_start = julian.to_jd(obs_start, fmt='jd')
        UV.Nants_data = len(metadata['antenna_names'])
        UV.Nants_telescope = metadata['polarizations']
        UV.antenna_names = []
        for cur_name in metadata['antenna_names']:
            UV.antenna_names.append(cur_name.upper())
        UV.antenna_numbers = numpy.array(numpy.arange(len(metadata['antenna_names'])))
        # UV.antenna_positions = numpy.array([numpy.zeros(len(metadata['baseline_m'])), metadata['baseline_m']], dtype = 'float')
        UV.antenna_positions = numpy.array( metadata['antenna_coord_relative_telescopescope_itrf_m'], dtype = 'float')
        UV.Nbls = metadata['num_baselines']
        UV.channel_width = metadata['channel_width']
        first_channel_center_freq = metadata['first_channel_center_freq']
        integration_time_seconds = metadata['integration_time_seconds']
        
        if 'baseline_uvw_vectors' in metadata.keys():
            baseline_vectors = numpy.asarray(metadata['baseline_uvw_vectors'])
        else:
            antenna_details = read_antenna_coordinates()
            
            if antenna_details is None:
                print("ERROR reading antenna file antenna_coordinates.txt")
                exit(1)

            baseline_vectors = numpy.zeros((UV.Nbls, 3), dtype=float)
            current_index = 0
            w = 0.0
            try:
                for i in range(0, len(UV.antenna_names)):
                    coord1 = antenna_details[UV.antenna_names[i]]
                    for j in range(0, i+1):
                        coord2 = antenna_details[UV.antenna_names[j]]
                        
                        u = coord2["N"] - coord1["N"]
                        v = coord2["E"] - coord1["E"]
                        
                        baseline_vectors[current_index][0] = u
                        baseline_vectors[current_index][1] = v
                        baseline_vectors[current_index][2] = w
            except Exception as e:
                print("ERROR processing antenna names: " + str(e))
                exit(1)
            
        UV.flex_spw = False # This controls whether or not individual channel mappings needs to be defined.
        
        UV.Nblts = 0
        UV.Nfreqs = 0
        UV.Ntimes = 0
        UV.ant_1_array = []
        UV.ant_2_array = []
        UV.baseline_array = []
        
    except Exception as e:
        print("ERROR building uvfits object..." + str(e))
        exit(10)
    
    # Loop through observation files
    for obs_descriptor_file in obs_file_list:
        try:
            with  open(obs_descriptor_file) as f:
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
        
        d_npol = obs_metadata['polarizations']
        d_npol_sq = d_npol * d_npol
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
                
            # In this case, since we have XX, XY, YX, and YY data, we have to set the number of
            # polarizations in the array to 4 (d_npol * d_npol with d_npol = 2)
            UV.Npols = d_npol_sq
            UV.Nants_data = 1
            
            # See https://github.com/RadioAstronomySoftwareGroup/pyuvdata/blob/76b43ba09228e500c811c5f80319810baaea611b/pyuvdata/uvdata/uvdata.py#L154
            # For a description of the polarization array.  This sequence -5...-8 specifies linear as defined here:
            # linear -5:-8 (XX, YY, XY, YX)
            UV.polarization_array = [-5, -6, -7, -8] 
            UV.Nspws = 1  # Number of non-contiguous spectral windows.  1 is the only value supported, but it's not set by default.
            UV.spw_array = [0]
            
        file_size = Path(data_file).stat().st_size
        
        num_time_blocks = file_size // bytes_per_block
        
        # Nblts is a UVFITS construct of number of time entries * num baselines
        Nblts = num_time_blocks * d_num_baselines 
        
        UV.Nblts += Nblts
        if UV.integration_time is not None:
            UV.integration_time = UV.integration_time.concatenate((UV.integration_time, numpy.full((Nblts), integration_time_seconds, dtype=float)))
        else:
            UV.integration_time = numpy.full((Nblts), integration_time_seconds, dtype=float)
            
        UV.ant_1_array = numpy.concatenate((UV.ant_1_array, numpy.zeros((Nblts)).astype(dtype=int))).astype(dtype=int)
        UV.ant_2_array = numpy.concatenate((UV.ant_2_array, numpy.ones((Nblts)).astype(dtype=int))).astype(dtype=int)
        
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
            UV.flag_array = numpy.ones((num_time_blocks* d_num_baselines, 1, d_num_channels,  d_npol_sq), dtype=bool)
        else:
            UV.flag_array = numpy.concatenate((UV.flag_array, numpy.ones((num_time_blocks* d_num_baselines, 1, d_num_channels,  d_npol_sq), dtype=bool))).astype(dtype=bool)
            
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
        
    UV.Ntimes = len(UV.time_array)
    UV.set_lsts_from_time_array()
    UV.write_uvfits(args.outputfile, spoof_nonessential=True,  write_lst=False, run_check=False,  force_phase=True,  check_extra=False)
    
    print("Done.")
    

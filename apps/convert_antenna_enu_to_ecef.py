#!/usr/bin/python3 

import pymap3d

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

        if len(line_vals) == 4:
            antenna_details[line_vals[0].upper()] = {"N": float(line_vals[2]), "E":float(line_vals[1]), "U":float(line_vals[3])}
        else:
            print("Couldn't split line on commas: " + cur_line)
            
    if len(antenna_details.keys()) == 0:
        return None
    else:
        return antenna_details

if __name__ == '__main__':
    ant_pos_enu = read_antenna_coordinates('antenna_coordinates.txt')

    # geodetic coordinates lat0, lon0, and h0
    # Reference for ENU offset is 2.3737m West of 2A and 27.5362m North of it.
    ref_lat = 40.0+49.0 / 60.0 +3.0 / 3600.0
    ref_lon = -(121.0+28.0 / 60.0 + 24.0 / 3600.0)
    ref_alt = 1008
    
    if ant_pos_enu is None:
        print("ERROR: Unable to read file.")
        exit(1)

    print('Antenna, X_ecef_m, Y_ecef_m, Z_ecef_m')
    for cur_key in ant_pos_enu.keys():
        cur_ant = ant_pos_enu[cur_key]
        # Note: ned2ecef wants North, East, Down.  Not Up.  So we have to reverse the sign on Up.
        (x, y, z) = pymap3d.ned2ecef(cur_ant['N'], cur_ant['E'], -cur_ant['U'], ref_lat, ref_lon, ref_alt)
        output_str = cur_key + ', ' +  str(x) + ', ' + str(y) + ', ' + str(z)
        print(output_str)

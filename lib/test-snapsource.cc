/* -*- c++ -*- */
/*
 * Copyright 2012 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cppunit/TextTestRunner.h>
#include <cppunit/XmlOutputter.h>

#include <gnuradio/unittests.h>
#include <gnuradio/block.h>
#include <iostream>
#include <fstream>
#include <boost/algorithm/string/replace.hpp>
#include <math.h>  // fabsf
#include <chrono>
#include <ctime>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
// win32 (mingw/msvc) specific
#ifdef HAVE_IO_H
#include <io.h>
#endif
#ifdef O_BINARY
#define	OUR_O_BINARY O_BINARY
#else
#define	OUR_O_BINARY 0
#endif

// should be handled via configure
#ifdef O_LARGEFILE
#define	OUR_O_LARGEFILE	O_LARGEFILE
#else
#define	OUR_O_LARGEFILE 0
#endif


#include "snap_source_impl.h"

// bool verbose=false;
int iterations = 100;
bool wait_for_data = true;

class comma_numpunct : public std::numpunct<char>
{
  protected:
    virtual char do_thousands_sep() const
    {
        return ',';
    }

    virtual std::string do_grouping() const
    {
        return "\03";
    }
};

bool testSNAPSource() {
	std::cout << "----------------------------------------------------------" << std::endl;

	std::cout << "Testing SNAP Source: " << std::endl;

	gr::ata::snap_source_impl *test=NULL;
	int starting_channel = 1792;
	int num_channels = 1024;
	int output_data_size = num_channels * 2; // IQ, one byte each
	int ending_channel = starting_channel + num_channels - 1;
	int data_size = sizeof(char); // GR size

	// The one specifies output triangular order rather than full matrix.
	test = new gr::ata::snap_source_impl(4030,1, // voltage
			false, false,false, starting_channel, ending_channel, data_size);

	int i;
	std::chrono::time_point<std::chrono::steady_clock> start, end;
	std::chrono::duration<double> elapsed_seconds = end-start;
	std::vector<int> ninitems;


	std::vector<char> inputItems_char;
	std::vector<char> outputItems;
	std::vector<const void *> inputPointers;
	std::vector<void *> outputPointers;

	int output_size = num_channels*2;
	for (i=0;i<output_size;i++) {
		inputItems_char.push_back(0x00);
		outputItems.push_back(0x00);
	}

	inputPointers.push_back((const void *)&inputItems_char[0]);

	outputPointers.push_back((void *)&outputItems[0]);
	outputPointers.push_back((void *)&outputItems[0]);
	outputPointers.push_back((void *)&outputItems[0]);
	outputPointers.push_back((void *)&outputItems[0]);

	// Run empty test
	int noutputitems;
	float elapsed_time,throughput;
	long input_buffer_total_bytes;
	float bits_throughput;

	int packet_size = test->packet_size();

	if (wait_for_data) {
		std::cout << "Waiting for enough packets to be queued to run the test at full speed..." << std::endl;
		while (test->data_available() < (iterations+1)*packet_size) {
			usleep(100);
		}

		std::cout << "Enough data has been received.  Proceeding to test..." << std::endl;
	}
	// Test memory queueing approach
	noutputitems = test->work_test(1,inputPointers,outputPointers);

	start = std::chrono::steady_clock::now();
	// make iterations calls to get average.
	for (i=0;i<iterations;i++) {
		noutputitems = test->work_test(1,inputPointers,outputPointers);
	}
	end = std::chrono::steady_clock::now();

	elapsed_seconds = end-start;

	elapsed_time = elapsed_seconds.count()/(float)iterations;
	throughput = num_channels  / elapsed_time;
	input_buffer_total_bytes = num_channels * data_size * 2;
	bits_throughput = 8 * input_buffer_total_bytes / elapsed_time;

	std::cout << std::endl << "GNURadio work() performance:" << std::endl;
	std::cout << "Elapsed time: " << elapsed_seconds.count() << std::endl;
	std::cout << "Timing Averaging Iterations: " << iterations << std::endl;
	std::cout << "Average Run Time:   " << std::fixed << std::setw(11) << std::setprecision(6) << elapsed_time << " s" << std::endl <<
				"Total throughput: " << std::setprecision(2) << throughput << " byte complex samples/sec" << std::endl <<
				"Projected processing rate: " << bits_throughput << " bps" << std::endl;

	// Reset test
	if (test != NULL) {
		delete test;
	}
	// ----------------------------------------------------------------------
	// Clean up io buffers

	inputPointers.clear();
	outputPointers.clear();
	inputItems_char.clear();
	outputItems.clear();
	ninitems.clear();

	return true;
}

int
main (int argc, char **argv)
{
	// Add comma's to numbers
	std::locale comma_locale(std::locale(), new comma_numpunct());

	// tell cout to use our new locale.
	std::cout.imbue(comma_locale);

	if (argc > 1) {
		// 1 is the file name
		if (strcmp(argv[1],"--help")==0) {
			std::cout << std::endl;
			std::cout << "Usage: test-snapsource" << std::endl;
			std::cout << "Where --source-zeros allows the block to be timed simply returning empty data." << std::endl;
			std::cout << "The default mode will wait for the test number of data packets to be available first before running the test." << std::endl;
			std::cout << std::endl;
			exit(0);
		}

		// Just a placeholder if we want to add params
		for (int i=1;i<argc;i++) {
			std::string param = argv[i];

			if (strcmp(argv[i],"--source-zeros")==0) {
				wait_for_data = false;
			}
			else {
				std::cout << "ERROR: Unknown parameter." << std::endl;
				exit(1);

			}

		}
	}
	bool was_successful;

	was_successful = testSNAPSource();

	std::cout << std::endl;

	return 0;
}


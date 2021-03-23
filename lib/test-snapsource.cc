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
int iterations = 10;
bool wait_for_data = true;
bool use_pcap = false;
std::string pcap_filename = "";
bool output_packed = false;
int starting_channel = 1792;
int num_channels = 1024;
int port = 10000;
std::string mcast_group="";

#define THREAD_RECEIVE


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
#ifndef _OPENMP
	std::cout << "WARNING: OMP not enabled.  Please install libomp and recompile." << std::endl;
#endif

	std::cout << "----------------------------------------------------------" << std::endl;

	std::cout << "Testing SNAP Source: " << std::endl;
	std::cout << "Starting channel: " << starting_channel << std::endl <<
			     "Num Channels: " << num_channels << std::endl <<
				 "Listening port: " << port << std::endl;

	gr::ata::snap_source_impl *test=NULL;
	int output_data_size = num_channels * 2; // IQ, one byte each
	int ending_channel = starting_channel + num_channels - 1;
	int data_size = sizeof(char); // GR size

	int data_source;
	if (use_pcap) {
		data_source = 3;
	}
	else {
		if (mcast_group.empty()) {
			data_source = 1;
		}
		else {
			data_source = 2;
		}
	}
	// The one specifies output triangular order rather than full matrix.
	test = new gr::ata::snap_source_impl(port,1, // voltage
			false, false,false, starting_channel, ending_channel, data_size, data_source, pcap_filename, false, output_packed, mcast_group);

	test->start();

	int i;
	std::chrono::time_point<std::chrono::steady_clock> start, end;
	std::chrono::duration<double> elapsed_seconds = end-start;
	std::vector<int> ninitems;


	std::vector<char> inputItems_char;
	std::vector<char> outputItems;
	std::vector<const void *> inputPointers;
	std::vector<void *> outputPointers;

	int output_size = num_channels*2;
	int entries_per_complete_frame = 16;
	for (i=0;i<output_size*entries_per_complete_frame;i++) {
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
	int packets_per_complete_frame = 4;

	if (wait_for_data) {
		std::cout << "Waiting for enough packets to be queued to run the test at full speed..." << std::endl;

		// Let's get aligned
		while (!test->packets_aligned()) {
			std::cout << "Packets not aligned yet.  Packets available: " << test->packets_available() << "...";
			if (test->packets_aligned()) {
				break;
			}
			else {
				usleep(100000);
			}
		}

		std::cout << "Packets aligned" << std::endl;

		if (use_pcap) {
			test->set_test_case_min_queue_length((iterations+1)*packet_size*packets_per_complete_frame);
		}

		// Now let's make sure we have enough data for the test.
		long required_packets = (iterations+1)*packets_per_complete_frame;

		while (test->packets_available() < required_packets) {
#ifndef THREAD_RECEIVE
			test->queue_data();
#endif
			usleep(50000);
			std::cout << test->packets_available() << "/" << required_packets << "...";
		}

		std::cout << std::endl << "Enough data has been received.  Proceeding to test..." << std::endl;
	}

	// Get the first run out of the way.
	noutputitems = test->work_test(entries_per_complete_frame,inputPointers,outputPointers);

	elapsed_time = 0.0;

	// make iterations calls to get average.
	for (i=0;i<iterations;i++) {
#ifndef THREAD_RECEIVE
		test->queue_data();
#endif

		start = std::chrono::steady_clock::now();
		noutputitems = test->work_test(entries_per_complete_frame,inputPointers,outputPointers);
		end = std::chrono::steady_clock::now();

		if (noutputitems == entries_per_complete_frame) {
			elapsed_seconds = end-start;

			elapsed_time += elapsed_seconds.count();
		}
		else {
			i--;
			// std::cout << "Error: got zero entries back from work_test.  Retesting." << std::endl;
		}
	}

	elapsed_time = elapsed_time / (float)iterations / entries_per_complete_frame;
	throughput = num_channels  / elapsed_time;
	input_buffer_total_bytes = num_channels * data_size * 2;
	bits_throughput = 8 * input_buffer_total_bytes / elapsed_time;

	std::cout << std::endl << "GNURadio work() performance:" << std::endl;
	std::cout << "Elapsed time: " << elapsed_seconds.count() << std::endl;
	std::cout << "Timing Averaging Iterations: " << iterations << std::endl;
	std::cout << "Average Run Time:   " << std::fixed << std::setw(11) << std::setprecision(6) << elapsed_time << " s" << std::endl <<
				"Total throughput: " << std::setprecision(2) << throughput << " byte complex (x and y) samples/sec" << std::endl <<
				"Projected processing rate: " << bits_throughput << " bps" << std::endl;

	// -------------------------------------------------------------------------------------------
	// Now just run memory copy test.

	test->create_test_buffer();

	// Get the first run out of the way.
	noutputitems = test->work_test_copy(1,inputPointers,outputPointers);

	elapsed_time = 0.0;

	start = std::chrono::steady_clock::now();
	// make iterations calls to get average.
	for (i=0;i<iterations;i++) {
		noutputitems = test->work_test_copy(1,inputPointers,outputPointers);
	}

	end = std::chrono::steady_clock::now();

	elapsed_seconds = end-start;

	elapsed_time = elapsed_seconds.count();

	elapsed_time = elapsed_time / (float)iterations;
	throughput = num_channels  / elapsed_time;
	input_buffer_total_bytes = num_channels * data_size * 2 * 2;  // channels * 1 * 2 (IQ) * 2 streams (X and Y)
	bits_throughput = 8 * input_buffer_total_bytes / elapsed_time;

	std::cout << std::endl << "System memory copy performance:" << std::endl;
	std::cout << "Elapsed time: " << elapsed_seconds.count() << std::endl;
	std::cout << "Timing Averaging Iterations: " << iterations << std::endl;
	std::cout << "Average Run Time:   " << std::fixed << std::setw(11) << std::setprecision(6) << elapsed_time << " s" << std::endl <<
				"Total throughput: " << std::setprecision(2) << throughput << " byte complex (x and y) samples/sec" << std::endl <<
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
			std::cout << "Usage: test-snapsource [--packed] [--start-channel=<channel>]  [--num-channels=num-channels]  [--pcapfile=<file>] [--mcast-group=<IPv4 Group>] [--port=<port>]" << std::endl;
			std::cout << "If --pcapfile is not specified, live network packets will be captured." << std::endl;
			std::cout << "If --mcast-group is specified, live network packets will listen for multicast packets on the specified group." << std::endl;
			std::cout << "--start-channel = first channel in the set.  Default is 1792." << std::endl <<
					     "--num-channels = total number of channels. Default is 1024. " << std::endl <<
						 "--port = UDP port number. " << std::endl;
			std::cout << "--packed will output packed 4-bit IQ rather than full 8-bit IQ." << std::endl;
			std::cout << std::endl;
			exit(0);
		}

		// Just a placeholder if we want to add params
		for (int i=1;i<argc;i++) {
			std::string param = argv[i];

			if (param.find("--pcapfile") != std::string::npos) { // disabled
				boost::replace_all(param,"--pcapfile=","");
				pcap_filename = param;
				use_pcap = true;
			}
			else if (strcmp(argv[i],"--packed")==0) {
				output_packed = true;
			}
			else if (param.find("--start-channel") != std::string::npos) { // disabled
				boost::replace_all(param,"--start-channel=","");
				starting_channel = atoi(param.c_str());
			}
			else if (param.find("--num-channels") != std::string::npos) { // disabled
				boost::replace_all(param,"--num-channels=","");
				num_channels = atoi(param.c_str());
			}
			else if (param.find("--mcast-group") != std::string::npos) { // disabled
				boost::replace_all(param,"--mcast-group=","");
				mcast_group = param;
			}
			else if (param.find("--port") != std::string::npos) { // disabled
				boost::replace_all(param,"--port=","");
				port = atoi(param.c_str());
			}
			else {
				std::cout << "ERROR: Unknown parameter: " << param << std::endl;
				exit(1);

			}

		}
	}
	bool was_successful;

	was_successful = testSNAPSource();

	std::cout << std::endl;

	return 0;
}


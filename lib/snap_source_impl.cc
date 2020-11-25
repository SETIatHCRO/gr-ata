/* -*- c++ -*- */
/*
 * Copyright 2017,2019,2020 ghostop14.
 * Copyright 2020 Derek Kozel
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <endian.h>
#include <inttypes.h>

#include "snap_source_impl.h"
#include <gnuradio/io_signature.h>
#include <sstream>

#include <ata/snap_headers.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define THREAD_RECEIVE

namespace gr {
namespace ata {

snap_source::sptr snap_source::make(int port,
		int headerType,
		bool notifyMissed,
		bool sourceZeros, bool ipv6,
		int starting_channel, int ending_channel) {
	int data_size;
	if (headerType == SNAP_PACKETTYPE_VOLTAGE) {
		data_size = sizeof(char);
	}
	else {
		data_size = sizeof(float);
	}
	return gnuradio::get_initial_sptr(
			new snap_source_impl(port, headerType,
					notifyMissed, sourceZeros, ipv6, starting_channel, ending_channel, data_size));
}

/*
 * The private constructor
 */
snap_source_impl::snap_source_impl(int port,
		int headerType,
		bool notifyMissed,
		bool sourceZeros, bool ipv6,
		int starting_channel, int ending_channel, int data_size)
: gr::sync_block("snap_source",
		gr::io_signature::make(0, 0, 0),
		gr::io_signature::make(2, 4,
				(headerType == SNAP_PACKETTYPE_VOLTAGE) ? data_size * (ending_channel-starting_channel+1)*2:data_size * (ending_channel-starting_channel+1))) {

	is_ipv6 = ipv6;

	d_port = port;
	d_last_channel_block = -1;
	d_notifyMissed = notifyMissed;
	d_sourceZeros = sourceZeros;
	d_partialFrameCounter = 0;

	d_header_type = headerType;
	d_found_start_channel = false;

	d_pmt_seqnum = pmt::string_to_symbol("sample_num");
	std::string id_str = identifier() + " chan " + std::to_string(starting_channel) + " UDP port " + std::to_string(d_port);

	d_block_name = pmt::string_to_symbol(id_str);
	// Configure packet parser
	d_header_size = 0;
	switch (d_header_type) {
	case SNAP_PACKETTYPE_VOLTAGE:
		d_header_size = 8;
		d_payloadsize = 8192; // 256 * 16 * 2; // channels * time steps * pols * sizeof(sc4)
		total_packet_size = 8200;

		d_starting_channel = starting_channel;
		d_ending_channel = ending_channel;
		d_ending_channel_packet_channel_id = ending_channel - 255;

		d_channel_diff = d_ending_channel - d_starting_channel + 1;
		channels_per_packet = 256;

		{
			int channel_check = d_channel_diff % 256;

			if (channel_check > 0) {
				std::stringstream msg_stream;
				msg_stream << "Channels must represent a 256 boundary (end_channel - start_channel + 1) % 256 must be zero.";
				GR_LOG_ERROR(d_logger, msg_stream.str());
				exit(2);
			}
		}
		// GR output vector length will be number of total channels * 2
		// since we expand the packed 8-bit to separate bytes for I and Q.
		d_veclen = d_channel_diff * 2;

		// We're going to lay out the 2-dimensional array as a contiguous block of memory.
		// This will make multi-vector copies in work faster as well, ensuring we have
		// contiguous memory.  The 16 comes from each packet having 16 time samples
		// across 256 channels per packet.
		vector_buffer_size = d_veclen * 16;

		x_vector_buffer = new char[vector_buffer_size];
		y_vector_buffer = new char[vector_buffer_size];

		single_polarization_bytes = d_payloadsize/2;

		break;

	case SNAP_PACKETTYPE_SPECT:
		d_header_size = 8;
		total_packet_size = 8200;
		d_payloadsize = 512 * 4 * sizeof(float); // 512 channels * 4 output indices, all float.

		d_starting_channel = 0;
		d_ending_channel = 4096;
		d_ending_channel_packet_channel_id = 3584;

		d_channel_diff = 4096;

		channels_per_packet = 512;

		xx_buffer = new float[4096];
		yy_buffer = new float[4096];
		xy_real_buffer = new float[4096];
		xy_imag_buffer = new float[4096];

		d_veclen = 4096;
		vector_buffer_size = d_veclen * sizeof(float);

		single_polarization_bytes = 0; // unused in this mode.
		break;

	default:
		GR_LOG_ERROR(d_logger, "Unknown source data format.");
		exit(1);
		break;
	}

	if (d_payloadsize < 8) {
		GR_LOG_ERROR(d_logger,
				"Payload size is too small.  Must be at "
				"least 8 bytes once header/trailer adjustments are made.");
		exit(1);
	}

	localBuffer = new unsigned char[total_packet_size];
	long maxCircBuffer;

	// Compute reasonable buffer size
	maxCircBuffer = total_packet_size * 3000;
	d_localqueue = new boost::circular_buffer<unsigned char>(maxCircBuffer);

	// Initialize receiving socket
	if (is_ipv6)
		d_endpoint =
				boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v6(), port);
	else
		d_endpoint =
				boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port);

	// TODO: Move opening of the socket to ::start()
	try {
		d_udpsocket = new boost::asio::ip::udp::socket(d_io_service, d_endpoint);
	} catch (const std::exception &ex) {
		throw std::runtime_error(std::string("[SNAP Source] Error occurred: ") +
				ex.what());
	}

	std::stringstream msg_stream;
	msg_stream << "Listening for data on UDP port " << port << ".";
	GR_LOG_INFO(d_logger, msg_stream.str());

	// We'll always produce blocks of 16 time vectors for voltage mode.
	if (d_header_type == SNAP_PACKETTYPE_VOLTAGE) {
		gr::block::set_output_multiple(16);
	}

	for (int i=0;i<7;i++) {
		twosComplementLUT[i] = i;
	}

	twosComplementLUT[8] = 0; // 1000 is a special case: -0 (as opposed to 0000 = +0)
	for (int i=9;i<16;i++) {
		twosComplementLUT[i] = i - 16;
	}

	message_port_register_out(pmt::mp("sync_header"));

#ifdef _OPENMP
	/*
	num_procs = omp_get_num_procs()/2;
	if (num_procs < 1)
		num_procs = 1;
	*/
	// Seems like we can hard-code this.
	// Two seems to do fine.
	num_procs = 2;
#else
	num_procs = 1;
	GR_LOG_WARN(d_logger, "OMP not enabled.  Performance may be degraded.  Please install libomp and recompile.");
#endif

#ifdef THREAD_RECEIVE
	proc_thread = new boost::thread(boost::bind(&snap_source_impl::runThread, this));
#endif
}

/*
 * Our destructor.
 */
snap_source_impl::~snap_source_impl() { stop(); }

bool snap_source_impl::stop() {
	if (proc_thread) {
		stop_thread = true;

		while (threadRunning)
			usleep(10);

		delete proc_thread;
		proc_thread = NULL;
	}

	if (d_udpsocket) {
		d_udpsocket->close();

		d_udpsocket = NULL;

		d_io_service.reset();
		d_io_service.stop();
	}

	if (localBuffer) {
		delete[] localBuffer;
		localBuffer = NULL;
	}

	if (x_vector_buffer) {
		delete[] x_vector_buffer;
		x_vector_buffer = NULL;
	}

	if (y_vector_buffer) {
		delete[] y_vector_buffer;
		y_vector_buffer = NULL;
	}

	if (xx_buffer) {
		delete[] xx_buffer;
		xx_buffer = NULL;
	}

	if (yy_buffer) {
		delete[] yy_buffer;
		yy_buffer = NULL;
	}

	if (xy_real_buffer) {
		delete[] xy_real_buffer;
		xy_real_buffer = NULL;
	}

	if (xy_imag_buffer) {
		delete[] xy_imag_buffer;
		xy_imag_buffer = NULL;
	}

	if (d_localqueue) {
		delete d_localqueue;
		d_localqueue = NULL;
	}

	if (test_buffer) {
		delete[] test_buffer;
		test_buffer = NULL;

	}
	return true;
}

int snap_source_impl::work_volt_mode(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items, bool liveWork) {
	gr::thread::scoped_lock guard(d_setlock);
	char *x_out = (char *)output_items[0];
	char *y_out = (char *)output_items[1];

#ifndef THREAD_RECEIVE
	queue_data();
#endif

	int bytesAvailable = data_available();

	// Handle case where no data is available
	if (bytesAvailable == 0) {
		d_partialFrameCounter = 0;

		if (d_sourceZeros) {
			// Just return 0's
			unsigned int numRequested = noutput_items * d_veclen;
			memset((void *)x_out, 0x00, numRequested);
			memset((void *)y_out, 0x00, numRequested);

			if (liveWork) {
				// Since we're sourcing zeros, output a definable sequence number.
				pmt::pmt_t pmt_sequence_number =pmt::from_long(-1);

				for (int i=0;i<noutput_items;i++) {
					add_item_tag(0, nitems_written(0) + i, d_pmt_seqnum, pmt_sequence_number,d_block_name);
					add_item_tag(1, nitems_written(0) + i, d_pmt_seqnum, pmt_sequence_number,d_block_name);
				}
			}
			return noutput_items;

		} else {
			// Returning 0 causes GNU Radio to call work again in 0.1s
			return 0;
		}
	}

	// Handle partial packets
	if (bytesAvailable < total_packet_size) {
		// since we should be getting these in UDP packet blocks matched on the
		// sender/receiver, this should be a fringe case, or a case where another
		// app is sourcing the packets.
		d_partialFrameCounter++;

		if (d_partialFrameCounter >= 100) {
			std::stringstream msg_stream;
			msg_stream << "Insufficient block data.  Check your sending "
					"app is using "
					<< d_payloadsize << " send blocks.";
			GR_LOG_WARN(d_logger, msg_stream.str());

			d_partialFrameCounter = 0;
		}
		return 0; // Don't memset 0x00 since we're starting to get data.  In this
		// case we'll hold for the rest.
	}

	snap_header hdr;

	if (!d_found_start_channel) {
		// synchronize on start of vector
		while ( (!d_found_start_channel) && (bytesAvailable >= total_packet_size)) {
			header_to_local_buffer();

			get_voltage_header(hdr);

			if (hdr.channel_id == d_starting_channel) {
				// We found our start channel packet.
				// Set that we're synchronized and exit our loop here.
				d_found_start_channel = true;
				std::stringstream msg_stream;
				msg_stream << "Data block alignment achieved with sample number " << hdr.sample_number << " as first block";
				GR_LOG_INFO(d_logger, msg_stream.str());

				if (liveWork) {
				    pmt::pmt_t meta = pmt::make_dict();

				    meta = pmt::dict_add(meta, pmt::mp("antenna_id"), pmt::mp(hdr.antenna_id));
				    meta = pmt::dict_add(meta, pmt::mp("starting_channel"), pmt::mp(hdr.channel_id));
				    meta = pmt::dict_add(meta, pmt::mp("sample_number"), pmt::mp(hdr.sample_number));
				    meta = pmt::dict_add(meta, pmt::mp("firmware_version"), pmt::mp(hdr.firmware_version));

				    pmt::pmt_t pdu = pmt::cons(meta, pmt::PMT_NIL);
				    message_port_pub(pmt::mp("sync_header"), pdu);
				}

				break;
			}
			else {
				// We found a packet that wasn't the first one of the vector.
				// Just dump it till we're synchronized.
				{
#ifdef THREAD_RECEIVE
					gr::thread::scoped_lock guard(d_net_mutex);
#endif
					for (int curByte=0;curByte < total_packet_size;curByte++) {
						d_localqueue->pop_front();
					}

					bytesAvailable = d_localqueue->size();
				}
			}
		}

		// We're still looking for the vector start packet and we haven't found it yet.
		if (!d_found_start_channel) {
			return 0;
		}
	}
	// If we're here, it's not a partial hanging frame
	d_partialFrameCounter = 0;

	// Now if we're here we should have at least 1 block.

	// Let's extract the vectors.  The total data is in a 16 time sample by 256 channel by 2 polarization
	// packet.  Multiple packets will be required to make up a whole time set, so we need to buffer
	// the chunks to a local temp buffer for each of the x and y data sets.  Once we have a complete set,
	// we can queue each of the complete time entries to a queue for output consumption.
	bool found_last_packet = false;
	int skippedPackets = 0;

	// Queue all the data we have into our local queue

	int noutput_items_x2 = noutput_items * 2;

	while ((bytesAvailable >= total_packet_size) && (x_vector_queue.size() < noutput_items_x2)) {
		fill_local_buffer();
		bytesAvailable -= total_packet_size;

		get_voltage_header(hdr);

		// check for bad channel id first.
		if ((hdr.channel_id < d_starting_channel) || (hdr.channel_id > d_ending_channel_packet_channel_id) ) {
			std::cout << "ERROR: Received an unexpected channel index.  Skipping packet.  Received block starting channel id: " << hdr.channel_id <<
					" expecting between " << d_starting_channel << " and " << d_ending_channel_packet_channel_id << std::endl;
			continue;
		}

		if (hdr.channel_id == d_starting_channel) {
			// We're starting a new vector, so zero out what we have.
			memset(x_vector_buffer,0x00,vector_buffer_size);
			memset(y_vector_buffer,0x00,vector_buffer_size);
		}

		// Check if we skipped packets by missing a channel block.
		// The if statement just checks that this isn't our very first packet we're processing.
		// d_last_channel_block is set to -1 in the constructor, and 0 could be a valid start channel.
		if (d_last_channel_block >= 0) {
			if (hdr.channel_id > d_last_channel_block) {
				int delta = (hdr.channel_id - d_last_channel_block) / channels_per_packet;

				// Delta should be 1.  So anything more than 1 is a skipped packet.
				skippedPackets += delta - 1;
			}
			else {
				// channel id < last block so we wrapped around.
				// int dist_to_end = (d_ending_channel_packet_channel_id - d_last_channel_block)/channels_per_packet;
				// int dist_from_start = (hdr.channel_id - d_starting_channel)/channels_per_packet;

				// skippedPackets += dist_to_end + dist_from_start;
				skippedPackets += (d_ending_channel_packet_channel_id - d_last_channel_block + hdr.channel_id - d_starting_channel) / channels_per_packet;
			}
		}

		// Store what we saw as the "last" packet we received.
		d_last_channel_block = hdr.channel_id;

		unsigned char *pData;  // Pointer to our UDP payload after the header.
		// Move to the beginning of our packet data section
		pData = (unsigned char *)&localBuffer[d_header_size];

		voltage_packet *vp;
		vp = (voltage_packet *)pData;

		// cycle through the time entry rows in the packet. (will always be 16)

		int channel_offset_within_time_block = (hdr.channel_id - d_starting_channel) * 2;

		int t;
		int sample;

		#pragma omp parallel for num_threads(2) collapse(2)
		for (t=0;t<16;t++) {
			for (sample=0;sample<256;sample++) {
				// This moves us in the packet memory to the correct time row
				int vector_start = t * d_veclen  + channel_offset_within_time_block;
				char *x_pol;
				char *y_pol;
				x_pol = &x_vector_buffer[vector_start];
				y_pol = &y_vector_buffer[vector_start];

				int TwoS = 2*sample;
				int TwoS1 = TwoS + 1;

				x_pol[TwoS] = (char)(vp->data[t][sample][0] >> 4); // I
				// Need to adjust twos-complement
				x_pol[TwoS] = TwosComplementLookup4Bit(x_pol[TwoS]); // TwosComplement4Bit(x_pol[TwoS]);

				x_pol[TwoS1] = (char)(vp->data[t][sample][0] & 0x0F);  // Q
				x_pol[TwoS1] = TwosComplementLookup4Bit(x_pol[TwoS1]); // TwosComplement4Bit(x_pol[TwoS1]);

				y_pol[TwoS] = (char)(vp->data[t][sample][1] >> 4); // I
				y_pol[TwoS] = TwosComplementLookup4Bit(y_pol[TwoS]); // TwosComplement4Bit(y_pol[TwoS]);

				y_pol[TwoS1] = (char)(vp->data[t][sample][1] & 0x0F);  // Q
				y_pol[TwoS1] = TwosComplementLookup4Bit(y_pol[TwoS1]); // TwosComplement4Bit(y_pol[TwoS1]);
			}
		}

		// Now check if we've completed a set.  If so, let's queue it up
		// for output consumption.
		if (hdr.channel_id == d_ending_channel_packet_channel_id) {
			// Queue up our vectors.  Again always 16 discrete time entries.
			for (int this_time_start=0;this_time_start<16;this_time_start++) {
				int block_start = this_time_start * d_veclen;

				data_vector<char> x_cur_vector(&x_vector_buffer[block_start],d_veclen);
				data_vector<char> y_cur_vector(&y_vector_buffer[block_start],d_veclen);

				// x_cur_vector.store(&x_vector_buffer[block_start],d_veclen);
				// y_cur_vector.store(&y_vector_buffer[block_start],d_veclen);

				x_vector_queue.push_back(x_cur_vector);
				y_vector_queue.push_back(y_cur_vector);
				seq_num_queue.push_back(hdr.sample_number);
			}
		}
	}

	int items_returned = noutput_items;
	// Move queue items to output items as needed
	// both queues will be the same size, so can just pick one.
	if (x_vector_queue.size() < noutput_items) {
		items_returned = x_vector_queue.size();
	}

	for (int i=0;i<items_returned;i++) {
		// This needs to come from the new queue
		data_vector<char> x_cur_vector = x_vector_queue.front();
		x_vector_queue.pop_front();
		data_vector<char> y_cur_vector = y_vector_queue.front();
		y_vector_queue.pop_front();
		uint64_t vector_seq_num = seq_num_queue.front();
		seq_num_queue.pop_front();

		int out_index = d_veclen*i;

		// Now move to work output vector.
		memcpy(&x_out[out_index],x_cur_vector.data_pointer(),d_veclen);
		memcpy(&y_out[out_index],y_cur_vector.data_pointer(),d_veclen);

		// Add sequence number start tag for down-stream coherence
		// Since each packet set contains 16 time samples for the same packet sequence number,
		// You'll see output vectors in blocks of 16 with the same sequence number.
		// This is expected.

		if (liveWork) {
			pmt::pmt_t pmt_sequence_number =pmt::from_long((long)vector_seq_num);

			add_item_tag(0, nitems_written(0) + i, d_pmt_seqnum, pmt_sequence_number,d_block_name);
			add_item_tag(1, nitems_written(0) + i, d_pmt_seqnum, pmt_sequence_number,d_block_name);
		}
	}

	// Notify on skipped packets
	NotifyMissed(skippedPackets);

	return items_returned;
}

int snap_source_impl::work_spec_mode(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items, bool liveWork) {
	gr::thread::scoped_lock guard(d_setlock);

	static bool firstTime = true;

	float *xx_out = (float *)output_items[0];
	float *yy_out = (float *)output_items[1];
	float *xy_real_out = (float *)output_items[2];
	float *xy_imag_out = (float *)output_items[3];

#ifndef THREAD_RECEIVE
	queue_data();
#endif

	int bytesAvailable = data_available();
	int items_returned = noutput_items;

	// Handle case where no data is available
	if (bytesAvailable == 0) {
		d_partialFrameCounter = 0;

		if (d_sourceZeros) {
			// Just return 0's
			unsigned int numRequested = noutput_items * vector_buffer_size;
			memset((void *)xx_out, 0x00, numRequested);
			memset((void *)yy_out, 0x00, numRequested);
			memset((void *)xy_real_out, 0x00, numRequested);
			memset((void *)xy_imag_out, 0x00, numRequested);

			return noutput_items;

		} else {
			// Returning 0 causes GNU Radio to call work again in 0.1s
			return 0;
		}
	}

	// Handle partial packets
	if (bytesAvailable < total_packet_size) {
		// since we should be getting these in UDP packet blocks matched on the
		// sender/receiver, this should be a fringe case, or a case where another
		// app is sourcing the packets.
		d_partialFrameCounter++;

		if (d_partialFrameCounter >= 100) {
			std::stringstream msg_stream;
			msg_stream << "Insufficient block data.  Check your sending "
					"app is using "
					<< d_payloadsize << " send blocks.";
			GR_LOG_WARN(d_logger, msg_stream.str());

			d_partialFrameCounter = 0;
		}
		return 0; // Don't memset 0x00 since we're starting to get data.  In this
		// case we'll hold for the rest.
	}

	snap_header hdr;

	if (!d_found_start_channel) {
		// synchronize on start of vector
		while ( (!d_found_start_channel) && (bytesAvailable >= total_packet_size)) {
			header_to_local_buffer();

			get_spectrometer_header(hdr);

			if (hdr.channel_id == d_starting_channel) {
				// We found our start channel packet.
				// Set that we're synchronized and exit our loop here.
				d_found_start_channel = true;
				std::stringstream msg_stream;
				msg_stream << "Data block alignment achieved with sample number " << hdr.sample_number << " as first block";
				GR_LOG_INFO(d_logger, msg_stream.str());

				if (liveWork) {
				    pmt::pmt_t meta = pmt::make_dict();

				    meta = pmt::dict_add(meta, pmt::mp("antenna_id"), pmt::mp(hdr.antenna_id));
				    meta = pmt::dict_add(meta, pmt::mp("starting_channel"), pmt::mp(hdr.channel_id));
				    meta = pmt::dict_add(meta, pmt::mp("sample_number"), pmt::mp(hdr.sample_number));
				    meta = pmt::dict_add(meta, pmt::mp("firmware_version"), pmt::mp(hdr.firmware_version));

				    pmt::pmt_t pdu = pmt::cons(meta, pmt::PMT_NIL);
				    message_port_pub(pmt::mp("sync_header"), pdu);
				}

				break;
			}
			else {
				// We found a packet that wasn't the first one of the vector.
				// Just dump it till we're synchronized.
				{
#ifdef THREAD_RECEIVE
					gr::thread::scoped_lock guard(d_net_mutex);
#endif
					for (int curByte=0;curByte < total_packet_size;curByte++) {
						d_localqueue->pop_front();
					}

					bytesAvailable = d_localqueue->size();
				}
			}
		}

		// We're still looking for the vector start packet and we haven't found it yet.
		if (!d_found_start_channel) {
			return 0;
		}
	}
	// If we're here, it's not a partial hanging frame
	d_partialFrameCounter = 0;

	// Now if we're here we should have at least 1 block.


	// Let's extract the vectors.  The total data is in a 16 time sample by 256 channel by 2 polarization
	// packet.  Multiple packets will be required to make up a whole time set, so we need to buffer
	// the chunks to a local temp buffer for each of the x and y data sets.  Once we have a complete set,
	// we can queue each of the complete time entries to a queue for output consumption.
	bool found_last_packet = false;
	int skippedPackets = 0;

	// Queue all the data we have into our local queue
	int snapshot_data_available = data_available();

	while (snapshot_data_available >= total_packet_size) {
		fill_local_buffer();
		snapshot_data_available -= total_packet_size;

		get_spectrometer_header(hdr);

		// check for bad channel id first.
		if ((hdr.channel_id < d_starting_channel) || (hdr.channel_id > d_ending_channel_packet_channel_id) ) {
			std::cout << "ERROR: Received an unexpected channel index.  Skipping packet.  Received block starting channel id: " << hdr.channel_id <<
					" expecting between " << d_starting_channel << " and " << d_ending_channel_packet_channel_id << std::endl;
			continue;
		}

		if (hdr.channel_id == d_starting_channel) {
			// We're starting a new vector, so zero out what we have.
			memset(xx_buffer,0x00,vector_buffer_size);
			memset(yy_buffer,0x00,vector_buffer_size);
			memset(xy_real_buffer,0x00,vector_buffer_size);
			memset(xy_imag_buffer,0x00,vector_buffer_size);
		}


		// Check if we skipped packets by missing a channel block.
		// The if statement just checks that this isn't our very first packet we're processing.
		// d_last_channel_block is set to -1 in the constructor, and 0 could be a valid start channel.
		if (d_last_channel_block >= 0) {
			if (hdr.channel_id > d_last_channel_block) {
				int delta = (hdr.channel_id - d_last_channel_block) / channels_per_packet;

				// Delta should be 1.  So anything more than 1 is a skipped packet.
				skippedPackets += delta - 1;
			}
			else {
				// channel id < last block so we wrapped around.
				int dist_to_end = (d_ending_channel_packet_channel_id - d_last_channel_block)/channels_per_packet;
				int dist_from_start = (hdr.channel_id - d_starting_channel)/channels_per_packet;

				skippedPackets += dist_to_end + dist_from_start;
			}
		}

		// Store what we saw as the "last" packet we received.
		d_last_channel_block = hdr.channel_id;

		unsigned char *pData;  // Pointer to our UDP payload after the header.
		// Move to the beginning of our packet data section
		pData = (unsigned char *)&localBuffer[d_header_size];

		spectrometer_packet *sp;
		sp = (spectrometer_packet *)pData;

		// cycle through the time entry rows in the packet. (will always be 16)

		// Channels received in this mode will always be 0-4095
		int channel_offset_within_block = hdr.channel_id;

		float *xx;
		float *yy;
		float *xy_real;
		float *xy_imag;

		xx = &xx_buffer[channel_offset_within_block];
		yy = &yy_buffer[channel_offset_within_block];
		xy_real = &xy_real_buffer[channel_offset_within_block];
		xy_imag = &xy_imag_buffer[channel_offset_within_block];

		for (int sample=0;sample<512;sample++) {
			*xx++ = sp->data[sample][0];
			*yy++ = sp->data[sample][1];
			*xy_real++ = sp->data[sample][2];
			*xy_imag++ = sp->data[sample][3];
		}

		// Now check if we've completed a set.  If so, let's queue it up
		// for output consumption.
		if (hdr.channel_id == d_ending_channel_packet_channel_id) {
			// Queue up our vector
			data_vector<float> xx_cur_vector;
			data_vector<float> yy_cur_vector;
			data_vector<float> xy_real_cur_vector;
			data_vector<float> xy_imag_cur_vector;

			xx_cur_vector.store(xx_buffer,d_veclen);
			yy_cur_vector.store(yy_buffer,d_veclen);
			xy_real_cur_vector.store(xy_real_buffer,d_veclen);
			xy_imag_cur_vector.store(xy_imag_buffer,d_veclen);

			xx_vector_queue.push_back(xx_cur_vector);
			yy_vector_queue.push_back(yy_cur_vector);
			xy_real_vector_queue.push_back(xy_real_cur_vector);
			xy_imag_vector_queue.push_back(xy_imag_cur_vector);

			seq_num_queue.push_back(hdr.sample_number);
		}
	}

	// Move queue items to output items as needed
	// both queues will be the same size, so can just pick one.
	if (xx_vector_queue.size() < noutput_items) {
		items_returned = xx_vector_queue.size();
	}

	for (int i=0;i<items_returned;i++) {
		// This needs to come from the new queue
		data_vector<float> xx_cur_vector = xx_vector_queue.front();
		xx_vector_queue.pop_front();
		data_vector<float> yy_cur_vector = yy_vector_queue.front();
		yy_vector_queue.pop_front();
		data_vector<float> xy_real_cur_vector = xy_real_vector_queue.front();
		xy_real_vector_queue.pop_front();
		data_vector<float> xy_imag_cur_vector = xy_imag_vector_queue.front();
		xy_imag_vector_queue.pop_front();
		uint64_t vector_seq_num = seq_num_queue.front();
		seq_num_queue.pop_front();

		// Now move to work output vector.
		memcpy(&xx_out[d_veclen*i],xx_cur_vector.data_pointer(),vector_buffer_size);
		memcpy(&yy_out[d_veclen*i],yy_cur_vector.data_pointer(),vector_buffer_size);

		// output the xy's if the ports are connected.
		if (output_items.size() > 2) {
			// xy is tagged as an optional output.
			memcpy(&xy_real_out[d_veclen*i],xy_real_cur_vector.data_pointer(),vector_buffer_size);
		}

		if (output_items.size() > 3) {
			// xy is tagged as an optional output.
			memcpy(&xy_imag_out[d_veclen*i],xy_imag_cur_vector.data_pointer(),vector_buffer_size);
		}

		// Add sequence number start tag for down-stream coherence
		// Since each packet set contains 16 time samples for the same packet sequence number,
		// You'll see output vectors in blocks of 16 with the same sequence number.
		// This is expected.

		if (liveWork) {
			pmt::pmt_t pmt_sequence_number =pmt::from_long((long)vector_seq_num);

			add_item_tag(0, nitems_written(0) + i, d_pmt_seqnum, pmt_sequence_number,d_block_name);
			add_item_tag(1, nitems_written(0) + i, d_pmt_seqnum, pmt_sequence_number,d_block_name);
		}
	}

	// Notify on skipped packets
	NotifyMissed(skippedPackets);

	return items_returned;
}

void snap_source_impl::create_test_buffer() {
	if (!test_buffer) {
		test_buffer = new char[d_channel_diff*2];
	}
}

int snap_source_impl::work_test_copy(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items) {
	// This just tests copying noutput_items from in to out to
	// isolate and benchmark memory copy performance.
	static int test_block_size = d_channel_diff * 2;

	char *x_out = (char *)output_items[0];
	char *y_out = (char *)output_items[1];

	for (int i=0;i<noutput_items;i++) {
		memcpy(&x_out[i*test_block_size],&test_buffer[0],test_block_size);
		memcpy(&y_out[i*test_block_size],&test_buffer[0],test_block_size);
	}

	return noutput_items;
}

int snap_source_impl::work_test(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items) {
	if (SNAP_PACKETTYPE_VOLTAGE) {
		return work_volt_mode(noutput_items, input_items, output_items, false);
	}
	else {
		return work_spec_mode(noutput_items, input_items, output_items, false);
	}
}

int snap_source_impl::work(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items) {
	if (SNAP_PACKETTYPE_VOLTAGE) {
		return work_volt_mode(noutput_items, input_items, output_items, true);
	}
	else {
		return work_spec_mode(noutput_items, input_items, output_items, true);
	}
}

void snap_source_impl::queue_data() {
	size_t bytesAvailable = netdata_available();

	// Let's be efficient here and say we have to have a total packet before
	// we try queuing data.  That'll cut down on thread locks that'll
	// disrupt work()

	if (bytesAvailable >= total_packet_size) {
		boost::asio::streambuf::mutable_buffers_type buf = d_read_buffer.prepare(bytesAvailable);
		size_t bytesRead = d_udpsocket->receive_from(buf, d_endpoint);

		if (bytesRead > 0) {
			d_read_buffer.commit(bytesRead);

			// Get the data and add it to our local queue.  We have to maintain a
			// local queue in case we read more bytes than noutput_items is asking
			// for.  In that case we'll only return noutput_items bytes
			const char *readData = boost::asio::buffer_cast<const char *>(d_read_buffer.data());
			{
#ifdef THREAD_RECEIVE
				gr::thread::scoped_lock guard(d_net_mutex);
#endif
				for (size_t i = 0; i < bytesRead; i++) {
					d_localqueue->push_back(readData[i]);
				}
			}

			d_read_buffer.consume(bytesRead);
		}
	}
}

void snap_source_impl::runThread() {
	threadRunning = true;

	while (!stop_thread) {
		queue_data();

		usleep(100);
	}

	threadRunning = false;
}

} /* namespace ata */
} /* namespace gr */

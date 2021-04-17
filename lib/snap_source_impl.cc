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
#include <boost/asio/signal_set.hpp>

#include <ata/snap_headers.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#define THREAD_RECEIVE

#define DS_NETWORK 1
#define DS_MCAST 2
#define DS_PCAP 3

// This is the maximum missed frames before we declare something went terribly wrong.
// 10000 = 0.04 seconds
// 25000 = 0.1 seconds
const int MAX_MISSED_SETS=20000;
// So the work function does naturally limit how big these buffers can get,
// and it puts backpressure on d_localqueue.
const int MAX_WORK_BUFF_SIZE=125000;
// This is number of packets now.  So memory is this * packet size.
#define MAX_NET_CIRC_BUFFER 1000000


namespace gr {
namespace ata {

snap_source::sptr snap_source::make(int port,
		int headerType,
		bool notifyMissed,
		bool sourceZeros, bool ipv6,
		int starting_channel, int ending_channel,
		int data_source, std::string file, bool repeat_file, bool packed_output,std::string mcast_group,
		bool send_start_msg, std::string udp_ip) {
	int data_size;
	if (headerType == SNAP_PACKETTYPE_VOLTAGE) {
		data_size = sizeof(char);
	}
	else {
		data_size = sizeof(float);
	}
	return gnuradio::get_initial_sptr(
			new snap_source_impl(port, headerType,
					notifyMissed, sourceZeros, ipv6, starting_channel, ending_channel, data_size, data_source, file, repeat_file,
					packed_output, mcast_group, send_start_msg, udp_ip));
}

/*
 * The private constructor
 */
snap_source_impl::snap_source_impl(int port,
		int headerType,
		bool notifyMissed,
		bool sourceZeros, bool ipv6,
		int starting_channel, int ending_channel, int data_size,
		int data_source, std::string file, bool repeat_file, bool packed_output,
		std::string mcast_group, bool send_start_msg, std::string udp_ip)
: gr::sync_block("snap_src_" + std::to_string(port) + "_",
		gr::io_signature::make(0, 0, 0),
		gr::io_signature::make(1, 4,
				(headerType == SNAP_PACKETTYPE_VOLTAGE) ? data_size * (ending_channel-starting_channel+1)*2:data_size * (ending_channel-starting_channel+1)))
#ifdef USE_CIRC_VB
seq_num_queue(MAX_WORK_BUFF_SIZE),x_vector_queue(MAX_WORK_BUFF_SIZE),y_vector_queue(MAX_WORK_BUFF_SIZE),
xx_vector_queue(MAX_WORK_BUFF_SIZE),yy_vector_queue(MAX_WORK_BUFF_SIZE),xy_real_vector_queue(MAX_WORK_BUFF_SIZE),xy_imag_vector_queue(MAX_WORK_BUFF_SIZE)
#endif
{
	d_udp_ip = udp_ip;

	d_send_start_msg = send_start_msg;

	if (data_source == DS_PCAP) {
		d_use_pcap = true;
	}
	else {
		d_use_pcap = false;
	}

	d_file = file;
	d_repeat_file = repeat_file;
	pcap_file_done = false;

	d_data_source = data_source;
	d_mcast_group = mcast_group;

	if (d_data_source == DS_MCAST) {
		d_use_mcast = true;
	}
	else {
		d_use_mcast = false;
	}

	if (data_source == DS_PCAP) {
		if (d_file.length() == 0) {
			std::stringstream msg;
			msg << "No PCAP file name provided.  Please provide a filename.";
			GR_LOG_ERROR(d_logger, msg.str());
			throw std::runtime_error("[SNAP Source] No PCAP file name provided.  Please provide a filename.");
			return;
		}

		if (FILE *file = fopen(d_file.c_str(), "r")) {
			fclose(file);
		} else {
			std::stringstream msg;
			msg << "Can't open pcap file.";
			GR_LOG_ERROR(d_logger, msg.str());
			throw std::runtime_error("[SNAP Source] can't open pcap file");
			return;
		}

		openPCAP();
	}

	is_ipv6 = ipv6;

	d_packed_output = packed_output;

	d_port = port;
	d_last_channel_block = -1;
	d_last_timestamp = 0;
	d_notifyMissed = notifyMissed;
	d_sourceZeros = sourceZeros;
	d_partialFrameCounter = 0;

	d_header_type = headerType;
	d_found_start_channel = false;

	d_pmt_seqnum = pmt::string_to_symbol("sample_num");
	std::string id_str = identifier() + " chan " + std::to_string(starting_channel) + " UDP port " + std::to_string(d_port);

	d_block_name = pmt::string_to_symbol(id_str);
	d_header_size = 0;

	multipacket_frame_pkt_ctr = 0;

	switch (d_header_type) {
	case SNAP_PACKETTYPE_VOLTAGE:
		d_header_size = sizeof(struct voltage_header);

		d_payloadsize = 8192; // 256 * 16 * 2; // channels * time steps * pols * sizeof(sc4)
		total_packet_size = d_payloadsize + d_header_size;

		d_starting_channel = starting_channel;
		d_ending_channel = ending_channel;
		d_ending_channel_packet_channel_id = ending_channel - 255;

		d_channel_diff = d_ending_channel - d_starting_channel + 1;

		if (d_channel_diff <= 256) {
			b_one_packet = true;
			// std::cout << "Configuring for single-packet mode" << std::endl;
		}
		else {
			b_one_packet = false;
		}

		channels_per_packet = 256;

		{
			int channel_check = d_channel_diff % 256;

			if (channel_check > 0) {
				std::stringstream msg_stream;
				msg_stream << "Channels must represent a 256 boundary (end_channel - start_channel + 1) % 256 must be zero.";
				GR_LOG_ERROR(d_logger, msg_stream.str());
				throw std::out_of_range ("Channels must represent a 256 boundary (end_channel - start_channel + 1) % 256 must be zero.");
			}
		}

		packets_per_frame = d_channel_diff / channels_per_packet;

		// GR output vector length will be number of total channels * 2
		// In all modes.
		// Unpacked mode: since we expand the packed 8-bit to separate bytes for I and Q.
		// Packed mode: 4-bits each for I & Q X [1-byte total], 4-bits each for I & Q Y [1-byte total], so still 2 bytes.

		d_veclen = d_channel_diff * 2;

		// We're going to lay out the 2-dimensional array as a contiguous block of memory.
		// This will make multi-vector copies in work faster as well, ensuring we have
		// contiguous memory.  The 16 comes from each packet having 16 time samples
		// across 256 channels per packet.
		vector_buffer_size = d_veclen * 16;

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

	// We'll always produce blocks of 16 time vectors for voltage mode.
	if (d_header_type == SNAP_PACKETTYPE_VOLTAGE) {
		gr::block::set_output_multiple(16);
	}

	message_port_register_out(pmt::mp("sync_header"));
	message_port_register_in(pmt::mp("sync"));
	set_msg_handler(pmt::mp("sync"), boost::bind(&snap_source_impl::handleSyncMsg, this, _1) );
}

void snap_source_impl::handleSyncMsg(pmt::pmt_t msg) {
	pmt::pmt_t data = pmt::cdr(msg);
	try {
		sync_timestamp = pmt::to_uint64(data);
	}
	catch(...) {
		std::stringstream msg;
		msg << "A sync PMT message was received that could not be converted to a uint64.";
		GR_LOG_WARN(d_logger, msg.str());
	}
}

/*
 * Our destructor.
 */
snap_source_impl::~snap_source_impl() {
	stop();
}

void snap_source_impl::openPCAP() {
	gr::thread::scoped_lock lock(fp_mutex);
	char errbuf[PCAP_ERRBUF_SIZE];
	try {
		pcapFile = pcap_open_offline(d_file.c_str(), errbuf);

		if (pcapFile == NULL) {
			std::stringstream msg;
			msg << "[SNAP Source] Error occurred: " << errbuf;
			GR_LOG_ERROR(d_logger, msg.str());
			return;
		}
	} catch (const std::exception &ex) {
		std::stringstream msg;
		msg << "[SNAP Source] Error occurred: " << ex.what();
		GR_LOG_ERROR(d_logger, msg.str());
		pcapFile = NULL;
	}
}

void snap_source_impl::closePCAP() {
	gr::thread::scoped_lock lock(fp_mutex);
	if (pcapFile != NULL) {
		pcap_close(pcapFile);
		pcapFile = NULL;
	}
}

bool snap_source_impl::start() {
	for (int i=0;i<8;i++) {
		twosComplementLUT[i] = i;
	}

	twosComplementLUT[8] = 0; // 1000 is a special case: -0 (as opposed to 0000 = +0)
	for (int i=9;i<16;i++) {
		twosComplementLUT[i] = i - 16;
	}

	if (!d_use_pcap) {
		// dividing by packets/frame will speed things up when more packets are expected.
		mmsg_sleep_time = MMSG_LENGTH / 2 * 32 / packets_per_frame;

		memset(msgs, 0, sizeof(msgs));
		for (int i = 0; i < MMSG_LENGTH; i++) {
			iovecs[i].iov_base         = bufs[i];
			iovecs[i].iov_len          = total_packet_size;
			msgs[i].msg_hdr.msg_iov    = &iovecs[i];
			msgs[i].msg_hdr.msg_iovlen = 1;
		}
		timeout.tv_sec = MMSG_TIMEOUT;
		timeout.tv_nsec = 0;

		// Initialize receiving socket
		boost::asio::ip::address mcast_addr;
		if (is_ipv6)
			d_endpoint =
					boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v6(), d_port);
		else {
			if (d_data_source == DS_NETWORK) {
				// Standard UDP
				if ( (d_udp_ip.length() == 0) || (d_udp_ip == "0.0.0.0") || (d_udp_ip == "any") || (d_udp_ip == "all") ) {
					d_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), d_port);
				}
				else {
					try {
						d_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(d_udp_ip), d_port);
					}
					catch (const std::exception &ex) {
						std::stringstream msg_stream;
						msg_stream << "Error converting  " << d_udp_ip << " to endpoint object.  Check address is valid.";
						GR_LOG_ERROR(d_logger, msg_stream.str());

						throw std::runtime_error(std::string("[SNAP Source] Error occurred: ") +
								ex.what());
					}
				}
			}
			else {
				// Multicast group
				try {
					mcast_addr = boost::asio::ip::address::from_string(d_mcast_group, ec);
				}
				catch (const std::exception &ex) {
					std::stringstream msg_stream;
					msg_stream << "Error converting  " << d_mcast_group << " to address object.  Check address is valid.";
					GR_LOG_ERROR(d_logger, msg_stream.str());

					throw std::runtime_error(std::string("[SNAP Source] Error occurred: ") +
							ex.what());
				}

				try {
					d_endpoint = boost::asio::ip::udp::endpoint(mcast_addr, d_port);
				}
				catch (const std::exception &ex) {
					std::stringstream msg_stream;
					msg_stream << "Error opening  " << d_mcast_group << " on port " << d_port;
					GR_LOG_ERROR(d_logger, msg_stream.str());

					throw std::runtime_error(std::string("[SNAP Source] Multicast Error occurred: ") +
							ex.what());
				}
			}
		}

		try {
			d_udpsocket = new boost::asio::ip::udp::socket(d_io_service, d_endpoint);
		} catch (const std::exception &ex) {
			throw std::runtime_error(std::string("[SNAP Source] Error occurred: ") +
					ex.what());
		}

		if (d_use_mcast) {
			try {
				boost::asio::ip::multicast::join_group option(mcast_addr);
				d_udpsocket->set_option(option);
			} catch (const std::exception &ex) {
				throw std::runtime_error(std::string("[SNAP Source] Multicast Error occurred: ") +
						ex.what());
			}
		}

		std::stringstream msg_stream;
		if (d_use_mcast) {
			msg_stream << "Listening for data on multicast group " << d_mcast_group << " on port " << d_port << ".";
		}
		else {
			msg_stream << "Listening for data on UDP port " << d_port << ".";
		}

		GR_LOG_INFO(d_logger, msg_stream.str());

		// Just to not leave this uninitialized:
		min_pcap_queue_size = 1;
	}
	else {
		min_pcap_queue_size = (d_channel_diff / channels_per_packet) * 16;

		if (min_pcap_queue_size == 0) {
			min_pcap_queue_size = 16;
		}

		reload_size = min_pcap_queue_size * 4;
	}

	switch (d_header_type) {
	case SNAP_PACKETTYPE_VOLTAGE:
		x_vector_buffer = new char[vector_buffer_size];
		memset(x_vector_buffer,0x00,vector_buffer_size);

		if (!d_packed_output) {
			y_vector_buffer = new char[vector_buffer_size];
			memset(y_vector_buffer,0x00,vector_buffer_size);
		}
		else {
			y_vector_buffer = NULL; // Not used in this mode.
		}
		break;
	case SNAP_PACKETTYPE_SPECT:
		xx_buffer = new float[4096];
		yy_buffer = new float[4096];
		xy_real_buffer = new float[4096];
		xy_imag_buffer = new float[4096];

		break;
	}

	localBuffer = new unsigned char[total_packet_size];

	// Compute reasonable buffer size
	d_localqueue = new boost::circular_buffer<data_vector<unsigned char>>(MAX_NET_CIRC_BUFFER);
	async_buffer = new unsigned char[total_packet_size];
	d_udp_recv_buf_size = total_packet_size;

#ifdef THREAD_RECEIVE
	proc_thread = new boost::thread(boost::bind(&snap_source_impl::runThread, this));
#endif

	return true;
}

bool snap_source_impl::stop() {
	stop_thread = true;

	if (proc_thread) {
		if (d_udpsocket) {
			d_udpsocket->cancel();
			d_io_service.stop();
		}

		while (threadRunning)
			usleep(10);

		delete proc_thread;
		proc_thread = NULL;
	}

	closePCAP();

	if (d_udpsocket) {
		if (d_use_mcast) {
			boost::system::error_code ec;
			boost::asio::ip::address mcast_addr = boost::asio::ip::address::from_string(d_mcast_group, ec);
			boost::asio::ip::multicast::leave_group option(mcast_addr);
			d_udpsocket->set_option(option);
		}
		d_udpsocket->close();

		d_udpsocket = NULL;

		d_io_service.reset();
		d_io_service.stop();
	}

	if (async_buffer) {
		delete[] async_buffer;
		async_buffer = NULL;
	}

	if (localBuffer) {
		delete[] localBuffer;
		localBuffer = NULL;
	}

	if (local_net_buffer) {
		delete[] local_net_buffer;
		local_net_buffer = NULL;
		local_net_buffer_size = 0;
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

void snap_source_impl::handle_receive(const boost::system::error_code& error,
		std::size_t bytes_transferred)
{
	// std::cout << "[" << identifier() << "] handle_receive called with " << bytes_transferred << " bytes" << std::endl;
	if (!error) {
		if (!d_found_start_channel) {
			// We're not synchronized on the first packet yet, so we're looking for it.
			if (SNAP_PACKETTYPE_VOLTAGE) {
				if (!voltage_synchronize(async_buffer)) {
					// we're still not sync'd.  So don't bother queueing the packet.
					start_receive();
					return;
				}
			}
			else {
				if (!spect_synchronize(async_buffer)) {
					// we're still not sync'd.  So don't bother queueing the packet.
					start_receive();
					return;
				}
			}
		}

		// check for bad channel id first.
		uint16_t channel_id;

		if (SNAP_PACKETTYPE_VOLTAGE) {
			struct voltage_header *v_hdr = (struct voltage_header *)async_buffer;
			channel_id = be16toh(v_hdr->chan);
		}
		else {
			uint64_t *header_as_uint64 = (uint64_t *)async_buffer;

			// Convert from network format to host format.
			uint64_t header = be64toh(*header_as_uint64);
			channel_id = ((header >> 8) & 0x07) * 512; // Id cycles 0-7.  Channel is 512*val
		}

		if ((channel_id < d_starting_channel) || (channel_id > d_ending_channel_packet_channel_id) ) {
			std::stringstream msg_stream;
			msg_stream << "Received an unexpected channel index.  Skipping packet.  Received block starting channel id: " << channel_id;

			if (b_one_packet) {
				msg_stream << " expected " << d_starting_channel;
			}
			else {
				msg_stream << " expected block channel between " << d_starting_channel << " and " << d_ending_channel_packet_channel_id;
			}

			GR_LOG_ERROR(d_logger, msg_stream.str());
		}

		// We'll only get here if we've sync'd and the id is good.  so the main work doesn't need to track this anymore.
		data_vector<unsigned char> new_data((unsigned char *)async_buffer,total_packet_size);
		{
#ifdef THREAD_RECEIVE
			gr::thread::scoped_lock guard(d_net_mutex);
#endif
			d_localqueue->push_back(new_data);
		}
		// An attempt at multipacket receive while still using async_receive.  If there's a lot of data outstanding,
		// this receive will grab the rest of it.
		//queue_data();
	}
	else {
		std::stringstream msg_stream;
		if (error) {
			msg_stream << "Network receive error code:" << error;
		}
		else {
			msg_stream << "Network received an incorrect number of bytes:" << bytes_transferred << ".  Should be " << d_udp_recv_buf_size;
		}
		GR_LOG_ERROR(d_logger, msg_stream.str());
	}

	start_receive();
}

void snap_source_impl::copy_volt_data_to_vector_buffer(snap_header& hdr) {
	voltage_packet *vp;
#ifdef ZEROCOPY
	unsigned char *pData;

	{
		// If we need to switch back from zc mode, this is where
		// we'd call fill_local_buffer()

		// We only need to lock while we're grabbing the data pointer
		gr::thread::scoped_lock guard(d_net_mutex);
		pData = d_localqueue->front().data_pointer();
	}
	vp = (voltage_packet *)&pData[d_header_size];
#else
	vp = (voltage_packet *)&localBuffer[d_header_size];
#endif

	// cycle through the time entry rows in the packet. (will always be 16)

	int channel_offset_within_time_block = (hdr.channel_id - d_starting_channel) * 2;

	if (d_packed_output) {
		unsigned char *x_pol;
		for (int t=0;t<16;t++) {
			// This moves us in the packet memory to the correct time row
			int vector_start = t * d_veclen  + channel_offset_within_time_block;
			// For packed output, the output is [IQ packed 4-bit] Xn,[IQ packed 4-bit] Yn,...
			// Both go in the x_pol output.
			x_pol = (unsigned char *)&x_vector_buffer[vector_start];

			for (int sample=0;sample<256;sample++) {
				int TwoS = 2*sample;

				x_pol[TwoS] = vp->data[sample][t][0];
				// In packed mode, this is actually y to put it in a single block output
				x_pol[TwoS + 1] = vp->data[sample][t][1];
			}
		}
	}
	else {
		// Note these are char rather than unsigned char because in this unpacking
		// mode, we actually two's complement extract the signed input.
		char *x_pol;
		char *y_pol;
		for (int t=0;t<16;t++) {
			// This moves us in the packet memory to the correct time row
			int vector_start = t * d_veclen  + channel_offset_within_time_block;
			x_pol = &x_vector_buffer[vector_start];
			y_pol = &y_vector_buffer[vector_start];

			for (int sample=0;sample<256;sample++) {
				int TwoS = 2*sample;
				int TwoS1 = TwoS + 1;

				// The 2.0 format reverses the [t][sample] index position to [sample][t].
				x_pol[TwoS] = (char)(vp->data[sample][t][0] >> 4); // I
				// Need to adjust twos-complement
				x_pol[TwoS] = TwosComplementLookup4Bit(x_pol[TwoS]); // TwosComplement4Bit(x_pol[TwoS]);

				x_pol[TwoS1] = (char)(vp->data[sample][t][0] & 0x0F);  // Q
				x_pol[TwoS1] = TwosComplementLookup4Bit(x_pol[TwoS1]); // TwosComplement4Bit(x_pol[TwoS1]);

				y_pol[TwoS] = (char)(vp->data[sample][t][1] >> 4); // I
				y_pol[TwoS] = TwosComplementLookup4Bit(y_pol[TwoS]); // TwosComplement4Bit(y_pol[TwoS]);

				y_pol[TwoS1] = (char)(vp->data[sample][t][1] & 0x0F);  // Q
				y_pol[TwoS1] = TwosComplementLookup4Bit(y_pol[TwoS1]); // TwosComplement4Bit(y_pol[TwoS1]);
			} // for sample
		} // for t
	} // if d_packet_output /else

#ifdef ZEROCOPY
	{
		// We're finished with the data in the queue head.  So we can remove it.
		gr::thread::scoped_lock guard(d_net_mutex);
		d_localqueue->pop_front();
	}
#endif
}

void snap_source_impl::queue_voltage_data(snap_header& hdr) {
	for (int this_time_start=0;this_time_start<16;this_time_start++) {
		int block_start = this_time_start * d_veclen;

		data_vector<char> x_cur_vector(&x_vector_buffer[block_start],d_veclen);

#ifdef USE_CIRC_VB
		if (x_vector_queue.size() == MAX_WORK_BUFF_SIZE) {
			std::stringstream msg_stream;
			msg_stream << "The vector queue is full.  Frames are getting lost.";
			GR_LOG_ERROR(d_logger,msg_stream.str());
		}
#endif

		x_vector_queue.push_back(x_cur_vector);

		if (!d_packed_output) {
			// If we're packed output, everything is in x_pol output.
			data_vector<char> y_cur_vector(&y_vector_buffer[block_start],d_veclen);
			y_vector_queue.push_back(y_cur_vector);
		}

		if (sync_timestamp == 0)
			seq_num_queue.push_back(hdr.sample_number);
	} // this_time_start
}

int snap_source_impl::work_volt_mode(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items, bool liveWork) {
	char *x_out = (char *)output_items[0];
	char *y_out = (char *)output_items[1];

#ifndef THREAD_RECEIVE
	if (!d_use_pcap) {
		// Getting data from the network
		queue_data();
	}
	else {
		queue_pcap_data();
	}
#endif

	int num_packets_available = packets_available();
	int max_wait_counter = 0;

	// Handle case where no data is available
	while (!stop_thread && !pcap_file_done && (num_packets_available == 0) && (x_vector_queue.size() == 0) ) {
		if (d_use_pcap) {
			usleep(8);
		}
		else {
			usleep(24);
		}

		num_packets_available = packets_available();

		// Returning zero has a massive delay on overall performance.
		// But waiting indefinitely when there's no packets causes this loop to hang and not respond to sigint.
		// So a quick counter takes care of it.
		if (num_packets_available == 0) {
			if (max_wait_counter++ > 120000)
				return 0;
		}
	}

	snap_header hdr;

	if (d_send_sync_pmt) {
		// we just synchronized.
		d_send_sync_pmt = false;

		if (liveWork && d_send_start_msg) {
			pmt::pmt_t meta = pmt::make_dict();

			meta = pmt::dict_add(meta, pmt::mp("antenna_id"), pmt::mp(async_volt_sync_hdr.antenna_id));
			meta = pmt::dict_add(meta, pmt::mp("starting_channel"), pmt::mp(async_volt_sync_hdr.channel_id));
			meta = pmt::dict_add(meta, pmt::mp("sample_number"), pmt::mp(async_volt_sync_hdr.sample_number));
			meta = pmt::dict_add(meta, pmt::mp("firmware_version"), pmt::mp(async_volt_sync_hdr.firmware_version));

			pmt::pmt_t pdu = pmt::cons(meta, pmt::PMT_NIL);
			message_port_pub(pmt::mp("sync_header"), pdu);
		}
	}

	// If we're here, async receive has synchronized and we have data to process.
	d_partialFrameCounter = 0;

	// Let's extract the vectors.  The total data is in a 16 time sample by 256 channel by 2 polarization
	// packet.  Multiple packets will be required to make up a whole time set, so we need to buffer
	// the chunks to a local temp buffer for each of the x and y data sets.  Once we have a complete set,
	// we can queue each of the complete time entries to a queue for output consumption.
	int skippedPackets = 0;

	while ((num_packets_available > 0) && (x_vector_queue.size() < noutput_items)) {
		fill_local_buffer();
		get_voltage_header(hdr);

		if (b_one_packet || ((d_last_timestamp > 0) && (hdr.sample_number != d_last_timestamp)) ) {
			// If we're in this code block, we have a next frame
			if (!b_one_packet) {
				// If we're not in 1-packet mode, queue whatever data we already had first since a new
				// timestamp means we're out of the previous frame, then check for missing frames.
				queue_voltage_data(hdr);

				// See if we missed any packets in the last set.
				// This calc only applies to multi-packet frames as in 1-packet mode a missed timestamp implies the missed packets
				skippedPackets += packets_per_frame - multipacket_frame_pkt_ctr;
			}

			// Check for missing frames.  On the first pass with only one packet, d_last_timestamp will be zero.
			if ( (d_last_timestamp > 0) && (hdr.sample_number > d_last_timestamp) ) {
				// check for missed frames.  We check > d_last_timestamp to assume on rollovers we didn't miss a frame (for now.  Should calc)
				uint64_t missed_sets = (hdr.sample_number - d_last_timestamp) / 16 - 1;

				// missed_sets will be zero when we haven't missed a frame
				if (missed_sets > 0) {
					skippedPackets += missed_sets * packets_per_frame;

					if  (missed_sets <= MAX_MISSED_SETS) {
						for (uint64_t missed_timestamp=d_last_timestamp+16;missed_timestamp<hdr.sample_number;missed_timestamp+=16) {
							// This constructor syntax initializes a vector of d_veclen size, but zero'd out data.
							// Gotta push back 16 time entries for each missing timestamp.
							for (int i=0;i<16;i++) {
								data_vector<char> x_cur_vector(d_veclen);
								x_vector_queue.push_back(x_cur_vector);
								if (!d_packed_output) {
									// If we're packed output, everything is in x_pol output.
									data_vector<char> y_cur_vector(d_veclen);
									y_vector_queue.push_back(y_cur_vector);
								}

								if (sync_timestamp == 0)
									seq_num_queue.push_back(missed_timestamp);
							}
						}
					}
					else {
						GR_LOG_WARN(d_logger,"Missed frames exceeded max missed sets.  Some data has been dropped.");
					}
				} // if missed_sets >0
			} // d_last_timestamp > 0 and sample_number > d_last_timestamp

			if (b_one_packet) {
				// If we're in 1-packet mode, we check for missing first, then queue what we just got
				// since each packet is a frame and is atomic.
				copy_volt_data_to_vector_buffer(hdr);
				queue_voltage_data(hdr);
			}
			else {
				multipacket_frame_pkt_ctr = 1;
				// We're starting a new multi-part vector, so zero out what we have and set that we have one packet
				memset(x_vector_buffer,0x00,vector_buffer_size);
				if (!d_packed_output)
					memset(y_vector_buffer,0x00,vector_buffer_size);

				// Then copy in what we just got to start the new frame.
				copy_volt_data_to_vector_buffer(hdr);
			}

			// make sure we change the last timestamp to our current timestamp for the next pass.
			d_last_timestamp = hdr.sample_number;
		} // if (b_one_packet || ((d_last_timestamp > 0) && (hdr.sample_number  != d_last_timestamp)) )
		else {
			// We'll only be here if we're not in b_one_packet mode and our timestamp hasn't changed

			if (d_last_timestamp == 0) {
				// This is our very first packet in a multipacket set.
				d_last_timestamp = hdr.sample_number;
				// multipacket_frame_pkt_ctr is initialized to 0 in the constructor
			}
			// In the middle of a multi-packet frame, so we're just filling it.
			copy_volt_data_to_vector_buffer(hdr);
			multipacket_frame_pkt_ctr++;
		}

		// copy_volt_data_to_vector_buffer() actually removes the packet from the ring buffer.
		num_packets_available--;
	} // while packets and x_vector < noutput_items

	int items_returned;
	// Move queue items to output items as needed
	// both queues will be the same size, so can just pick one.
	if (x_vector_queue.size() >= noutput_items) {
		items_returned = noutput_items;
	}
	else {
		items_returned = x_vector_queue.size();
	}

	// This is where data actually gets moved to output_items
	for (int i=0;i<items_returned;i++) {
		// This needs to come from the new queue
		int out_index = d_veclen*i;

		// Now move to work output vector.
		memcpy(&x_out[out_index],x_vector_queue.front().data_pointer(),d_veclen);
		x_vector_queue.pop_front();
		if (!d_packed_output) {
			memcpy(&y_out[out_index],y_vector_queue.front().data_pointer(),d_veclen);
			y_vector_queue.pop_front();
		}

		// We'll only send tags if we haven't received a sync handshake
		if (sync_timestamp == 0) {
			// Add sequence number start tag for down-stream coherence
			// Since each packet set contains 16 time samples for the same packet sequence number,
			// You'll see output vectors in blocks of 16 with the same sequence number.
			// This is expected.

			if (liveWork) {
				uint64_t vector_seq_num = seq_num_queue.front();
				seq_num_queue.pop_front();

				pmt::pmt_t pmt_sequence_number =pmt::from_uint64(vector_seq_num);

				add_item_tag(0, nitems_written(0) + i, d_pmt_seqnum, pmt_sequence_number,d_block_name);
				if (!d_packed_output) {
					add_item_tag(1, nitems_written(0) + i, d_pmt_seqnum, pmt_sequence_number,d_block_name);
				}
			}
			else {
				seq_num_queue.pop_front();
			}
		}
	}

	// Notify on skipped packets
	NotifyMissed(skippedPackets);

	return items_returned;
}

int snap_source_impl::work_spec_mode(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items, bool liveWork) {
	static bool firstTime = true;

	float *xx_out = (float *)output_items[0];
	float *yy_out = (float *)output_items[1];
	float *xy_real_out = (float *)output_items[2];
	float *xy_imag_out = (float *)output_items[3];
	int items_returned = noutput_items;

#ifndef THREAD_RECEIVE
	if (!d_use_pcap) {
		// Getting data from the network
		queue_data();
	}
	else {
		queue_pcap_data();
	}
#endif

	int num_packets_available = packets_available();

	// Handle case where no data is available
	while (!stop_thread && !pcap_file_done && (num_packets_available == 0) && (x_vector_queue.size() == 0) ) {
		if (d_use_pcap) {
			usleep(8);
		}
		else {
			usleep(24);
		}

		num_packets_available = packets_available();
	}

	snap_header hdr;

	if (d_send_sync_pmt) {
		// we just synchronized.
		d_send_sync_pmt = false;

		// memcpy((void *)&hdr,(void *)&async_volt_sync_hdr,sizeof(snap_header));

		if (liveWork) {
			pmt::pmt_t meta = pmt::make_dict();

			meta = pmt::dict_add(meta, pmt::mp("antenna_id"), pmt::mp(async_volt_sync_hdr.antenna_id));
			meta = pmt::dict_add(meta, pmt::mp("starting_channel"), pmt::mp(async_volt_sync_hdr.channel_id));
			meta = pmt::dict_add(meta, pmt::mp("sample_number"), pmt::mp(async_volt_sync_hdr.sample_number));
			meta = pmt::dict_add(meta, pmt::mp("firmware_version"), pmt::mp(async_volt_sync_hdr.firmware_version));

			pmt::pmt_t pdu = pmt::cons(meta, pmt::PMT_NIL);
			message_port_pub(pmt::mp("sync_header"), pdu);
		}
	}

	d_partialFrameCounter = 0;

	// Now if we're here we should have at least 1 block.

	// Let's extract the vectors.  The total data is in a 16 time sample by 256 channel by 2 polarization
	// packet.  Multiple packets will be required to make up a whole time set, so we need to buffer
	// the chunks to a local temp buffer for each of the x and y data sets.  Once we have a complete set,
	// we can queue each of the complete time entries to a queue for output consumption.
	int skippedPackets = 0;

	// Queue all the data we have into our local queue
	int snapshot_packets_available = packets_available();

	while ((snapshot_packets_available > 0) && (xx_vector_queue.size() < noutput_items)) {
		fill_local_buffer();
		get_spectrometer_header(hdr);
		snapshot_packets_available--;

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
		uint64_t vector_seq_num = seq_num_queue.front();

		// Now move to work output vector.
		memcpy(&xx_out[d_veclen*i],xx_vector_queue.front().data_pointer(),vector_buffer_size);
		memcpy(&yy_out[d_veclen*i],yy_vector_queue.front().data_pointer(),vector_buffer_size);

		// output the xy's if the ports are connected.
		if (output_items.size() > 2) {
			// xy is tagged as an optional output.
			memcpy(&xy_real_out[d_veclen*i],xy_real_vector_queue.front().data_pointer(),vector_buffer_size);

			if (output_items.size() > 3) {
				// xy is tagged as an optional output.
				memcpy(&xy_imag_out[d_veclen*i],xy_imag_vector_queue.front().data_pointer(),vector_buffer_size);
			}
		}

		xx_vector_queue.pop_front();
		yy_vector_queue.pop_front();
		xy_real_vector_queue.pop_front();
		xy_imag_vector_queue.pop_front();
		seq_num_queue.pop_front();

		// Add sequence number start tag for down-stream coherence
		// Since each packet set contains 16 time samples for the same packet sequence number,
		// You'll see output vectors in blocks of 16 with the same sequence number.
		// This is expected.

		if (liveWork && (sync_timestamp > 0)) {
			pmt::pmt_t pmt_sequence_number =pmt::from_uint64(vector_seq_num);

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
	work_called = true;

	gr::thread::scoped_lock guard(d_setlock);

	if (d_use_pcap && pcap_file_done) {
		if (packets_available() == 0) {
			GR_LOG_INFO(d_logger,"End of PCAP file reached.");

			return WORK_DONE;
		}
	}

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
		int num_packets = bytesAvailable / total_packet_size;

		if (num_packets > 0) {
			long bytes_to_read = num_packets * total_packet_size; // This makes sure we just do full packet blocks

			// This basically does what prepare does.
			if (!local_net_buffer) {
				local_net_buffer = new unsigned char[bytes_to_read];
				local_net_buffer_size = bytes_to_read;
			}
			else {
				if (bytes_to_read > local_net_buffer_size) {
					// Just limit unrestricted growth
					if (bytes_to_read > (10000 * total_packet_size)) {
						bytes_to_read = 10000 * total_packet_size;
					}

					delete[] local_net_buffer;
					local_net_buffer = new unsigned char[bytes_to_read];
					local_net_buffer_size = bytes_to_read;
				}
			}

			size_t bytesRead = d_udpsocket->receive_from(boost::asio::buffer(local_net_buffer,bytes_to_read), d_endpoint);
			if (bytesRead > 0) {
				// Get the data and add it to our local queue.  We have to maintain a
				// local queue in case we read more bytes than noutput_items is asking
				// for.  In that case we'll only return noutput_items bytes
				{
					for (long i = 0; i < num_packets; i++) {
						unsigned char *pData = &local_net_buffer[i*total_packet_size];

						if (SNAP_PACKETTYPE_VOLTAGE) {
							if (!d_found_start_channel) {
								// We're not synchronized on the first packet yet, so we're looking for it.
								if (!voltage_synchronize(pData)) {
									// we're still not sync'd.  So don't bother queueing the packet.
									continue;
								}
							}

							struct voltage_header *v_hdr = (struct voltage_header *)pData;
							uint16_t channel_id = be16toh(v_hdr->chan);
							if ((channel_id < d_starting_channel) || (channel_id > d_ending_channel_packet_channel_id) ) {
								std::stringstream msg_stream;
								msg_stream << "Received an unexpected channel index.  Skipping packet.  Received block starting channel id: " << channel_id;

								if (b_one_packet) {
									msg_stream << " expected " << d_starting_channel;
								}
								else {
									msg_stream << " expected block channel between " << d_starting_channel << " and " << d_ending_channel_packet_channel_id;
								}

								GR_LOG_ERROR(d_logger, msg_stream.str());

								continue;
							}
						}
						else {
							if (!d_found_start_channel) {
								// We're not synchronized on the first packet yet, so we're looking for it.
								if (!spect_synchronize(pData)) {
									// we're still not sync'd.  So don't bother queueing the packet.
									continue;
								}
							}

							uint64_t *header_as_uint64 = (uint64_t *)pData;

							// Convert from network format to host format.
							uint64_t header = be64toh(*header_as_uint64);
							uint16_t channel_id = ((header >> 8) & 0x07) * 512; // Id cycles 0-7.  Channel is 512*val

							if ((channel_id < d_starting_channel) || (channel_id > d_ending_channel_packet_channel_id) ) {
								std::stringstream msg_stream;
								msg_stream << "Received an unexpected channel index.  Skipping packet.  Received block starting channel id: " << channel_id;

								if (b_one_packet) {
									msg_stream << " expected " << d_starting_channel;
								}
								else {
									msg_stream << " expected block channel between " << d_starting_channel << " and " << d_ending_channel_packet_channel_id;
								}

								GR_LOG_ERROR(d_logger, msg_stream.str());

								continue;
							}
						}

						data_vector<unsigned char> new_data((unsigned char *)pData,total_packet_size);

						{

#ifdef THREAD_RECEIVE
							gr::thread::scoped_lock guard(d_net_mutex);
#endif
							d_localqueue->push_back(new_data);
						}
					}
				}
			}
		}
	}
}

void snap_source_impl::queue_pcap_data() {

	// Lets try to keep 16 packets queued up at a time.
	long queue_size = packets_available();

	// while (!pcap_file_done && !stop_thread && (queue_size < min_pcap_queue_size) ) {
	if (queue_size < min_pcap_queue_size) {
		long queue_diff = min_pcap_queue_size - queue_size;
		long matchingPackets = 0;

		static int sizeUDPHeader = sizeof(struct udphdr);
		const u_char *p;
		int etherIPHeaderSize;
		uint16_t destPort;
		size_t len;
		unsigned char *pData;

		while ( (matchingPackets < reload_size) && (p = pcap_next(pcapFile, &pcap_header)) && !stop_thread ) {
			if (pcap_header.len != pcap_header.caplen) {
				continue;
			}
			auto eth = reinterpret_cast<const ether_header *>(p);

			// jump over and ignore vlan tag
			if (ntohs(eth->ether_type) == ETHERTYPE_VLAN) {
				p += 4;
				eth = reinterpret_cast<const ether_header *>(p);
			}
			if (ntohs(eth->ether_type) != ETHERTYPE_IP) {
				continue;
			}
			auto ip = reinterpret_cast<const iphdr *>(p + sizeof(ether_header));
			if (ip->version != 4) {
				continue;
			}

			if (ip->protocol != IPPROTO_UDP) {
				continue;
			}

			// IP Header length is defined in a packet field (IHL).  IHL represents
			// the # of 32-bit words So header size is ihl * 4 [bytes]
			etherIPHeaderSize = sizeof(ether_header) + ip->ihl * 4;

			auto udp = reinterpret_cast<const udphdr *>(p + etherIPHeaderSize);

			destPort = ntohs(udp->dest);
			len = ntohs(udp->len) - sizeUDPHeader;

			if ((destPort == d_port) && (len == total_packet_size)) {
				matchingPackets++;

				pData = (u_char *)&p[etherIPHeaderSize + sizeUDPHeader];

				if (SNAP_PACKETTYPE_VOLTAGE) {
					if (!d_found_start_channel) {
						// We're not synchronized on the first packet yet, so we're looking for it.
						if (!voltage_synchronize(pData)) {
							// we're still not sync'd.  So don't bother queueing the packet.
							continue;
						}
					}

					struct voltage_header *v_hdr = (struct voltage_header *)pData;
					uint16_t channel_id = be16toh(v_hdr->chan);
					if ((channel_id < d_starting_channel) || (channel_id > d_ending_channel_packet_channel_id) ) {
						std::stringstream msg_stream;
						msg_stream << "Received an unexpected channel index.  Skipping packet.  Received block starting channel id: " << channel_id;

						if (b_one_packet) {
							msg_stream << " expected " << d_starting_channel;
						}
						else {
							msg_stream << " expected block channel between " << d_starting_channel << " and " << d_ending_channel_packet_channel_id;
						}

						GR_LOG_ERROR(d_logger, msg_stream.str());

						continue;
					}
				}
				else {
					if (!d_found_start_channel) {
						// We're not synchronized on the first packet yet, so we're looking for it.
						if (!spect_synchronize(pData)) {
							// we're still not sync'd.  So don't bother queueing the packet.
							continue;
						}
					}

					uint64_t *header_as_uint64 = (uint64_t *)pData;

					// Convert from network format to host format.
					uint64_t header = be64toh(*header_as_uint64);
					uint16_t channel_id = ((header >> 8) & 0x07) * 512; // Id cycles 0-7.  Channel is 512*val

					if ((channel_id < d_starting_channel) || (channel_id > d_ending_channel_packet_channel_id) ) {
						std::stringstream msg_stream;
						msg_stream << "Received an unexpected channel index.  Skipping packet.  Received block starting channel id: " << channel_id;

						if (b_one_packet) {
							msg_stream << " expected " << d_starting_channel;
						}
						else {
							msg_stream << " expected block channel between " << d_starting_channel << " and " << d_ending_channel_packet_channel_id;
						}

						GR_LOG_ERROR(d_logger, msg_stream.str());

						continue;
					}
				}

				data_vector<unsigned char> new_data(pData,len);

				{
					gr::thread::scoped_lock guard(d_net_mutex);
					d_localqueue->push_back(new_data);
				}
			} // if ports match
		} // while read

		// if ((!p) && (matchingPackets < queue_diff)) {
		if (!p) {
			// We've reached the end of the file.  restart it if necessary.
			if (d_repeat_file) {
				closePCAP();
				openPCAP();
			}
			else {
				pcap_file_done = true;
			}
		}
		// Refresh the queue size counter (work() will be consuming while we're filling here)
		// queue_size = packets_available();
	} // queue_size < min_queue_size
}

int snap_source_impl::mmsg_receive()
{
	int retval = recvmmsg(d_udpsocket->native_handle(), msgs, MMSG_LENGTH, MSG_DONTWAIT, nullptr);
	if (retval == -1) {
		//GR_LOG_ERROR(d_logger,"ERROR receiving data from recvmmsg (-1)");
		return 0;
	}

	// check for bad channel id first.
	uint16_t channel_id;
	unsigned char *cur_pkt;

	/*
	if (retval > 1) {
		printf("%d messages received\n", retval);
	}
	*/
	gr::thread::scoped_lock guard(d_net_mutex);

	for (int i = 0; i < retval; i++) {
		cur_pkt = bufs[i];

		if (!d_found_start_channel) {
			// We're not synchronized on the first packet yet, so we're looking for it.
			if (SNAP_PACKETTYPE_VOLTAGE) {
				if (!voltage_synchronize(cur_pkt)) {
					// we're still not sync'd.  So don't bother queueing the packet.
					continue;
				}
			}
			else {
				if (!spect_synchronize(cur_pkt)) {
					// we're still not sync'd.  So don't bother queueing the packet.
					continue;
				}
			}
		}

		if (SNAP_PACKETTYPE_VOLTAGE) {
			struct voltage_header *v_hdr = (struct voltage_header *)cur_pkt;
			channel_id = be16toh(v_hdr->chan);
		}
		else {
			uint64_t *header_as_uint64 = (uint64_t *)cur_pkt;

			// Convert from network format to host format.
			uint64_t header = be64toh(*header_as_uint64);
			channel_id = ((header >> 8) & 0x07) * 512; // Id cycles 0-7.  Channel is 512*val
		}

		if ((channel_id < d_starting_channel) || (channel_id > d_ending_channel_packet_channel_id) ) {
			std::stringstream msg_stream;
			msg_stream << "Received an unexpected channel index.  Skipping packet.  Received block starting channel id: " << channel_id;

			if (b_one_packet) {
				msg_stream << " expected " << d_starting_channel;
			}
			else {
				msg_stream << " expected block channel between " << d_starting_channel << " and " << d_ending_channel_packet_channel_id;
			}

			GR_LOG_ERROR(d_logger, msg_stream.str());
		}

		// We'll only get here if we've sync'd and the id is good.  so the main work doesn't need to track this anymore.
		data_vector<unsigned char> new_data((unsigned char *)cur_pkt,total_packet_size);
		d_localqueue->push_back(new_data);
	}

	return retval;
}


void snap_source_impl::runThread() {
	threadRunning = true;

	if (!d_use_pcap) {
		// data dump until work starts.
		//bool printed_msg = false;

		while (!work_called && !stop_thread) {
			/*
			if (!printed_msg) {
				start_time = std::chrono::steady_clock::now();
				std::cout << "Waiting on work..." << std::endl;
				printed_msg = true;
			}
			*/
			int retval = recvmmsg(d_udpsocket->native_handle(), msgs, MMSG_LENGTH, MSG_DONTWAIT, nullptr);
			sleep(4);
		}

		/*
		end_time = std::chrono::steady_clock::now();
		std::cout << "Work called" << std::endl;
		std::chrono::duration<double> elapsed_seconds = end_time - start_time;
		std::cout << "Time between start and work: " << elapsed_seconds.count() << std::endl;
		*/
	}

	while (!stop_thread) {
		if (!d_use_pcap) {
			// Getting data from the network
			//queue_data();
			// so each packet is 16 time samples at 4 microseconds each.  So a full packet will be
			// once every 64 microseconds, and we can handle large blocks of packets at a time.
			// So this just gets some worker thread relief.
			// usleep(60);
			if (!d_use_pcap) {
				int pkts_received = mmsg_receive();
				if (pkts_received == 0) {
					usleep(mmsg_sleep_time);
				}
				else {
					// wait 1/2 packet
					usleep(16);
				}

				/*
				start_receive();
				d_io_service.run();
				 */
			}

		}
		else {
			queue_pcap_data();
			usleep(8);
		}
	}

	threadRunning = false;
}

} /* namespace ata */

} /* namespace gr */

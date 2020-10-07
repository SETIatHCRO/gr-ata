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

namespace gr {
namespace ata {

snap_source::sptr snap_source::make(int port,
		int headerType,
		bool notifyMissed,
		bool sourceZeros, bool ipv6,
		int starting_channel, int ending_channel) {
	return gnuradio::get_initial_sptr(
			new snap_source_impl(port, headerType,
					notifyMissed, sourceZeros, ipv6, starting_channel, ending_channel));
}

/*
 * The private constructor
 */
 snap_source_impl::snap_source_impl(int port,
		 int headerType,
		 bool notifyMissed,
		 bool sourceZeros, bool ipv6,
		 int starting_channel, int ending_channel)
 : gr::sync_block("snap_source",
		 gr::io_signature::make(0, 0, 0),
		 gr::io_signature::make(2, 2, sizeof(char) * (ending_channel-starting_channel+1)*2)) {

	 is_ipv6 = ipv6;

	 d_port = port;
	 d_last_channel_block = 0;
	 d_notifyMissed = notifyMissed;
	 d_sourceZeros = sourceZeros;
	 d_partialFrameCounter = 0;

	 d_starting_channel = starting_channel;
	 d_ending_channel = ending_channel;
	 d_ending_channel_packet_channel_id = ending_channel - 255;

	 d_channel_diff = d_ending_channel - d_starting_channel + 1;

	 int channel_check = d_channel_diff % 256;

	 if (channel_check > 0) {
		 std::stringstream msg_stream;
		 msg_stream << "Channels must represent a 256 boundary (end_channel - start_channel + 1) % 256 must be zero.";
		 GR_LOG_ERROR(d_logger, msg_stream.str());
		 exit(2);
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

	 d_found_start_channel = false;

	 d_header_type = headerType;

	 d_pmt_channel = pmt::string_to_symbol("channel");

	 // Configure packet parser
	 d_header_size = 0;
	 switch (d_header_type) {
	 case SNAP_PACKETTYPE_VOLTAGE:
		 d_header_size = 8;
		 d_payloadsize = 8192; // 256 * 16 * 2; // channels * time steps * pols * sizeof(sc4)
		 total_packet_size = 8200;
		 break;

	 case SNAP_PACKETTYPE_SPECT:
		 d_header_size = 8;
		 total_packet_size = 8200;
		 d_payloadsize = 512 * 4 * 4; // channels * pols * sizeof(uint32_t)
		 break;

	 default:
		 GR_LOG_ERROR(d_logger, "Unknown source data format.");
		 exit(1);
		 break;
	 }

	 single_polarization_bytes = d_payloadsize/2;

	 if (d_payloadsize < 8) {
		 GR_LOG_ERROR(d_logger,
				 "Payload size is too small.  Must be at "
				 "least 8 bytes once header/trailer adjustments are made.");
		 exit(1);
	 }

	 localBuffer = new char[total_packet_size];
	 long maxCircBuffer;

	 // Compute reasonable buffer size
	 maxCircBuffer = total_packet_size * 1500; // 12.3 MiB
	 d_localqueue = new boost::circular_buffer<char>(maxCircBuffer);

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
	 gr::block::set_output_multiple(16);
 }

 /*
  * Our destructor.
  */
 snap_source_impl::~snap_source_impl() { stop(); }

 bool snap_source_impl::stop() {
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

	 if (d_localqueue) {
		 delete d_localqueue;
		 d_localqueue = NULL;
	 }
	 return true;
 }

 int snap_source_impl::work(int noutput_items,
		 gr_vector_const_void_star &input_items,
		 gr_vector_void_star &output_items) {
	 gr::thread::scoped_lock guard(d_setlock);

	 static bool firstTime = true;
	 static int underRunCounter = 0;

	 char *x_out = (char *)output_items[0];
	 char *y_out = (char *)output_items[1];

	 int bytesAvailable = netdata_available();
	 unsigned int numRequested = noutput_items * d_veclen;

	 // Handle case where no data is available
	 if ((bytesAvailable == 0) && (d_localqueue->size() == 0)) {
		 underRunCounter++;
		 d_partialFrameCounter = 0;

		 if (d_sourceZeros) {
			 // Just return 0's
			 memset((void *)x_out, 0x00, numRequested);
			 memset((void *)y_out, 0x00, numRequested);
			 return noutput_items;

		 } else {
			 if (underRunCounter == 0) {
				 if (!firstTime) {
					 std::cout << "nU";
				 } else
					 firstTime = false;
			 } else {
				 if (underRunCounter > 100)
					 underRunCounter = 0;
			 }

			 // Returning 0 causes GNU Radio to call work again in 0.1s
			 return 0;
		 }
	 }

	 int bytesRead;
	 int localNumItems;

	 // we could get here even if no data was received but there's still data in
	 // the queue. however we read blocks so we want to make sure we have data before
	 // we call it.
	 if (bytesAvailable > 0) {
		 boost::asio::streambuf::mutable_buffers_type buf =
				 d_read_buffer.prepare(bytesAvailable);
		 // http://stackoverflow.com/questions/28929699/boostasio-read-n-bytes-from-socket-to-streambuf
		 bytesRead = d_udpsocket->receive_from(buf, d_endpoint);

		 if (bytesRead > 0) {
			 d_read_buffer.commit(bytesRead);

			 // Get the data and add it to our local queue.  We have to maintain a
			 // local queue in case we read more bytes than noutput_items is asking
			 // for.  In that case we'll only return noutput_items bytes
			 const char *readData =
					 boost::asio::buffer_cast<const char *>(d_read_buffer.data());
			 for (int i = 0; i < bytesRead; i++) {
				 d_localqueue->push_back(readData[i]);
			 }
			 d_read_buffer.consume(bytesRead);
		 }
	 }

	 // Handle partial packets
	 if (d_localqueue->size() < total_packet_size) {
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
		 while ( (!d_found_start_channel) && (d_localqueue->size() >= total_packet_size)) {
			 for (int curByte = 0; curByte < d_header_size; curByte++) {
				 localBuffer[curByte] = d_localqueue->at(curByte);
			 }

			 if (d_header_type ==SNAP_PACKETTYPE_VOLTAGE) {
				 get_voltage_header(hdr);
			 }
			 else {
				 get_spectrometer_header(hdr);
			 }

			 if (hdr.channel_id == d_starting_channel) {
				 // We found our start channel packet.
				 // Set that we're synchronized and exit our loop here.
				 d_found_start_channel = true;
				 std::stringstream msg_stream;
				 msg_stream << "Found start channel packet.";
				 GR_LOG_INFO(d_logger, msg_stream.str());
				 break;
			 }
			 else {
				 // We found a packet that wasn't the first one of the vector.
				 // Just dump it till we're synchronized.
				 for (int curByte=0;curByte < total_packet_size;curByte++) {
					 d_localqueue->pop_front();
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
	 while (d_localqueue->size() >= total_packet_size) {
		 for (int curByte = 0; curByte < total_packet_size; curByte++) {
			 localBuffer[curByte] = d_localqueue->front();;
			 d_localqueue->pop_front();
		 }

		 if (d_header_type ==SNAP_PACKETTYPE_VOLTAGE) {
			 get_voltage_header(hdr);
		 }
		 else {
			 get_spectrometer_header(hdr);
		 }

		 // check for bad channel id first.
		 if ((hdr.channel_id < d_starting_channel) || (hdr.channel_id > d_ending_channel_packet_channel_id) ) {
			 std::cout << "ERROR: Received an unexpected channel index.  Skipping packet.  Received channel id: " << hdr.channel_id << std::endl;
			 continue;
		 }

		 if (hdr.channel_id == d_starting_channel) {
			 // We're starting a new vector, so zero out what we have.
			 memset(x_vector_buffer,0x00,vector_buffer_size);
			 memset(y_vector_buffer,0x00,vector_buffer_size);
		 }

		 // Check if we skipped packets by missing a channel block.
		 // First cycle through last will be zero and we'lll be synchronized on the start channel.
		 if (d_last_channel_block > 0) {
			 if (hdr.channel_id > d_last_channel_block) {
				 int delta = (hdr.channel_id - d_last_channel_block) / 256;

				 // Delta should be 1.  So anything more than 1 is a skipped packet.
				 skippedPackets += delta - 1;
			 }
			 else {
				 // channel id < last block so we wrapped around.
				 int dist_to_end = (d_ending_channel_packet_channel_id - d_last_channel_block)/256;
				 int dist_from_start = (hdr.channel_id - d_starting_channel)/256;

				 skippedPackets += dist_to_end + dist_from_start;
			 }
		 }

		 // Store what we saw as the "last" packet we received.
		 d_last_channel_block = hdr.channel_id;

		 char *pData;  // Pointer to our UDP payload after the header.
		 // Move to the beginning of our packet data section
		 pData = &localBuffer[d_header_size];

		 // cycle through the time entry rows in the packet. (will always be 16)

		 int block_channel_offset = (hdr.channel_id - d_starting_channel + 1) * 2;

		 // x polarization
		 for (int row=0;row<16;row++) {
			 // This moves us in the packet memory to the correct time row
			 int block_start = row * d_veclen;
			 char *x_pol;
			 x_pol = &x_vector_buffer[block_start + block_channel_offset];

			 for (int sample=0;sample<256;sample++) {
				 *x_pol++ = (int8_t)((uint8_t)(*pData) & 0xF0)/16; // I
				 *x_pol++ = (int8_t)((uint8_t)(*pData++) << 4)/16;  // Q
			 }
		 }

		 // Now process y polarization
		 for (int row=0;row<16;row++) {
			 // This moves us in the packet memory to the correct time row
			 int block_start = row * d_veclen;
			 char *y_pol;
			 y_pol = &y_vector_buffer[block_start + block_channel_offset];

			 for (int sample=0;sample<256;sample++) {
				 *y_pol++ = (int8_t)((uint8_t)(*pData) & 0xF0)/16; // I
				 *y_pol++ = (int8_t)((uint8_t)(*pData++) << 4)/16;  // Q
			 }
		 }

		 // Now check if we've completed a set.  If so, let's queue it up
		 // for output consumption.
		 if (hdr.channel_id == d_ending_channel_packet_channel_id) {
			 // Queue up our vectors.  Again always 16 discrete time entries.
			 for (int row=0;row<16;row++) {
				 data_vector x_cur_vector;
				 data_vector y_cur_vector;

				 int block_start = row * d_veclen;

				 x_cur_vector.store(&x_vector_buffer[block_start],d_veclen);
				 y_cur_vector.store(&y_vector_buffer[block_start],d_veclen);

				 x_vector_queue.push_back(x_cur_vector);
				 y_vector_queue.push_back(y_cur_vector);
			 }
		 }
	 }

	 // To Do:
	 // Move queue to output items
	 int items_returned = noutput_items;

	 // both queues will be the same size, so can just pick one.
	 if (x_vector_queue.size() < noutput_items) {
		 items_returned = x_vector_queue.size();
	 }

	 for (int i=0;i<items_returned;i++) {
		 // This needs to come from the new queue
		 data_vector x_cur_vector = x_vector_queue.front();
		 x_vector_queue.pop_front();
		 data_vector y_cur_vector = y_vector_queue.front();
		 y_vector_queue.pop_front();

		 // Now move to work output vector.
		 memcpy(&x_out[d_veclen*i],x_cur_vector.data_pointer(),d_veclen);
		 memcpy(&y_out[d_veclen*i],y_cur_vector.data_pointer(),d_veclen);

		 /*
		 // Add channel start tag for good measure
		 pmt::pmt_t pmt_channel_number =pmt::from_long((long)hdr.channel_id);

		 add_item_tag(0, nitems_written(0) + i, d_pmt_channel, pmt_channel_number);
		 add_item_tag(1, nitems_written(0) + i, d_pmt_channel, pmt_channel_number);
		 */
	 }

	 // Notify on skipped packets
	 if (skippedPackets > 0 && d_notifyMissed) {
		 std::stringstream msg_stream;
		 msg_stream << "[UDP source:" << d_port
				 << "] missed  packets: " << skippedPackets;
		 GR_LOG_WARN(d_logger, msg_stream.str());
	 }

	 return items_returned;
 }
} /* namespace grnet */
} /* namespace gr */

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
                                  bool sourceZeros, bool ipv6) {
  return gnuradio::get_initial_sptr(
      new snap_source_impl(port, headerType,
                          notifyMissed, sourceZeros, ipv6));
}

/*
 * The private constructor
 */
snap_source_impl::snap_source_impl(int port,
                                 int headerType,
                                 bool notifyMissed,
                                 bool sourceZeros, bool ipv6)
    : gr::sync_block("snap_source",
                     gr::io_signature::make(0, 0, 0),
                     gr::io_signature::make(2, 2, sizeof(char) * 512)) {

  is_ipv6 = ipv6;

  d_port = port;
  d_seq_num = 0;
  d_notifyMissed = notifyMissed;
  d_sourceZeros = sourceZeros;
  d_partialFrameCounter = 0;
  
  d_header_type = headerType;

  d_pmt_channel = pmt::string_to_symbol("channel");

  // Configure packet parser
  d_header_size = 0;
  switch (d_header_type) {
  case SNAP_PACKETTYPE_VOLTAGE:
	d_itemsize = 1;  // Output will be bytes
	d_veclen = 512;

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

  int out_multiple = 16;

  gr::block::set_output_multiple(out_multiple);
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

  if (d_localqueue) {
    delete d_localqueue;
    d_localqueue = NULL;
  }
  return true;
}

size_t snap_source_impl::data_available() {
  return (netdata_available() + d_localqueue->size());
}

size_t snap_source_impl::netdata_available() {
  // Get amount of data available
  boost::asio::socket_base::bytes_readable command(true);
  d_udpsocket->io_control(command);
  size_t bytes_readable = command.get();

  return bytes_readable;
}

void snap_source_impl::get_voltage_header(snap_voltage_header& hdr) {
	  uint64_t *header_as_uint64;
	  header_as_uint64 = (uint64_t *)localBuffer;

	  // Convert from network format to host format.
	  uint64_t header = be64toh(*header_as_uint64);

	  hdr.antenna_id = header & 0x3f;
	  hdr.channel_id = (header >> 6) & 0x0fff;
	  hdr.sample_number = (header >> 18) & 0x3fffffffffULL;
	  hdr.firmware_version = (header >> 56) & 0xff;
}

int snap_source_impl::work(int noutput_items,
                          gr_vector_const_void_star &input_items,
                          gr_vector_void_star &output_items) {
  gr::thread::scoped_lock guard(d_setlock);

  static bool firstTime = true;
  static int underRunCounter = 0;

  char *x_pol = (char *)output_items[0];
  char *y_pol = (char *)output_items[1];
  
  int bytesAvailable = netdata_available();
  unsigned int numRequested = noutput_items * d_veclen;

  // Handle case where no data is available
  if ((bytesAvailable == 0) && (d_localqueue->size() == 0)) {
    underRunCounter++;
    d_partialFrameCounter = 0;

    if (d_sourceZeros) {
      // Just return 0's
      memset((void *)x_pol, 0x00, numRequested);
      memset((void *)y_pol, 0x00, numRequested);
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

  // If we're here, it's not a partial hanging frame
  d_partialFrameCounter = 0;

  // Now if we're here we should have at least 1 block.

  // let's figure out how much we have in relation to noutput_items, accounting
  // for headers

  long blocksRequested = 0;
  long blocksAvailable = 0;

  if (d_header_type == SNAP_PACKETTYPE_VOLTAGE) {
	  blocksRequested = noutput_items / 16; // 16 vectors of 256 samples for 2 polarizations.
	  blocksAvailable = d_localqueue->size() / total_packet_size;
  }
  else {
	  // SPEC TODO:  Calc blocks
	  blocksRequested = noutput_items / 16;
	  blocksAvailable = d_localqueue->size() / total_packet_size;
  }

  if (blocksAvailable == 0) {
	  return 0;
  }

  long blocksRetrieved;

  if (blocksRequested <= blocksAvailable)
    blocksRetrieved = blocksRequested;
  else
    blocksRetrieved = blocksAvailable;

  // We're going to have to read the data out in blocks, account for the header,
  // then just move the data part into the out[] array.

  char *pData;
  pData = &localBuffer[d_header_size];
  int outIndex = 0;
  int skippedPackets = 0;

  for (int curPacket = 0; curPacket < blocksRetrieved; curPacket++) {
    // Move a packet to our local buffer
    for (int curByte = 0; curByte < total_packet_size; curByte++) {
      localBuffer[curByte] = d_localqueue->at(0);
      d_localqueue->pop_front();
    }

    // Interpret the header if present
    snap_voltage_header hdr;

    if (d_header_type != SNAP_PACKETTYPE_NONE) {
    	// Sample numbers within a packet will be sample_number to sample_number + 15 (16 time samples across 255 channels)
    	// So missing a packet would mean that the current sample_number > last sample_number +16
    	get_voltage_header(hdr);
    	static int print_ctr = 0;

    	if (print_ctr++ < 8) {
			printf("Packet Count: %d\n",print_ctr);
			printf("Sample Number: %" PRId64 "\n",hdr.sample_number);
			printf("Channel Number: %d\n",hdr.channel_id);
			printf("Antenna: %d\n",hdr.antenna_id);
			printf("Firmware version: %d\n\n",hdr.firmware_version);
    	}

      if (hdr.sample_number > (d_seq_num+1)) {
    	  skippedPackets += (hdr.sample_number - d_seq_num) -1;
      }

      d_seq_num = hdr.sample_number;
    }

    long block_bytes = 16 * 256;

    for (unsigned int i = 0; i < block_bytes; i++) {
		*x_pol = (int8_t)((uint8_t)(*pData) & 0xF0)/16;
		*x_pol++ = (int8_t)((*pData++) << 4)/16;
    }

    // Because of the way C++ stores multi-dimensional arrays we can split the polarizations like this.
    for (unsigned int i = 0; i < block_bytes; i++) {
		*y_pol = (int8_t)((uint8_t)(*pData) & 0xF0)/16;
		*y_pol++ = (int8_t)((*pData++) << 4)/16;
    }

    pmt::pmt_t pmt_channel_number =pmt::from_long((long)hdr.channel_id);

    add_item_tag(0, nitems_written(0) + curPacket, d_pmt_channel, pmt_channel_number);
    add_item_tag(1, nitems_written(0) + curPacket, d_pmt_channel, pmt_channel_number);

  }

  if (skippedPackets > 0 && d_notifyMissed) {
    std::stringstream msg_stream;
    msg_stream << "[UDP source:" << d_port
               << "] missed  packets: " << skippedPackets;
    GR_LOG_WARN(d_logger, msg_stream.str());
  }

  // If we had less data than requested, it'll be reflected in the return value.
  return blocksRetrieved;
}
} /* namespace grnet */
} /* namespace gr */

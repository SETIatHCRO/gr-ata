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

#include "snap_source_impl.h"
#include <gnuradio/io_signature.h>
#include <sstream>

#include <ata/snap_headers.h>

namespace gr {
namespace ata {

snap_source::sptr snap_source::make(size_t itemsize, size_t vlen, int port,
                                  int headerType, int payloadsize,
                                  bool notifyMissed,
                                  bool sourceZeros, bool ipv6) {
  return gnuradio::get_initial_sptr(
      new snap_source_impl(itemsize, vlen, port, headerType, payloadsize,
                          notifyMissed, sourceZeros, ipv6));
}

/*
 * The private constructor
 */
snap_source_impl::snap_source_impl(size_t itemsize, size_t vlen, int port,
                                 int headerType, int payloadsize,
                                 bool notifyMissed,
                                 bool sourceZeros, bool ipv6)
    : gr::sync_block("snap_source",
                     gr::io_signature::make(0, 0, 0),
                     gr::io_signature::make(1, 2, sizeof(char) * vlen)) {

  is_ipv6 = ipv6;

  d_itemsize = itemsize;
  d_veclen = vlen;

  d_port = port;
  d_seq_num = 0;
  d_notifyMissed = notifyMissed;
  d_sourceZeros = sourceZeros;
  d_partialFrameCounter = 0;
  
  d_header_type = headerType;

  d_block_size = d_itemsize * d_veclen;
  d_payloadsize = payloadsize;

  // Configure packet parser
  d_header_size = 0;
  switch (d_header_type) {
  case SNAP_PACKETTYPE_VOLTAGE:
    d_header_size = 8;
    d_payloadsize = 256 * 16 * 2; // channels * time steps * pols * sizeof(sc4)
    break;

  case SNAP_PACKETTYPE_SPECT:
    d_header_size = 8;
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

  // Not sure the purpose of these vars
  d_precompDataSize = d_payloadsize - d_header_size;
  d_precompDataOverItemSize = d_precompDataSize / d_itemsize;

  localBuffer = new char[d_header_size + d_payloadsize];
  long maxCircBuffer;

  // Compute reasonable buffer size
  maxCircBuffer = d_payloadsize * 1500; // 12 MiB
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

  // Configure output data block size
  int out_multiple = (d_payloadsize - d_header_size) / d_block_size;

  if (out_multiple == 1)
	  out_multiple = 2; // Ensure we get pairs, for instance complex -> ichar pairs

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

// Parse the packet header to get the sequence number
uint64_t snap_source_impl::get_header_seqnum() {
  uint64_t retVal = 0;

  if (d_header_type != SNAP_PACKETTYPE_NONE) {
	  retVal = ((snap_header *)localBuffer)->sample_number;
  }

  return retVal;
}

int snap_source_impl::work(int noutput_items,
                          gr_vector_const_void_star &input_items,
                          gr_vector_void_star &output_items) {
  gr::thread::scoped_lock guard(d_setlock);

  static bool firstTime = true;
  static int underRunCounter = 0;

  char *output_real = (char *)output_items[0];
  char *output_imag = (char *)output_items[1];
  
  int bytesAvailable = netdata_available();
  unsigned int numRequested = noutput_items * d_block_size;

  // Handle case where no data is available
  if ((bytesAvailable == 0) && (d_localqueue->size() == 0)) {
    underRunCounter++;
    d_partialFrameCounter = 0;

    if (d_sourceZeros) {
      // Just return 0's
      memset((void *)output_real, 0x00, numRequested); // numRequested will be in bytes
      memset((void *)output_imag, 0x00, numRequested); // numRequested will be in bytes
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
  // the queue. however read blocks so we want to make sure we have data before
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
  if (d_localqueue->size() < d_payloadsize) {
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

      // This is just a safety to clear in the case there's a hanging partial
      // packet. If we've lingered through a number of calls and we still don't
      // have any data, clear the stale data.
      while (d_localqueue->size() > 0)
        d_localqueue->pop_front();

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

  // Number of data-only blocks requested (set_output_multiple() should make
  // sure this is an integer multiple)
  long blocksRequested = noutput_items / d_precompDataOverItemSize;
  // Number of blocks available accounting for the header as well.
  long blocksAvailable = d_localqueue->size() / (d_payloadsize);
  long blocksRetrieved;
  int itemsreturned;

  if (blocksRequested <= blocksAvailable)
    blocksRetrieved = blocksRequested;
  else
    blocksRetrieved = blocksAvailable;

  // items returned is going to match the payload (actual data) of the number of
  // blocks.
  itemsreturned = blocksRetrieved * d_precompDataOverItemSize;

  // We're going to have to read the data out in blocks, account for the header,
  // then just move the data part into the out[] array.

  char *pData;
  pData = &localBuffer[d_header_size];
  int outIndex = 0;
  int skippedPackets = 0;

  for (int curPacket = 0; curPacket < blocksRetrieved; curPacket++) {
    // Move a packet to our local buffer
    for (int curByte = 0; curByte < d_payloadsize; curByte++) {
      localBuffer[curByte] = d_localqueue->at(0);
      d_localqueue->pop_front();
    }

    // Interpret the header if present
    if (d_header_type != SNAP_PACKETTYPE_NONE) {
    	// Sample numbers within a packet will be sample_number to sample_number + 15 (16 time samples across 255 channels)
    	// So missing a packet would mean that the current sample_number > last sample_number +16
      uint64_t sample_number = get_header_seqnum();

      if (sample_number > 0) { // d_seq_num will be 0 when this block starts
        if (sample_number > d_seq_num) {
        	// Each packet will be 16 time samples.
        	// Sample # should be d_seq_num + 16 when no packets are dropped.
        	// So (sample_number - (d_seq_num + 16) ) / 16 will tell us how many packets we missed.
          skippedPackets += (sample_number - (d_seq_num + 16)) / 16;
        }

        // Store as current for next pass.
        d_seq_num = sample_number;
      } else {
        // just starting.  Prime it for no loss on the first packet.
        d_seq_num = sample_number;
      }
    }

    for (unsigned int i = 0; i < itemsreturned; i++) {
    *output_real++ = (int8_t)((uint8_t)(*pData) & 0xF0)/16;
    *output_imag++ = (int8_t)((*pData++) << 4)/16;
    }
  }

  if (skippedPackets > 0 && d_notifyMissed) {
    std::stringstream msg_stream;
    msg_stream << "[UDP source:" << d_port
               << "] missed  packets: " << skippedPackets;
    GR_LOG_WARN(d_logger, msg_stream.str());
  }

  // If we had less data than requested, it'll be reflected in the return value.
  return itemsreturned;
}
} /* namespace grnet */
} /* namespace gr */

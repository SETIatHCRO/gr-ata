/* -*- c++ -*- */
/* 
 * Copyright 2019 ghostop14.
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

#ifndef INCLUDED_ATA_ATA_STREAM_IMPL_H
#define INCLUDED_ATA_ATA_STREAM_IMPL_H

#include <ata/ata_stream.h>
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <queue>

#define ATA_HEADER_OLD 0
#define ATA_HEADER_NEW 1

namespace gr {
  namespace ata {

  typedef struct NewATAPacket {
  	// New header is CHDR format: https://files.ettus.com/manual/page_rtp.html
  	uint8_t chdr;
  	uint8_t frac_time; // optional
  	uint8_t byte2;
  	uint8_t byte3;
  	uint8_t byte4;
  	uint8_t byte5;
  	uint8_t byte6;
  	uint8_t byte7;
  } NEW_ATA_PACKET_HEADER;

  typedef struct ATAPacket
  {
    uint8_t group, version, bitsPerSample, binaryPoint;
    uint32_t order;
    uint8_t type, streams, polCode, hdrLen;
    uint32_t src;
    uint32_t chan;
    uint32_t seq;
    double freq;
    double sampleRate;
    float usableFraction;
    float reserved;
    uint64_t absTime;
    uint32_t flags;
    uint32_t len;
  } ATA_PACKET_HEADER;

    class ata_stream_impl : public ata_stream
    {
     protected:
	    int d_payloadSize;
	    int d_packetSize;
	    int d_headerFormat;
	    int d_headerSize;

    	bool d_verbose;
    	int d_udpport;

        size_t d_block_size;

        boost::system::error_code ec;

        boost::asio::io_service d_io_service;
        boost::asio::ip::udp::endpoint d_endpoint;
        boost::asio::ip::udp::socket *udpsocket;

        boost::asio::streambuf read_buffer;
    	std::queue<char> localQueue;

        boost::mutex d_mutex;

    	int maxSize;

     public:
      ata_stream_impl(int udpport, int ata_header, int payloadSize, bool verbose);
      ~ata_stream_impl();

      bool stop();

      size_t dataAvailable();
      size_t netDataAvailable();

      // Where all the action really happens
      int work(int noutput_items,
         gr_vector_const_void_star &input_items,
         gr_vector_void_star &output_items);
    };

  } // namespace ata
} // namespace gr

#endif /* INCLUDED_ATA_ATA_STREAM_IMPL_H */


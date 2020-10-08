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

#ifndef INCLUDED_ATA_SNAP_source_impl_H
#define INCLUDED_ATA_SNAP_source_impl_H

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/circular_buffer.hpp>
#include <ata/snap_source.h>

namespace gr {
namespace ata {

// Make a vector data type that behaves like a native
// data type for use with std::deque

// This struct is needed/used to recast the data
// to an appropriate array.
struct voltage_packet {
	unsigned char data[16][256][2];
};

class ATA_API data_vector {
protected:
	char *data=NULL;
	size_t data_size=0;

public:
	data_vector() {
		data_size = 0;
		data = NULL;
	};

	data_vector(const data_vector& src) {
		if (src.data && (src.data_size > 0)) {
			data_size = src.data_size;
			data = new char[data_size];
			memcpy(data,src.data,data_size);
		}
	};

	data_vector(char *src_data,size_t src_size) {
		if (src_data && (src_size > 0)) {
			data_size = src_size;
			data = new char[data_size];
			memcpy(data,src_data,data_size);
		}
		else {
			data = NULL;
			data_size = 0;
		}
	};

	data_vector& operator= ( const data_vector & src) {
		if (src.data && (src.data_size > 0)) {
			data_size = src.data_size;
			data = new char[data_size];
			memcpy(data,src.data,data_size);
		}
		else {
			if (data) {
				delete[] data;
				data = NULL;
				data_size = 0;
			}
		}
	};

	virtual bool empty() {
		if (data_size == 0) {
			return true;
		}
		else {
			return true;
		}
	}

	virtual char * data_pointer() { return data; };

	virtual void store(char *src_data,size_t src_size) {
		if (data) {
			delete[] data;
		}

		if (src_data && (src_size > 0)) {
			data_size = src_size;
			data = new char[data_size];
			memcpy(data,src_data,data_size);
		}
		else {
			data = NULL;
			data_size = 0;
		}
	};


	virtual size_t size() { return data_size; };

	virtual ~data_vector() {
		if (data) {
			delete[] data;
			data = NULL;
			data_size = 0;
		}
	}
};

class ATA_API snap_source_impl : public snap_source {
protected:
  size_t d_veclen;

  bool d_notifyMissed;
  bool d_sourceZeros;
  int d_partialFrameCounter;

  bool is_ipv6;

  int d_port;
  int d_header_type;
  int d_header_size;
  uint16_t d_payloadsize;
  uint16_t total_packet_size;
  long d_udp_recv_buf_size;

  pmt::pmt_t d_pmt_channel;

  uint16_t d_last_channel_block;
  uint16_t d_starting_channel;
  uint16_t d_ending_channel;
  uint16_t d_ending_channel_packet_channel_id;
  int d_channel_diff;
  bool d_found_start_channel;
  int single_polarization_bytes;

  boost::system::error_code ec;

  boost::asio::io_service d_io_service;
  boost::asio::ip::udp::endpoint d_endpoint;
  boost::asio::ip::udp::socket *d_udpsocket;

  boost::asio::streambuf d_read_buffer;

  // A queue is required because we have 2 different timing
  // domains: The network packets and the GR work()/scheduler
  boost::circular_buffer<unsigned char> *d_localqueue;
  unsigned char *localBuffer;
  char *x_vector_buffer;
  char *y_vector_buffer;
  int vector_buffer_size;
  std::deque<data_vector> x_vector_queue;
  std::deque<data_vector> y_vector_queue;

  void get_voltage_header(snap_header& hdr) {
  	  uint64_t *header_as_uint64;
  	  header_as_uint64 = (uint64_t *)localBuffer;

  	  // Convert from network format to host format.
  	  uint64_t header = be64toh(*header_as_uint64);

  	  hdr.antenna_id = header & 0x3f;
  	  hdr.channel_id = (header >> 6) & 0x0fff;
  	  hdr.sample_number = (header >> 18) & 0x3fffffffffULL;
  	  hdr.firmware_version = (header >> 56) & 0xff;
  }

  void get_spectrometer_header(snap_header& hdr) {
  	  uint64_t *header_as_uint64;
  	  header_as_uint64 = (uint64_t *)localBuffer;

  	  // Convert from network format to host format.
  	  uint64_t header = be64toh(*header_as_uint64);

  	  hdr.antenna_id = header & 0xff;
  	  hdr.channel_id = (header >> 8) & 0x07;
  	  hdr.sample_number = (header >> 11) & 0x1fffffffffffULL;
  	  hdr.firmware_version = (header >> 56) & 0xff;
  }


public:
  snap_source_impl(int port, int headerType,
                  bool notifyMissed, bool sourceZeros, bool ipv6,
				  int starting_channel, int ending_channel);
  ~snap_source_impl();

  bool stop();

  size_t data_available() {
    return (netdata_available() + d_localqueue->size());
  };

  size_t netdata_available() {
    // Get amount of data available
    boost::asio::socket_base::bytes_readable command(true);
    d_udpsocket->io_control(command);
    size_t bytes_readable = command.get();

    return bytes_readable;
  };


  int work(int noutput_items, gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items);
};

} // namespace ata 
} // namespace gr

#endif /* INCLUDED_ATA_SNAP_source_impl_H */

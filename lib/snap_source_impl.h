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

class ATA_API snap_source_impl : public snap_source {
protected:
  size_t d_itemsize;
  size_t d_veclen;
  size_t d_block_size;

  bool d_notifyMissed;
  bool d_sourceZeros;
  int d_partialFrameCounter;

  bool is_ipv6;

  int d_port;
  int d_header_type;
  int d_header_size;
  uint16_t d_payloadsize;
  uint16_t total_packet_size;
  uint16_t precomp_samples_per_pkt;
  long d_udp_recv_buf_size;

  uint64_t d_seq_num;

  boost::system::error_code ec;

  boost::asio::io_service d_io_service;
  boost::asio::ip::udp::endpoint d_endpoint;
  boost::asio::ip::udp::socket *d_udpsocket;

  boost::asio::streambuf d_read_buffer;

  // A queue is required because we have 2 different timing
  // domains: The network packets and the GR work()/scheduler
  boost::circular_buffer<char> *d_localqueue;
  char *localBuffer;

  uint64_t get_header_seqnum();

public:
  snap_source_impl(size_t vecLen, int port, int headerType,
                  bool notifyMissed, bool sourceZeros, bool ipv6);
  ~snap_source_impl();

  bool stop();

  size_t data_available();
  inline size_t netdata_available();

  int work(int noutput_items, gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items);
};

} // namespace ata 
} // namespace gr

#endif /* INCLUDED_ATA_SNAP_source_impl_H */

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "ata_stream_impl.h"

namespace gr {
  namespace ata {

    ata_stream::sptr
    ata_stream::make(int udpport, int ata_header, int payloadSize, bool verbose)
    {
      return gnuradio::get_initial_sptr
        (new ata_stream_impl(udpport, ata_header, payloadSize, verbose));
    }

    /*
     * The private constructor
     */
    ata_stream_impl::ata_stream_impl(int udpport, int ata_header, int payloadSize, bool verbose)
      : gr::sync_block("ata_stream",
              gr::io_signature::make(0, 0, 0),
              gr::io_signature::make(1, 1, sizeof(char)))
    {
    	d_payloadSize = payloadSize;
    	d_headerFormat = ata_header;

    	if (d_headerFormat == ATA_HEADER_NEW)
    		d_headerSize = sizeof(NEW_ATA_PACKET_HEADER);
    	else
    		d_headerSize = sizeof(ATA_PACKET_HEADER);

    	d_packetSize = d_headerSize + d_payloadSize;
    	maxSize=256*d_packetSize;

    	d_verbose = verbose;
    	d_udpport = udpport;

    	d_block_size = 1; // d_itemsize * d_veclen;

        d_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), udpport);

		udpsocket = new boost::asio::ip::udp::socket(d_io_service,d_endpoint);

		d_io_service.run();

		gr::block::set_output_multiple(d_payloadSize);
    }

    /*
     * Our virtual destructor.
     */
    ata_stream_impl::~ata_stream_impl()
    {
    	bool retVal = stop();
    }

    bool ata_stream_impl::stop() {
        if (udpsocket) {
			udpsocket->close();

			udpsocket = NULL;

            d_io_service.reset();
            d_io_service.stop();
        }
        return true;
    }

    size_t ata_stream_impl::dataAvailable() {
    	// Get amount of data available
    	boost::asio::socket_base::bytes_readable command(true);
    	udpsocket->io_control(command);
    	size_t bytes_readable = command.get();

    	return (bytes_readable+localQueue.size());
    }

    size_t ata_stream_impl::netDataAvailable() {
    	// Get amount of data available
    	boost::asio::socket_base::bytes_readable command(true);
    	udpsocket->io_control(command);
    	size_t bytes_readable = command.get();

    	return bytes_readable;
    }


    int
    ata_stream_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
        gr::thread::scoped_lock guard(d_mutex);

    	int bytesAvailable = netDataAvailable();
	/*
	if (d_verbose) {
    		std::cout << "[ATA Stream] processing " << bytesAvailable << " received bytes" << std::endl;
	}
	*/

    	// quick exit if nothing to do
        if ((bytesAvailable == 0) && (localQueue.size() == 0))
        	return 0;

        int curSize = bytesAvailable + localQueue.size();

        // We don't have enough yet.
        if (curSize < d_packetSize)
        	return 0;

        // First let's queue up our data to our local queue
        char *out = (char *) output_items[0];
    	int bytesRead;
    	int returnedItems;
    	int localNumItems;
        int i;

        // numRequested is the payload + header.  However we're only returning the payload
        int blocksRequested = noutput_items / d_payloadSize;
        int blocksAvailable = (bytesAvailable + localQueue.size()) / d_packetSize; // slightly diff than requested.  need to acct for header

        if (blocksAvailable == 0)
        	return 0;

    	if (d_verbose) {
    		std::cout << "[ATA Stream] processing " << bytesAvailable << " received bytes" << std::endl;
    	}

    	// we could get here even if no data was received but there's still data in the queue.
    	// however read blocks so we want to make sure we have data before we call it.
    	if (bytesAvailable > 0) {
            boost::asio::streambuf::mutable_buffers_type buf = read_buffer.prepare(bytesAvailable);
        	// http://stackoverflow.com/questions/28929699/boostasio-read-n-bytes-from-socket-to-streambuf
            bytesRead = udpsocket->receive_from(buf,d_endpoint);

            if (bytesRead > 0) {
                read_buffer.commit(bytesRead);

                // Get the data and add it to our local queue.  We have to maintain a local queue
                // in case we read more bytes than noutput_items is asking for.  In that case
                // we'll only return noutput_items bytes
                const char *readData = boost::asio::buffer_cast<const char*>( read_buffer.data());

                // move data to the buffer
                for (i=0;i<bytesRead;i++) {
                	localQueue.push(readData[i]);
                }
            	read_buffer.consume(bytesRead);
            }
    	}

    	// Now let's return our data
    	// One more recalc on blocksAvailable just in case bytesRead < bytesAvailable
    	blocksAvailable = (localQueue.size()) / d_packetSize;

        for (int curBlock=0;curBlock<blocksAvailable;curBlock++) {
        	int blockStartIndex = curBlock * d_payloadSize;

        	// for now just drop the header
        	for (i=0;i<d_headerSize;i++) {
        		localQueue.pop();
        	}

        	// Now load the data to our output buffer
        	for (i=blockStartIndex;i<(blockStartIndex+d_payloadSize);i++) {
        		out[i]=localQueue.front();
        		localQueue.pop();
        	}
        }

    	// If we had less data than requested, it'll be reflected in the return value.
        return (blocksAvailable*d_payloadSize);
    }

  } /* namespace ata */
} /* namespace gr */


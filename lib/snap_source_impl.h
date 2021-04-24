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
#include <pcap/pcap.h>
#include <sys/socket.h>

namespace gr {
namespace ata {

#define SNAPFORMAT_2_0_0

#define MMSG_LENGTH 32
#define MMSG_TIMEOUT 1

const int VP_DATA_STRIDE=256*16*2;

#ifdef SNAPFORMAT_2_0_0
struct voltage_header {
	uint8_t version;
	uint8_t type;
	uint16_t n_chans;
	uint16_t chan;
	uint16_t feng_id;
	uint64_t timestamp;
};
#else
struct voltage_header {
	uint64_t header;
};
#endif

// This struct is needed/used to recast the data
// to an appropriate array.

struct voltage_packet {
#ifdef SNAPFORMAT_2_0_0
	// 256 channels,16 times, 2 polarizations
	unsigned char data[256][16][2];
#else
	// 16 times, 256 channels, 2 polarizations
	unsigned char data[16][256][2];
#endif
};

struct spectrometer_packet {
	/*
	 * Each spectrometer dump is a 64 kiB data set, comprising 4096 channels and 4
32-bit floats per channel. Each data dump is transmitted from the SNAP in 8 UDP packets, each with an 512 channel
(8 kiB) payload and 8 byte header
	 */
	// 512 channels, 4 output indices ( XX, YY, real XY*, imag XY*)
	float data[512][4];
};

// Make a vector data type that behaves like a native
// data type for use with std lists (vectors/deques/etc)

template <typename T>
class data_vector {
protected:
	T *data=NULL;
	size_t data_size=0;

public:
	data_vector() {
		data_size = 0;
		data = NULL;
	};

	data_vector(const data_vector<T>& src) {
		if (src.data && (src.data_size > 0)) {
			data_size = src.data_size;
			data = new T[data_size];
			memcpy(data,src.data,data_size*sizeof(T));
		}
	};

	data_vector(T *src_data,size_t src_size) {
		/*
		if (src_data && (src_size > 0)) {
			data_size = src_size;
			data = new T[data_size];
			memcpy(data,src_data,data_size*sizeof(T));
		}
		else {
			data = NULL;
			data_size = 0;
		}
		 */
		// Pulled off safeties to get a speedup
		data_size = src_size;
		data = new T[data_size];
		memcpy(data,src_data,data_size*sizeof(T));
	};

	data_vector(size_t src_size) {
		// Initialize with empty memory.
		data_size = src_size;
		data = new T[data_size];
		memset(data,0x00,data_size*sizeof(T));
	};

	data_vector<T>& operator= ( const data_vector<T> & src) {
		clear();

		if (src.data && (src.data_size > 0)) {
			data_size = src.data_size;
			data = new T[data_size];
			memcpy(data,src.data,data_size*sizeof(T));
		}

		return *this;
	};

	virtual bool empty() {
		if (data_size == 0) {
			return true;
		}
		else {
			return true;
		}
	};

	virtual T * data_pointer() { return data; };

	virtual void store(T *src_data,size_t src_size) {
		if (!src_data || (src_size == 0)) {
			// If we requested NULL or zero size, clear our buffer.
			if (data) {
				delete[] data;
				data = NULL;
			}
			data_size = 0;
			return;
		}

		if ((src_size != data_size) && (data) ) {
			// If the new size is different than the old size,
			// let's change our buffer.  This'll save memory
			// delete/news if the size is re-used.
			delete[] data;
			data = NULL;
		}

		if (!data) {
			data_size = src_size;
			data = new T[data_size];
		}

		// We can get here if the buffer size didn't change,
		// or a new data block was created.  In either case
		// data_size should be correct.
		memcpy(data,src_data,data_size*sizeof(T));
	};


	virtual size_t size() { return data_size; };

	virtual void clear() {
		if (data) {
			delete[] data;
			data = NULL;
		}
		data_size = 0;
	}

	virtual ~data_vector() {
		if (data) {
			delete[] data;
			// Pulled safeties off for speedup
			// data = NULL;
			// data_size = 0;
		}
	};
};

class ATA_API snap_source_impl : public snap_source {
protected:
	size_t d_veclen;

	bool d_notifyMissed;
	bool d_sourceZeros;
	int d_partialFrameCounter;

	bool is_ipv6;

	bool d_use_pcap;
	std::string d_file;
	bool d_repeat_file;
	bool pcap_file_done;
	pcap_t *pcapFile = NULL;
	boost::mutex fp_mutex;
	long min_pcap_queue_size;
	long reload_size;
	pcap_pkthdr pcap_header;

	int d_data_source;
	bool d_use_mcast;
	std::string d_mcast_group;

	bool d_send_start_msg;

	bool d_packed_output;

	int d_port;
	int d_header_type;
	int d_header_size;
	uint16_t d_payloadsize;
	uint16_t total_packet_size;
	long d_udp_recv_buf_size;

	pmt::pmt_t d_pmt_seqnum;
	pmt::pmt_t d_block_name;

	uint16_t d_last_channel_block;
	uint64_t d_last_timestamp;

	uint16_t d_starting_channel;
	uint16_t d_ending_channel;
	uint16_t d_ending_channel_packet_channel_id;
	int d_channel_diff;
	int packets_per_frame;
	int multipacket_frame_pkt_ctr;
	bool b_one_packet;
	bool d_found_start_channel;
	int single_polarization_bytes;

	boost::system::error_code ec;

	boost::asio::io_service d_io_service;
	boost::asio::ip::udp::endpoint d_endpoint;
	boost::asio::ip::udp::socket *d_udpsocket = NULL;
	std::string d_udp_ip;

	// These local net buffers are only used in non-threaded mode in queue_data.
	unsigned char *local_net_buffer = NULL;
	long local_net_buffer_size=0;

	// multimessage receive (mmsg)
	struct mmsghdr msgs[MMSG_LENGTH];
	struct iovec iovecs[MMSG_LENGTH];
	unsigned char bufs[MMSG_LENGTH][9000];
	struct timespec timeout;
	int mmsg_sleep_time = 0;

	// Separate receive thread
	boost::thread *proc_thread=NULL;
	bool threadRunning=false;
	bool stop_thread = false;
	gr::thread::mutex d_net_mutex;
	unsigned int d_cpu;
	unsigned int d_cpu_node;
	bool work_called = false;
	// std::chrono::time_point<std::chrono::steady_clock> start_time, end_time;

	// Actual thread function
	virtual void runThread();

	// Two's complement lookup table
	char twosComplementLUT[16];

	// A queue is required because we have 2 different timing
	// domains: The network packets and the GR work()/scheduler
	boost::circular_buffer<data_vector<unsigned char>> *d_localqueue;
	unsigned char *localBuffer;
	char *test_buffer = NULL;
#ifdef ZEROCOPY
	unsigned char *zc_front_pointer = NULL;
#endif

	// Common mode items
	int vector_buffer_size;
	int channels_per_packet;
#ifdef USE_CIRC_VB
	boost::circular_buffer<uint64_t> seq_num_queue;
#else
	std::deque<uint64_t> seq_num_queue;
#endif

	// async receive items
	unsigned char *async_buffer = NULL;
	bool d_send_sync_pmt = false;
	snap_header async_volt_sync_hdr;
	snap_header async_spect_sync_hdr;
	uint64_t sync_timestamp = 0;

	// Voltage Mode buffers
	char *x_vector_buffer = NULL;
	char *y_vector_buffer = NULL;
#ifdef USE_CIRC_VB
	boost::circular_buffer<data_vector<char>> x_vector_queue;
	boost::circular_buffer<data_vector<char>> y_vector_queue;
#else
	std::deque<data_vector<char>> x_vector_queue;
	std::deque<data_vector<char>> y_vector_queue;
#endif

	// Spectrometer mode items
	float *xx_buffer = NULL;
	float *yy_buffer = NULL;
	float *xy_real_buffer = NULL;
	float *xy_imag_buffer = NULL;
#ifdef USE_CIRC_VB
	boost::circular_buffer<data_vector<float>> xx_vector_queue;
	boost::circular_buffer<data_vector<float>> yy_vector_queue;
	boost::circular_buffer<data_vector<float>> xy_real_vector_queue;
	boost::circular_buffer<data_vector<float>> xy_imag_vector_queue;
#else
	std::deque<data_vector<float>> xx_vector_queue;
	std::deque<data_vector<float>> yy_vector_queue;
	std::deque<data_vector<float>> xy_real_vector_queue;
	std::deque<data_vector<float>> xy_imag_vector_queue;
#endif

	void openPCAP();
	void closePCAP();

	int mmsg_receive();

	void copy_volt_data_to_vector_buffer(snap_header& hdr);
	void queue_voltage_data(snap_header& hdr);

	void get_voltage_header(snap_header& hdr, unsigned char *pBuff) {
		struct voltage_header *v_hdr;
		v_hdr = (struct voltage_header *)pBuff;
		hdr.antenna_id = be16toh(v_hdr->feng_id);
		hdr.channel_id = be16toh(v_hdr->chan);
		hdr.firmware_version = v_hdr->version; // no need to network->host order, only a byte
		hdr.sample_number = be64toh(v_hdr->timestamp);
		hdr.type = v_hdr->type;
	}

	bool voltage_synchronize(unsigned char *pBuff) {
		// We're not synchronized on the first packet yet, so we're looking for it.
		struct voltage_header *v_hdr  = (struct voltage_header *)pBuff;
		uint16_t channel_id = be16toh(v_hdr->chan);

		if ( channel_id == d_starting_channel ) {
			// We found our start channel packet.
			// Set that we're synchronized and that we need to
			// send out the pmt (can't cross threads with it so the main work thread
			// will need to do it.
			d_found_start_channel = true;

			get_voltage_header(async_volt_sync_hdr,pBuff);

			std::stringstream msg_stream;
			if (d_use_pcap) {
				msg_stream << "Data block alignment achieved for " << d_file << " with timestamp " << async_volt_sync_hdr.sample_number << " as first block";
			}
			else {
				msg_stream << "Data block alignment achieved for port " << d_port << " with timestamp " << async_volt_sync_hdr.sample_number << " as first block";
			}

			GR_LOG_INFO(d_logger, msg_stream.str());

			d_send_sync_pmt = true;

			return true;
		}
		else {
			// we're still not sync'd.
			return false;
		}
	}

	bool spect_synchronize(unsigned char *pBuff) {
		// We're not synchronized on the first packet yet, so we're looking for it.
		uint64_t *header_as_uint64 = (uint64_t *)pBuff;
		uint64_t header = be64toh(*header_as_uint64);
		uint16_t channel_id = ((header >> 8) & 0x07) * 512; // Id cycles 0-7.  Channel is 512*val;

		if ( channel_id == d_starting_channel ) {
			// We found our start channel packet.
			// Set that we're synchronized and that we need to
			// send out the pmt (can't cross threads with it so the main work thread
			// will need to do it.
			d_found_start_channel = true;

			get_spect_header(async_spect_sync_hdr, pBuff);

			std::stringstream msg_stream;
			if (d_use_pcap) {
				msg_stream << "Data block alignment achieved for " << d_file << " with timestamp " << async_spect_sync_hdr.sample_number << " as first block";
			}
			else {
				msg_stream << "Data block alignment achieved for port " << d_port << " with timestamp " << async_spect_sync_hdr.sample_number << " as first block";
			}

			GR_LOG_INFO(d_logger, msg_stream.str());

			d_send_sync_pmt = true;

			return true;
		}
		else {
			// we're still not sync'd.
			return false;
		}
	}

	void get_spect_header(snap_header& hdr, unsigned char *pBuff) {
		uint64_t *header_as_uint64 = (uint64_t *)pBuff;

		// Convert from network format to host format.
		uint64_t header = be64toh(*header_as_uint64);

		hdr.antenna_id = header & 0xff;
		hdr.channel_id = ((header >> 8) & 0x07) * 512; // Id cycles 0-7.  Channel is 512*val
		hdr.sample_number = (header >> 11) & 0x1fffffffffffULL;
		hdr.firmware_version = (header >> 56) & 0xff;
	}

	void get_spectrometer_header(snap_header& hdr) {
		uint64_t *header_as_uint64;
		header_as_uint64 = (uint64_t *)localBuffer;

		// Convert from network format to host format.
		uint64_t header = be64toh(*header_as_uint64);

		hdr.antenna_id = header & 0xff;
		hdr.channel_id = ((header >> 8) & 0x07) * 512; // Id cycles 0-7.  Channel is 512*val
		hdr.sample_number = (header >> 11) & 0x1fffffffffffULL;
		hdr.firmware_version = (header >> 56) & 0xff;
	}

	void NotifyMissed(int skippedPackets) {
		if (skippedPackets > 0 && d_notifyMissed) {
			std::stringstream msg_stream;
			msg_stream << "[UDP source:" << d_port
					<< "] missed packets: " << skippedPackets;

			if (d_localqueue->full()) {
				msg_stream << ".  Queue full.  Network packets are not being processed fast enough.";
			}

			GR_LOG_WARN(d_logger, msg_stream.str());
		}
	};

	void header_to_local_buffer(void)
	{
		gr::thread::scoped_lock guard(d_net_mutex);

		unsigned char *first_packet = d_localqueue->front().data_pointer();
		memcpy(localBuffer,first_packet,d_header_size);
	};

	void get_voltage_header(snap_header& hdr) {
#ifdef ZEROCOPY
		{
			gr::thread::scoped_lock guard(d_net_mutex);
			zc_front_pointer = d_localqueue->front().data_pointer();
		}

		struct voltage_header *v_hdr = (struct voltage_header *)zc_front_pointer;
#else
		struct voltage_header *v_hdr = (struct voltage_header *)localBuffer;
#endif

		hdr.antenna_id = be16toh(v_hdr->feng_id);
		hdr.channel_id = be16toh(v_hdr->chan);
		hdr.firmware_version = v_hdr->version; // no need to network->host order, only a byte
		hdr.sample_number = be64toh(v_hdr->timestamp);
		hdr.type = v_hdr->type;
	}

	void fill_local_buffer(void) {
		gr::thread::scoped_lock guard(d_net_mutex);

		unsigned char *first_packet = d_localqueue->front().data_pointer();
		memcpy(localBuffer,first_packet,total_packet_size);
		d_localqueue->pop_front();
	};

	void start_receive() {
		if (stop_thread)
			return;

		d_udpsocket->async_receive_from(
				boost::asio::buffer(async_buffer,d_udp_recv_buf_size), d_endpoint,
				boost::bind(&snap_source_impl::handle_receive, this,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred));
	};

	void handle_receive(const boost::system::error_code& error, std::size_t bytes_transferred);

	int work_volt_mode(int noutput_items, gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items, bool liveWork);
	int work_volt_mode_old(int noutput_items, gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items, bool liveWork);
	int work_spec_mode(int noutput_items, gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items, bool liveWork);
public:
	snap_source_impl(int port, int headerType,
			bool notifyMissed, bool sourceZeros, bool ipv6,
			int starting_channel, int ending_channel, int data_size,
			int data_source, std::string file="", bool repeat_file=false, bool packed_output=false,
			std::string mcast_group="", bool send_start_msg=false, std::string udp_ip="");

	~snap_source_impl();

	virtual bool start();
	virtual bool stop();

	void handleSyncMsg(pmt::pmt_t msg);

	size_t packet_size() { return total_packet_size; };
	bool packets_aligned() { return d_found_start_channel; };
	void queue_data();
	void queue_pcap_data();

	void create_test_buffer();

	void set_test_case_min_queue_length(long min_queue_length) { min_pcap_queue_size = min_queue_length; };

	size_t packets_available() {
		gr::thread::scoped_lock guard(d_net_mutex);
		size_t queue_size = d_localqueue->size();
		return queue_size;
	};

	size_t netdata_available() {
		// Get amount of data available
		boost::asio::socket_base::bytes_readable command(true);
		d_udpsocket->io_control(command);
		size_t bytes_readable = command.get();

		return bytes_readable;
	};

	int8_t TwosComplement4Bit(int8_t b) {
		if (b > 7) { // Max before 4th bit gets flipped on.
			if (b==8) {
				return 0;
			}
			else {
				return b - 16;
			}
		}
		else {
			return b;
		}
	};

	int8_t TwosComplementLookup4Bit(int8_t b) {
		return twosComplementLUT[b];
	}

	int work_test_copy(int noutput_items, gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items);

	int work_test(int noutput_items, gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items);

	int work(int noutput_items, gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items);
};

} // namespace ata 
} // namespace gr

#endif /* INCLUDED_ATA_SNAP_source_impl_H */

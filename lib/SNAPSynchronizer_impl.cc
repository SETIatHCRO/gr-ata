/* -*- c++ -*- */
/*
 * Copyright 2020 ghostop14.
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
#include "SNAPSynchronizer_impl.h"

// #define _TIMEWORK

#ifdef _TIMEWORK
#include <chrono>
#include <ctime>
#define iterations 100
#endif

#include <limits>

namespace gr {
namespace ata {

SNAPSynchronizer::sptr
SNAPSynchronizer::make(int num_inputs, int num_channels, bool bypass)
{
	return gnuradio::get_initial_sptr
			(new SNAPSynchronizer_impl(num_inputs, num_channels, bypass));
}


/*
 * The private constructor
 */
SNAPSynchronizer_impl::SNAPSynchronizer_impl(int num_inputs, int num_channels, bool bypass)
: gr::sync_block("SNAPSynchronizer",
		gr::io_signature::make(num_inputs, num_inputs, sizeof(char)*2*num_channels),
		gr::io_signature::make(num_inputs, num_inputs, sizeof(char)*2*num_channels)),
		d_num_inputs(num_inputs), d_num_channels(num_channels),d_bypass(bypass)
{
	if (!d_bypass) {
		queueList.reserve(d_num_inputs);

		for (int i=0;i<d_num_inputs;i++) {
			// std::deque<TaggedS8IQData> newQueue;
			std::deque<char *> *newQueue;
			newQueue = new std::deque<char *>;
			queueList.push_back(newQueue);
		}
	}

	d_num_channels_x2 = 2*d_num_channels;

	// frame size is used to take a sixteen time frame block in as a single unit.
	// The * 16 here comes from the SNAP packet format.
	size_long = sizeof(long);
	frame_size = size_long + d_num_channels_x2 * 16;
	frame_size_16 = d_num_channels_x2 * 16;

	d_pmt_seqnum = pmt::string_to_symbol("sample_num");

	d_block_name = pmt::string_to_symbol(identifier());

	pmt_sequence_number_minus_one =pmt::from_long(-1);

	tag_list = new long[d_num_inputs];

	// We'll set tags ourselves, since they may change if we have to realign.
	if (d_bypass)
		set_tag_propagation_policy(TPP_ONE_TO_ONE);
	else
		set_tag_propagation_policy(TPP_DONT);

	// The SNAP outputs packets in 16-time step blocks.  So let's take advantage of that here.
	set_output_multiple(16);
}

/*
 * Our virtual destructor.
 */
SNAPSynchronizer_impl::~SNAPSynchronizer_impl()
{
	delete[] tag_list;

	// Have to clear any lingering memory
	if (!d_bypass) {
		for (int cur_input=0;cur_input<d_num_inputs;cur_input++) {
			while (queueList[cur_input]->size() > 0) {
				char *full_data = queueList[cur_input]->front();
				queueList[cur_input]->pop_front();
				delete [] full_data;
			}

			delete queueList[cur_input];
		}
	}
}

bool SNAPSynchronizer_impl::tags_match(int noutput_items, gr_vector_const_void_star &input_items) {
	std::vector<gr::tag_t> input0_tags;
	this->get_tags_in_window(input0_tags, 0, 0, noutput_items);
	int num_tags = input0_tags.size();

	if (num_tags == 0) {
		GR_LOG_ERROR(d_logger,"Input 0 has no tags.");
		throw std::runtime_error("No input tags received with stream.  Make sure a SNAP Source block is feeding this block.  Each vector should have a seq_num tag key with the value of the sequence number.");
		return false;
	}

	long actual_tags[num_tags];

	for (int cur_tag=0;cur_tag < num_tags;cur_tag++) {
		long tag_val = pmt::to_long(input0_tags[cur_tag].value);
		actual_tags[cur_tag] = tag_val;
	}

	for (int i=1;i<d_num_inputs;i++) {
		std::vector<gr::tag_t> tags;
		this->get_tags_in_window(tags, i, 0, noutput_items);

		if (num_tags == 0) {
			std::stringstream stream;
			stream << "No input tags received with stream " << i <<
					".  Make sure a SNAP Source block is feeding this block.  Each vector should have a seq_num tag key with the value of the sequence number.";
			GR_LOG_ERROR(d_logger,stream.str());
			throw std::runtime_error(stream.str());
			return false;
		}

		// Tag vector lengths don't match.  Something definitely
		// doesn't match.
		if (tags.size() != num_tags)
			return false;

		// If any of the tags don't match, return false
		for (int cur_tag=0;cur_tag < num_tags;cur_tag++) {
			long tag_val = pmt::to_long(tags[cur_tag].value);
			if (tag_val != actual_tags[cur_tag])
				return false;
		}
	}

	return true;
}

int
SNAPSynchronizer_impl::work_bypass_mode(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items)
{
	gr::thread::scoped_lock guard(d_setlock);
	for (int i=0;i<d_num_inputs;i++) {
		const char *in = (const char *) input_items[i];
		char *out = (char *) output_items[i];

		memcpy(out, in, noutput_items * d_num_channels_x2);
		// Don't have to worry about tags because in this mode, we don't
		// remove the tag propogation policy.
	}

	// One check if the tags are aligned.
	static bool first_time = true;

	if (first_time) {
		first_time = false;

		bool tags_synchronized = tags_match(noutput_items,input_items);

		if (!tags_synchronized) {
			std::cout << "WARNING streams are not synchronized on start." << std::endl;
		}
		else {
			std::cout << "Streams starting synchronized." << std::endl;
		}
	}

	return noutput_items;
}

#ifdef OLD_SLOW_VERSION
int
SNAPSynchronizer_impl::work_slow_version(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items)
{
	gr::thread::scoped_lock guard(d_setlock);

#ifdef _TIMEWORK
	static int current_iteration = 0;

	std::chrono::time_point<std::chrono::steady_clock> start, end;
	static float elapsed_time = 0.0;
	/*
	if (current_iteration == 0) {
		std::cout << "Starting timing..." << std::endl;
	}
	 */
	start = std::chrono::steady_clock::now();
#endif

	// If we're in bypass mode, just copy the data and on the first pass,
	// put up a notice on synchronization state.
	if (d_bypass) {
		return work_bypass_mode(noutput_items, input_items, output_items);
	}

	// Used in loops
	int cur_input;
	int cur_item;
	long cur_tag;

	// First lets copy the tags to a faster array.
	// get_tags does a lot of vector work, so we can avoid it
	// by grabbing them once.  This saves us noutput_items many
	// additional calls to get_tags and all the vector work
	// further down since we need them here anyway to determine
	// If we need to queue the data or not.

	long tag_matrix[noutput_items][d_num_inputs];
	// bool b_tags_match = true;

	for (cur_input=0;cur_input<d_num_inputs;cur_input++) {
		std::vector<gr::tag_t> tags;
		this->get_tags_in_window(tags, cur_input, 0, noutput_items);

		for (cur_item=0;cur_item < tags.size();cur_item++) {
			cur_tag = pmt::to_long(tags[cur_item].value);

			tag_matrix[cur_item][cur_input] = cur_tag;
		}
	}

	/* Commented out for performance
	cur_input=0;

	while ((cur_input<d_num_inputs) && b_tags_match) {
		long match_tag = tag_matrix[0][cur_input];

		for (cur_item=1;cur_item < noutput_items;cur_item++) {
			if (tag_matrix[cur_item][cur_input] != match_tag) {
				b_tags_match = false;
				break;
			}
		}

		cur_input++;
	}

	// Fast track data pipeline: If the queues are empty and the tags match,
	// We're good just continue copying for speed.  No need to queue.
	if (queues_empty() && b_tags_match) {
		for (cur_input=0;cur_input<d_num_inputs;cur_input++) {
			const char *in = (const char *) input_items[cur_input];
			char *out = (char *) output_items[cur_input];

			// Simply copy the input data to the output
			memcpy(out, in, noutput_items * d_num_channels_x2);

			// Manually copy the tags
			std::vector<gr::tag_t> tags;
			this->get_tags_in_window(tags, cur_input, 0, noutput_items);

			for (cur_tag=0;cur_tag<tags.size();cur_tag++) {
				add_item_tag(cur_input, nitems_written(0) + cur_tag, tags[cur_tag].key, tags[cur_tag].value,d_block_name);
			}
		}

		return noutput_items;
	}
	*/

	// If we're here, we have synchronization issues to align
	// ------------------------------------------------------
	// Queue up the data we received
	for (cur_input=0;cur_input<d_num_inputs;cur_input++) {
		// Go through each of the inputs,
		// Grab its tag vectors
		// Go through its input items and queue them up.
		const char * input_stream = (const char *)input_items[cur_input];
		for (cur_item=0;cur_item < noutput_items;cur_item++) {
			cur_tag = tag_matrix[cur_item][cur_input];

			// If the tag value is -1, the block sourced zeros which we don't want to
			// queue here.  We'll handle missing sequence numbers when we queue it later.
			if (cur_tag >= 0) {
				TaggedS8IQData new_data(&input_stream[cur_item*d_num_channels_x2],d_num_channels_x2, cur_tag);
				queueList[cur_input].push_back(new_data);
			}
		}
	}

	// Now go through each item as the outer loop and fill the output stream.
	// This is where we'll grab the lowest index in all of the queues, and
	// use this information to output either a valid block or all zeros if
	// we missed one or were out of sync.

	int filled_blocks=0;
	long lowest_num; // The block above won't queue data if the tag is -1 (blank / zeros).  This forces synchronization.

	for (cur_item=0;cur_item < noutput_items; cur_item++) {
		// Go through each item in each queue and grab the lowest number.
		// Make sure we have data in all queues.  We have to do this check
		// After every cycle where we pop from the queue because we won't
		// know if we emptied it below till the next cycle.

		// Initialize with the first item id
		lowest_num = queueList[0].front().get_tag();

		for (cur_input=0;cur_input<d_num_inputs;cur_input++) {
			if (queueList[cur_input].size() == 0) {
				// std::cout << "[SNAP Synchronizer] At least one queue was empty.  Waiting till next call to process more data.  Work returning " << filled_blocks << std::endl;
				return filled_blocks;
			}
			cur_tag = queueList[cur_input].front().get_tag();
			// This saves some function calls below
			tag_list[cur_input] = cur_tag;

			if (cur_tag < lowest_num) {
				lowest_num = cur_tag;
			}
		}

		filled_blocks++;

		// Now we know the lowest number, so we know what sequence
		// number to synchronize on.

		// If the current queue has that number, output the data
		// Otherwise output zeros.  In either case, make sure to
		// Add the proper tag in for it.
		for (cur_input=0;cur_input<d_num_inputs;cur_input++) {
			char * output_stream = (char *)output_items[cur_input];

			cur_tag = tag_list[cur_input];

			if (cur_tag == lowest_num) {
				// Output the data
				TaggedS8IQData full_data = queueList[cur_input].front();
				queueList[cur_input].pop_front();

				pmt::pmt_t pmt_sequence_number =pmt::from_long(cur_tag);
				add_item_tag(cur_input, nitems_written(0) + cur_item, d_pmt_seqnum, pmt_sequence_number,d_block_name);
				memcpy(&output_stream[d_num_channels_x2*cur_item], full_data.data_pointer(), d_num_channels_x2);
			}
			else {
				// Output a blank frame for this output
				add_item_tag(cur_input, nitems_written(0) + cur_item, d_pmt_seqnum, pmt_sequence_number_minus_one,d_block_name);
				memset(&output_stream[d_num_channels_x2*cur_item], 0x00, d_num_channels_x2);
			}
		}
	}

#ifdef _TIMEWORK
	end = std::chrono::steady_clock::now();

	std::chrono::duration<double> elapsed_seconds = end-start;
	float throughput;
	long input_buffer_total_bytes;
	float bits_throughput;

	elapsed_seconds = end-start;

	elapsed_time += elapsed_seconds.count() / noutput_items; // let's get a per-vector time

	current_iteration++;

	if (current_iteration == 100) {
		elapsed_time = elapsed_time / (float)iterations;
		throughput = d_num_channels * d_num_inputs  / elapsed_time;
		input_buffer_total_bytes = d_num_channels_x2;
		bits_throughput = 8 * input_buffer_total_bytes / elapsed_time;

		std::cout << std::endl << "GNURadio work() performance:" << std::endl;
		std::cout << "Elapsed time: " << elapsed_seconds.count() << std::endl;
		std::cout << "Timing Averaging Iterations: " << iterations << std::endl;
		std::cout << "Average Run Time:   " << std::fixed << std::setw(11) << std::setprecision(6) << elapsed_time << " s" << std::endl <<
				"Total throughput: " << std::setprecision(2) << throughput << " byte complex samples/sec" << std::endl <<
				"Individual stream throughput: " << std::setprecision(2) << throughput / d_num_inputs << " byte complex samples/sec" << std::endl <<
				"Projected processing rate: " << bits_throughput << " bps" << std::endl;
	}
#endif

	return noutput_items;
}
#endif

int
SNAPSynchronizer_impl::work(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items)
{
	// If we're in bypass mode, just copy the data and on the first pass,
	// put up a notice on synchronization state.
	if (d_bypass) {
		return work_bypass_mode(noutput_items, input_items, output_items);
	}

	int num_blocks = noutput_items / 16;
	gr::thread::scoped_lock guard(d_setlock);

#ifdef _TIMEWORK
	static int current_iteration = 0;

	std::chrono::time_point<std::chrono::steady_clock> start, end;
	static float elapsed_time = 0.0;
	/*
	if (current_iteration == 0) {
		std::cout << "Starting timing..." << std::endl;
	}
	 */
	start = std::chrono::steady_clock::now();
#endif

	// Iterate through num_blocks (a block is 16 input items) for each input
	// Create a frame_size buffer
	// Grab the first tag (since they'll all be the same for a 16-block set) and copy that to the beginning of the buffer
	// Then copy the remaining 16 time entries in.
	// Queue that up

	for (int cur_input=0;cur_input<d_num_inputs;cur_input++) {
		std::vector<gr::tag_t> tags;
		this->get_tags_in_window(tags, cur_input, 0, noutput_items);
		// Here's where we make an optimization assumption.
		// The SNAP block is outputting 16 time entries per frame all with the same sequence number
		// We're "assuming" here that 16 entries at a clip are all the same.  So we can just
		// Grab the first of a 16-block set as the number we care about.
		const char * input_stream = (const char *)input_items[cur_input];
		for (int cur_block=0;cur_block<num_blocks;cur_block++) {
			unsigned long cur_tag = pmt::to_long(tags[cur_block*16].value);
			char *buff;
			buff = new char[frame_size];
			memcpy(buff,(void *)&cur_tag,size_long);
			memcpy(&buff[size_long],&input_stream[frame_size_16*cur_block],frame_size_16);

			queueList[cur_input]->push_back(buff);
		}
	}

	// Now that we're queued up, let's see how we align and fill the outbound data.
	// Note, it's very likely queues will not be aligned:
	// queue Id: 1 2 3 4 5 6
	// queue Id: 2 3 4 5 6 7
	// queue Id: 1 3 4 5 7 8
	// ...
	// So let's loop through how many output blocks we need
	// Check on each block cycle what the lowest id is
	// Use that lowest block number to fill the output for this output block
	// If a queue doesn't have that id (misaligned), we'll fill the output for that block with zeros
	// We're responsible here for deleting any buffer we created when we pop from a queue

	unsigned long lowest_num;

	for (int cur_block=0;cur_block<num_blocks;cur_block++) {
		// Go through each block in each queue and grab the lowest number.
		// Make sure we have data in all queues.  We have to do this check
		// After every cycle where we pop from the queue because we won't
		// know if we emptied it below till the next cycle.

		bool lowest_set = false;

		// We have to loop all the way through the queues here to find the lowest value for each block we need.
		for (int cur_input=0;cur_input<d_num_inputs;cur_input++) {
			if (queueList[cur_input]->size() > 0) {
				unsigned long cur_tag = *((unsigned long *)queueList[cur_input]->front());

				// So here we have to be a bit careful as sequence numbers can go to LONG_MAX and roll over.
				// Since we don't have NAN for long (it's all float/double), we have to track it manually here.
				if (!lowest_set || (cur_tag < lowest_num) ) {
					lowest_num = cur_tag;
					lowest_set = true;
				}
			}
			else {
				// This is our check that one of the queues may have been emptied.

				// We can't keep processing since we don't know what may come in next.  So
				// We're going to have to exit with what we have.
				// We ran out and only filled the previous block, so that's all we can say we produced.
				return cur_block * 16;
			}
		}

		// Now we know the lowest number still in each queue, so we know what sequence
		// number to synchronize on.
		// Loop through the inputs and for the current block,
		// Output either: the data for the lowest sequence number, or zero's.
		for (int cur_input=0;cur_input<d_num_inputs;cur_input++) {
			char * output_stream = (char *)output_items[cur_input];
			if (queueList[cur_input]->size() > 0) {
				char *buff = queueList[cur_input]->front();
				unsigned long cur_tag = *((unsigned long *)buff);

				if (cur_tag == lowest_num) {
					// Output the data
					pmt::pmt_t pmt_sequence_number = pmt::from_long(cur_tag);
					// I only need to output the tag once now for the xengine to pick it up.  We can just output
					// it at the beginning of the block
					add_item_tag(cur_input, nitems_written(0) + cur_block*frame_size_16, d_pmt_seqnum, pmt_sequence_number,d_block_name);

					memcpy(&output_stream[cur_block*frame_size_16], &buff[size_long], frame_size_16);

					// clean up memory
					queueList[cur_input]->pop_front();
					delete [] buff;
				}
				else {
					// Output a blank frame for this output
					add_item_tag(cur_input, nitems_written(0) + cur_block*frame_size_16, d_pmt_seqnum, pmt_sequence_number_minus_one,d_block_name);

					memset(&output_stream[cur_block*frame_size_16], 0x00, frame_size_16);
				}
			}
		}
	}

#ifdef _TIMEWORK
	end = std::chrono::steady_clock::now();

	std::chrono::duration<double> elapsed_seconds = end-start;
	float throughput;
	long input_buffer_total_bytes;
	float bits_throughput;

	elapsed_seconds = end-start;

	elapsed_time += elapsed_seconds.count() / noutput_items; // let's get a per-vector time

	current_iteration++;

	if (current_iteration == 100) {
		elapsed_time = elapsed_time / (float)iterations;
		throughput = d_num_channels * d_num_inputs  / elapsed_time;
		input_buffer_total_bytes = d_num_channels_x2;
		bits_throughput = 8 * input_buffer_total_bytes / elapsed_time;

		std::cout << std::endl << "GNURadio work() performance:" << std::endl;
		std::cout << "Elapsed time: " << elapsed_seconds.count() << std::endl;
		std::cout << "Timing Averaging Iterations: " << iterations << std::endl;
		std::cout << "Average Run Time:   " << std::fixed << std::setw(11) << std::setprecision(6) << elapsed_time << " s" << std::endl <<
				"Total throughput: " << std::setprecision(2) << throughput << " byte complex samples/sec" << std::endl <<
				"Individual stream throughput: " << std::setprecision(2) << throughput / d_num_inputs << " byte complex samples/sec" << std::endl <<
				"Projected processing rate: " << bits_throughput << " bps" << std::endl;
	}
#endif

	return noutput_items;
}

} /* namespace ata */
} /* namespace gr */


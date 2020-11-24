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
#ifdef _OPENMP
#include <omp.h>
#endif

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
		for (int i=0;i<num_inputs;i++) {
			std::deque<TaggedS8IQData> newQueue;
			queueList.push_back(newQueue);
		}
	}

	d_num_channels_x2 = 2*d_num_channels;

	d_pmt_seqnum = pmt::string_to_symbol("sample_num");

	d_block_name = pmt::string_to_symbol(identifier());

	// We'll set tags ourselves, since they may change if we have to realign.
	if (d_bypass)
		set_tag_propagation_policy(TPP_ONE_TO_ONE);
	else
		set_tag_propagation_policy(TPP_DONT);
}

/*
 * Our virtual destructor.
 */
SNAPSynchronizer_impl::~SNAPSynchronizer_impl()
{
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
	#pragma omp parallel for num_threads(2)
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

		/*
		int tag_val;
		int sync_val;
		bool tags_synchronized = true;

		// std::cout << "Synchronizer Bypass Mode. Initial stream identifiers:" << std::endl;
		for (int i=0;i<d_num_inputs;i++) {
			std::vector<gr::tag_t> tags;
			this->get_tags_in_window(tags, i, 0, noutput_items);

			tag_val = pmt::to_long(tags[0].value);

			if (i == 0) {
				sync_val = tag_val;
			}

			if (tags.size() > 0) {
				// std::cout << "Stream " << i << ": " << tag_val << std::endl;

				if (tag_val != sync_val)
					tags_synchronized = false;
			}
			else {
				std::cout << "Stream " << i << ": WARNING - No tag found." << std::endl;
			}
		}
		*/

		if (!tags_synchronized) {
			std::cout << "WARNING streams are not synchronized on start." << std::endl;
		}
		else {
			std::cout << "Streams starting synchronized." << std::endl;
		}
	}

	return noutput_items;
}

int
SNAPSynchronizer_impl::work(int noutput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items)
{
	gr::thread::scoped_lock guard(d_setlock);
	// If we're in bypass mode, just copy the data and on the first pass,
	// put up a notice on synchronization state.
	if (d_bypass) {
		return work_bypass_mode(noutput_items, input_items, output_items);
	}

	// Used in loops
	int cur_input;
	int cur_item;
	long cur_tag;

	// Fast track data pipeline: If the queues are empty and the tags match,
	// We're good just continue copying for speed.  No need to queue.
	if (queues_empty() && tags_match(noutput_items, input_items)) {
		/*
		static int print_counter=0;

		print_counter++;

		if (print_counter == 1) {
			std::cout << "Full alignment.  Fast-tracking data." << std::endl;
		}

		if (print_counter > 200) {
			print_counter = 0;
		}
		*/
		#pragma omp parallel for num_threads(2)
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

	// If we're here, we have synchronization issues to align
	// ------------------------------------------------------
	// First lets copy the tags to a faster array.
	// get_tags does a lot of vector work, so we can avoid it
	// by grabbing them once.  This saves us noutput_items many
	// additional calls to get_tags and all the vector work
	// further down since we need them here anyway to deterimine
	// If we need to queue the data or not.

	long tag_matrix[noutput_items][d_num_inputs];
	std::vector<gr::tag_t> tags;

	for (cur_input=0;cur_input<d_num_inputs;cur_input++) {
		this->get_tags_in_window(tags, cur_input, 0, noutput_items);

		for (cur_item=0;cur_item < tags.size();cur_item++) {
			cur_tag = pmt::to_long(tags[cur_item].value);

			tag_matrix[cur_item][cur_input] = cur_tag;
		}
	}

	// Queue up the data we received
	#pragma omp parallel for num_threads(2)
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
		for (cur_input=0;cur_input<d_num_inputs;cur_input++) {
			if (queueList[cur_input].size() == 0) {
				// std::cout << "[SNAP Synchronizer] At least one queue was empty.  Waiting till next call to process more data."  << std::endl;
				return filled_blocks;
			}
		}

		filled_blocks++;

		// Initialize with the first item
		lowest_num = queueList[0].front().get_tag();

		for (cur_input=1;cur_input<d_num_inputs;cur_input++) {
			cur_tag = queueList[cur_input].front().get_tag();

			if (cur_tag < lowest_num) {
				lowest_num = cur_tag;
			}
		}

		// Now we know the lowest number, so we know what sequence
		// number to synchronize on.

		// If the current queue has that number, output the data
		// Otherwise output zeros.  In either case, make sure to
		// Add the proper tag in for it.
		for (cur_input=0;cur_input<d_num_inputs;cur_input++) {
			char * output_stream = (char *)output_items[cur_input];

			cur_tag = queueList[cur_input].front().get_tag();

			if (cur_tag == lowest_num) {
				// Output the data
				TaggedS8IQData full_data = queueList[cur_input].front();
				queueList[cur_input].pop_front();

				pmt::pmt_t pmt_sequence_number =pmt::from_long(full_data.get_tag());
				add_item_tag(cur_input, nitems_written(0) + cur_item, d_pmt_seqnum, pmt_sequence_number,d_block_name);
				memcpy(&output_stream[d_num_channels_x2*cur_item], full_data.data_pointer(), d_num_channels_x2);
			}
			else {
				// Output a blank frame for this output
				pmt::pmt_t pmt_sequence_number =pmt::from_long(-1);
				add_item_tag(cur_input, nitems_written(0) + cur_item, d_pmt_seqnum, pmt_sequence_number,d_block_name);
				memset(&output_stream[d_num_channels_x2*cur_item], 0x00, d_num_channels_x2);
			}
		}
	}

	return noutput_items;
}

} /* namespace ata */
} /* namespace gr */


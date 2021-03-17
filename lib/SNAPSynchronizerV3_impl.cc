/* -*- c++ -*- */
/*
 * Copyright 2021 ghostop14.
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
#include "SNAPSynchronizerV3_impl.h"

namespace gr {
namespace ata {

SNAPSynchronizerV3::sptr
SNAPSynchronizerV3::make(int num_inputs, int num_channels)
{
	return gnuradio::get_initial_sptr
			(new SNAPSynchronizerV3_impl(num_inputs, num_channels));
}


/*
 * The private constructor
 */
SNAPSynchronizerV3_impl::SNAPSynchronizerV3_impl(int num_inputs, int num_channels)
: gr::block("SNAPSynchronizerV3",
		gr::io_signature::make(num_inputs, num_inputs, sizeof(char)*2*num_channels),
		gr::io_signature::make(num_inputs, num_inputs, sizeof(char)*2*num_channels)),
		d_num_inputs(num_inputs), d_num_channels(num_channels)
{
	d_synchronized = false;

	d_num_channels_x2 = 2*d_num_channels;

	// frame size is used to take a sixteen time frame block in as a single unit.
	// The * 16 here comes from the SNAP packet format.
	frame_size = sizeof(char)*d_num_channels_x2;

	d_pmt_seqnum = pmt::string_to_symbol("sample_num");

	d_block_name = pmt::string_to_symbol(identifier());

	pmt_sequence_number_minus_one =pmt::from_long(-1);

	tag_list = new unsigned long[d_num_inputs];

	set_tag_propagation_policy(TPP_DONT);

	// The SNAP outputs packets in 16-time step blocks.  So let's take advantage of that here.
	set_output_multiple(16);

	message_port_register_out(pmt::mp("sync"));
}

/*
 * Our virtual destructor.
 */
SNAPSynchronizerV3_impl::~SNAPSynchronizerV3_impl()
{
	delete[] tag_list;
}

void
SNAPSynchronizerV3_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
{
	for (int i=0;i< ninput_items_required.size();i++) {
		ninput_items_required[i] = noutput_items;
	}
}

int
SNAPSynchronizerV3_impl::general_work (int noutput_items,
		gr_vector_int &ninput_items,
		gr_vector_const_void_star &input_items,
		gr_vector_void_star &output_items)
{
	if (d_synchronized) {
		// If we're synchronized, just memcpy the results.  The SNAP source blocks
		// will fill in missing sequence numbers with zeros so they'll stay aligned.
		for (int cur_input=0;cur_input<d_num_inputs;cur_input++) {
			const char * input_stream = (const char *)input_items[cur_input];
			char * output_stream = (char *)output_items[cur_input];
			memcpy(output_stream, input_stream, noutput_items*frame_size);

			// And copy the tags
			std::vector<gr::tag_t> tags;
			this->get_tags_in_window(tags, cur_input, 0, noutput_items);

			int num_tags = tags.size();

			for (int i=0;i<num_tags;i++) {
				add_item_tag(cur_input, i, tags[i].key, tags[i].value, d_block_name);
			}
		}
		consume_each (noutput_items);

		// Tell runtime system how many output items we produced.
		return noutput_items;
	}
	else {
		gr::thread::scoped_lock guard(d_setlock);

		// We need to synchronize.
		// Each timestamp will always be t[n+1] = t[n] + 16
		// Look at each input channel's tags and find the highest starting tag.
		// Take the timestamp diff for each input, and that's how many items we need to consume.
		unsigned long highest_tag = 0;
		unsigned long first_input_timestamp;

		bool test_sync = true;

		// Find the highest tag in the first slot.
		for (int cur_input=0;cur_input<d_num_inputs;cur_input++) {
			std::vector<gr::tag_t> tags;
			// We only need the first tag.  No need to get them all.
			this->get_tags_in_window(tags, cur_input, 0, 1);

			unsigned long tag0 = pmt::to_long(tags[0].value);

			if (cur_input == 0) {
				first_input_timestamp = tag0;
			}
			else {
				if (tag0 != first_input_timestamp) {
					test_sync = false;
				}
			}

			// Save these so we don't have to get tags and iterate again.
			tag_list[cur_input] = tag0;

			if (tag0 > highest_tag) {
				highest_tag = tag0;
			}
		}

		if (test_sync) {
			// we're actually now synchronized.  We'll set our sync flag and process as if we came in sync'd
			d_synchronized = true;
			for (int cur_input=0;cur_input<d_num_inputs;cur_input++) {
				const char * input_stream = (const char *)input_items[cur_input];
				char * output_stream = (char *)output_items[cur_input];
				memcpy(output_stream, input_stream, noutput_items*frame_size);

				// And copy the tags
				std::vector<gr::tag_t> tags;
				this->get_tags_in_window(tags, cur_input, 0, noutput_items);

				int num_tags = tags.size();

				for (int i=0;i<num_tags;i++) {
					add_item_tag(cur_input, i, tags[i].key, tags[i].value,d_block_name);
				}
			}
			consume_each (noutput_items);

	        pmt::pmt_t pdu = pmt::cons( pmt::intern("synctimestamp"), pmt::from_long(highest_tag) );
			message_port_pub(pmt::mp("sync"),pdu);

			std::stringstream msg_stream;
			msg_stream << "Synchronized on timestamp " << highest_tag;
			GR_LOG_INFO(d_logger, msg_stream.str());

			// Tell runtime system how many output items we produced.
			return noutput_items;
		}

		// So we're still not sync'd so we need to figure out what we need to dump.
		unsigned long relative_tags = noutput_items - 16;

		for (int cur_input=0;cur_input<d_num_inputs;cur_input++) {

			// tag_diff will increment by 16 with the tag #'s so no need to divide by 16.
			// We need this # anyway.
			unsigned long tag_diff = highest_tag - tag_list[cur_input];

			if (tag_diff == 0) {
				consume(cur_input,0);
			}
			else {
				if (tag_diff > relative_tags) {
					consume(cur_input,noutput_items);
				}
				else {
					consume(cur_input, tag_diff);
				}
			}
		}

		// We're going to return 0 here so we don't forward any data along yet.  That won't happen till we're synchronized.
		return 0;
	}
}

} /* namespace ata */
} /* namespace gr */


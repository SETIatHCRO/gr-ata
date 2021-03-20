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

#ifndef INCLUDED_ATA_SNAPSYNCHRONIZERV3_IMPL_H
#define INCLUDED_ATA_SNAPSYNCHRONIZERV3_IMPL_H

#include <ata/SNAPSynchronizerV3.h>

namespace gr {
  namespace ata {

    class SNAPSynchronizerV3_impl : public SNAPSynchronizerV3
    {
     private:
		bool d_synchronized;
		int d_num_inputs;
		int d_num_channels;
    	int d_num_channels_x2;
    	int frame_size;

    	pmt::pmt_t d_pmt_seqnum;
    	pmt::pmt_t d_block_name;
    	pmt::pmt_t pmt_sequence_number_zero;

    	unsigned long *tag_list;

     public:
      SNAPSynchronizerV3_impl(int num_inputs, int num_channels);
      ~SNAPSynchronizerV3_impl();

      // Where all the action really happens
      void forecast (int noutput_items, gr_vector_int &ninput_items_required);

      int general_work(int noutput_items,
           gr_vector_int &ninput_items,
           gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items);

    };

  } // namespace ata
} // namespace gr

#endif /* INCLUDED_ATA_SNAPSYNCHRONIZERV3_IMPL_H */


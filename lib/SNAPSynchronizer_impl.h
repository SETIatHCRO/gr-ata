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

#ifndef INCLUDED_ATA_SNAPSYNCHRONIZER_IMPL_H
#define INCLUDED_ATA_SNAPSYNCHRONIZER_IMPL_H

#include <ata/SNAPSynchronizer.h>

namespace gr {
  namespace ata {

  class TaggedS8IQData {
  protected:
  	char *data=NULL;
  	size_t data_size=0;
  	long tag;

  public:
  	TaggedS8IQData() {
  		data_size = 0;
  		data = NULL;
  		tag = -1;
  	};

  	TaggedS8IQData(const TaggedS8IQData& src) {
  		if (src.data && (src.data_size > 0)) {
  			data_size = src.data_size;
  			data = new char[data_size];
  			memcpy(data,src.data,data_size*sizeof(char));
  			tag = src.tag;
  		}
  	};

  	TaggedS8IQData(const char *src_data,size_t src_size, long src_tag) {
  		// Pulled off safeties to get a speedup
  		data_size = src_size;
  		data = new char[data_size];

  		// This extra if allows me to create a block of specified size
  		// of zero's if needed.
  		if (src_data)
  			memcpy(data,src_data,data_size*sizeof(char));
  		else
  			memset(data,0x00,data_size*sizeof(char));

  		tag = src_tag;
  	};

  	TaggedS8IQData& operator= ( const TaggedS8IQData & src) {
  		if (src.data && (src.data_size > 0)) {
  			data_size = src.data_size;
  			data = new char[data_size];
  			memcpy(data,src.data,data_size*sizeof(char));
  		}
  		else {
  			if (data) {
  				delete[] data;
  				data = NULL;
  				data_size = 0;
  			}
  		}

		tag = src.tag;

  		return *this;
  	};

  	long get_tag() { return tag; };

  	size_t get_data_size() { return data_size; };

  	virtual bool empty() {
  		if (data_size == 0) {
  			return true;
  		}
  		else {
  			return true;
  		}
  	};

  	virtual char * data_pointer() { return data; };

  	virtual void store(char *src_data,size_t src_size, long src_tag) {
  		if (!src_data || (src_size == 0)) {
  			// If we requested NULL or zero size, clear our buffer.
  			if (data) {
  				delete[] data;
  				data = NULL;
  			}
  			data_size = 0;
  			tag = src_tag;
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
  			data = new char[data_size];
  		}

  		// We can get here if the buffer size didn't change,
  		// or a new data block was created.  In either case
  		// data_size should be correct.
  		memcpy(data,src_data,data_size*sizeof(char));
		tag = src_tag;
  	};


  	virtual size_t size() { return data_size; };

  	virtual ~TaggedS8IQData() {
  		if (data) {
  			delete[] data;
  		}
  	};
  };

    class SNAPSynchronizer_impl : public SNAPSynchronizer
    {
     private:
    	int d_num_inputs;
    	int d_num_channels;
    	bool d_bypass;
    	int d_num_channels_x2;

    	pmt::pmt_t d_pmt_seqnum;
    	pmt::pmt_t d_block_name;

    	pmt::pmt_t pmt_sequence_number_minus_one;
    	std::vector<std::deque<TaggedS8IQData>> queueList;

    	bool queues_empty() {
    		for (int i=0;i<d_num_inputs;i++) {
    			if (queueList[i].size() > 0) {
    				return false;
    			}
    		}

    		return true;
    	};

    	bool tags_match(int noutput_items, gr_vector_const_void_star &input_items);

     public:
      SNAPSynchronizer_impl(int num_inputs, int num_channels, bool bypass);
      ~SNAPSynchronizer_impl();

      // Where all the action really happens
      int work_bypass_mode(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );

      int work(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );
    };

  } // namespace ata
} // namespace gr

#endif /* INCLUDED_ATA_SNAPSYNCHRONIZER_IMPL_H */


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

#ifndef INCLUDED_ATA_SNAPSYNCHRONIZERV3_H
#define INCLUDED_ATA_SNAPSYNCHRONIZERV3_H

#include <ata/api.h>
#include <gnuradio/block.h>

namespace gr {
  namespace ata {

    /*!
     * \brief <+description of block+>
     * \ingroup ata
     *
     */
    class ATA_API SNAPSynchronizerV3 : virtual public gr::block
    {
     public:
      typedef std::shared_ptr<SNAPSynchronizerV3> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of ata::SNAPSynchronizerV3.
       *
       * To avoid accidental use of raw pointers, ata::SNAPSynchronizerV3's
       * constructor is in a private implementation
       * class. ata::SNAPSynchronizerV3::make is the public interface for
       * creating new instances.
       */
      static sptr make(int num_inputs, int num_channels);
    };

  } // namespace ata
} // namespace gr

#endif /* INCLUDED_ATA_SNAPSYNCHRONIZERV3_H */


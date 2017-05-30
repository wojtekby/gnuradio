/* -*- c++ -*- */
/* Copyright 2015-2016 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <volk/volk.h>
#include <gnuradio/digital/header_format_default.h>
#include <gnuradio/math.h>

namespace gr {
  namespace digital {

    header_format_default::sptr
    header_format_default::make(const std::string &access_code,
                                int threshold)
    {
      return header_format_default::sptr
        (new header_format_default(access_code, threshold));
    }

    header_format_default::header_format_default(const std::string &access_code,
                                                 int threshold)
      : header_format_base(),
        d_data_reg(0), d_mask(0), d_threshold(0),
        d_pkt_len(0), d_pkt_count(0), d_nbits(0)
    {
      if(!set_access_code(access_code)) {
        throw std::runtime_error("header_format_default: Setting access code failed");
      }

      set_threshold(threshold);
    }

    header_format_default::~header_format_default()
    {
    }

    bool
    header_format_default::set_access_code(const std::string &access_code)
    {
      d_access_code_len = access_code.length();	// # of bits in the access code

      if(access_code.size() > 64) {
        return false;
      }

      // set len top bits to 1.
      d_mask = ((~0ULL) >> (64 - d_access_code_len));

      d_access_code = 0;
      for(unsigned i = 0; i < d_access_code_len; i++) {
        d_access_code = (d_access_code << 1) | (access_code[i] & 1);
      }

      return true;
    }

    unsigned long long
    header_format_default::access_code() const
    {
      return d_access_code;
    }

    void
    header_format_default::set_threshold(unsigned int thresh)
    {
      if(d_threshold > d_access_code_len) {
        throw std::runtime_error("header_format_default: Cannot set threshold " \
                                 "larger than the access code length.");
      }
      d_threshold = thresh;
    }

    unsigned int
    header_format_default::threshold() const
    {
      return d_threshold;
    }

    bool
    header_format_default::format(int nbytes_in,
                                  const unsigned char *input,
                                  pmt::pmt_t &output,
                                  pmt::pmt_t &info)
    {
      uint8_t* bytes_out = (uint8_t*)volk_malloc(header_nbytes(),
                                                 volk_get_alignment());

      header_buffer header(bytes_out);
      header.add_field64(d_access_code, d_access_code_len);
      //header.add_field64(d_access_code, d_access_code_len);
      //printf("%d Header format: d_access_code: %d, d_access_code_len: %d, d_nbytes_in: %d", d_access_code, d_access_code_len, nbytes_in);
      header.add_field16((uint16_t)(nbytes_in));
      header.add_field16((uint16_t)(nbytes_in));

      // Package output data into a PMT vector
      output = pmt::init_u8vector(header_nbytes(), bytes_out);

      // Creating the output pmt copies data; free our own here.
      volk_free(bytes_out);

      return true;
    }

    bool
    header_format_default::parse(int nbits_in,
                                 const unsigned char *input,
                                 std::vector<pmt::pmt_t> &info,
                                 int &nbits_processed)
    {
      nbits_processed = 0;
      printf("Inside header_format_default::parse \n");
      //printf("Header nbits_in: %d \n", nbits_in);
      //printf("Header nbits_processed: %d \n", nbits_processed);

      while(nbits_processed < nbits_in) {
        if(d_state==STATE_HAVE_SYNC) printf("First header decoded \n");
        switch(d_state) {
	case STATE_SYNC_SEARCH:    // Look for the access code correlation
        printf("STATE_SYNC_SEARCH \n"); 
	  while(nbits_processed < nbits_in) {
            // shift in new data
            d_data_reg = (d_data_reg << 1) | ((input[nbits_processed++]) & 0x1);

            // compute hamming distance between desired access code and current data
            uint64_t wrong_bits = 0;
            uint64_t nwrong = d_threshold+1;

            wrong_bits = (d_data_reg ^ d_access_code) & d_mask;
            volk_64u_popcnt(&nwrong, wrong_bits);

     //       printf("Header wrong_bits: %" PRIu64  " \n", wrong_bits);
    //        std::cout << "Header wrong_bits: " << wrong_bits << "\n";
       //     printf("Header d_threshold: %d \n", d_threshold);  

            if(nwrong <= d_threshold) {
              enter_have_sync();
              break;
            }
          }
          break;

	case STATE_HAVE_SYNC:
         printf("STATE_HAVE_SYNC \n"); 
        // printf("header.bits():  %d, d_access_code_len:  %d \n", header_nbits(), d_access_code_len);
	  while(nbits_processed <= nbits_in) {    // Shift bits one at a time into header
            d_hdr_reg.insert_bit(input[nbits_processed++]);
            //if(d_hdr_reg.length() == (header_nbits()-d_access_code_len)) {
              if(d_hdr_reg.length() == 64) {
	      // we have a full header, check to see if it has been received properly
	      if(header_ok()) {
                //printf("HEADER OK \n");
                int payload_len = header_payload();
		enter_have_header(payload_len);
               // printf("Header payload len: %d \n", payload_len);
                info.push_back(d_info);
                return true;
              }
	      else {
		enter_search();    // bad header
                return false;
              }
              break;
            }
          }
          break;
        }
      }

      return false;
    }

    size_t
    header_format_default::header_nbits() const
    {
      return 2*d_access_code_len + 8*2*sizeof(uint16_t);
    }

    inline void
    header_format_default::enter_have_sync()
    {
      d_state = STATE_HAVE_SYNC;
      d_hdr_reg.clear();
    }

    inline void
    header_format_default::enter_have_header(int payload_len)
    {
      d_state = STATE_SYNC_SEARCH;
      d_pkt_len = payload_len;
      d_pkt_count = 0;
    }

    bool
    header_format_default::header_ok()
    {
      // confirm that two copies of header info are identical
      uint16_t len0 = d_hdr_reg.extract_field16(0, 16);
      uint16_t len1 = d_hdr_reg.extract_field16(16, 16);
      return (len0 ^ len1) == 0;
    }

    int
    header_format_default::header_payload()
    {
      uint16_t len = d_hdr_reg.extract_field16(0, 16);

      d_info = pmt::make_dict();
      d_info = pmt::dict_add(d_info, pmt::intern("payload symbols"),
                             pmt::from_long(8*len));
      return static_cast<int>(len);
    }

  } /* namespace digital */
} /* namespace gr */

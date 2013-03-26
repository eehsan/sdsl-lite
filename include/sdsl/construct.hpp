/* sdsl - succinct data structures library
    Copyright (C) 2012 Simon Gog

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see http://www.gnu.org/licenses/ .
*/
/*! \file construct.hpp
    \brief construct.hpp contains methods to construct indexes (compressed suffix arrays and trees).
	\author Simon Gog
*/

#ifndef INCLUDED_SDSL_CONSTRUCT
#define INCLUDED_SDSL_CONSTRUCT

#include "sdsl_concepts.hpp"
#include "int_vector.hpp"
#include "construct_lcp.hpp"
#include "construct_bwt.hpp"
#include "construct_sa.hpp"
#include <string>

namespace sdsl
{

template<class int_vector>
bool contains_no_zero_symbol(const int_vector& text, const std::string& file)
{
    for (int_vector_size_type i=0; i < text.size(); ++i) {
        if ((uint64_t)0 == text[i]) {
            throw std::logic_error((std::string("Error: File \"")+file+std::string("\" contains zero symbol.")).c_str());
            return false;
        }
    }
    return true;
}

template<class int_vector>
void append_zero_symbol(int_vector& text)
{
    text.resize(text.size()+1);
    text[text.size()-1] = 0;
}


template<class t_index>
void construct(t_index& idx, std::string file, uint8_t num_bytes=0)
{
    tMSS file_map;
    cache_config config;
    if (is_ram_file(file)) {
        config.dir = "@";
    }
    construct(idx, file, config, num_bytes);
}

template<class t_index, class t_data>
void construct_im(t_index& idx, t_data data, uint8_t num_bytes=0)
{
    std::string tmp_file = ram_file_name(util::to_string(util::pid())+"_"+util::to_string(util::id()));
    util::store_to_file(data, tmp_file);
    construct(idx, tmp_file, num_bytes);
    ram_fs::remove(tmp_file);
}

//! Constructs an index object of type t_index for a text stored on disk.
/*!
 * \param idx       	t_index object.  Any sdsl suffix array of suffix tree.
 * \param file      	Name of the text file. The representation of the file
 *                  	is dependent on the next parameter.
 * \
 * \param num_bytes 	If `num_bytes` equals 0, the file format is a serialized
 *				    	int_vector<>. Otherwise the file is interpreted as sequence
 *                  	of `num_bytes`-byte integer stored in big endian order.
 */
template<class t_index>
void construct(t_index& idx, const std::string& file, cache_config& config, uint8_t num_bytes=0)
{
    // delegate to CSA or CST construction
    typename t_index::index_category 		index_tag;
    construct(idx, file, config, num_bytes, index_tag);
}

// Specialization for WTs
template<class t_index>
void construct(t_index& idx, const std::string& file, cache_config& config, uint8_t num_bytes, wt_tag)
{
    int_vector<t_index::alphabet_category::WIDTH> text;
    util::load_vector_from_file(text, file, num_bytes);
    std::string tmp_key = util::to_string(util::pid())+"_"+util::to_string(util::id());
    std::string tmp_file_name = util::cache_file_name(tmp_key, config);
    util::store_to_file(text, tmp_file_name);
    util::clear(text);
    int_vector_file_buffer<t_index::alphabet_category::WIDTH> text_buf(tmp_file_name);
    {
        t_index tmp(text_buf, text_buf.int_vector_size);
        idx.swap(tmp);
    }
    sdsl::remove(tmp_file_name);
}

// Specialization for CSAs
template<class t_index>
void construct(t_index& idx, const std::string& file, cache_config& config, uint8_t num_bytes, csa_tag)
{
    const char* KEY_TEXT = key_text_trait<t_index::alphabet_category::WIDTH>::KEY_TEXT;
    const char* KEY_BWT  = key_bwt_trait<t_index::alphabet_category::WIDTH>::KEY_BWT;
    typedef int_vector<t_index::alphabet_category::WIDTH> text_type;
    {
        // (1) check, if the text is cached
        if (!util::cache_file_exists(KEY_TEXT, config)) {
            text_type text;
            util::load_vector_from_file(text, file, num_bytes);
            if (contains_no_zero_symbol(text, file)) {
                append_zero_symbol(text);
                util::store_to_cache(text, KEY_TEXT, config);
            }
        }
        util::register_cache_file(KEY_TEXT, config);
    }
    {
        // (2) check, if the suffix array is cached
        if (!util::cache_file_exists(constants::KEY_SA, config)) {
            construct_sa<t_index::alphabet_category::WIDTH>(config);
        }
        util::register_cache_file(constants::KEY_SA, config);
    }
    {
        //  (3) construct BWT
        if (!util::cache_file_exists(KEY_BWT, config)) {
            construct_bwt<t_index::alphabet_category::WIDTH>(config);
        }
        util::register_cache_file(KEY_BWT, config);
    }
    {
        t_index tmp(config);
        idx.swap(tmp);
    }
    if (config.delete_files) {
        util::delete_all_files(config.file_map);
    }
}

// Specialization for CSTs
template<class t_index>
void construct(t_index& idx, const std::string& file, cache_config& config, uint8_t num_bytes, cst_tag)
{
    const char* KEY_TEXT = key_text_trait<t_index::alphabet_category::WIDTH>::KEY_TEXT;
    const char* KEY_BWT  = key_bwt_trait<t_index::alphabet_category::WIDTH>::KEY_BWT;
    csa_tag csa_t;
    {
        // (1) check, if the compressed suffix array is cached
        typename t_index::csa_type csa;
        if (!util::cache_file_exists(util::class_to_hash(csa), config)) {
            cache_config csa_config(false, config.dir, config.id, config.file_map);
            construct(csa, file, csa_config, num_bytes, csa_t);
            config.file_map = csa_config.file_map;
            util::store_to_cache(csa, util::class_to_hash(csa), config);
        }
        util::register_cache_file(util::class_to_hash(csa), config);
    }
    {
        // (2) check, if the longest common prefix array is cached
        util::register_cache_file(KEY_TEXT, config);
        util::register_cache_file(KEY_BWT, config);
        util::register_cache_file(constants::KEY_SA, config);
        if (!util::cache_file_exists(constants::KEY_LCP, config)) {
            construct_lcp_PHI<t_index::alphabet_category::WIDTH>(config);
        }
        util::register_cache_file(constants::KEY_LCP, config);
    }
    {
        t_index tmp(config);
        tmp.swap(idx);
    }
    if (config.delete_files) {
        util::delete_all_files(config.file_map);
    }
}

} // end namespace sdsl
#endif

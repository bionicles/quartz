/* Copyright (c) 2013-2014 Y. William Yu. Released under CC0 1.0 Universal. */
/* jumpgate.cpp */

#include "global.h"
#include "jumpgate.h"
#include <parallel/algorithm>

template <typename T>
void FreeAll( T & t ) {
    T tmp;
    t.swap( tmp );
}

void read_entry_database::dictionary_load(std::string dict_fn, std::vector<uint32_t> &dict_vec, std::vector<uint32_t> &jumpgate) {
    
	// Create jumpgate with high bits pointing to low bits
	//jumpgate=create_jumpgate(&dict[0], num_dict_entries);
	const uint32_t neg_one = -1;
	std::cerr << "Allocating jumpgate ... ";
	//std::vector<uint32_t> jg;
	uint64_t jg_size = (1UL << 32);
	jumpgate.resize(jg_size, neg_one);
    
    // Code here for better memory management
    // Release memory from dict and reread from disk
	std::vector<uint64_t> dict;
    dict.resize(BUFFER_SIZE);
    
    // Create low_bits & populate waypoints
	std::cerr << "Creating truncated dictionary / populating waypoints ... ";

    unsigned long num_dict_entries = 0;
	std::ifstream f_dict (dict_fn, std::ios::in | std::ios::binary);
	if (f_dict.good()) {
		f_dict.read( (char*) &num_dict_entries, sizeof(num_dict_entries));
        dict_vec.resize(num_dict_entries);

        long remaining_entries = num_dict_entries;
        long i = 0;
        long buffer_num = 0;
        while (remaining_entries > 0) {
            buffer_num = (BUFFER_SIZE<remaining_entries)?BUFFER_SIZE : remaining_entries;
            remaining_entries = remaining_entries - buffer_num;
		    f_dict.read( (char*) (&dict[0]), buffer_num*sizeof(readseq));
            int j = 0;
            while (j < buffer_num) {
                // Creating low_bits
                dict_vec[i]=low_bits(dict[j]);
                // Populating waypoints
                uint32_t curr_high = high_bits(dict[j]);
                if (jumpgate[curr_high]>i)
                    jumpgate[curr_high]=i;
                // Increment both counters
                i++;
                j++;
            }
        }
        f_dict.close();
        assert(i == num_dict_entries);
	} else {
		std::cerr << "Dictionary could not be opened. Please remedy." << std::endl;
		exit(-25);
	}

	std::cerr << "Filling in remainder ..." << std::endl;
	uint32_t curr_high = num_dict_entries;
	for (int64_t k = jg_size - 1; k>=0; --k) {
		if (jumpgate[k]==neg_one)
			jumpgate[k]=curr_high;
		else
			curr_high = jumpgate[k];
	} 

    //jumpgate=jg;
//	for (unsigned int i=0; i<num_dict_entries; ++i) {
//		dict_vec[i]=low_bits(dict[i]);
//	}
	//munmap(dict_file,num_dict_entries + 1);
}

// Note that dict_fn must contain a dictionary (preferably sorted) of 64-bit integers, with first value being the number of remaining intgers in the file (i.e. size of dictionary)
// Additionally, [dict_fn].swapped must contain the same dictionary, with low and high 32-bits swapped, in the same format, and sorted accordingly
read_entry_database::read_entry_database (std::string dict_fn, bool lm) {
    lowmem = lm;
	
	dictionary_load(dict_fn, dict_low, jumpgate_low);
	
	std::string dict_swapped_fn = dict_fn;
	dict_swapped_fn.append(".swapped");

    if (lowmem) {
        std::cerr << "Running in low memory mode; not loading swapped dictionary." << std::endl;
    } else {
    	dictionary_load(dict_swapped_fn, dict_high, jumpgate_high);
    }
}

inline readseq mask(int i) {return ~(3UL << (2*i));}
inline readseq add(int i,readseq j) {return j << (2*i); }
inline readseq testers(int i, int j, readseq work) {return (work & mask(i)) + add(i,j);}

// Returns a vector V with non-negative substitutions denoting positions of all
// new SNPs (i.e. some substitution at that point would make work match something
// in the dictionary). If there is a substitution at point i, then V[i]=i.
// Additionally, if there is an exact match, we use V[32]=-100). Non-specified points
// will be denoted with a -1. Thus, this will generally be a length 33 vector.
//
// Additionally, if there are no exact matches or Hamming neighbors in the dictionary,
// then we'll return an empty vector.
std::vector<int> read_entry_database::check_hamming_neighbors(const readseq work) {
	std::vector<int> ans_vec;
	ans_vec.resize(33,-1);
	if (count_low(work)==1) {
		ans_vec[32]=-100;
	}
	readseq test;
	// We get the bounds explicitly only once, and then do binary search. This is equivalent to calling red.count many times (this code is copied from the count function)
	{
		const uint32_t *x_high = ((uint32_t*) &work)+1;
		const uint32_t lb = jumpgate_low[*x_high];
		uint32_t ub;
		if (*x_high == (1UL << 32)-1)
			ub = dict_low.size();
		else
			ub = jumpgate_low[*x_high+1];
		for (int i=0; i<16; ++i) {
			for (int j=0; j<4; ++j) {
				test = testers(i,j,work);
				//if((test!=work)&&count(test)) {
				if((test!=work)&& \
					std::binary_search(&(dict_low[lb]),&(dict_low[ub]),low_bits(test))) {
					ans_vec[i]=i;
					break;
				}
			}
		}
	}
	// Again preventing duplication of work for count_swapped
    if (lowmem) {
		for (int i=16; i<32; ++i) {
			for (int j=0; j<4; ++j) {
				test = testers(i,j,work);
				if((test!=work)&&count_high(test)) {
					ans_vec[i]=i;
					break;
				}
			}
		}
    }
    else
	{
		const uint32_t *x_low =  ((uint32_t*) &work);
		const uint32_t lb = jumpgate_high[*x_low];
		uint32_t ub;
		if (*x_low == (1UL << 32)-1)
			ub = dict_high.size();
		else
			ub = jumpgate_high[*x_low+1];
		for (int i=16; i<32; ++i) {
			for (int j=0; j<4; ++j) {
				test = testers(i,j,work);
				if((test!=work)&& \
					std::binary_search(&(dict_high[lb]),&(dict_high[ub]),high_bits(test))) {
					ans_vec[i]=i;
					break;
				}
			}
		}
	}
	for (int i=0; i<33; ++i) {
		if (ans_vec[i]!=-1) {
			return ans_vec;
		}
	}
	std::vector<int> empty;
	return empty;
}

// Find out whether an element is in the read_entry_database
//int red_count (const read_entry_database & red, uint64_t x);
// Find out whether an element is in the read_entry_database
int read_entry_database::count_low (const uint64_t x)  const {
	const uint32_t * x_low = ((uint32_t*) &x);
	const uint32_t * x_high = ((uint32_t*) &x)+1;
	//uint32_t x_low = low_bits(x);
//	const uint32_t x_high = high_bits(x);
	const uint32_t lb = jumpgate_low[*x_high];
	uint32_t ub;
	if (*x_high == (1UL << 32)-1)
		ub = dict_low.size();
	else
		ub = jumpgate_low[*x_high+1];
//	if (ub > dict.size())
//		ub = dict.size();
	//return std::binary_search(&(dict[lb]),&(dict[ub]),x);
	return std::binary_search(&(dict_low[lb]),&(dict_low[ub]),*x_low);
//	return std::binary_search(dict.begin(),dict.end(),x);
}

// Does the same thing as count, but looks it up instead in the swapped copy
int read_entry_database::count_high (const uint64_t x)  const {
    
    if (lowmem) {
        return count_low(x);
    }

	const uint32_t * x_low = ((uint32_t*) &x);
	const uint32_t * x_high = ((uint32_t*) &x)+1;
	//const uint32_t x_low = low_bits(x);
	const uint32_t lb = jumpgate_high[*x_low];
	//uint32_t ub = jumpgate_swapped[*x_high+1];
	uint32_t ub;
	if (*x_low == (1UL << 32)-1)
		ub = dict_high.size();
	else
		ub = jumpgate_high[*x_low+1];
//	if (ub > dict_swapped.size())
//		ub = dict_swapped.size();
	return std::binary_search(&(dict_high[lb]),&(dict_high[ub]),*x_high); 
//	return std::binary_search(dict.begin(),dict.end(),x);
}



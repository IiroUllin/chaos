

	/*------------------------------------------------------+
	 |														|
	 |	pseudoRNG + related utilities v0.2 (Nov 5, 2025)	|
	 |														|
	 +------------------------------------------------------*/



#ifndef __CHAOS_HEADER__
#define __CHAOS_HEADER__


#include <cstdint>					//	Fixed width integers; requires C++11
#include "generic.h"				//	My generic definitions


namespace chs {

	//
	//	128bit structure to contain RNG state
	//
	class RNG {
		private:
			uint64_t state[2] = {0};	//	NOTE: xoroshiro128+ does not change 0
			union {
				uint64_t i64[2];		//	A couple qwords to store stuff,
				fp64_t f64[2];			//	e.g., Gaussian random variables in Box-Muller
			} cache = {.i64 = {NaN}};	//	Initialize with NaN			
		public:
			//	Hashing function for seeding the RNG state
			//	<data> is supposed to be 64 bit (8 byte) aligned; <length> >= 8 (in bytes)
			//	WARNING: data is consumed in 64 bit chunks
			//	if <length> is not divisible by 8, the leftovers are not utilized
			void hash(const void* data, std::size_t length);

			uint64_t int64();			//	random 64 bits
			fp64_t U12();				//	Uniform[1,2)
			fp64_t U01();				//	Uniform[0,1]
			fp64_t N01();				//	Gaussian with 0 mean and variance 1 via rejection sampling
			fp64_t n01();				//	Another N(0,1) Gaussian via Box-Muller
	};
	

	/*------------------------------------------------------------------------------+
	 |																				|
	 | 	Mixers: fast hash functions producing "random" output from the <state>		|
	 |	These are not necessarily best for RNG when used to produce a sequence		|
	 |	of numbers: their primary purpose is to maximize bit mixing (avalanching)	|
	 |	after single use...															|
	 |																				|
	 +------------------------------------------------------------------------------*/


	uint8_t		mix8	(uint8_t state);
	uint16_t	mix16	(uint16_t state);
	uint32_t	mix32	(uint32_t state);
	uint64_t	mix64	(uint64_t state);
	


}

#endif

#include "chaos.h"
#include <cmath>
#include <cassert>		//	Assertions for debugging




/*----------------------------------------------------------------------------------+
 |																					|
 |	Code from xoroshiro128+ by D. Blackman and S. Vigna (2016-18); public domain	|
 |																					|
 +----------------------------------------------------------------------------------*/


//
//	Cyclic rotate left; amount should be less than 64
//
static inline uint64_t rotl(const uint64_t state, const int amount) {
	return (state << amount) | (state >> (64 - amount));
}

//
//	Generate random 64 bits via xoroshiro128+
//	The <result> is calculated after the state change, not before, as in the original implementation
//	NOTE: the <state[0,1]> are modified 
//
static inline uint64_t next(uint64_t* state) {
	uint64_t s0 = state[0];
	uint64_t s1 = state[1];

	s1 ^= s0;
	s0 = rotl(s0, 24) ^ s1 ^ (s1 << 16);
	s1 = rotl(s1, 37);

	state[0] = s0;
	state[1] = s1;

	return s0 + s1;
}




//
//	Just some LCG; not optimized
//
uint8_t chs::mix8(uint8_t state) {
    return state * 97 + 111;
}


//
//	Just some LCG; not optimized
//
uint16_t chs::mix16(uint16_t state) {
    return state * 4093 + 32719;
}


//
//	Prospector, https://github.com/skeeto/hash-prospector
//	Attribution: C. Wellon; public domain
//	Check the link for even faster mixers with slightly higher bias
//
uint32_t chs::mix32(uint32_t state) {
	state++;		//	Not needed, but prevents 0->0 and slightly improves mixing
    state = (state ^ (state >> 17)) * 0xed5ad4bbU;
    state = (state ^ (state >> 11)) * 0xac4c1b51U;
    state = (state ^ (state >> 15)) * 0x31848babU;
    return state ^ (state >> 14);
}


//
//	SplitMix64 algorithm (used, e.g., in Java)
//	Attribution: S. Vigna, 2015; public domain
//
uint64_t chs::mix64(uint64_t state) {
	//	NOTE: when SplitMix64() is used as an RNG, 
	//	the new <state> is the incremented value 
	//	from the first line, not the returned value
	state += 0x9e3779b97f4a7c15U;
	state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9U;
	state = (state ^ (state >> 27)) * 0x94d049bb133111ebU;
	return state ^ (state >> 31);
}


//
//	128 bit hashing function:
//	Takes <length> bytes from <data>
//	and hashes it into the chs::RNG.state[0,1] (two qwords) 
//	NOTE: <data> must be aligned and <length> should contain at least one 64 bit chunk
//
void chs::RNG::hash(const void* data, std::size_t length){
	assert(length >= 8);						//	<data> must contain at least one 64 bit block
	length >>= 3;								//	original length is in bytes, this one is in qwords (64 bits)
	assert(!(uintptr_t(data) & 0b111));			//	Last 3 bits must be 0 for int64-aligned data
	uint64_t* qw_data = (uint64_t*) data;		//	Convert <data> into uint64 pointer
	int index = 0;								//	Currently updated entry of the <state>

	state[0] = 0;	state[1] = 0;				//	Reset the state first
	//	NOTE: this will erase the <state> even if <length> < 8 and no data is hashed

	//	NOTE: The cache is also cleared
	cache = {NaN};
		
	while (length > 0) {
		state[index] ^= chs::mix64(*qw_data);	//	Mix current data chunck and merge it into the <state[index]> 
		index ^= 1;								//	0->1 and 1->0
		//	Mix its mixture into the "other" state qword
		//	NOTE: the constant here is just random and not optimized in any way
		state[index] ^= chs::mix64(*qw_data ^ 0x543210123456789U);	
		qw_data++; length--;
	}
}

//
//	Generate random 64 bits (xoroshiro128+ algorithm)
//
uint64_t chs::RNG::int64(){
	return next(state);
}



//
//	Genereate a quick Uniform[0,1] fp64
//	NOTE: 1.0 can still be generated due to roundoff errors, despite max(next(state))=2^64-1
//	because of 2^(-53) machine precision.
//	Additionally, even though FP near 0 are spaced finer than 2^(-52), 
//	this remains the sampling precision, i.e., once the "bins" get close enough to this limit
//	the quality of the uniform distribution near 0 will diminish.
//	In particular, -log(u01) is a poor approximation to exponential distribution 
//	already near values around 30 (or even earlier, depending on required precision)...
//	
fp64_t chs::RNG::U01(){
	return 5.42101086242752217003726400434970855712890625e-20 * next(state);	//	2^(-64)
}



//
//	Genereate Uniform[1,2) fp64
//	NOTE: 1 may be generated if next() produces 0; 2 can never be generated due to IEEE754 FP representation
//	This generator has the same roundoff issues as the one above, however it will not generate 2,
//	thus -log(2.0-u12) will produce a quick exponential random variable without risk of NaN.
//	
fp64_t chs::RNG::U12(){
	union {
		uint64_t i64;
		fp64_t f64;
	} result;				//	Overlap int64 and fp64 values

	//	Use the highest 52 bits for mantissa (shift them to the lowest 52 bits of i64)
	//	Set the top 12 bits to 001111111111 which creates "+" sign and exponent 1023 (0 after bias)
	result.i64 = (next(state) >> 12) | 0x3FF0000000000000U;
	return result.f64;		//	Return the corresponding FP value
}


//
//	Genereate N(0,1) fp64 via Box-Muller algorithm
//
fp64_t chs::RNG::n01(){
	if (cache.i64[0] != NaN) {				//	If some value is cached, return it... 
		fp64_t result = cache.f64[0];
		cache.i64[0] = NaN;					//	...and clear the cache
		return result;
	};
	fp64_t phi = 2.0 * PI * U01();					//	angular part,
	fp64_t z = sqrt(-2.0 * log(2.0 - U12()));	//	radial part; use 2-U12() to avoid 0
	cache.f64[0] = z * cos(phi);				//	Store one...
	return z * sin(phi);						//	...return the other
}


//
//	Genereate N(0,1) fp64 via rejection sampling algorithm in a circle
//
fp64_t chs::RNG::N01(){
	if (cache.i64[0] != NaN) {				//	If some value is cached, return it... 
		fp64_t result = cache.f64[0];
		cache.i64[0] = NaN;					//	...and clear the cache
		return result;
	};

	fp64_t x, y, r;
	do {
		x = 2.0 * U01() - 1.0;
		y = 2.0 * U01() - 1.0;
		r = x * x + y * y;
	} while ((r == 0.0) || (r > 1.0));

	//	(x, y) is now uniform in the unit circle; r ~ U(0,1); -log(r) ~ Exp(1)				

	r = 1.0 / sqrt(r); x *= r; y *= r;		//	x and y now contain cosine and sine of a random angle in (0,2π)
	r = sqrt(4.0 * log(r));					//	r is now the radial part of 2d N(0,1)

	cache.f64[0] = x * r;					//	Store one...
	return y * r;							//	...return the other
	//return E;
}




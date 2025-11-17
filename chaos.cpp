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
//	Evolve the <state> using xoroshiro128+
//	The <result> is is the sum <state[0] + state[1]> 
//
static inline uint64_t next(uint64_t* state) {
	uint64_t s0 = state[0];
	uint64_t s1 = state[1];

	s1 ^= s0;

	s0 = rotl(s0, 24) ^ s1 ^ (s1 << 16);
	s1 = rotl(s1, 37);

	state[0] = s0; state[1] = s1;

	return s0 + s1;
}

//
//	xoroshiro128+ jump function, equivalent to 2^64 calls to next()
//
static void jump(uint64_t* state) {
	static const uint64_t JUMP[] = {0xdf900294d8f554a5U, 0x170865df4b3201fcU};

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	for(int i = 0; i < 2; i++)
		for(int b = 0; b < 64; b++) {
			if (JUMP[i] & uint64_t(1) << b) {	//	...maybe makes this endianess-indifferent?
				s0 ^= state[0];
				s1 ^= state[1];
			}
			::next(state);						//	Evolve the state
		}

	state[0] = s0;
	state[1] = s1;
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
//	Convert uint64 into (0,1] fp64
//	0 is avoided to be able to take logarithms for Exp-distributed random variables
//
static inline fp64_t chs::U01(uint64_t bits) {
	return ldexp((fp64_t) bits, -64);
}



//
//	128 bit hashing function.
//	Takes <length> bytes from <data>
//	and hashes it into i64[0,1] (two qwords)
//	remaining streams are then generated using the xoroshiro128+ jump function.
//	NOTE: <data> must be aligned and <length> should contain at least one 64 bit chunk
//	NOTE: this will erase the <state> and <cache> even if <length> < 8 and no data is hashed
//
void chs::RNG::hash(const void* data, std::size_t length){
	assert(length >= 8);						//	<data> must contain at least one 64 bit block
	length >>= 3;								//	original length is in bytes, this one is in qwords (64 bits)
	assert(!(uintptr_t(data) & 0b111));			//	Last 3 bits must be 0 for int64-aligned data
	uint64_t* qw_data = (uint64_t*) data;		//	Convert <data> into uint64 pointer
	int i = 0;									//	Currently updated qword of the first <state>

	i64[0] = 0;	i64[1] = 0;						//	Clear the bits of the 1st bit stream

	while (length > 0) {
		i64[i] ^= chs::mix64(*qw_data);			//	Mix current data chunk and merge it into the <state> 
		i ^= 1;									//	0->1 and 1->0
		//	Mix its mixture into the "other" state qword
		//	NOTE: the constant here is ad hoc and not optimized in any way
		i64[i] ^= chs::mix64(*qw_data ^ 0xA1B2C3D4E5F60789U);	
		qw_data++; length--;
	}

	//	Generate the remaining bit streams for SIMD processing using the xoroshiro jump function
	for (i = 2; i < 2 * chs::STREAM_NUM; i += 2) {
		i64[i] = i64[i-2];						//	Should probably be auto-vectorized by clang...
		i64[i+1] = i64[i-1]; 
		jump(&i64[i]);
	}

	activeStream = chs::STREAM_NUM-1;			//	Now calling next() will make activeStream == 0,
	next();										//	call xoroshiro on all hashed  bits, and generate UInt64 cache
}


//
//	Increment activeStream and generate new states for all streams
//	once all current streams have been used. 
//
void chs::RNG::next(){
	activeStream++;									//	Move to the next stream
													//	Expect that activeStream < STREAM_NUM
	if (__builtin_expect(activeStream == chs::STREAM_NUM, false)){
		activeStream = 0;							//	Start from 0 again...
		activeType = UInt64;						//	...default: random integers
		//	Call xoroshiro's next() function for all streams
		for (int i = 0; i < 2 * chs::STREAM_NUM; i += 2)
			cui[i>>1] = ::next(&i64[i]);			//	Evolve the state and store UInt64 in the cache
	}

	activeType = UInt64;

	//if (__builtin_expect(type != activeType, false))
	//	generate(type);								//	Regenerate the cache if not the same type as before: done

}

//
//	Currently regenerates the entire cache
//	-- sufficient to only go from activeStream on
//
void chs::RNG::generate(Distribution type){
	switch (type) {
		case UFP01: break;
		case UInt64: for (int i = 0; i < 2 * chs::STREAM_NUM; i += 2) cui[i>>1] = i64[i] + i64[i + 1]; break;
		default:;
	}

	activeType = type;								//	Update the type of the current distribution
}


//
//	Knuth's MMIX RNG: LCG generator employing just i64[0]
//	Use for testing and benchmarking purposes ONLY!
//
fp64_t chs::RNG::U01_lcg(){
	i64[0] = i64[0] * 6364136223846793005 + 1442695040888963407;
	return ldexp((fp64_t) i64[0], -64);
}

//
//	Generate random 64 bits (xoroshiro128+ algorithm)
//
uint64_t chs::RNG::int64(){
	//	Check if cache contains UInt64 values
	if (__builtin_expect(activeType != UInt64, false))
		generate(UInt64);							//	If not: regenerate the cache

	uint64_t result = cui[activeStream];			//	Value from the current stream
	next();											//	Move to next stream; generate new bits if needed
	return result;
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
	//	Check if cache contains UInt64 values
	if (__builtin_expect(activeType != UInt64, false))
		generate(UInt64);							//	If not: regenerate the cache

	uint64_t result = cui[activeStream];			//	Value from the current stream
	next();											//	Move to next stream; generate new bits if needed
	return 5.42101086242752217003726400434970855712890625e-20 * result;			//	2^(-64)
}



//
//	Genereate Uniform[1,2) fp64
//	NOTE: 1 may be generated if next() produces 0; 2 can never be generated due to IEEE754 FP representation
//	This generator has the same round-off issues as the one above, however it will not generate 2,
//	thus -log(2.0-u12) will produce a quick exponential random variable without risk of NaN.
//	
fp64_t chs::RNG::U12(){
	union {
		uint64_t i64;
		fp64_t f64;
	} result;				//	Overlap int64 and fp64 values

	//	Use the highest 52 bits for mantissa (shift them to the lowest 52 bits of i64)
	//	Set the top 12 bits to 001111111111 which creates "+" sign and exponent 1023 (0 after bias)
	//	result.i64 = (next() >> 12) | 0x3FF0000000000000U;
	//return result.f64;		//	Return the corresponding FP value
	return 0.0;
}


//
//	Genereate N(0,1) fp64 via Box-Muller algorithm
//
fp64_t chs::RNG::n01(){
	/*
	if (cache.i64[CHAOS_FLAGS] & CHAOS_FLAG_GAUSS) {		//	If some value is cached, return it... 
		fp64_t result = cache.f64[CHAOS_GAUSS];
		cache.i64[CHAOS_FLAGS] &= ~CHAOS_FLAG_GAUSS	;		//	...and clear the cache
		return result;
	};
	fp64_t phi = 2.0 * PI * U01();				//	angular part,
	fp64_t z = sqrt(-2.0 * log(2.0 - U12()));	//	radial part; use 2-U12() to avoid 0
	cache.f64[0] = z * cos(phi);				//	Store one...
	cache.i64[CHAOS_FLAGS] |= CHAOS_FLAG_GAUSS;	//	Raise the flag that a Gaussian variable is stored
	return z * sin(phi);						//	...return the other
												*/
	return 0.0;
}


//
//	Genereate N(0,1) fp64 via rejection sampling algorithm in a circle
//
fp64_t chs::RNG::N01(){
	/*
	if (cache.i64[CHAOS_GAUSS] != NaN) {		//	If some value is cached, return it... 
		fp64_t result = cache.f64[CHAOS_GAUSS];
		cache.i64[CHAOS_GAUSS] = NaN;			//	...and clear the cache
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
	*/
	return 0.0;
}




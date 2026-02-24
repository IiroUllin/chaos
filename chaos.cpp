#include "chaos.hpp"
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
			//	static_cast: otherwise 1 may be given smaller size container (e.g. 32 bit)
			if (JUMP[i] & static_cast<uint64_t>(1) << b) {
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
uint8_t chs::mix8(const uint8_t state) {
    return state * 97 + 111;
}


//
//	Just some LCG; not optimized
//
uint16_t chs::mix16(const uint16_t state) {
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
//	Convert uint64 into [0,1) fp64
//	1.0 will never be generated;
//	0.0 is only generated if all bits are 0
//	The next smallest number would be 2^(-64) 
//	and the largest, 1 - 2^(-52)
//	
static inline fp64_t chs::FP01(uint64_t bits) {
	//	Return 0.0 if all bits are zeros
	if (__builtin_expect((bits == 0), false)) return 0.0;

	int shift = __builtin_clzll(bits);			//	Number of leading zeros; use the "long-long" version for 64 bit
	union {										//	Union to contain result
		uint64_t i64result;
		fp64_t f64result;
	};

	if (__builtin_expect((shift < 12), true)){	//	Shift to the right,
		i64result = bits >> (11 - shift);
	}
	else {										//	Shift to the left,
		i64result = bits << (shift - 11);
	}
	//	Mantissa is in place now; leading 1 is in position 52
 	i64result &= 0x000FFFFFFFFFFFFFU;			//	Clear the exponent and sign (top 12) bits
	i64result |= static_cast<uint64_t>(1022 - shift) << 52;
	//	Here 1023 would correspond to <result> = 2^0 * 1.[mantissa]
	//	Observe that <result> == 1.0 can never appear, because its FP representation is
	//	<mantissa> == 0; <exponent> == 1023 -- but here <exponent> <= 1022

	return f64result;
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
		//	Call xoroshiro's next() function for all streams
		//	Convert random uint64 into fp(0,1)...
		for (int i = 0; i < 2 * chs::STREAM_NUM; i += 2) f64[i>>1] = chs::FP01(::next(&i64[i]));
	}
}




//
//	Knuth's MMIX RNG: LCG generator employing just i64[0]
//	Use for testing and benchmarking purposes only
//
fp64_t chs::RNG::U01_lcg(){
	i64[0] = i64[0] * 6364136223846793005 + 1442695040888963407;
	return chs::FP01(i64[0]);
}

//
//	Generate random 64 bits (uint64)...
//
uint64_t chs::RNG::int64(){
	int i = activeStream << 1;						//	states take x2 the space...
	uint64_t result = i64[i] + i64[i+1];			//	get uint64 directly from the <state>
	next();											//	Move to the next stream; generate new bits if needed
	return result;
}
//
//	...or an integer in [0...num-1]
//
uint64_t chs::RNG::int64(uint64_t num){
	assert(num > 0);
	//	A convoluted way to find the maximal number divisible by num, minus one:
	uint64_t result, max = UINT64_MAX;
	if (__builtin_popcountll(num) > 1)				//	If num is not a power of 2; use "long-long" version
		max = UINT64_MAX - (UINT64_MAX % num) - 1;

	do {
		int i = activeStream << 1;					//	states take x2 the space...
		result = i64[i] + i64[i+1];					//	get uint64 directly from the <state>
		next();										//	Move to the next stream; generate new bits if needed
	} while (result > max);							//	Discard values greater than <max> to avoid bias
	
	return result % num;
}

//
//	Genereate a Uniform(0,1] fp64
//
fp64_t chs::RNG::U01(){
	fp64_t result = f64[activeStream];				//	FP value from the current stream
	next();											//	Move to the next stream; generate new bits if needed
	return result;
}

//
//	Genereate an Exp(1) fp64
//
fp64_t chs::RNG::Exp1(){
	fp64_t result = f64[activeStream];				//	FP value from the current stream
	next();											//	Move to the next stream; generate new bits if needed
	return -log(1.0 - result);						//	result may be 0.0: subtract from 1.0 to avoid NaN	
}

//
//	Genereate an Exp(ln2) fp64
//	WARNING: CHECK ENDIANNESS: CLEARING BITS WITHIN
//
fp64_t chs::RNG::Eln2(){
	union {
		uint64_t bits;
		fp64_t X;									//	fp64 view of bits
	};
 	bits = i64[activeStream];						//	i64 value from the current stream

	int empty = 0;									//	Number of empty bits
													//
	while (__builtin_expect((bits == 0), false)) {
		empty += 64;								//	The entire 64 bit chunk is 0
		next();										//	Get another random 64 bits
	};
	
	int empty2 = __builtin_clzll(bits);				//	Number of empty bits within current nonzero block
	//assert(empty < 16);							//	To check how large empty blocks we get sometimes
	
	empty += empty2;								//	Total number of empty bits
	//	empty should now be distributed as Geometric(1/2)

	//	Use the remaining bits to create a U[0.5,1) random variable
	bits <<= ++empty2;								//	Rotate out the leading 0 bits and the following 1
	bits >>= 12;									//	Clear space for mantissa and sign
	bits |= 0x3FE0000000000000U;					//	Set the exponent; the constant is 1022 << 52
	//	At this point U should be U[0.5,1) random variable
	//	It only has 64-empty2 significant bits, so with probability 1/2048, it won't fill all 52 bits of the significand
	//	To improve accuracy, call next() and use the entire new 64 bit chunk

	//	Now we could simply call...
	//X = -log2(X);		
	//assert ((X > 0.0) && (X <= 1.0));				//	Should be in (0,1]
	//	...this would defeat the purpose of the entire routine, however, as we could have called -log2(F01(bits)) right away

	X = 2.0/log(2) * atanh((1 - X) / (1 + X)); 
	
	next();											//	Move to the next stream; generate new bits if needed
	return X + static_cast<fp64_t>(empty);	
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
	
	if (icache != NaN) {					//	If some value is cached, return it... 
		fp64_t result = cache;
		icache = NaN;						//	...and clear the cache
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

	cache = x * r;							//	Store one...
	return y * r;							//	...return the other
}


//
//	Approximate N(0,1) fp64 via Binom(64,1/2) by
//	adding the 64 random bits
//
fp64_t chs::RNG::qN01(){
	//	Grab the number of 1 bits in the random i64, subtract the mean of 32
	//	and normalize to get the correct variance.
	//	Note that sqrt(variance) of Binom(64,1/2) is 4
	return 0.25 * static_cast<fp64_t>(__builtin_popcountll(int64()) - 32);
}
	


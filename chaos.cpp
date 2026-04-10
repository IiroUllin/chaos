#include "chaos.hpp"
#include <cassert>					//	Assertions for debugging
//#include <iostream>
//#include <iomanip>

#include "ziggurat/zt_exp.hpp"		//	Ziggurat table for Exp(1) generator
#include "ziggurat/zt_gauss.hpp"	//	Ziggurat table for N(0,1) generator
#include <immintrin.h>				//	AVX2 packed integer types



/*--------------------------------------------------------------------------------------+
 |																						|
 |	Routines from xoroshiro128+ by D. Blackman and S. Vigna (2016-18); public domain	|
 |	+ vectorized versions for avx2, avx512 instructions sets.							|				
 |																						|
 +--------------------------------------------------------------------------------------*/


//
//	Cyclic rotate left; amount should be less than 64
//
static inline uint64_t __rotl(const uint64_t state, const int amount) {
	return (state << amount) | (state >> (64 - amount));
}
//
//	Vectorized rotate left for 4 64-bit integers
//
static inline __m256i __rotl_avx2(const __m256i state, const int amount) {
    return _mm256_or_si256(_mm256_slli_epi64(state, amount), _mm256_srli_epi64(state, 64 - amount));
}



//
//	Evolve the <state> using xoroshiro128+
//	The <result> is is the sum <state[0] + state[SIMD_BLOCK]>
//	(The original algorithm returns s0 + s1 before changing the state.)
//	NOTE: Use state[SIMD_BLOCK] instead of state[1] because
//	this function is called in __jump() even when vectorization is enabled

static inline uint64_t __next(uint64_t* state) {
	uint64_t s0 = state[0],
			 s1 = state[chs::SIMD_BLOCK] ^ s0;

	s0 = __rotl(s0, 24) ^ s1 ^ (s1 << 16);
	s1 = __rotl(s1, 37);

	state[0] = s0; state[chs::SIMD_BLOCK] = s1;

	return s0 + s1;
}
//
//	Vectorized version for 4 streams at once;
//	state[0] contains 4 consecutive L-qwords; state[1] -- 4 H-qwords
//
static inline __m256i __next_avx2(__m256i* state) {
    __m256i s0 = state[0],
			s1 = _mm256_xor_si256(state[1], s0);

	// s0 = __rotl(s0, 24) ^ s1 ^ (s1 << 16);
    s0 = _mm256_xor_si256(_mm256_xor_si256(__rotl_avx2(s0, 24), s1), _mm256_slli_epi64(s1, 16));
	s1 = __rotl_avx2(s1, 37);

	state[0] = s0; state[1] = s1;

    return _mm256_add_epi64(s0, s1);
}


//
//	xoroshiro128+ jump function, equivalent to 2^64 calls to __next()
//	The L and H qwords are separated by chs::SIMD_BLOCK qwords
//
static void __jump(uint64_t* state) {
	static const uint64_t JUMP[] = {0xDF900294D8F554A5U, 0x170865DF4B3201FCU};

	uint64_t s0 = 0,
			 s1 = 0;
	for(int i = 0; i < 2; i++)
		for(int b = 0; b < 64; b++) {
			//	static_cast: otherwise 1 may be given smaller size container (e.g. 32 bit)
			if (JUMP[i] & static_cast<uint64_t>(1) << b) {
				s0 ^= state[0];						//	The L qword
				s1 ^= state[chs::SIMD_BLOCK];		//	H qword is shifted by SIMD_BLOCK
			}
			__next(state);							//	Evolve the state
		}

	state[0] = s0;
	state[chs::SIMD_BLOCK] = s1;
}



//
//	Create remaining streams executing xoroshiro's __jump() function 
//
void chs::RNG::update_streams() {
	for (int i = 1; i < STREAM_NUM; i++){								
		i64[_L[i]] = i64[_L[i-1]];					//	Copy the L qword
		i64[_H[i]] = i64[_H[i-1]];					//	Copy the H qword 
		__jump(&i64[_L[i]]);						//	Call xoroshiro's __jump() function
	}
	//std::cout << "\n" << std::hex << std::uppercase;
	//for (int i = 0; i < STREAM_NUM; i++)
	//	std::cout << i64[_L[i]] + i64[_H[i]]<< "\n";
}

//
//	128 bit hashing function.
//	Takes <length> bytes from <data>
//	and hashes it into i64[0] and i64[SIMD_BLOCK] (two qwords)
//	remaining streams are then generated using the xoroshiro128+ jump function.
//	NOTE: <data> must be aligned and <length> should contain at least one 64 bit chunk
//	NOTE: this will erase the <state> and <cache> even if <length> < 8 and no data is hashed
//
void chs::RNG::hash(const void* data, std::size_t length){
	assert(length >= 8);						//	<data> must contain at least one 64 bit block
	length >>= 3;								//	original length is in bytes, this one is in qwords (64 bits)
	assert(!(uintptr_t(data) & 0b111));			//	Last 3 bits must be 0 for int64-aligned data
	uint64_t* qw_data = (uint64_t*) data;		//	Convert <data> into uint64 pointer

	i64[_L[0]] = 0;	i64[_H[0]] = 0;				//	Clear the bits of the 0th bit stream

	while (length > 0) {
		i64[_L[0]] ^= chs::mix64(*qw_data);		//	Mix current data chunk and merge it into the <state> 
		//	Mix its mixture into the "other" state qword
		//	NOTE: the constant here is ad hoc and not optimized in any way
		i64[_H[0]] ^= chs::mix64(*qw_data ^ 0xA1B2C3D4E5F60789U);	
		qw_data++; length--;
	}

	update_streams();							//	Generate all the other states from the hashed first state
	activeStream = STREAM_NUM - 1;				//	Now calling next() will make activeStream == 0,
	next();										//	call xoroshiro on all hashed  bits, and generate UInt64 cache
}


//
//	Increment activeStream and generate new states for all streams
//	once all current streams have been used. 
//
void chs::RNG::next(){
	activeStream++;								//	Move to the next bit stream
	if (__builtin_expect(activeStream == STREAM_NUM, false)){	//	Expect that activeStream < STREAM_NUM
		//
		activeStream = 0;						//	Start from 0 again...
		//
		//	Call xoroshiro's __next() function for all streams
		//	Convert random uint64 into [0,1) fp64-s...
		//
#ifdef __AVX2__
		__m256i *data = (__m256i*) i64;						//	C-style cast; i64 MUST be properly aligned, or this may crash!
		__m256d *floats = (__m256d*) f64;					//	Same here!
		for (int i = 0; i < BLOCK_NUM; i++){
			*(floats++) = fp01_avx2(__next_avx2(data));		//	Process four 64 byte chunks at a time
			data += 2;										//	Increment by 2 (data++ would point to the H-qwords of the same states)
		}
#else
		for (int i = 0; i < STREAM_NUM; i++) f64[i] = fp01(__next(&i64[_L[i]]));	//	No vectorization enabled
#endif
		//
		//	fp01() gives speed boost of about 20% relative to FP01() at the expense of some accuracy:
		//	log2(min.value) would be -52 vs -64 in the latter case.
		//	Define symbol, HPFP01, to substitute fp01() with FP01() globally...
	}
}





//
//	Generate a 64 bit integer in {0...N-1}
//	using Daniel Lemire's "Fast Random Integer Generation in an Interval" TOMACS v.29(1); 2019
//
uint64_t chs::RNG::int64(const uint64_t N){
	uint128_t value = (uint128_t) int64() * N,					//	Use (intrinsic) 128 bit integer to hold the product 
			  threshold = value & UINT64_MAX;					//	...mod 2^64
	
	if (__builtin_expect((threshold < N), false)){				//	Rejection sampling here
		//	Explicitly casting to 64 bits to get the remainder -- could be faster on some CPUs
		uint64_t bias = static_cast<uint64_t>(-N) % N;			//	-N = 2^64-N (mod 2^64) -- gets into uint64_t range
		while (__builtin_expect((threshold < bias), false)){	//	Biased values are in the range [0,bias)
			value = (uint128_t) int64() * N;
			threshold = value & UINT64_MAX;
		}
	};

	return static_cast<uint64_t>(value >> 64);					//	Return top 64 bits which should be uniform in [0,N)
}


//
//	Generate a 32 bit integer in {0...N-1}
//	This version could be slightly faster due to 64 bit multiplication
//
uint32_t chs::RNG::int32(const uint32_t N){
	uint64_t value = ((int64() >> 32) * N),						//	Use top 32 bits (higher RNG quality), multiply by N, 
			 threshold = value & UINT32_MAX;					//	...mod 2^32	
	
	if (__builtin_expect((threshold < N), false)){				//	Rejection sampling here
		//	Explicitly casting to 32 bits to get the remainder -- could be faster on some CPUs
		uint64_t bias = static_cast<uint32_t>(-N) % N;			//	2^32-N = -N (mod 2^32)
		while (__builtin_expect((threshold < bias), false)){	//	Biased values are in the range [0,bias)
			value = ((int64() >> 32) * N);
			threshold = value & UINT32_MAX;
		}
	};

	return static_cast<uint32_t>(value >> 32);					//	Return top 32 bits which should be uniform in [0,N)
}




//
//	General ziggurat method with exponential sampling for the irregular (bottom) piece; based on...
//	Marsaglia, Tsang; "The Ziggurat Method for Generating Random Variables" J.Stat.Soft. 5(8), 2002
//
fp64_t chs::RNG::zig(const ZigguratTable &zt) {
	constexpr int LAST = ZIG_SIZE - 1;					//	Index of the last element of the ziggurat table
	union {
		fp64_t X;										//	The return value
		uint64_t iX;									//	Access to bits of X to control the sign
	};
	fp64_t Y;											//	(X,Y) will be the generated point under the PDF
	uint64_t bits = int64(),							//	Get the random  bits from the stream
			 sign = zt.sgn & bits;						//	Randomize the sign bit if the distribution is symmetric 
	bits <<= 1;											//	Discard the top bit used for sign

	do {												//	Infinite loop; break when a value is returned
		int i = (bits >> (64 - ZIG_SIZE_L2)),			//	Take the first size_l2 bits as layer index
			j = i + 1;									//	Next layer index
		bits <<= ZIG_SIZE_L2;							//	Rotate the rest to the top
		X = fp01(bits) * zt.X[j];						//	Generate X: convert bits to [0,1) and scale
		
		if (__builtin_expect(X <= zt.X[i], true)) {		//	MAIN PATH: accept the X value
getOut:		iX ^= sign;									//	Flip the sign of X if <sign> is set...
			return X;									//	...and return the value
		}

		if (__builtin_expect(i < LAST, true))
			Y = U(zt.Y[j], zt.Y[i]);					//	REGULAR LAYERS: generate a random Y-value in the rectangle 
			//	Testing shows that trying to use lower bits of X to generate Y via
			//	Y = zt.Y[j] + (zt.Y[i] - zt.Y[j]) * fp01(bits << 31);
			//	doesn't improve performance (and, of course, introduces some statistical bias)...
		else {
			assert(i == LAST);							//	IRREGULAR (BOTTOM) layer:
			X = Exp1() + zt.X[i];						//	Generate a shifted exponential random variable
			if (zt.exp) 
				goto getOut;							//	Y-comparison is not required
			Y = zt.Y[j] * exp(-X) * U01();				//	Generate a random Y coordinate under the exponential curve
		}

		if (Y <= zt.pdf(X)) goto getOut;				//	Accept X if (X,Y) is under the graph of the PDF
		//
		//	If we have not returned by now, the sample is rejected: restart from the top...
		//
		bits = int64();									//	Get the new i64 value from the bit stream
	} while (true);
}



//
//	Generate N(0,1) fp64 via ziggurat method
//
fp64_t chs::RNG::N01() {
	return zig(ZT_GAUSS);
}



//
//	Generate Exp(1) fp64 via ziggurat method
//
fp64_t chs::RNG::E1() {
	return zig(ZT_EXP);
}


//
//	Generate Exp(1) fp64 using log2-approximation method
//
fp64_t chs::RNG::Exp1(){
	union {
		uint64_t bits;
		fp64_t X;									//	fp64 view of bits
	};
 	bits = int64();									//	i64 value from the current stream

	int empty = 0;									//	Number of empty bits

	while (__builtin_expect((bits == 0), false)) {
		empty += 64;								//	The entire 64 bit chunk is 0
		bits = int64();								//	Get another random 64 bits
	};
	
	int empty2 = __builtin_clzll(bits);				//	Number of starting 0 bits within current nonzero block
	//assert(empty < 16);							//	To check how large empty blocks we get sometimes
	
	empty += empty2;								//	Total number of 0 bits
	//	empty should now be distributed as Geometric(1/2)

	//	Use the remaining bits to create a U[0.5,1) random variable
	bits <<= ++empty2;								//	Rotate out the leading 0 bits and the following 1
	bits >>= 12;									//	Clear space for mantissa and sign
	bits |= 0x3FE0000000000000U;					//	Set the exponent; the constant is 1022 << 52
	
	assert ((X >= 0.5) && (X < 1.0));				//	At this point X should be U[0.5,1) random variable
	//	It only has 64-empty2 significant bits, so with probability 1/2048, it won't fill all 52 bits of the significand
	//	To improve accuracy, call next() and use the entire new 64 bit chunk

	//	Now we could...
	//return log(X) + LN2 * static_cast<fp64_t>(empty)
	//	...this would defeat the purpose of this method, however, as we could have called -log2(F01(bits)) * log(2) right away
	//	part of the speed up comes from optimizing the log() function, as we don't have to deal with NaNs and all that 

	fp64_t Y = (X - 1.0) / (X + 1.0),
		   Z = Y * Y,
		   //
		   //	This approximation min/max-es the error using the Remez algorithm, as AI claims
		   //	however the number ratios are pretty much the same as for regular Pade,
		   //	so I am not sure that there is any improvement: need to double check myself...
		   P = 2.0 - Z * (2.564101905828859341 - Z * (0.791195655518972807 - Z * 0.034091605553198947)),
		   Q = -1.0 + Z * (1.615383344686411516 - Z * (0.734261765415777174 - Z * 0.083262657310573967));
		   //
		   //	This is Pade order [7/6]
		   //P = -30030 + Z * (38500 - Z * (11886 - Z * 512)),
		   //Q = 15015 - Z * (24255 - Z * (11025 - Z * 1225));
		   //
		   //	This is Pade order [5/4]
		   //P = -1890 + Z * (1470 - Z * 128),
		   //Q = 945 - Z * (1050 - Z * 225);
		   //
		   //	This is Pade order [3/2]
		   //P = -30 + 8 * Z,
		   //Q = 15 - 9 * Z;
	

		
	return Y * P / Q + LN2 * static_cast<fp64_t>(empty);	//	Rescale to Exp(1)	
	//return Y * P / Q + static_cast<fp64_t>(empty);	
}



//
//	Generate N(0,1) fp64 via Box-Muller algorithm
//	"A Note on the Generation of Random Normal Deviates". AMS. 29 (2); 1958
//
fp64_t chs::RNG::N01_BxM(){

	if (icache != NaN) {						//	If some value is cached, return it... 
		fp64_t result = cache;
		icache = NaN;							//	...and clear the cache
		return result;
	};

	fp64_t phi = 2.0 * PI * U01();				//	angular part,
	fp64_t z = sqrt(2.0 * E1());				//	radial part: generate Exp(1) random variable
	cache = z * cos(phi);						//	Store one...
	return z * sin(phi);						//	...return the other
}


//
//	Generate N(0,1) fp64 via rejection sampling algorithm in a circle
//	Due to Marsaglia & Bray, "A Convenient Method for Generating Normal Variables". SIAM Rev. 6 (3): 260–264; 1964
//
fp64_t chs::RNG::N01_rej(){
	
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

	//	(x, y) is now uniform in the unit circle; r ~ U(0,1); -ln(r) ~ Exp(1)				

	r = 1.0 / sqrt(r); x *= r; y *= r;		//	x and y now contain cosine and sine of a random angle in (0,2π)
	r = sqrt(4.0 * log(r));					//	r is now the radial part of 2d N(0,1)

	cache = x * r;							//	Store one...
	return y * r;							//	...return the other
}


//
//	Approximate N(0,1) fp64 via Binom(64,1/2) by
//	adding the 64 random bits
//
fp64_t chs::RNG::N01_bin(){
	//	Grab the number of 1 bits in the random i64, subtract the mean of 32
	//	and normalize to get the correct variance.
	//	Note that sqrt(variance) of Binom(64,1/2) is 4
	return 0.25 * static_cast<fp64_t>(__builtin_popcountll(int64()) - 32);
}
	


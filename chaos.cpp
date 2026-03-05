#include "chaos.hpp"
#include <cassert>		//	Assertions for debugging


#include "zt_exp.hpp"	//	Ziggurat table for Exp() variable generator


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
	static const uint64_t JUMP[] = {0xDF900294D8F554A5U, 0x170865DF4B3201FCU};

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	for(int i = 0; i < 2; i++)
		for(int b = 0; b < 64; b++) {
			//	static_cast: otherwise 1 may be given smaller size container (e.g. 32 bit)
			if (JUMP[i] & static_cast<uint64_t>(1) << b) {
				s0 ^= state[0];
				s1 ^= state[1];
			}
			next(state);						//	Evolve the state
		}

	state[0] = s0;
	state[1] = s1;
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

	activeStream = chs::STREAM_NUM - 2;			//	Now calling next() will make activeStream == 0,
	next();										//	call xoroshiro on all hashed  bits, and generate UInt64 cache
}


//
//	Increment activeStream and generate new states for all streams
//	once all current streams have been used. 
//
inline void chs::RNG::next(){
	activeStream += 2;							//	Move to the next bit stream; increment by two, because states are two qwords
	if (__builtin_expect(activeStream == 2 * chs::STREAM_NUM, false)){	//	Expect that activeStream < 2 * STREAM_NUM
		activeStream = 0;						//	Start from 0 again...
		//	Call xoroshiro's next() function for all streams
		//	Convert random uint64 into fp(0,1)...
		for (int i = 0; i < 2 * chs::STREAM_NUM; i += 2) f64[i>>1] = FP01(::next(&i64[i]));
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
//	Genereate an Exp(1) fp64 using ziggurat method
//
fp64_t chs::RNG::Ez(){
	const int s2 = __ZT_EXP_LL2;						//	log2(<Table Size>)
	const int last = (1 << s2) - 1;						//	Index of the last element of the ziggurat table

	for(;;) {											//	Infinite loop; break when a value is returned
		uint64_t bits = int64();						//	Get i64 value from the bit stream

		int i = (bits >> (64-s2));						//	Take the top s2 bits as layer index (32 ziggurat layers at the moment)

		bits <<= s2;									//	Rotate the rest to the top
		fp64_t X = FP01(bits);							//	Convert them to [0,1) FP

		if (__builtin_expect((i == 0), false)) {		//	The irreggular (bottom) layer
			if (X <= __ZT_EXP_P)						//	Use X to check rectangular vs exponential part
				return __ZT_EXP_X[last] * U01();		//	Uniform in the rectangular part
			else return __ZT_EXP_X[last] + E1();		//	Exponential + shift otherwise
		} else {
			X *= __ZT_EXP_X[i];							//	Stretch X to [0, __ZT_EXP[i]]
			//	Accept if it is smaller than the previous margin
			if (__builtin_expect(X <= __ZT_EXP_X[i-1], true)) return X;
			//	If not, generate a random Y-value in the rectangle and check if (X,Y) is under the PDF graph
			fp64_t Y = U(__ZT_EXP_Y[i], __ZT_EXP_Y[i-1]);
			if (Y <= exp(-X)) return X;					//	Accept if under the curve
		}
	}
}


//
//	Genereate an Exp(1) fp64
//
fp64_t chs::RNG::E1(){
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
//	Genereate N(0,1) fp64 via Box-Muller algorithm
//	"A Note on the Generation of Random Normal Deviates". AMS. 29 (2); 1958
//
fp64_t chs::RNG::n01(){

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
fp64_t chs::RNG::qN01(){
	//	Grab the number of 1 bits in the random i64, subtract the mean of 32
	//	and normalize to get the correct variance.
	//	Note that sqrt(variance) of Binom(64,1/2) is 4
	return 0.25 * static_cast<fp64_t>(__builtin_popcountll(int64()) - 32);
}
	


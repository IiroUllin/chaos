#include <iostream>
#include <iomanip>
#include <cassert>
#include <string>
#include <vector>
#include <random>		//	For benchmarking
#include <time.h>
#include <math.h>
#include <cstdint>
#include <bitset> 		//	For std::bitset
#include <functional>	//	For lambda-functions

#include "chaos.hpp"

//#define SAMPLE_NUM 16777216			//	Number of samples for calculations
#define SAMPLE_NUM 10000000				//	Number of samples for calculations


struct testType {
	double time;					//	Will contain time in [ms]
	fp64_t data[8];					//	...for whatever quantities computed (up to 8)
};



//
//	Compute the mean and variance of the provided random variable
//	also return the elapsed time in [ms]
//
testType benchMean (std::function<fp64_t()> X){	//	X is a random variable (method of chs::RNG)	
	struct timespec start, finish;
	double elapsed;

	fp64_t sum = 0.0, sum2 = 0.0;

	clock_gettime(CLOCK_MONOTONIC, &start);
	//	Note that CLOCK_MONOTONIC may not be provided by the OS
	//	-- can be checked by sysconf(_SC_MONOTONIC_CLOCK)
	//	In this case CLOCK_REALTIME can be used instead (must always be provided)
	//	This clock, however, can be changed while the program is running
	//	in which case the overall results may be incorrect
	//	CLOCK_MONOTONIC_COARSE can also be used. It is faster but coarser

	for (int j=0; j<SAMPLE_NUM; j++) {
		fp64_t value = X();
		sum += value;
		sum2 += value * value;
	}

	clock_gettime(CLOCK_MONOTONIC, &finish);

	//	A sanity check that the math didn't go awfully awry
	assert(!(std::isnan(sum) | std::isnan(sum2)));
	
	sum /= SAMPLE_NUM;					//	Mean
	sum2 /= SAMPLE_NUM;					//	2nd moment


	elapsed = 1e3 * (finish.tv_sec - start.tv_sec) + 1e-6 * (finish.tv_nsec - start.tv_nsec);

	return {.time = elapsed, .data = {sum, sum2 - sum * sum, 0, 0, 0, 0, 0, 0}};
}



int main() {

	testType result;			//	Will contain benchmark results
	chs::RNG rng;				//	RNG object stores the state and provides samplers
	


	std::cout << "\n### Hashing function (seeding) ###\n\n";
	

	//	Hash 0, 1, 2, 3
	for (uint64_t seed = 0; seed < 4; seed ++) {
		rng.hash(&seed, 8);
		std::cout << "\t" << seed << "\t->\t" << std::bitset<64>(rng.int64()) << "\n";
	};

	//	Then system time...
	struct timespec time;
	clock_gettime(CLOCK_REALTIME, &time);
	rng.hash(&time, sizeof(time));
	std::cout << " [time: " << (sizeof(time) << 3) << "bit]\t->\t" << std::bitset<64>(rng.int64()) << "\n\n";

	//	Initialize built-in routines for benchmarking purposes
	srand(static_cast<unsigned int>(time.tv_sec));					//	C style RNG
	std::random_device ranDev;										//	Default C++ std:: RNG
	std::default_random_engine gen(ranDev());
	std::uniform_real_distribution<double> distr(0.0, 1.0); 

	//
	//	Some formatting specification:
	//
	const size_t width = round(log(sqrt(SAMPLE_NUM))/log(10.0));	//	~sqrt(SAMPLE_NUM) relevant digits
	std::cout << std::fixed;


	//	Output first few  UInt64 and U01 random numbers
	std::cout << "### Some successive random numbers ###\n\nUInt64:\t\t";
	for (int i = 0; i < 4; i++) std::cout << rng.int64() << " ";
	std::cout << "\nUnif[0,1):\t";
	for (int i = 0; i < 4; i++) std::cout << rng.U01() << " ";


	std::cout << "\n\n### Averaging over " << SAMPLE_NUM << " samples ###\n\n";


	//	Array of random variable samplers and their names
	struct Sampler {
		std::function<fp64_t()> func;
		std::string id;
	};
	//	There must be a more elegant way to do this...
	std::vector <Sampler> X = {
		{.func = [&](){return static_cast<fp64_t>(rand()) / RAND_MAX;},		.id = "rand() benchmark\t"},
		{.func = [&](){return distr(gen);},		.id = "std:: benchmark\t\t"},
		{.func = [&](){return rng.U01_lcg();},	.id = "LCG benchmark\t\t"},
		{.func = [&](){return rng.U01();},		.id = "Unif[0,1)\t\t"},
		{.func = [&](){return rng.Exp1();},		.id = "Exp(1) [-ln(U01)]\t"},
		{.func = [&](){return rng.E1();},		.id = "Exp(1)\t\t\t"},
		{.func = [&](){return rng.Ez();},		.id = "Exp(1) [ziggurat]\t"},
		{.func = [&](){return rng.N01();},		.id = "N(0,1) [rejection]\t"},
		{.func = [&](){return rng.n01();},		.id = "N(0,1) [Box-Muller]\t"},
		{.func = [&](){return rng.qN01();},		.id = "N(0,1) [binom]\t\t"},
		{.func = [&](){return trunc(rng.U01() * 7);},	.id = "{0...6} [truncated]\t"},
		{.func = [&](){return rng.int64(7);},	.id = "{0...6} [64 bit]\t"},
		{.func = [&](){return rng.int32(7);},	.id = "{0...6} [32 bit]\t"}
	};


	//
	//	Iterate over remaining samplers computing mean and variance
	//
	for (auto sampler { std::begin(X) }; sampler != std::end(X); ++sampler){
		result = benchMean(sampler->func);
		std::cout << sampler->id << std::setprecision(width)
			<< "Mean: " << result.data[0] 
			<< "\tVariance: " << result.data[1] 
			<< "\t\t" << std::setprecision(0) << result.time << "ms\n";
	};
	
	//
	//	FYI:
	//	On my Lunar Lake 258V; -O2: 10,000,000 "return 0" samples with mean and variance computation take 28ms;
	//	In debug mode: about 31ms.
	//	So all extra time is presumably spent on the relevant RNG...
	//


	std::cout << "\n";

	return 0;
}

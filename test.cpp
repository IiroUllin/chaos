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
//#define SAMPLE_NUM 10000000			//	Number of samples for calculations
#define SAMPLE_NUM 134217728			//	Number of samples for calculations

int flag = 1;							//	Flag for terminal output

struct testType {
	double time;						//	Will contain time in [ms]
	fp64_t data[8];						//	...for whatever quantities computed (up to 8)
};



//
//	Compute the mean and variance of the provided random variable
//	while filling in an array; return the elapsed time in [ms]
//
testType benchMean (std::function<fp64_t()> X, std::vector<fp64_t> &data){	//	X is a random variable (method of chs::RNG)	
	struct timespec start, finish;
	double elapsed;
	std::string chars[] = {"|","/","-","\\"};
	int index = 0;

	fp64_t sum = 0.0, sum2 = 0.0;
    //std::fill(data.begin(), data.end(), 0.0);					//	Clear the data

	if (flag) std::cout << " ";									//	Prepare for output

	clock_gettime(CLOCK_MONOTONIC, &start);
	//	Note that CLOCK_MONOTONIC may not be provided by the OS
	//	-- can be checked by sysconf(_SC_MONOTONIC_CLOCK)
	//	In this case CLOCK_REALTIME can be used instead (must always be provided)
	//	This clock, however, can be changed while the program is running
	//	in which case the overall results may be incorrect
	//	CLOCK_MONOTONIC_COARSE can also be used. It is faster but coarser

	for (int j = 0; j < SAMPLE_NUM; j++) {
		data[j] = X();
		if (__builtin_expect(flag, 0)) {
			if (__builtin_expect((j & 0xFFFFF) == 0, false)) {
				std::cout << "\b" << chars[index] << std::flush;
				index = (index + 1) & 0b11;
			}
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &finish);

	if (flag) std::cout << "\b";									//	Remove last symbol
	
	//	Compute some statistics 
	for (int j=0; j<SAMPLE_NUM; j++) {
		fp64_t value = data[j];
		sum += value;
		sum2 += value * value;
	}

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
	double refTime;				//	Reference time (LCG)


	std::cout << "\n### Hashing function (seeding) ###\n\n";
	

	//	Hash 0, 1, 2, 3
	for (uint64_t seed = 0; seed < 3; seed ++) {
		rng.hash(&seed, 8);
		std::cout << "\t" << seed << "\t->\t" << std::bitset<64>(rng.int64()) << "\n";
	};

	//	Then system time...
	struct timespec time;
	std::cout << "  -- Consecutive timer reads [" << (sizeof(time) << 3) <<" bits] --\n";
	for (int i = 1; i <= 3; i++) {
		clock_gettime(CLOCK_REALTIME, &time); 
		rng.hash(&time, sizeof(time));
		std::cout << "    " << time.tv_nsec << "\t->\t" << std::bitset<64>(rng.int64()) << "\n";
	};




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
	std::cout << "\n### Some successive random numbers ###\n\nUInt64:\t\t";
	for (int i = 0; i < 4; i++) std::cout << rng.int64() << " ";
	std::cout << "\nUnif[0,1):\t";
	for (int i = 0; i < 4; i++) std::cout << rng.U01() << " ";


	//
	//	Create an array of SAMPLE_NUM size to store random numbers and to do some basic math with them
	//
	std::vector<fp64_t> data(SAMPLE_NUM, 0.0);		//	Fill with 0.0 - makes no difference...
	//
	//	Array of random variable samplers and their names
	//
	struct Sampler {
		std::function<fp64_t()> func;
		std::string id;
	};
	//	Is there a more elegant way to do this?..
	std::vector <Sampler> X = {
		//{.func = [&](){return rng.U01_lcg();},	.id = "LCG    [benchmark]\t"},
		{.func = [&](){return static_cast<fp64_t>(rand()) / RAND_MAX;},		.id = "rand()\t[benchmark]\t"},
		//{.func = [&](){return distr(gen);},		.id = "std::  [benchmark]\t"},
		{.func = [&](){return rng.U01();},		.id = "U01()\t\t\t"},
		{.func = [&](){return rng.E1_log();},		.id = "Exp(1)\t[-ln(U01)]\t"},
		{.func = [&](){return rng.Exp1();},		.id = "Exp1()\t[log approx]\t"},
		{.func = [&](){return rng.E1();},		.id = "E1()\t[ziggurat]\t"},
		{.func = [&](){return rng.Ez();},		.id = "Ez()\t[opt.zigg]\t"},
		{.func = [&](){return rng.N01_rej();},	.id = "N(0,1)\t[rejection]\t"},
		{.func = [&](){return rng.N01_BxM();},	.id = "N(0,1)\t[Box-Muller]\t"},
		{.func = [&](){return rng.N01();},		.id = "N01()\t[ziggurat]\t"},
		{.func = [&](){return rng.Nz();},		.id = "Nz()\t[opt.zigg]\t"},
		{.func = [&](){return trunc(rng.U01() * 7);},	.id = "{0...6}\t[truncated]\t"},
		{.func = [&](){return rng.int64(7);},	.id = "{0...6}\t[64 bit]\t"},
		{.func = [&](){return rng.int32(7);},	.id = "{0...6}\t[32 bit]\t"}
	};
	//	A sampler object for warm-up; start with std:: generator
	Sampler tmp = {.func = [&](){return distr(gen);}, .id = ""};


	std::cout << "\n\n### Averaging over " << SAMPLE_NUM << " samples ###\n";
	//	Run std:: generator to "warm-up" the CPU
	
	std::cout << "  -- Warming up with the awesomly slow std:: generator " << std::flush;
	result = benchMean(tmp.func, data);
	refTime = result.time;
	std::cout << std::setprecision(0) << refTime << "ms" << " --\n";
	

	flag = 0;										//	Switch off rotating calculations indicator
	
	tmp.func = [&](){return rng.U01_lcg();};		//	LCG for baseline time
	result = benchMean(tmp.func, data);
	refTime = result.time;
	std::cout << "  -- Using a simple LCG to estimate the baseline time: <BASELINE> = " << std::setprecision(0) << refTime << "ms" << " --\n";
	std::cout << "  -- The following time intervals are computed relative [... - <BASELINE>] to it --\n\n";


	//
	//	Iterate over remaining samplers computing mean and variance
	//
	for (auto sampler { std::begin(X) }; sampler != std::end(X); ++sampler){
		result = benchMean(sampler->func, data);
		std::cout << sampler->id << std::setprecision(width)
			<< "mean: " << result.data[0] 
			<< "\tvar: " << result.data[1] 
			<< "\t" << std::setprecision(0) << result.time - refTime << "ms\n";
	};
	


	std::cout << "\n";

	return 0;
}

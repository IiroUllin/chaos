CC = clang


#	Extra header files here
INCDIR = /home/ibrahim/include/

#	Build path 
BUILDDIR = build/

#	Test (executable) path
TESTDIR = test/

#	Object and temporary files
OBJDIR = obj/

#	Library path (will look for lib-chaos here)
LIBDIR = $(BUILDDIR)


#	AVX512 switches
SIMD = -mavx2

#	These enable AVX512 -- don't seem to work on Lunar Lake (generate illegal commands)
#SIMD = -mavx512f -mavx512vl -mavx512bw -mavx512dq

#
#
#	Keep the needed version and comment out the other
#
#	RELEASE:
#CPPFLAGS = -I$(INCDIR) -std=c++11 -W -O2 $(SIMD) -DNDEBUG
#
#	DEBUG:
CPPFLAGS = -I$(INCDIR) -std=c++11 -Wall -march=native -O2 -ffp-model=precise -ffp-contract=on $(SIMD) 
#
#	Comments:
#
#	-march=native enables all instruction sets available on the host machine
#	-ffp-model=strict/fast/precise umbrella settings for FP optimizations
#	-ffp-contract=on/off/fast fuse add multiply toggle
#	-ffinite-math-only assume that NaN and Infinity never occur



LDFLAGS = -L$(LIBDIR) -lstdc++ -lm -lchaos



#
#	.PHONY lists targets that are names of recipes rather than of output files
#	
.PHONY:	lib-chaos test clean
	#	test: run the test suite
	#	clean: clean all files except the source code

#	By default the first target is default :)
#	But we can set it explicitly
.DEFAULT_GOAL := lib-chaos





chaos.o: chaos.h chaos.cpp
	$(CC) $(CPPFLAGS) -c chaos.cpp -o $(OBJDIR)chaos.o



#
#	Create the (static) chaos library file
#
lib-chaos: chaos.o
	ar crs $(BUILDDIR)libchaos.a $(OBJDIR)chaos.o

#
#	Create an executable file with tests for the chaos library
#
test-chaos:	lib-chaos test.cpp
	$(CC) $(CPPFLAGS) -o $(TESTDIR)test-chaos test.cpp $(LDFLAGS)

#
#	Run the tests
#
test:	test-chaos
	$(TESTDIR)test-chaos

clean:
	rm -f $(BUILDDIR)libchaos.a $(TESTDIR)test-chaos $(OBJDIR)*

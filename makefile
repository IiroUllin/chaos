CC = clang
#CC = gcc


#	Extra header files here; use absolute path
INCLUDE = -I/home/ibrahim/include/

#	Library path
LIB = -L/home/ibrahim/lib/

#
#
#	Keep the needed version and comment out the other
#
#	RELEASE:
CPPFLAGS = $(INCLUDE) -std=c++11 -W -march=native -O2 -ffp-model=precise -ffp-contract=on -DNDEBUG
#
#	DEBUG:
#CPPFLAGS = $(INCLUDE) -std=c++11 -Werror -O2 -ffp-model=precise
#
#	Comments:
#
#	-march=native enables all instruction sets available on the host machine
#	-ffp-model=strict/fast/precise umbrella settings for FP optimizations
#	-ffp-contract=on/off/fast fuse add multiply toggle
#	-ffinite-math-only assume that NaN and Infinity never occur



LDFLAGS = -L. -lstdc++ -lm -lchaos



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
	$(CC) $(CPPFLAGS) -c chaos.cpp -o chaos.o



#
#	Create the (static) chaos library file
#
lib-chaos: chaos.o
	ar crs build/libchaos.a chaos.o

#
#	Create an executable file with tests for the chaos library
#
test-chaos:	lib-chaos test.cpp
	$(CC) $(CPPFLAGS) -o test/test-chaos test.cpp $(LDFLAGS)

#
#	Run the tests
#
test:	test-chaos
	test/test-chaos

clean:
	rm -f *.o *.a test-chaos

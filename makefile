CC = clang




####	DIRECTORY STRUCTURE		####

#
#	WARNING: DO NOT LEAVE ANY SPACES OR TABS BETWEEN THE MACRO AND # 
#	THEY WILL BE INCLUDED IN THE MACRO ITSELF
#	THIS MAY LEAD TO UNEXPECTED DISASTERS SUCH AS rm $(DIR)/*
#	TRANSLATING INTO rm [something] /*
#	
SRCDIR = .#							Source directory; root OK for small projects
BUILDDIR = build#					Build path: final output goes here
TESTDIR = test#						Test (executable) files go here
OBJDIR = obj#						Object and temporary files go here
INCDIR = /home/ibrahim/include#		Extra header files here (not from this project)
LIBDIR = $(BUILDDIR)#				Local libraries here (if needed)

TARGET = $(SRCDIR)/chaos.hpp $(LIBDIR)/libchaos.a#	Main targets of the project
TEST = $(TESTDIR)/test-chaos#		Test file executable


####	COMPILER AND LINKER FLAGS		####

#	AVX512 switches
SIMD = -mavx2

#	These enable AVX512 -- don't work on Lunar Lake (generate illegal commands)
#SIMD = -mavx512f -mavx512vl -mavx512bw -mavx512dq

#
#
#	Keep the needed version and comment out the other
#
#	RELEASE:
CFLAGS = -I$(INCDIR) -std=c++11 -Wall -O3 -ffp-contract=on $(SIMD) -DNDEBUG
#CFLAGS = -I$(INCDIR) -std=c++11 -W -O2 $(SIMD) -DNDEBUG
#
#	DEBUG:
#CFLAGS = -I$(INCDIR) -std=c++11 -Wall -march=native -O2 -ffp-model=precise -ffp-contract=on $(SIMD) 
#
#	Comments:
#
#	-march=native enables all instruction sets available on the host machine
#	-ffp-model=strict/fast/precise umbrella settings for FP optimizations
#	-ffp-contract=on/off/fast fuse add multiply toggle
#	-ffinite-math-only assume that NaN and Infinity never occur; note that std::isnan() is assumed to return false in this case



LDFLAGS = -L$(LIBDIR) -lstdc++ -lm -lchaos



####	BUILD RULES		####


#
#	.PHONY lists targets that are names of recipes rather than of output files
#	
.PHONY: test clean install
#	test: run the test suite
#	clean: clean all files except the source code


#	By default the first target is default :)
#	But we can set it exlicitly
.DEFAULT_GOAL := install


#
#	GENERIC IMPLICIT RULES
#
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CC) $(CFLAGS) -c $^ -o $@

$(BUILDDIR)/lib%.a: $(OBJDIR)/%.o
	ar crs $@ $^



#
#	EXPLICIT BUILD RULES
#



#
#	Create an executable file with tests for the chaos library
#
$(TEST): $(TARGET) test.cpp
	$(CC) $(CFLAGS) -o $(TEST) test.cpp $(LDFLAGS)

#
#	Run the tests
#
test: $(TEST)
	$(TEST)

#
#	Copy header and library files to external directories
#
install: $(TARGET)
	@echo "Updating (global) headers and libraries..."
	rsync $(SRCDIR)/chaos.hpp ~/include/
	rsync $(BUILDDIR)/libchaos.a ~/lib/

#
#	WARNING!!! THINK THRICE WHEN USING ANY WILDCARDS WITH rm -f
#	ALWAYS DO A DRY RUN WITH ls [same arguments] FIRST!!!
#	THEN DO rm -i [same arguments] JUST IN CASE!!!
#
clean:
	rm -f $(BUILDDIR)/* $(TESTDIR)/* $(OBJDIR)/*

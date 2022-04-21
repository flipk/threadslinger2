
OBJDIR = obj

CXXFLAGS += -fdiagnostics-color=always -std=c++11

LIB_TARGETS = t2t2
PROG_TARGETS = t1

t2t2_TARGET = $(OBJDIR)/libt2t2.a
t2t2_CXXSRCS = thread2thread2.cc
t2t2_DOXYFILE = Doxy.t2t2
DOXYGEN_TARGETS += t2t2

t1_TARGET = $(OBJDIR)/t1
t1_CXXSRCS = thread2thread2_test.cc
t1_DEPLIBS = $(t2t2_TARGET)
t1_LIBS = -lpthread
EXTRA_CLEAN += testrun_clean

# if you just type 'make' it does everything.
test: all testrun

testrun: 0log

0log: $(t1_TARGET)
	$(t1_TARGET) | tee 0log.temp
	@mv 0log.temp 0log

testrun_clean:
	rm -f 0log*

bundle:
	git bundle create ts2.bundle --all
	git bundle verify ts2.bundle

EXTRA_CLEAN += bundle_clean

bundle_clean:
	rm -f ts2.bundle

include ../pfkutils/Makefile.inc

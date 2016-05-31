# 
# Code parameters (#define) are in 'Config' file.
# 
# The Makefile uses all .c and .h files in ./src and its subdirs 
# for compilation automatically. No need to edit this file if you add 
# a file or directory.
# This Makefile also produces src/config.h and src/print_settings.c from the
# 'Config' file by means of a few lines of bash.
# 
# The way to set your compilation parameters is to set environment variables 
# in ~/.bashrc :
#
#	TANDAV_CC is the compiler, usually mpicc
#
# 	TANDAV_CFLAGS are the compilation flags, including optimization. Make sure 
# 		to enable c99, openmp, all warnings and debugging symbols. With this
# 		code you REALLY want inter-file-optimization & vectorization.
# 	
# 	TANDAV_LDFLAGS are the libraries to link in (-lX) and their dirs 
# 		(-L/home/jdonnert/Libs/lib). Most notably here is MPI. 
# 		GSL libraries are linked atomatically.
# 	
# 	TANDAV_CPPFLAGS are the include directories (-I/home/username/include)
#
# If your LD_LIBRARY_PATH and CPPFLAGS variables are set correctly, you might 
# not have to change TANDAV_LDFLAGS and TANDAV_CPPFLAGS at all.
#
# Examples: 
#
# icc, mpich:
#
# 	export TANDAV_CC="mpicc"
#	export TANDAV_CFLAGS="-Wall -g -openmp -std=c99 -fstrict-aliasing \
#		-ansi-alias-check -O3 -xhost -qopt-report"
#	export TANDAV_CFLAGS_DEBUG="-Wall -g -openmp -std=c99"
# 	export TANDAV_LDFLAGS="-lmpich"
# 	export TANDAV_CPPFLAGS=""
#
# gcc, mpich, AMD Bulldozer:
#
#	export TANDAV_CFLAGS="-g -O2 -fopenmp -std=c99 -fstrict-aliasing  \
#		-march=bdver1 -mtune=native -mprefer-avx128 -mieee-fp -flto \
#		-minline-all-stringops -fprefetch-loop-arrays \
#		--param prefetch-latency=300 -funroll-all-loops"
#	export TANDAV_CFLAGS_DEBUG="-Wall -g -openmp -std=c99"
# 	export TANDAV_LDFLAGS="-lmpich -L/home/donnert/Libs/lib"
# 	export TANDAV_CPPFLAGS="-I/home/donnert/Libs/include"
# 
# CrayPE:
#
# 	export TANDAV_CC="cc"
# 	export TANDAV_CFLAGS="-O3 -h msglevel_2 -h msgs"
#	export TANDAV_CFLAGS_DEBUG="-Wall -hmsgs -eF -O thread1"
# 	export TANDAV_LDFLAGS="-L/home/users/n17421/Libs/lib"
# 	export TANDAV_CPPFLAGS="-I/home/users/n17421/Libs/include"

SHELL = /bin/bash 	# This should always be present in a Makefile

EXEC = Tandav

CC 	 	= $(TANDAV_CC)
LIBS 	= -lm -lgsl -lgslcblas $(TANDAV_LDFLAGS)
CFLAGS 	= $(TANDAV_CFLAGS) $(TANDAV_CPPFLAGS)

ifeq ($(MAKECMDGOALS),debug)
CFLAGS	= $(TANDAV_CFLAGS_DEBUG) $(TANDAV_CPPFLAGS)
endif

SRCDIR = src

SRCFILES := ${shell find $(SRCDIR) -name \*.c -print} # all .c files in SRCDIR

ifeq (,$(wildcard $(SRCDIR)/print_settings.c)) # add if missing
SRCFILES += $(SRCDIR)/print_settings.c 
endif

INCLFILES := ${shell find src -name \*.h -print} # all .h files in SRCDIR
INCLFILES += Config Makefile $(SRCDIR)/config.h

OBJFILES = $(SRCFILES:.c=.o)

# rules

%.o : %.c
	@echo [CC] $@
	@$(CC) $(CFLAGS)  -o $@ -c $<

$(EXEC) debug: $(OBJFILES) | settings
	$(CC) -g $(CFLAGS) $(OBJFILES) $(LIBS) -o $(EXEC)
	@ctags -w $(SRCFILES) $(INCLFILES)

$(OBJFILES)	: $(INCLFILES)

$(SRCDIR)/config.h : Config 
	@echo Generating $(SRCDIR)/config.h from Config
	@sed '/^#/d; /^$$/d; s/^/#define /g' Config > $(SRCDIR)/config.h

$(SRCDIR)/print_settings.c : Config	# does not work with sh, needs bash
	@echo Generating $(SRCDIR)/print_settings.c from Config
	@echo '/* Autogenerated File  */          '  > $(SRCDIR)/print_settings.c
	@echo '#include "includes.h"              ' >> $(SRCDIR)/print_settings.c
	@echo 'void Print_Compile_Time_Settings(){' >> $(SRCDIR)/print_settings.c
	@echo '	rprintf("Compiled with : \n"      ' >> $(SRCDIR)/print_settings.c
	@sed '/^#/d; /^$$/d; s/^/"      /g; s/$$/ \\n"/g;' Config >> \
		$(SRCDIR)/print_settings.c
	@echo '); return ;}                       ' >> $(SRCDIR)/print_settings.c

.PHONY : settings

settings :
	@echo " "
	@echo 'CC = ' $(CC)
	@echo 'CFLAGS =' $(CFLAGS)
	@echo 'LDFLAGS =' $(LIBS)
	@echo 'EXEC =' $(EXEC)
	@echo " "

clean : settings # remove all compiled files
	rm -f $(OBJFILES) $(EXEC) src/config.h src/print_settings.c \
		${shell find $(SRCDIR) -name \*.optrpt -print} tags

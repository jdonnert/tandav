# Compilation parameters in 'Config' file

SHELL = /bin/bash # This should always be present in a Makefile

ifndef SYSTYPE # set this in your ~/.bashrc or ~/.tcshrc
SYSTYPE := ${hostname}
endif

CC		 = mpicc
OPTIMIZE = -Wall -g -O2 
MPI_INCL = $(CPPFLAGS)
MPI_LIBS = $(LDFLAGS)
GSL_INCL =
GSL_LIBS = 
FFT_INCL =
FFT_LIBS =

# machine specifics
ifeq ($(SYSTYPE),"DARWIN")
CC       =  mpicc
OPTIMIZE = -O0 -g -Wall -lmpich -mtune=native -march=corei7 -ftree-vectorizer-verbose=2
MPI_LIBS = 
MPI_INCL = 
GSL_INCL =  
GSL_LIBS = 
FFT_INCL =
FFT_LIBS =
endif

ifeq ($(SYSTYPE),mach64.ira.inaf.it)
CC       =  mpicc
OPTIMIZE =  -g -O3 -march=bdver1 -mtune=native -mprefer-avx128 -mieee-fp -minline-all-stringops -fprefetch-loop-arrays --param prefetch-latency=300 -funroll-all-loops 
MPI_LIBS = -L/homes/donnert/Libs/lib
MPI_INCL = -I/homes/donnert/Libs/include
GSL_INCL =  
GSL_LIBS = 
FFT_INCL =
FFT_LIBS =
endif

ifeq ($(SYSTYPE),getorin.ira.inaf.it)
CC       =  mpicc
OPTIMIZE =  -g  -O3 -Wall -g -lmpich -finline -finline-functions -funroll-loops -xhost  -mkl -use-intel-optimized-headers -ipo -fast-transcendentals
MPI_LIBS = -L/homes/donnert/Libs/lib
MPI_INCL = -I/homes/donnert/Libs/include
GSL_INCL =  
GSL_LIBS = 
FFT_INCL =
FFT_LIBS =
endif

# end systypes

EXEC = Tandav

SRCDIR = src/

# do not add a ".c" file here, or "make clean" will send it into the abyss
OBJFILES = main.o aux.o cosmo.o domain.o update.o print_settings.o \
		   drift.o init.o kick.o setup.o time.o tree.o unit.o memory.o \
		   profile.o sort.o finish.o \
		   io/io.o \
		   		io/read_snapshot.o io/write_snapshot.o io/rw_parameter_file.o \
				io/write_restart_file.o io/read_restart_file.o

INCLFILES = config.h globals.h tree.h cosmo.h unit.h aux.h macro.h proto.h \
			memory.h profile.h io/io.h ../Makefile ../Config constants.h \
			kick.h setup.h update.h drift.h tree.h time.h

OBJS = $(addprefix $(SRCDIR),$(OBJFILES))
INCS = $(addprefix $(SRCDIR),$(INCLFILES))

CFLAGS = -std=c99 -fopenmp -g $(OPTIMIZE) $(GSL_INCL) $(MPI_INCL) $(FFT_INCL)

LIBS = -lm -lgsl -lgslcblas $(MPI_LIBS) $(GSL_LIBS) $(FFTW_LIBS)

%.o: %.c
	echo [CC] $@
	$(CC) $(CFLAGS)  -o $@ -c $<

$(EXEC)	: $(OBJS)
	@echo " "
	@echo 'SYSTYPE : ' $(SYSTYPE)
	@echo 'CFLAGS : ' $(CFLAGS)
	@echo 'EXEC : ' $(EXEC)
	@echo 'LIBS : ' $(LIBS)
	@echo " "
	$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o $(EXEC)
	@cd src && ctags  *.[ch]

$(OBJS)	: $(INCS)

$(SRCDIR)config.h : Config 
	@echo [CC] $(SRCDIR)config.h
	@sed '/^#/d; /^$$/d; s/^/#define /g' Config > $(SRCDIR)config.h

$(SRCDIR)print_settings.c : Config	# does not work with sh shell
	@echo [CC] $(SRCDIR)print_settings.c
	@echo '/* Autogenerated File  */' >  $(SRCDIR)print_settings.c
	@echo '#include "globals.h"' >>  $(SRCDIR)print_settings.c
	@echo '#include "proto.h"' >>  $(SRCDIR)print_settings.c
	@echo 'void print_compile_time_settings(){' >> $(SRCDIR)print_settings.c
	@echo '	rprintf("Compiled with : \n"' >> $(SRCDIR)print_settings.c
	@sed '/^#/d; /^$$/d; s/^/"      /g; s/$$/ \\n"/g;' Config >>  $(SRCDIR)print_settings.c
	@echo '); return ;}' >> $(SRCDIR)print_settings.c

clean : 
	rm -f $(OBJS) $(EXEC) src/config.h src/print_settings.c

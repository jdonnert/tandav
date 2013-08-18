/* Memory management */
#include "globals.h"

static void *Memory = NULL; 

static size_t NBytesLeft = 0;
static size_t MemSize = 0;
static size_t NMemBlocks = 0; // all blocks, also empty ones

static struct memory_block_infos {
	void * Start;
	size_t Size;
	char File[CHARBUFSIZE];
	char Func[CHARBUFSIZE];
	int Line;
} MemBlock[MAXMEMOBJECTS];

int find_memory_object_from_ptr(void *);
int find_free_object_from_size(const size_t);
size_t get_system_memory_size();

void *Malloc_info(const char* file, const char* func, const int line, 
		size_t size)
{
	if ( (size % MEM_ALIGNMENT) > 0) // make sure we stay aligned
		size = (size / MEM_ALIGNMENT + 1) * MEM_ALIGNMENT;

	if (size < MEM_ALIGNMENT)
		size = MEM_ALIGNMENT;

	Assert_Info(file, func, line, NBytesLeft >= size, 
			"Can't allocate Memory, %zu bytes wanted, %zu bytes left", 
			size, NBytesLeft);

	const int i = find_free_object_from_size(size);

printf("MALLOC, %d, %zu %d , %s %s %d\n", 
		i, size, NMemBlocks, file, func, line); 

	strncpy(MemBlock[i].File, file, CHARBUFSIZE);
	strncpy(MemBlock[i].Func, func, CHARBUFSIZE);
	MemBlock[i].Line = line;

	if (!MemBlock[i].Size) { // couldn't find a hole, add new at the end
		
		MemBlock[i].Start = Memory + MemSize - NBytesLeft;
	
		MemBlock[i].Size = size;
		
		NMemBlocks++;

		NBytesLeft -= size;
	}
Print_Memory_Usage();
	return MemBlock[i].Start;
}

void *Realloc_info(const char* file, const char* func, const int line, 
		void *ptr, size_t new_size)
{
	if (ptr == NULL)
		return Malloc_info(file, func, line, new_size);

	if ( (new_size % MEM_ALIGNMENT) > 0)
		new_size = (new_size / MEM_ALIGNMENT + 1) * MEM_ALIGNMENT;

	if (new_size < MEM_ALIGNMENT)
		new_size = MEM_ALIGNMENT;

	const int i = find_memory_object_from_ptr(ptr);
	int i_return = i; 

printf("REALLOC, %d, %zu %d %s %s %d  \n", 
		i, new_size, NMemBlocks, file, func, line); 

	if (i == NMemBlocks-1) { // enlarge last block

		const int delta = new_size - MemBlock[i].Size;

		MemBlock[i].Size += delta;

		NBytesLeft += delta;

		i_return = i;
	
	} else if (new_size < MemBlock[i].Size) { // Free and move

		printf("%zu %zu \n",  new_size, MemBlock[i].Size);
		
		void *dest = Malloc_info(file, func, line, new_size);
		void *src = MemBlock[i].Start;
		size_t nBytes = MemBlock[i].Size;

		memcpy(dest, src, nBytes);

		Free(MemBlock[i].Start);

		i_return = NMemBlocks-1;
	}

Print_Memory_Usage();
	return MemBlock[i_return].Start;
}

void Free_info(const char* file, const char* func, const int line, void *ptr) 
{
printf("Free,  %zu %d, %s %s %d \n",   NMemBlocks, file, func, line); fflush(stdout);
    if (ptr == NULL)        
		printf("WARNING Task %d. You tried to free a NULL pointer "
				"in file %s, function %s(), line %d\n", 
				Task.Rank, file, func, line);

	const int i = find_memory_object_from_ptr(ptr);

printf("Free, %d, %zu %d, %s %s %d \n", i,  NMemBlocks, file, func, line); 

	memset(MemBlock[i].Start, 0, MemBlock[i].Size);

	MemBlock[i].Start = NULL;
	strncpy(MemBlock[i].File,"",CHARBUFSIZE);
	strncpy(MemBlock[i].Func,"",CHARBUFSIZE);
	MemBlock[i].Line = 0;
	
    return ;
}

void Init_Memory_Management()
{
	MemSize = Param.MaxMemSize * 1024L * 1024L; // parameter is in MBytes

	size_t availNbytes = get_system_memory_size();

	size_t minNbytes = 0, maxNbytes = 0;

	MPI_Reduce(&availNbytes, &maxNbytes, 1, MPI_LONG_LONG, MPI_MAX, 0, 
			MPI_COMM_WORLD);
	MPI_Reduce(&availNbytes, &minNbytes, 1, MPI_LONG_LONG, MPI_MIN, 0, 
			MPI_COMM_WORLD);

	rprintf("Init Memory Manager\n"
			"   Max Usable Memory   %zu bytes \n" 
			"   Min Usable Memory   %zu bytes \n"
			"   Requested  Memory   %zu bytes \n", 
			maxNbytes, minNbytes, MemSize);

	int fail = posix_memalign(&Memory, MEM_ALIGNMENT, MemSize);

	Assert(!fail, "Couldn't allocate Memory. MaxMemSize too large ?");

	NBytesLeft = MemSize;

	memset(Memory, 0, NBytesLeft);

	return ;
}

void Print_Memory_Usage()
{
	size_t nBytesLeftGlobal[Sim.NTask];

	MPI_Allgather(&NBytesLeft, 1, MPI_BYTE, 
			nBytesLeftGlobal, sizeof(*nBytesLeftGlobal), MPI_BYTE, 
			MPI_COMM_WORLD);
	
	int minIdx = 0;

	for (int i = 0; i < Sim.NTask; i++)
		if (nBytesLeftGlobal[i] < nBytesLeftGlobal[minIdx])
			minIdx = i;

	if (Task.Rank == minIdx) {
		
		printf("\nMemory Manager: Reporting Task %d with %zu MB free memory\n"
			   "   No	Address        Size    Cumulative      "
			   "           Function  File:Line\n", Task.Rank, 
			   NBytesLeft/1024/1024);

		size_t memCumulative = 0;

		for (int i = 0; i < NMemBlocks; i++) {
			
			memCumulative += MemBlock[i].Size;

			printf("   %d	%p %7zu	 %8zu  %21s()  %s:%d\n",
				i, MemBlock[i].Start, MemBlock[i].Size/1024/1024, 
				memCumulative, MemBlock[i].File,MemBlock[i].Func, 
				MemBlock[i].Line);
		}

		printf("\n");
	}


	MPI_Barrier(MPI_COMM_WORLD);
	
	return ;
}

void Finish_Memory_Management()
{
	rprintf("Memory Manager: Freeing %d MB of Memory \n", Param.MaxMemSize);

	if (Memory != NULL)
		free(Memory);

	return ;
}

int find_memory_object_from_ptr(void *ptr)
{
	int i = 0;

	for (i = 0; i < NMemBlocks; i++) 
		if (ptr == MemBlock[i].Start)
			break;

	Assert(i < NMemBlocks,"Could not find memory object belonging to %p", ptr);

	return i;
}

int find_free_object_from_size(const size_t size)
{
	int i = 0;

	for (i = 0; i < NMemBlocks; i++)
		if ((MemBlock[i].Start == NULL) && (MemBlock[i].Size <= size))
			break;

	return i;
}

/* Get system memory size in a rather portable way
 *
 * Author:  David Robert Nadeau
 * Site:    http://NadeauSoftware.com/
 * License: Creative Commons Attribution 3.0 Unported License
 *          http://creativecommons.org/licenses/by/3.0/deed.en_US
 */

#if defined(__unix__) || defined(__unix) || defined(unix) || \
	(defined(__APPLE__) && defined(__MACH__))

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>

#if defined(BSD)
#include <sys/sysctl.h>
#endif // BSD

#endif // unix

size_t get_system_memory_size()
{
#if defined(__unix__) || defined(__unix) || defined(unix) || \
	(defined(__APPLE__) && defined(__MACH__))
	
#if defined(CTL_HW) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM64))

	int mib[2];
	mib[0] = CTL_HW;

#if defined(HW_MEMSIZE)
	mib[1] = HW_MEMSIZE;            // OSX.

#elif defined(HW_PHYSMEM64)
	mib[1] = HW_PHYSMEM64;          // NetBSD, OpenBSD.

#endif // HW_PHYSMEM64
	
	int64_t size = 0;               // 64-bit 

	size_t len = sizeof( size );
	
	if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
		return (size_t)size;

	return 0L;			// Failed?

#elif defined(_SC_AIX_REALMEM) // AIX
	return (size_t)sysconf( _SC_AIX_REALMEM ) * (size_t)1024L;

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE) 
	/* FreeBSD, Linux, OpenBSD, Solaris */
	
	return (size_t)sysconf( _SC_PHYS_PAGES ) *
		(size_t)sysconf( _SC_PAGESIZE );

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE) // Legacy

	return (size_t)sysconf( _SC_PHYS_PAGES ) *
		(size_t)sysconf( _SC_PAGE_SIZE );

#elif defined(CTL_HW) && (defined(HW_PHYSMEM) || defined(HW_REALMEM))
	/* BSD, OLD OSX */

	int mib[2];
	
	mib[0] = CTL_HW;

#if defined(HW_REALMEM)
	mib[1] = HW_REALMEM;		// FreeBSD
#elif defined(HW_PYSMEM)
	mib[1] = HW_PHYSMEM;		// Other 
#endif

	unsigned int size = 0;		// 32-bit

	size_t len = sizeof( size );
	
	if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
		return (size_t)size;
	
	return 0L;			// Failed? 
#endif // sysctl and sysconf variants
#endif // unix
}


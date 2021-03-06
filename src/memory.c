#include "memory.h"

#define MAXMEMOBJECTS 1000L

int posix_memalign(void **memptr, size_t alignment, size_t size);
static void merge_free_memory_blocks(int);
static int find_memory_block_from_ptr(void *);
static int reserve_free_block_from_size(const size_t);
static size_t get_system_memory_size();

static void *Memory = NULL;

static size_t NBytes_Left = 0; // size of block at the end
static size_t Mem_Size = 0;
static int NMem_Blocks = 0; // all blocks, also empty ones

static struct memory_block_infos {
	void * Start;
	size_t Size;
	char Name[CHARBUFSIZE];
	char File[CHARBUFSIZE];
	char Func[CHARBUFSIZE];
	int Line;
	bool In_Use;
} Mem_Block[MAXMEMOBJECTS];

static void *buffer; // Multi-purpose thread-safe buffer: BUFFER_SIZE
#pragma omp threadprivate(buffer)

void *Malloc_info(const char* file, const char* func, const int line,
		size_t size, const char *name)
{
	Assert_Info(file, func, line, size > 0, // check input
			"Can't allocate an array of size 0 !");

	Assert_Info(file, func, line, NBytes_Left >= size,
			"Can't allocate Memory, Bytes: %zu > %zu, %zu total",
			size, NBytes_Left, Mem_Size);

	size = MAX(MEM_ALIGNMENT, size); // don't break alignment

	if ( (size % MEM_ALIGNMENT) > 0) // make sure we stay aligned
		size = (size / MEM_ALIGNMENT + 1) * MEM_ALIGNMENT;

	int i = reserve_free_block_from_size(size);

	Mem_Block[i].In_Use = true;

	strncpy(Mem_Block[i].Name, name, CHARBUFSIZE);
	strncpy(Mem_Block[i].File, file, CHARBUFSIZE);
	strncpy(Mem_Block[i].Func, func, CHARBUFSIZE);
	Mem_Block[i].Line = line;

	if (i == NMem_Blocks) { // couldn't find a free gap, add new at the end

		Mem_Block[i].Start = Memory + Mem_Size - NBytes_Left;

		Mem_Block[i].Size = size;

		NMem_Blocks++;

		NBytes_Left -= size;
	}

	memset(Mem_Block[i].Start, 0, Mem_Block[i].Size);

	return Mem_Block[i].Start;
}

void *Realloc_info(const char* file, const char* func, const int line,
		void *ptr, size_t new_size, const char *name)
{
	if (new_size == 0) {

		if (ptr != NULL)
			Free(ptr);

		return NULL;
	}

	if (ptr == NULL) {

		ptr = Malloc_info(file, func, line, new_size, name);

		return ptr;
	}

	if ( (new_size % MEM_ALIGNMENT) > 0)
		new_size = (new_size / MEM_ALIGNMENT + 1) * MEM_ALIGNMENT;

	const int i = find_memory_block_from_ptr(ptr);

	int i_return = i;

	if (i == NMem_Blocks-1) { // enlarge last block

		const int delta = abs(new_size - Mem_Block[i].Size);
		
		Assert_Info(file, func,line, delta < NBytes_Left,
				"Not enough memory to Realloc %zu MB, have %d."
				" Increase MaxMem_Size ?", 
				delta/1024/1024, NBytes_Left/1024/1024);

		Mem_Block[i].Size += delta;

		NBytes_Left -= delta;

	} else if (new_size > Mem_Block[i].Size) { // move old to new and free

		void *dest = Malloc_info(file, func, line, new_size, name);

		void *src = Mem_Block[i].Start;

		size_t nBytes = Mem_Block[i].Size;

		memcpy(dest, src, nBytes);

		Free(Mem_Block[i].Start);

		i_return = NMem_Blocks - 1;

		printf("Moving Memory Block %d -> %d \n",i, i_return);
	}

	return Mem_Block[i_return].Start;
}

void Free_info(const char* file, const char* func, const int line, void *ptr) 
{
#ifdef DEBUG
	Warn(ptr == NULL, "You tried to free a NULL pointer in file "
		"%s, function %s() : %d\n", file, func, line);
#endif

	if (ptr == NULL)
		return;

	const int i = find_memory_block_from_ptr(ptr);

	memset(Mem_Block[i].Start, 0, Mem_Block[i].Size);

	Mem_Block[i].In_Use = false;
	strncpy(Mem_Block[i].Name, "", CHARBUFSIZE);
	strncpy(Mem_Block[i].File, "", CHARBUFSIZE);
	strncpy(Mem_Block[i].Func, "", CHARBUFSIZE);
	Mem_Block[i].Line = 0;

	merge_free_memory_blocks(i);

	ptr = NULL;

	return ;
}

void *Get_Thread_Safe_Buffer (size_t nBytes)
{
	Assert(nBytes <= Task.Buffer_Size, 
			"Requested too much Buffer space %d > %d"
			"Increase BUFFER_SIZE. ",
			nBytes, Task.Buffer_Size);
	
	memset(buffer, 0, nBytes);

	return buffer;
}

/*
 * Grab a huge chunk of memory and reserve a small chunk for every thread so
 * we a space to define thread-safe variable larger than the stack. The total
 * size is controlled by BUFFER_SIZE. Because we use the buffer for the
 * particle ordering it should at least hold Npart_Total_Max * sizeof(size_t).
 */

void Init_Memory_Management()
{

	#pragma omp parallel
	{

	Task.Buffer_Size = Param.Buffer_Size * 1024L * 1024L / NThreads;

	#pragma omp critical
	buffer = malloc(Task.Buffer_Size); // let the system take a local chunk
	
	memset(buffer, 0, Task.Buffer_Size);

	} // omp parallel

#ifdef MEMORY_MANAGER

	Mem_Size = (Param.Max_Mem_Size - Param.Buffer_Size) * 1024L * 1024L; // MB

	size_t nBytesMax = get_system_memory_size();

	size_t minNbytes = 0, maxNbytes = 0;

	MPI_Reduce(&nBytesMax, &maxNbytes, 1, MPI_LONG_LONG, MPI_MAX, MASTER,
			MPI_COMM_WORLD);
	MPI_Reduce(&nBytesMax, &minNbytes, 1, MPI_LONG_LONG, MPI_MIN, MASTER,
			MPI_COMM_WORLD);

	rprintf("Init Memory Manager\n"
			"   Max Usable Memory per task %zu bytes = %zu MB\n"
			"   Min Usable Memory per task %zu bytes = %zu MB\n"
			"   Requested  Memory per task %zu bytes = %zu MB\n\n",
			maxNbytes, maxNbytes/1024/1024, minNbytes,
			minNbytes/1024/1024, Mem_Size, Mem_Size/1024/1024);

	int fail = posix_memalign(&Memory, MEM_ALIGNMENT, Mem_Size);

	Assert(!fail, "Couldn't allocate Memory. MaxMemSize %d too large ?",
			Param.Max_Mem_Size);

	NBytes_Left = Mem_Size;

 	//memset(Memory, 0, NBytes_Left); // can be slow

#endif // MEMORY_MANAGER

	return;
}

void Print_Memory_Usage()
{
#ifdef MEMORY_MANAGER

	#pragma omp master
	{

	size_t nBytes_Left_Global[NRank];

	MPI_Allgather(&NBytes_Left, sizeof(NBytes_Left), MPI_BYTE,
			nBytes_Left_Global, sizeof(*nBytes_Left_Global),
			MPI_BYTE, MPI_COMM_WORLD);

	int max_Idx = 0;

	for (int i = 0; i < NRank; i++)
		if (nBytes_Left_Global[i] > nBytes_Left_Global[max_Idx])
			max_Idx = i;

	if (Task.Rank != max_Idx) // no returns inside an omp region
		goto skip;

	printf("\nMemory Manager: Reporting Blocks of (%d:%d) with %g / %d MB "
			"available memory\n   No  Used      Address      Size (MB)    "
			"Cumulative          Variable       File:Line\n"
			"-----------------------------------------------"
			"-------------------------------------------------------\n",
			Task.Rank, Task.Thread_ID, (double) NBytes_Left/1024/1024,
			Param.Max_Mem_Size);

	size_t mem_Cumulative = 0;

	for (int i = 0; i < NMem_Blocks; i++) {

		mem_Cumulative += Mem_Block[i].Size;

		printf("  %03d   %d    %11p     %7.3f      %8.3f   %20s  %s:%d\n",
			i,Mem_Block[i].In_Use, Mem_Block[i].Start,
			(double) Mem_Block[i].Size/1024/1024,
			(double) mem_Cumulative/1024/1024,
			Mem_Block[i].Name, Mem_Block[i].File,
			Mem_Block[i].Line);
	}

	printf("\nExternal Thread-Safe Buffer: %d x %g = %g MB, "
			"Minimum required: %g MB\n\n",
			NThreads, Task.Buffer_Size/1024.0/1024,
			(float) Param.Buffer_Size, 
			Task.Npart_Total_Max * NThreads * sizeof(uint64_t)/1024.0/1024);

	printf("\n");

	skip: ;

	} // omp master

	#pragma omp barrier

#endif

	return;
}

void Get_Free_Memory(size_t *total, size_t *largest, size_t *smallest)
{
	*total = *largest = *smallest = 0;

	for (int i = 0; i < NMem_Blocks; i++) {

		if (Mem_Block[i].In_Use)
			continue;

		int size = Mem_Block[i].Size;

		*total += size;

		*smallest = MIN(*smallest, size);
		*largest = MAX(*largest, size);
	}

	return ;
}

void Finish_Memory_Management()
{
	rprintf("\nMemory Manager: Freeing %d MB of Memory, "
			"%d MB of Omp buffer \n", Param.Max_Mem_Size, Param.Buffer_Size);

	if (Memory != NULL)
		free(Memory);

	#pragma omp parallel
	if (buffer != NULL)
		free(buffer);

	return ;
}

static int find_memory_block_from_ptr(void *ptr)
{
	int i = 0;

	for (i = 0; i < NMem_Blocks; i++)
		if (ptr == Mem_Block[i].Start)
			break;

	Assert(i < NMem_Blocks,
		"Could not find memory block belonging to %p", ptr);

	return i;
}

static int reserve_free_block_from_size(const size_t size)
{
	int i = 0;

	for (i = 0; i < NMem_Blocks; i++)
		if ( !Mem_Block[i].In_Use && (Mem_Block[i].Size >= size))
			break;

	Mem_Block[i].In_Use = true;

	return i;
}

/* merge memory blocks to minimize fragmentation */
static void merge_free_memory_blocks(int i)
{
	if (i == NMem_Blocks-1) { // Last, merge right into free

		NMem_Blocks--;
		NBytes_Left += Mem_Block[i].Size;

		Mem_Block[i].Start = NULL;
		Mem_Block[i].Size = 0;

		if (i != 0 && !Mem_Block[i-1].In_Use)
			merge_free_memory_blocks(i-1); // merge left into free

	} else if (!Mem_Block[i+1].In_Use) { // merge right

		Mem_Block[i].Size += Mem_Block[i+1].Size;

		void *src = &Mem_Block[i+2].Start;
		void *dest = &Mem_Block[i+1].Start;
		size_t nBytes = (NMem_Blocks - (i+1)) * sizeof(*Mem_Block);

		memmove(dest, src, nBytes);

		NMem_Blocks--;

	} else if (i != 0 && ! Mem_Block[i-1].In_Use) { // merge left

		Mem_Block[i-1].Size += Mem_Block[i].Size;

		void *src = &Mem_Block[i+1].Start;
		void *dest = &Mem_Block[i].Start;
		size_t nBytes = (NMem_Blocks - (i+1)) * sizeof(*Mem_Block);

		memmove(dest, src, nBytes);

		NMem_Blocks--;
	}

	return ;
}

/* Get system memory size in a rather portable way
 * This represents the hard upper bound, maybe leave 10% for the OS ?
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
		return (size_t) size;

	return 0L;			// Failed? 
#endif // sysctl and sysconf variants
#endif // unix
}

#include "globals.h"

/* Memory management */
void *Malloc_info(const char* file, const char* func, const int line, 
		size_t size)
{
	void *result = malloc(size);

	Assert_Info(func, file, line, result != NULL || size==0,
			"Allocation failed, %zu Bytes \n" ,size);

	return result;
}

void *Realloc_info(const char* file, const char* func, const int line, 
		void *ptr, size_t new_size)
{
	void * result = realloc(ptr, new_size);

	Assert_Info(func, file, line, result != NULL || new_size==0,
			"Reallocation failed: %zu bytes \n" ,new_size);

	return (result);
}

void Free_info(const char* file, const char* func, const int line, 
		void *ptr) 
{
    if (ptr != NULL)        
    	free(ptr);

	else
		printf("WARNING Task %d. You tried to free a NULL pointer "
				"in file %s, function %s(), line %d\n", 
				Task.Rank, file, func, line);
    return;
}

/* Reallocates the Particle structures. Takes the relative change
 * as argument, not the total number. Add or Remove via sign of nPart.
 * Also updates Task.Npart and Task.NPartTotal. 
 * Expands P so that space for nPart[type] is at offset[type]
 * Contracts P so that the last nPart[type] particles are removed */
void Reallocate_P_Info(const char *func, const char *file, int line, 
		int dNpart[NO_PART_TYPES], int offset_out[NO_PART_TYPES])
{
	size_t offset[NO_PART_TYPES] = { 0 };
	ptrdiff_t new_npart[NO_PART_TYPES] = { 0 };
	size_t new_npartTotal = 0;
	
	for (int type = 0; type < NO_PART_TYPES; type++) { // calc offset

		new_npart[type] = Task.Npart[type] + dNpart[type];
		
		new_npartTotal += new_npart[type];
		
        Assert(new_npart[type] >= 0, 
			"Can't alloc negative particles, type %d, delta %d, current %d,\n"
			"requested from %s, %s(), line %d", 
			type, dNpart[type], Task.Npart[type], file, func, line);

		if (dNpart[type]) 
			continue; // don't need offset here

		for (int i=0; i <= type; i++)
			offset[type] += new_npart[i];

		offset[type] -= max(0, dNpart[type]); // correct for dNpart > 0 
	}

	for (int type = 0; type < NO_PART_TYPES; type++) { // move left

		size_t src = offset[type] - dNpart[type]; 
		size_t dest = offset[type];
		ptrdiff_t npart = Task.NpartTotal - src;

		if (dNpart[type] < 0 && npart > 0)
			memmove(&P[dest], &P[src], npart*sizeof(*P));
	}

	size_t nBytes = sizeof(*P) * new_npartTotal;
	P = Realloc(P, nBytes);

	for (int type = 0; type < NO_PART_TYPES-1; type++) { // move right

		size_t src = offset[type];
		size_t dest = offset[type] + dNpart[type];
		ptrdiff_t npart = Task.NpartTotal - src;

		if (dNpart[type] > 0 && npart > 0)
			memmove(&P[dest], &P[src], npart*sizeof(*P));
	}

	Task.NpartTotal = new_npartTotal;
	
	for (int type = 0; type < NO_PART_TYPES; type++) // book keeping
		Task.Npart[type] = new_npart[type];

	if (offset_out != NULL) // return ptrs to freed space
		memcpy(offset_out, offset, NO_PART_TYPES);
		
	return ;
}

/* Error Handling, we use variable arguments to be able
 * to print more informative error messages */
void Assert_Info(const char *func, const char *file, int line,
		int expr, const char *errmsg, ...)
{
    if (expr)
        return;

	va_list varArgList;

	va_start(varArgList, errmsg);

	/* we fucked up, tell them */
    fprintf(stderr, "\nERROR Task %d: In file %s, function %s(), line %d :\n\n", 
			Task.Rank, file, func, line);

	vfprintf(stderr, errmsg, varArgList); 
	
	fprintf(stderr, "\n\n"); 
	
	fflush(stderr);

	va_end(varArgList);

    MPI_Abort(MPI_COMM_WORLD, -1); // finish him ...

    exit(-1); // ... fatality

    return;
}

#ifndef MEMPLUMB_H
#define MEMPLUMB_H
#include <stdlib.h>
#ifdef MEMPLUMB_ENABLE
// Starts the MemPlumb logger.
void MemPlumb_Startup(void);
// Stops the MemPlumb logger.
void MemPlumb_Shutdown(void);
// Inner functionality of our 'malloc' replacement. Don't call this yourself (unless you really want to, I guess).
void *MemPlumb_Malloc(size_t allocSize, int lineNum, char const *fileName);
// Our 'malloc' drop-in replacement macro.
#define MP_Malloc(allocSize) MemPlumb_Malloc(allocSize, __LINE__, __FILE__)
// Inner functionality of our 'realloc' replacement. Don't call this yourself (unless you really want to, I guess).
void *MemPlumb_Realloc(void *allocPtr, size_t newSize, int lineNum, char const *fileName);
// Our 'realloc' drop-in replacement macro.
#define MP_Realloc(allocPtr, newSz) MemPlumb_Realloc(allocPtr, newSz, __LINE__, __FILE__)
// Inner functionality of our 'free' replacement. Don't call this yourself (unless you really want to, I guess).
void MemPlumb_Free(void *allocPtr, int lineNum, char const *fileName);
// Our 'free' drop-in replacement macro.
#define MP_Free(allocPtr) MemPlumb_Free(allocPtr, __LINE__, __FILE__)
#ifdef MEMPLUMB_MASTER
#include <stdio.h>
#include <string.h>
#if __STDC_VERSION__ >= 201112L
#include <stddef.h>
#define MEMPLUMB_ALIGNMENT sizeof(max_align_t)
#else
#define MEMPLUMB_ALIGNMENT 16U // Fallback for C99/older compilers
#endif
struct tMemPlumbEntry
{
	size_t allocSize;
	char unsigned *allocAddr;
	char *fileName;
	int lineNum;
	int isFree;
};
static struct
{
	struct tMemPlumbEntry *dat;
	size_t cap;
	size_t lng;
	FILE *out;
}
MemPlumb_Diary;
void MemPlumb_Startup(void)
{
	MemPlumb_Diary.dat = calloc(1, sizeof * MemPlumb_Diary.dat);
	if (MemPlumb_Diary.dat == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Could not allocate memory for MemPlumb_Diary\n");
		exit(1);
	}
	MemPlumb_Diary.cap = 1;
	MemPlumb_Diary.lng = 0;
	MemPlumb_Diary.out = fopen("MemPlumbDiary.txt", "w");
	if (MemPlumb_Diary.out == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Could not open output file for MemPlumb_Diary\n");
		exit(1);
	}
}
static void MemPlumb_Validate(struct tMemPlumbEntry *ent)
{
	for (size_t idx = 0; idx < MEMPLUMB_ALIGNMENT; ++idx) if (ent->allocAddr[idx] != 0XFD) fprintf(MemPlumb_Diary.out, "UNDERFLOW: Overwrote byte %zu of allocation pre-guard, expected 0XFD, got 0X%2X. Allocated @ %s:%d\n", idx, ent->allocAddr[idx], ent->fileName, ent->lineNum);
	for (size_t idx = 0; idx < MEMPLUMB_ALIGNMENT; ++idx) if (ent->allocAddr[ent->allocSize + MEMPLUMB_ALIGNMENT + idx] != 0XFD) fprintf(MemPlumb_Diary.out, "OVERFLOW: Overwrote byte %zu of allocation post-guard, expected 0XFD, got 0X%2X. Allocated @ %s:%d\n", idx, ent->allocAddr[ent->allocSize + MEMPLUMB_ALIGNMENT + idx], ent->fileName, ent->lineNum);
}
void MemPlumb_Shutdown(void)
{
	for (size_t idx = 0; idx < MemPlumb_Diary.lng; ++idx)
	{
		struct tMemPlumbEntry *ent = &MemPlumb_Diary.dat[idx];
		if (ent->isFree == 0)
		{
			if (ent->allocAddr)
			{
				MemPlumb_Validate(ent);
				free(ent->allocAddr);
			}
			fprintf(MemPlumb_Diary.out, "LEAK: Allocated @ %s:%d, no matching MP_Free found\n", ent->fileName, ent->lineNum);
		}
		if (ent->fileName) free(ent->fileName);
	}
	fflush(MemPlumb_Diary.out);
	fclose(MemPlumb_Diary.out);
	free(MemPlumb_Diary.dat);
}
static struct tMemPlumbEntry *MemPlumb_Search(void *ptr)
{
	for (size_t idx = MemPlumb_Diary.lng; idx != 0; --idx)
	{
		struct tMemPlumbEntry *ent = &MemPlumb_Diary.dat[idx - 1];
		if (ent->allocAddr == ptr) return ent;
	}
	return NULL;
}
static void MemPlumb_Grow(int lineNum, char const *fileName)
{
	if (MemPlumb_Diary.cap == MemPlumb_Diary.lng)
	{
		void *newDiaryPtr = realloc(MemPlumb_Diary.dat, (MemPlumb_Diary.cap << 1U) * (sizeof * MemPlumb_Diary.dat));
		if (newDiaryPtr == NULL)
		{
			fprintf(stderr, "ERR (MemPlumb): Could not reallocate memory for MemPlumb_Diary @ %s:%d\n", fileName, lineNum);
			fflush(MemPlumb_Diary.out);
			fclose(MemPlumb_Diary.out);
			exit(1);
		}
		MemPlumb_Diary.dat = newDiaryPtr;
		memset(&MemPlumb_Diary.dat[MemPlumb_Diary.cap], 0U, MemPlumb_Diary.cap * (sizeof * MemPlumb_Diary.dat));
		MemPlumb_Diary.cap <<= 1U;
	}
}
void *MemPlumb_Malloc(size_t allocSize, int lineNum, char const *fileName)
{
	MemPlumb_Grow(lineNum, fileName);
	void *dat = malloc(allocSize + (MEMPLUMB_ALIGNMENT << 1U));
	if (dat == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Could not allocate %zu bytes requested @ %s:%d\n", allocSize, fileName, lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	struct tMemPlumbEntry *ent = MemPlumb_Search(dat);
	if (ent == NULL) ent = &MemPlumb_Diary.dat[MemPlumb_Diary.lng++];
	ent->isFree = 0;
	ent->allocAddr = dat;
	ent->allocSize = allocSize;
	// Poison bytes
	memset(&ent->allocAddr[0], 0XFD, MEMPLUMB_ALIGNMENT);
	memset(&ent->allocAddr[MEMPLUMB_ALIGNMENT], 0XCD, allocSize);
	memset(&ent->allocAddr[MEMPLUMB_ALIGNMENT + allocSize], 0XFD, MEMPLUMB_ALIGNMENT);
	ent->lineNum = lineNum;
	if (ent->fileName) free(ent->fileName);
	size_t strLng = strlen(fileName);
	ent->fileName = malloc(strLng + 1);
	if (ent->fileName == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Could not allocate memory for allocation's file name @ %s:%d\n", fileName, lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	memcpy(ent->fileName, fileName, strLng);
	ent->fileName[strLng] = '\0';
	return &ent->allocAddr[MEMPLUMB_ALIGNMENT];
}
void *MemPlumb_Realloc(void *allocPtr, size_t newSize, int lineNum, char const *fileName)
{
	if (allocPtr == NULL) return MemPlumb_Malloc(newSize, lineNum, fileName);
	if (newSize == 0U)
	{
		fprintf(stderr, "ERR (MemPlumb): realloc(ptr, 0) has implementation specific behaviour, and in C23 was determined to be UB. MemPlumb does not support it as it has no guarantee of portability @ %s:%d\n", fileName, lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	MemPlumb_Grow(lineNum, fileName);
	void *realPtr = (unsigned char *)allocPtr - MEMPLUMB_ALIGNMENT;
	struct tMemPlumbEntry *oldEnt = MemPlumb_Search(realPtr);
	if (oldEnt == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Attempted to realloc pointer value that wasn't allocated through MemPlumb @ %s:%d\n", fileName, lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	MemPlumb_Validate(oldEnt);
	size_t oldSize = oldEnt->allocSize;
	void *newPtr = realloc(oldEnt->allocAddr, newSize + (MEMPLUMB_ALIGNMENT << 1U));
	if (newPtr == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Could not reallocate %zu bytes requested @ %s:%d\n", newSize, fileName, lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	oldEnt->isFree = 1;
	struct tMemPlumbEntry *newEnt = MemPlumb_Search(newPtr);
	if (newEnt == NULL) newEnt = &MemPlumb_Diary.dat[MemPlumb_Diary.lng++];
	newEnt->allocAddr = newPtr;
	newEnt->allocSize = newSize;
	memset(&newEnt->allocAddr[0], 0XFD, MEMPLUMB_ALIGNMENT);
	if (oldSize < newSize) memset(&newEnt->allocAddr[MEMPLUMB_ALIGNMENT + oldSize], 0XCD, newSize - oldSize);
	memset(&newEnt->allocAddr[MEMPLUMB_ALIGNMENT + newSize], 0XFD, MEMPLUMB_ALIGNMENT);
	newEnt->isFree = 0;
	newEnt->lineNum = lineNum;
	if (newEnt->fileName) free(newEnt->fileName);
	size_t strLng = strlen(fileName);
	newEnt->fileName = malloc(strLng + 1);
	if (newEnt->fileName == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Could not allocate memory for reallocation's file name @ %s:%d\n", fileName, lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	memcpy(newEnt->fileName, fileName, strLng);
	newEnt->fileName[strLng] = '\0';
	return &newEnt->allocAddr[MEMPLUMB_ALIGNMENT];
}
void MemPlumb_Free(void *allocPtr, int lineNum, char const *fileName)
{
	if (allocPtr == NULL) return;
	void *realPtr = (unsigned char *)allocPtr - MEMPLUMB_ALIGNMENT;
	struct tMemPlumbEntry *ent = MemPlumb_Search(realPtr);
	if (ent == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Attempted to free pointer value that wasn't allocated through MemPlumb @ %s:%d\n", fileName, lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	if (ent->isFree)
	{
		fprintf(stderr, "ERR (MemPlumb): Attempted to double free pointer @ %s:%d\n", fileName, lineNum);
		fprintf(MemPlumb_Diary.out, "DOUBLE FREE: Attempting to free pointer @ %s:%d, previously freed @ %s:%d\n", fileName, lineNum, ent->fileName, ent->lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	MemPlumb_Validate(ent);
	memset(&ent->allocAddr[0], 0XFD, MEMPLUMB_ALIGNMENT);
	memset(&ent->allocAddr[MEMPLUMB_ALIGNMENT], 0XDD, ent->allocSize);
	memset(&ent->allocAddr[MEMPLUMB_ALIGNMENT + ent->allocSize], 0XFD, MEMPLUMB_ALIGNMENT);
	ent->isFree = 1;
	free(ent->allocAddr);
	if (ent->fileName) free(ent->fileName);
	size_t strLng = strlen(fileName);
	ent->fileName = malloc(strLng + 1);
	if (ent->fileName == NULL)
	{
		fprintf(stderr, "ERR (MemPlumb): Could not allocate memory for reallocation's file name @ %s:%d\n", fileName, lineNum);
		fflush(MemPlumb_Diary.out);
		fclose(MemPlumb_Diary.out);
		exit(1);
	}
	ent->lineNum = lineNum;
	memcpy(ent->fileName, fileName, strLng);
	ent->fileName[strLng] = '\0';
}
#undef MEMPLUMB_ALIGNMENT
#endif//MEMPLUMB_MASTER
#else//~MEMPLUMB_ENABLE
#define MemPlumb_Startup() ((void)0U)
#define MemPlumb_Shutdown() ((void)0U)
#define MP_Malloc(sz) malloc(sz)
#define MP_Realloc(ptr, sz) realloc(ptr, sz)
#define MP_Free(ptr) free(ptr)
#endif//MEMPLUMB_ENABLE
#endif//MEMPLUMB_H
/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

static char *heap_listp = 0;

static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void *coalesce(void *ptr);

/* header/footer size */
#define WSIZE 4 

/* single word (4) or double word (8) alignment */
#define DSIZE 8

/* rounds up to the nearest multiple of DSIZE */
#define ALIGN(size) (((size) + (DSIZE - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Extend heap by this amount (bytes) */
#define CHUNKSIZE (1<<12)

#define MAX(x, y) ((x) > (y) ? (x): (y))                                                                                                                        

/* pack a size and allocate bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)  // 저장된 주소에서 블록 크기 읽음(header+payload+footer)
#define GET_ALLOC(p) (GET(p) & 0x1)  // 저장된 주소에서 할당 여부 읽음

/* given block ptr bp, compute address of its header and footer */
/* bp: payload 시작 주소 */
/* HDRP: 헤더 시작 주소 */
/* FTRP: 풋터 시작 주소 */
#define HDRP(bp) ((char *)(bp) - WSIZE) 
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
// 현재 블록의 헤더 크기를 읽어서 앞으로 이동(다음 블록의 시작점으로 이동)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))  
// 이전 블록의 풋터 크기를 읽어서 뒤로 이동(이전 블록의 시작점으로 이동)
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))  

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    heap_listp = mem_sbrk(4 * WSIZE);
    if (heap_listp == (void *)-1)
    {
        return -1;
    }

    PUT(heap_listp, 0);  // padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));  // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));  // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));      // epilogue header
    heap_listp += (2 * WSIZE);

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
    {
        return -1;
    }

    return 0;
}

/* 힙 크기 늘리기 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    bp = mem_sbrk(size);
    if (bp == (void *) - 1)
    {
        return NULL;
    }

    PUT(HDRP(bp), PACK(size, 0));  // free block header
    PUT(FTRP(bp), PACK(size, 0));  // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  // new epilogue header

    return bp;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      // 요청한 크기를 내부 규칙에 맞게 조정한 최종 블록 크기
    size_t extendsize; // 적당한 크기가 없으면 heap을 얼마나 더 늘릴지 나타내는 크기
    char *bp;

    if (size == 0)
    {
        return NULL;
    }

    // size를 할당기 내부에서 쓸 실제 블록 크기 asize로 바꾸기
    if (size <= DSIZE)
    {
        asize = 2 * DSIZE;
    }
    else
    {
        asize = ALIGN(size + DSIZE);
    }

    // 맞는 비할당 블럭 찾기
    bp = find_fit(asize);

    // 블럭에 할당하기
    if (bp != NULL)
    {
        place(bp, asize);
        return bp;
    }

    // 맞는 블럭이 없다면 메모리 추가하고 블럭 할당
    extendsize = MAX(asize, CHUNKSIZE);
    bp = extend_heap(extendsize/WSIZE);
    if (bp == NULL)
    {
        return NULL;
    }

    place(bp, asize);
    return bp;
}

/* 맞는 블럭 찾기 */
static void *find_fit(size_t asize)
{
    void *bp;
    
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        // 블럭이 할당되지 않고, 크기가 필요한 크기 이상일 때
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp)))
        {
            return bp;
        }
    }
    return NULL;
}

/* free block 안에 요청 블록 배치 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));

    // 나누고 남는 조각이 최소 블록 크기 이상이면 split
    if ((csize - asize) >= (2 * DSIZE))
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    // 현재 블럭 해제
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    coalesce(ptr);
}

static void *coalesce(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    // 이전 블럭과 이후 블럭 할당 여부 확인
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

    // 둘 다 할당되어 있다면
    if (prev_alloc && next_alloc)
    {
        return ptr;
    }
    // 이전 블럭 할당됨, 이후 블럭 할당 안됨
    else if (prev_alloc && !next_alloc)
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }
    // 이전 블럭 할당 안됨, 이후 블럭 할당됨
    else if (!prev_alloc && next_alloc)
    {
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));

        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    // 둘 다 할당 가능
    else
    {
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));

        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(HDRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    return ptr;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return mm_malloc(size);
    }

    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    void *newptr = mm_malloc(size);
    if (newptr == NULL)
    {
        return NULL;
    }

    size_t oldSize = GET_SIZE(HDRP(ptr)) - DSIZE;
    size_t copySize = size < oldSize ? size : oldSize;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}
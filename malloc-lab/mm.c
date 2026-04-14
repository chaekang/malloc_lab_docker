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
#include <stdint.h>

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
static char *free_list_head = NULL;

static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void *coalesce(void *ptr);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);

/* header/footer size */
#define WSIZE 4 

/* single word (4) or double word (8) alignment */
#define DSIZE 8

/* Extend heap by this amount (bytes) */
#define CHUNKSIZE (1<<12)

/* rounds up to the nearest multiple of DSIZE */
#define ALIGN(size) (((size) + (DSIZE - 1)) & ~0x7)

#define MAX(x, y) ((x) > (y) ? (x): (y))                                                                                                                        

/* pack a size and allocate bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* read and write a word at address p */
// 헤더, 풋터용
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

#define PTRSIZE sizeof(void *)  // 포인터 크기

// pred/succ에 사용
#define GET_PTR(p) (*(void **)(p))  // 주소 p에 저장된 포인터 하나 읽기
#define PUT_PTR(p, ptr) (*(void **)(p) = (ptr))  // 주소 p에 포인터 val 저장하기

// pred, succ 포인터가 저장되는 슬롯의 주소
#define PRED_PTR(bp) ((char *)(bp))  // free block payload의 맨 앞칸
#define SUCC_PTR(bp) ((char *)(bp) + PTRSIZE)  // pred 바로 다음 칸

#define PRED(bp) (GET_PTR(PRED_PTR(bp)))  // bp 블록의 pred 값 읽기
#define SUCC(bp) (GET_PTR(SUCC_PTR(bp)))  // bp 블록의 succ 값 읽기

#define MIN_FREE_BLOCK_SIZE ALIGN(WSIZE + PTRSIZE + PTRSIZE + WSIZE)  // free block의 전체 크기

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    free_list_head = NULL;

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

    return coalesce(bp);
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
    asize = ALIGN(size + DSIZE);
    if (asize <= MIN_FREE_BLOCK_SIZE)
    {
        asize = MIN_FREE_BLOCK_SIZE;
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
    char *bp = free_list_head;

    while (bp != NULL)
    {
        if (GET_SIZE(HDRP(bp)) >= asize)
        {
            return bp;
        }
        bp = SUCC(bp);
    }
    return NULL;
}

/* free block 안에 요청 블록 배치 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    remove_free_block(bp);

    // 나누고 남는 조각이 최소 블록 크기 이상이면 split
    if ((csize - asize) >= MIN_FREE_BLOCK_SIZE)
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        void *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(csize - asize, 0));
        PUT(FTRP(next_bp), PACK(csize - asize, 0));
        insert_free_block(next_bp);
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

// 블록이 free 되었을 때 free list에 삽입
static void insert_free_block(void *bp)
{
    SUCC(bp) = free_list_head;
    PRED(bp) = NULL;

    if (free_list_head != NULL)
    {
        PRED(free_list_head) = bp;
    }
    free_list_head = bp;
}

// 블록이 할당되었을 때 free list에서 삭제
static void remove_free_block(void *bp)
{
    void *prev = PRED(bp);
    void *next = SUCC(bp);

    if (prev != NULL)
    {
        SUCC(prev) = next;
    }
    else
    {
        free_list_head = next;
    }

    if (next != NULL)
    {
        PRED(next) = prev;
    }

    PRED(bp) = NULL;
    SUCC(bp) = NULL;
}

/*
 * mm_free - 사용자가 반납한 블록의 할당을 해제하는 함수
 */
void mm_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    // 현재 블럭 해제
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    coalesce(ptr);
}

static void *coalesce(void *ptr)
{
    // 이전 블럭과 이후 블럭 할당 여부 확인
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

    size_t size = GET_SIZE(HDRP(ptr));

    // 둘 다 할당되어 있다면
    if (prev_alloc && next_alloc)
    {
        insert_free_block(ptr);
        return ptr;
    }
    // 이전 블럭 할당됨, 이후 블럭 할당 안됨
    else if (prev_alloc && !next_alloc)
    {
        void *next_bp = NEXT_BLKP(ptr);
        
        size += GET_SIZE(HDRP(next_bp));
        remove_free_block(next_bp);

        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));

        insert_free_block(ptr);
        return ptr;
    }
    // 이전 블럭 할당 안됨, 이후 블럭 할당됨
    else if (!prev_alloc && next_alloc)
    {
        void *prev_bp = PREV_BLKP(ptr);

        size += GET_SIZE(HDRP(prev_bp));
        remove_free_block(prev_bp);

        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));

        insert_free_block(prev_bp);
        return prev_bp;
    }
    // 둘 다 할당 가능
    else
    {
        void *prev_bp = PREV_BLKP(ptr);
        void *next_bp = NEXT_BLKP(ptr);

        remove_free_block(prev_bp);
        remove_free_block(next_bp);

        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));

        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(next_bp), PACK(size, 0));

        insert_free_block(prev_bp);
        return prev_bp;
    }
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

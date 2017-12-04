// Homework 11 Tests, Fall 2017
// Written by Sir Austin J. Adams IV, Esq.
//
// Warning: much the structure of this file is shamelessly copypasted from
// https://libcheck.github.io/check/doc/check_html/check_3.html

#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "my_malloc.h"

#define SBRK_SIZE 2048
#define HEAP_SIZE 0x2000
#define TOTAL_METADATA_SIZE (sizeof(metadata_t) + sizeof(int))
#define MIN_BLOCK_SIZE (TOTAL_METADATA_SIZE + 1)
#define CANARY_MAGIC_NUMBER 0xE629

extern metadata_t *freelist;

static bool my_sbrk_called;
static bool my_sbrk_call_expected;
static uint8_t *my_sbrk_fake_heap;
// Determines the position of the break at the beginning of the tests
static int my_sbrk_imaginary_prev_calls;
// Fake my_sbrk()
void *my_sbrk(int increment) {
    ck_assert_int_eq(increment, SBRK_SIZE);
    ck_assert(my_sbrk_call_expected);
    ck_assert(!my_sbrk_called);
    my_sbrk_called = true;

    // Handle OOM
    if (my_sbrk_imaginary_prev_calls == HEAP_SIZE/SBRK_SIZE) {
        return NULL;
    } else {
        return my_sbrk_fake_heap + my_sbrk_imaginary_prev_calls * SBRK_SIZE;
    }
}

void setup_malloc_malloc(void) {
    freelist = NULL;
    my_sbrk_called = false;
    my_sbrk_fake_heap = calloc(1, HEAP_SIZE);
    // Out of memory. Shouldn't happen but check anyway
    ck_assert(my_sbrk_fake_heap);
}

void teardown_malloc_malloc(void) {
    free(my_sbrk_fake_heap);
    my_sbrk_fake_heap = NULL;
}

// Called by a test to set up freelist and fake heap
void init_malloc_test(int prev_sbrk_calls, bool should_sbrk) {
    my_sbrk_call_expected = should_sbrk;
    my_sbrk_imaginary_prev_calls = prev_sbrk_calls;
}

void create_situation_1(metadata_t **Aout, metadata_t **Bout, metadata_t **Cout) {
    // Construct the following situation:
    //                  +768+TMS   +960+2*TMS
    //                 /          /
    //                 |         .
    //  +0      +512   |     +896+TMS +1216+2*TMS
    // /       /       |    /    .   /
    // |       |       |    |    |   |
    //  ---------------------------------   ---
    // |///////|       |////|    |///|   ...   |
    // |///////|     A |////|  B |///|   ... C |
    //  ---------------------------------   ---
    //  \_____/\______/\____/\__/\___/\________/
    //     /     |        |   |    \       |
    //   512   256+TMS  128  64+TMS 256  SBRK_SIZE-1216-2*TMS
    // (in use)       (in use)
    //
    // freelist->B->A->C

    // Setup C
    metadata_t *C = (metadata_t *)(my_sbrk_fake_heap + 1216 + 2*TOTAL_METADATA_SIZE);
    C->next = NULL;
    C->size = SBRK_SIZE - 1216 - 2*TOTAL_METADATA_SIZE;
    if (Cout) *Cout = C;

    // Setup A
    metadata_t *A = (metadata_t *)(my_sbrk_fake_heap + 512);
    A->next = C;
    A->size = 256 + TOTAL_METADATA_SIZE;
    if (Aout) *Aout = A;

    // Setup B
    metadata_t *B = (metadata_t *)(my_sbrk_fake_heap + 896 + TOTAL_METADATA_SIZE);
    B->next = A;
    B->size = 64 + TOTAL_METADATA_SIZE;
    // Put B at the head of the freelist
    freelist = B;
    // Return B
    if (Bout) *Bout = B;
}

//
// malloc() tests
//

// sbrk()ing and allocating some space works
START_TEST(test_malloc_malloc_initial) {
    init_malloc_test(0, true);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(128);

    // Check return value
    ck_assert(ret);
    uint8_t *split_left_choice = my_sbrk_fake_heap + sizeof (metadata_t);
    uint8_t *split_right_choice =  my_sbrk_fake_heap + SBRK_SIZE - sizeof (int) - 128;
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 128 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They called sbrk()
    ck_assert(my_sbrk_called);
    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains one node
    ck_assert(freelist);
    ck_assert(!freelist->next);
    ck_assert_int_eq(freelist->size, SBRK_SIZE - 128 - TOTAL_METADATA_SIZE);
    // Check address of this node
    if (ret == split_left_choice) {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap + 128 + TOTAL_METADATA_SIZE);
    } else {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap);
    }
}
END_TEST

// Allocating some space after sbrk() has already occurred works
START_TEST(test_malloc_malloc_initial_sbrked) {
    init_malloc_test(1, false);
    freelist = (metadata_t *)my_sbrk_fake_heap;
    freelist->size = SBRK_SIZE;
    freelist->next = NULL;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(64);

    // Check return value
    ck_assert(ret);
    uint8_t *split_left_choice = my_sbrk_fake_heap + sizeof (metadata_t);
    uint8_t *split_right_choice =  my_sbrk_fake_heap + SBRK_SIZE - sizeof (int) - 64;
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 64 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains one node
    ck_assert(freelist);
    ck_assert(!freelist->next);
    ck_assert_int_eq(freelist->size, SBRK_SIZE - 64 - TOTAL_METADATA_SIZE);
    // Check address of this node
    if (ret == split_left_choice) {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap + 64 + TOTAL_METADATA_SIZE);
    } else {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap);
    }
}
END_TEST

// Test that they merge after sbrk()ing
START_TEST(test_malloc_malloc_sbrk_merge) {
    // printf("Starting SBRK Test \n");
    init_malloc_test(1, true);

    // Construct the following situation:
    //  +0      +32    +160+TMS      +SBRK_SIZE-64-TMS
    // /       /       /             /    +SBRK_SIZE
    // |       |       |             |   /
    //  ---------------------   ---------
    // |///////|       |/////.../////|   |
    // |///////|     A |/////.../////|  B|
    //  ---------------------   ---------
    //  \_____/\______/\_____________/\__/
    //     /     |            |         \___
    //    32   128+TMS SBRK_SIZE-224-2*TMS  64+TMS
    // (in use)            (in use)
    //
    // freelist->B->A

    metadata_t *A = (metadata_t *)(my_sbrk_fake_heap + 32);
    A->size = 128 + TOTAL_METADATA_SIZE;
    A->next = NULL;

    metadata_t *B = (metadata_t *)(my_sbrk_fake_heap + SBRK_SIZE - 64 - TOTAL_METADATA_SIZE);
    B->size = 64 + TOTAL_METADATA_SIZE;
    B->next = A;
    freelist = B;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(256);

    // After requesting 256 bytes, it should look like one of these.
    // Remember, you have to merge after sbrk()ing!
    //
    // If left split:
    // --------------
    //  +0      +32    +160+TMS      +SBRK_SIZE-64-TMS  +2*SBRK_SIZE
    // /       /       /             /       +SBRK_SIZE |
    // |       |       |             |      /  +192     /
    //  ---------------------   ----------------   ----
    // |///////|       |/////.../////|     |    ...    |
    // |///////|     A |/////.../////|    C|    ...   D|
    //  ---------------------   ----------- ----   ----
    //  \_____/\______/\_____________/\____/\__________/
    //     /     |            |           \          |
    //    32   128+TMS SBRK_SIZE-224-2*TMS 256+TMS SBRK_SIZE-192
    // (in use)            (in use)
    //
    // freelist->A->D
    // returned: C
    //
    // If right split:
    // ---------------
    //  +0      +32    +160+TMS      +SBRK_SIZE-64-TMS
    // /       /       /             / +2*SBRK_SIZE-256-TMS
    // |       |       |             |           \     ,-- +2*SBRK_SIZE
    //  ---------------------   ----------   ----------
    // |///////|       |/////.../////|    ...    |     |
    // |///////|     A |/////.../////|    ...   D|    C|
    //  ---------------------   ----------   ----------
    //  \_____/\______/\_____________/\__________/\____/
    //     /     |            |             \        \____
    //    32   128+TMS SBRK_SIZE-224-2*TMS SBRK_SIZE-192  256+TMS
    // (in use)            (in use)
    //
    // freelist->A->D
    // returned: C

    // Check return value
    ck_assert(ret);
    uint8_t *split_left_choice = my_sbrk_fake_heap + SBRK_SIZE - 64 - TOTAL_METADATA_SIZE + sizeof (metadata_t);
    uint8_t *split_right_choice = my_sbrk_fake_heap + 2*SBRK_SIZE - 256 - TOTAL_METADATA_SIZE + sizeof (metadata_t);
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 256 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They called sbrk()
    ck_assert(my_sbrk_called);
    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains two nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(!freelist->next->next);
    ck_assert_int_eq(freelist->size, 128 + TOTAL_METADATA_SIZE);
    ck_assert_int_eq(freelist->next->size, SBRK_SIZE - 192);
    ck_assert_ptr_eq(freelist, A);
    // Check address of this node
    uint8_t *split_left_D = my_sbrk_fake_heap + SBRK_SIZE + 192;
    uint8_t *split_right_D = my_sbrk_fake_heap + SBRK_SIZE - 64 - TOTAL_METADATA_SIZE;

    if (ret == split_left_choice) {
        ck_assert_ptr_eq((uint8_t *)freelist->next, split_left_D);
    } else {
        ck_assert_ptr_eq((uint8_t *)freelist->next, split_right_D);
    }
    //printf("Ending SBRK Test\n");
}
END_TEST

// Check the smallest available choice is chosen
START_TEST(test_malloc_malloc_perfect1) {
    // printf("Starting Perfect1 Test\n");
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(64);

    // Check return value
    ck_assert(ret);
    ck_assert(ret == (uint8_t *)(B + 1));

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 64 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains two nodes
    ck_assert_ptr_eq(freelist, A);
    ck_assert_ptr_eq(A->next, C);
    ck_assert(!C->next);
}
END_TEST

// Check the smallest choice which fits is chosen
START_TEST(test_malloc_malloc_perfect2) {
    // printf("Starting Next Test \n");
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(256);

    // Check return value
    ck_assert(ret);
    ck_assert(ret == (uint8_t *)(A + 1));

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 256 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains two nodes
    ck_assert_ptr_eq(freelist, B);
    ck_assert_ptr_eq(B->next, C);
    ck_assert(!C->next);
}
END_TEST

// Check the smallest choice which fits is chosen
START_TEST(test_malloc_malloc_perfect3) {
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_size = C->size;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(C->size - TOTAL_METADATA_SIZE);

    // Check return value
    ck_assert(ret);
    ck_assert(ret == (uint8_t *)(C + 1));

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, expected_size);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains two nodes
    ck_assert_ptr_eq(freelist, B);
    ck_assert_ptr_eq(B->next, A);
    ck_assert(!A->next);
}
END_TEST

START_TEST(test_malloc_malloc_split1) {
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_C_size = C->size;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(32);

    // Check return value
    // left split:
    //            +896+TMS  +928+2*TMS
    //               |        |
    // [   B   ] --> [ result | remainder ]
    //               \________/\_________/
    //                 32+TMS       32
    // freelist->remainder->A->C
    //
    // right split:
    //            +896+TMS     +928+TMS
    //               |           |
    // [   B   ] --> [ remainder | result ]
    //               \__________/\_______/
    //                    32       32+TMS
    // freelist->remainder->A->C
    ck_assert(ret);
    uint8_t *split_left_choice = my_sbrk_fake_heap + 896 + TOTAL_METADATA_SIZE + sizeof (metadata_t);
    uint8_t *split_right_choice =  my_sbrk_fake_heap + 928 + TOTAL_METADATA_SIZE + sizeof (metadata_t);
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 32 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains three nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    // Check remainder
    if (ret == split_left_choice) {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap + 928 + 2*TOTAL_METADATA_SIZE);
    } else {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap + 896 + TOTAL_METADATA_SIZE);
    }
    ck_assert_int_eq(freelist->size, 32);
    ck_assert_ptr_eq(freelist->next, A);
    ck_assert_int_eq(A->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next->next, C);
    ck_assert_int_eq(C->size, expected_C_size);
}
END_TEST

START_TEST(test_malloc_malloc_split2) {
    // printf("Start split2 Test \n");
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(128);

    // Check return value
    // left split:
    //              +512  +640+TMS
    //               |        |
    // [   A   ] --> [ result | remainder ]
    //               \________/\_________/
    //                 128+TMS     128
    // freelist->B->remainder->C
    //
    // right split:
    //              +512       +640
    //               |           |
    // [   A   ] --> [ remainder | result ]
    //               \__________/\_______/
    //                   128       128+TMS
    // freelist->B->remainder->C
    ck_assert(ret);
    uint8_t *split_left_choice = my_sbrk_fake_heap + 512 + sizeof (metadata_t);
    uint8_t *split_right_choice =  my_sbrk_fake_heap + 640 + sizeof (metadata_t);
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 128 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains three nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    ck_assert_ptr_eq(freelist, B);
    ck_assert_int_eq(B->size, expected_B_size);
    // Check remainder
    if (ret == split_left_choice) {
        ck_assert_ptr_eq((uint8_t *)freelist->next, my_sbrk_fake_heap + 640 + TOTAL_METADATA_SIZE);
    } else {
        ck_assert_ptr_eq((uint8_t *)freelist->next, my_sbrk_fake_heap + 512);
    }
    ck_assert_int_eq(freelist->next->size, 128);
    ck_assert_ptr_eq(freelist->next->next, C);
    ck_assert_int_eq(C->size, expected_C_size);
}
END_TEST

START_TEST(test_malloc_malloc_split3) {
    // printf("Starting Next Test \n");
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(420);

    // Check return value
    // left split:
    //          +1216+2*TMS  +1636+3*TMS
    //               |        |
    // [   C   ] --> [ result | remainder ]
    //               \________/\_________/
    //                 420+TMS  SBRK_SIZE-1636-3*TMS
    // freelist->B->A->remainder
    //
    // right split:
    //          +1216+2*TMS    SBRK_SIZE-420-TMS
    //               |           |
    // [   C   ] --> [ remainder | result ]
    //               \__________/\_______/
    //        SBRK_SIZE-1636-3*TMS  420+TMS
    // freelist->B->A->remainder
    ck_assert(ret);
    uint8_t *split_left_choice = my_sbrk_fake_heap + 1216 + 2*TOTAL_METADATA_SIZE + sizeof (metadata_t);
    uint8_t *split_right_choice =  my_sbrk_fake_heap + SBRK_SIZE - 420 - TOTAL_METADATA_SIZE + sizeof (metadata_t);
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 420 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains three nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    ck_assert_ptr_eq(freelist, B);
    ck_assert_int_eq(B->size, expected_B_size);
    ck_assert_ptr_eq(freelist->next, A);
    ck_assert_int_eq(A->size, expected_A_size);
    // Check remainder
    if (ret == split_left_choice) {
        ck_assert_ptr_eq((uint8_t *)freelist->next->next, my_sbrk_fake_heap + 1636 + 3*TOTAL_METADATA_SIZE);
    } else {
        ck_assert_ptr_eq((uint8_t *)freelist->next->next, my_sbrk_fake_heap + 1216 + 2*TOTAL_METADATA_SIZE);
    }
}
END_TEST

START_TEST(test_malloc_malloc_waste1) {
    // printf("Start waste1 Test \n");
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    // I'm lazy, so reuse situation 1 but kick C out of the freelist
    A->next = NULL;
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(A->size - TOTAL_METADATA_SIZE - 1);
    ck_assert(ret);
    ck_assert(ret == (uint8_t *)(A + 1));

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, expected_A_size);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains one node, B
    ck_assert(freelist);
    ck_assert(!freelist->next);

    ck_assert_ptr_eq(freelist, B);
    ck_assert_int_eq(B->size, expected_B_size);
}
END_TEST

START_TEST(test_malloc_malloc_waste2) {
    // printf("Start waste2 Test\n");
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(C->size - TOTAL_METADATA_SIZE - 4);
    ck_assert(ret);
    ck_assert(ret == (uint8_t *)(C + 1));

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, expected_C_size);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains two nodes, B->A
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(!freelist->next->next);

    ck_assert_ptr_eq(freelist, B);
    ck_assert_int_eq(B->size, expected_B_size);
    ck_assert_ptr_eq(freelist->next, A);
    ck_assert_int_eq(A->size, expected_A_size);
}
END_TEST

START_TEST(test_malloc_malloc_waste3) {
    init_malloc_test(1, false);

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    // I'm lazy, so tweak situation 1 so that B is the only thing in the
    // freelist
    freelist = B;
    B->next = NULL;
    unsigned int expected_B_size = B->size;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_malloc(B->size - TOTAL_METADATA_SIZE - (MIN_BLOCK_SIZE - 1));
    ck_assert(ret);
    ck_assert(ret == (uint8_t *)(B + 1));

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, expected_B_size);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // freelist is empty
    ck_assert(!freelist);
}
END_TEST

START_TEST(test_malloc_malloc_zero) {
    init_malloc_test(0, false);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    void *ret = my_malloc(0);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);
}
END_TEST

START_TEST(test_malloc_malloc_toobig) {
    init_malloc_test(0, false);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    void *ret = my_malloc(SBRK_SIZE);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, SINGLE_REQUEST_TOO_LARGE);
}
END_TEST

START_TEST(test_malloc_malloc_oom) {
    init_malloc_test(HEAP_SIZE/SBRK_SIZE, true);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = NO_ERROR;
    void *ret = my_malloc(8);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, OUT_OF_MEMORY);
}
END_TEST

//
// free() tests
//
void setup_malloc_free(void) {
    setup_malloc_malloc();
    init_malloc_test(1, false);
}

void teardown_malloc_free(void) {
    teardown_malloc_malloc();
}

START_TEST(test_malloc_free_null) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    my_free(NULL);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);
}
END_TEST

START_TEST(test_malloc_free_bad_meta_canary) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    metadata_t *meta = (metadata_t *)(my_sbrk_fake_heap + 128);
    meta->size = 64;
    meta->canary = 0xBEEF;
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    *trailing_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, CANARY_CORRUPTED);
    ck_assert(!freelist);
}
END_TEST

START_TEST(test_malloc_free_bad_trailing_canary) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    metadata_t *meta = (metadata_t *)(my_sbrk_fake_heap + 128);
    meta->size = 64;
    meta->canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    *trailing_canary = 420;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, CANARY_CORRUPTED);
    ck_assert(!freelist);
}
END_TEST

START_TEST(test_malloc_free_empty_freelist) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    metadata_t *meta = (metadata_t *)(my_sbrk_fake_heap + 64);
    meta->size = 128 + TOTAL_METADATA_SIZE;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 1 node
    ck_assert(freelist);
    ck_assert(!freelist->next);
    ck_assert_ptr_eq(freelist, meta);
}
END_TEST

START_TEST(test_malloc_free_no_merge1) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)((uint8_t *)B + B->size + 8);
    meta->size = 32 + TOTAL_METADATA_SIZE;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 4 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(freelist->next->next->next);
    ck_assert(!freelist->next->next->next->next);

    // Freelist should be:
    // freelist->meta->B->A->C
    ck_assert_ptr_eq(freelist, meta);
    ck_assert_int_eq(freelist->size, 32 + TOTAL_METADATA_SIZE);
    ck_assert_ptr_eq(freelist->next, B);
    ck_assert_int_eq(freelist->next->size, expected_B_size);
    ck_assert_ptr_eq(freelist->next->next, A);
    ck_assert_int_eq(freelist->next->next->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next->next->next, C);
    ck_assert_int_eq(freelist->next->next->next->size, expected_C_size);
}
END_TEST

START_TEST(test_malloc_free_no_merge2) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)my_sbrk_fake_heap;
    meta->size = 500;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 4 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(freelist->next->next->next);
    ck_assert(!freelist->next->next->next->next);

    // Freelist should be:
    // freelist->B->A->meta->C
    ck_assert_ptr_eq(freelist, B);
    ck_assert_int_eq(freelist->size, expected_B_size);
    ck_assert_ptr_eq(freelist->next, A);
    ck_assert_int_eq(freelist->next->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next->next, meta);
    ck_assert_int_eq(freelist->next->next->size, 500);
    ck_assert_ptr_eq(freelist->next->next->next, C);
    ck_assert_int_eq(freelist->next->next->next->size, expected_C_size);
}
END_TEST

START_TEST(test_malloc_free_left_merge1) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)((uint8_t *)A + A->size);
    meta->size = 64;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 3 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    // Freelist should be:
    // freelist->B->A+meta->C
    ck_assert_ptr_eq(freelist, B);
    ck_assert_int_eq(freelist->size, expected_B_size);
    ck_assert_ptr_eq(freelist->next, A);
    ck_assert_int_eq(freelist->next->size, expected_A_size + 64);
    ck_assert_ptr_eq(freelist->next->next, C);
    ck_assert_int_eq(freelist->next->next->size, expected_C_size);
}
END_TEST

START_TEST(test_malloc_free_left_merge2) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)((uint8_t *)B + B->size);
    // Choose the biggest possible slice of space after B to free. This way,
    // B+meta ends up after A in the freelist because it'll be larger and the
    // freelist is sorted ascending by block size. If you copy and paste your
    // friend's merging code from last semester (looking at you, Jim), you will
    // fail this test!
    meta->size = 256 - MIN_BLOCK_SIZE;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 3 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    // Freelist should be:
    // freelist->A->B+meta->C
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next, B);
    ck_assert_int_eq(freelist->next->size, expected_B_size + 256 - MIN_BLOCK_SIZE);
    ck_assert_ptr_eq(freelist->next->next, C);
    ck_assert_int_eq(freelist->next->next->size, expected_C_size);
}
END_TEST

// Merge a gigantic block, just for good clean fun
START_TEST(test_malloc_free_left_merge3) {
    // Move the break to after SBRK_SIZE*2 bytes
    my_sbrk_imaginary_prev_calls++;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    // I'm lazy and don't want to use C in this test, so just remove C from the
    // freelist
    A->next = NULL;
    // Create a block D at the beginning of memory
    unsigned int expected_D_size = 420;
    metadata_t *D = (metadata_t *)my_sbrk_fake_heap;
    D->size = expected_D_size;
    D->next = NULL;
    // D is biggest now, so put it at the end of the freelist
    A->next = D;

    // The freelist should look like this now:
    // freelist->B->A->D

    metadata_t *meta = (metadata_t *)((uint8_t *)B + B->size);
    meta->size = 2*SBRK_SIZE - 960 - 2*TOTAL_METADATA_SIZE;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 3 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    // Freelist should be:
    // freelist->A->D->B+meta
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next, D);
    ck_assert_int_eq(freelist->next->size, expected_D_size);
    ck_assert_ptr_eq(freelist->next->next, B);
    ck_assert_int_eq(freelist->next->next->size, 2*SBRK_SIZE - 896 - TOTAL_METADATA_SIZE);
}
END_TEST

START_TEST(test_malloc_free_right_merge1) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)((uint8_t *)A - 128);
    meta->size = 128;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 3 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    // Freelist should be:
    // freelist->B->A+meta->C
    ck_assert_ptr_eq(freelist, B);
    ck_assert_int_eq(freelist->size, expected_B_size);
    ck_assert_ptr_eq(freelist->next, meta);
    ck_assert_int_eq(freelist->next->size, expected_A_size + 128);
    ck_assert_ptr_eq(freelist->next->next, C);
    ck_assert_int_eq(freelist->next->next->size, expected_C_size);
}
END_TEST

START_TEST(test_malloc_free_right_merge2) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    // I'm lazy, so shrink A so that merging B with some space to the left will
    // make it bigger than A
    A->size = 128;
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)((uint8_t *)B - 100);
    meta->size = 100;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 3 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    // Freelist should be:
    // freelist->A->B+meta->C
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next, meta);
    ck_assert_int_eq(freelist->next->size, expected_B_size + 100);
    ck_assert_ptr_eq(freelist->next->next, C);
    ck_assert_int_eq(freelist->next->next->size, expected_C_size);
}
END_TEST

// Merge another gigantic block, just for even more good clean fun
START_TEST(test_malloc_free_right_merge3) {
    // Move the break to after SBRK_SIZE*2 bytes
    my_sbrk_imaginary_prev_calls++;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    // I'm lazy, so just shrink C a bit and put it at the head of the freelist
    // but at the end of the heap
    C = (metadata_t *)(my_sbrk_fake_heap + 2*SBRK_SIZE - 8 - TOTAL_METADATA_SIZE);
    C->size = 8 + TOTAL_METADATA_SIZE;
    // Put the new baby C at the head of the freelist instead
    A->next = NULL;
    C->next = B;
    freelist = C;

    // The freelist should look like this now:
    // freelist->C->B->A

    // Put the block MIN_BLOCK_SIZE bytes after B so it doesn't try to merge
    // with B
    metadata_t *meta = (metadata_t *)((uint8_t *)B + B->size + MIN_BLOCK_SIZE);
    meta->size = 2*SBRK_SIZE - 968 - 3*TOTAL_METADATA_SIZE - MIN_BLOCK_SIZE;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 3 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    // Freelist should be:
    // freelist->B->A->C+meta
    ck_assert_ptr_eq(freelist, B);
    ck_assert_int_eq(freelist->size, expected_B_size);
    ck_assert_ptr_eq(freelist->next, A);
    ck_assert_int_eq(freelist->next->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next->next, meta);
    ck_assert_int_eq(freelist->next->next->size, 2*SBRK_SIZE - 960 - 2*TOTAL_METADATA_SIZE - MIN_BLOCK_SIZE);
}
END_TEST

START_TEST(test_malloc_free_double_merge1) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)((uint8_t *)A + A->size);
    meta->size = 128;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 2 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(!freelist->next->next);

    // Freelist should be:
    // freelist->A+meta+B->C
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size + 128 + expected_B_size);
    ck_assert_ptr_eq(freelist->next, C);
    ck_assert_int_eq(freelist->next->size, expected_C_size);
}
END_TEST

START_TEST(test_malloc_free_double_merge2) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)((uint8_t *)B + B->size);
    meta->size = 256;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 2 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(!freelist->next->next);

    // Freelist should be:
    // freelist->A->B+meta+C
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next, B);
    ck_assert_int_eq(freelist->next->size, expected_B_size + 256 + expected_C_size);
}
END_TEST

// Merge ANOTHER gigantic block, just for an encore of good clean fun
START_TEST(test_malloc_free_double_merge3) {
    // Move the break to after SBRK_SIZE*2 bytes
    my_sbrk_imaginary_prev_calls++;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    // I'm lazy, so just shrink C a bit and put it at the head of the freelist
    // but at the end of the heap
    C = (metadata_t *)(my_sbrk_fake_heap + 2*SBRK_SIZE - 8 - TOTAL_METADATA_SIZE);
    C->size = 8 + TOTAL_METADATA_SIZE;
    // Put the new baby C at the head of the freelist instead
    A->next = NULL;
    C->next = B;
    freelist = C;

    // The freelist should look like this now:
    // freelist->C->B->A

    metadata_t *meta = (metadata_t *)((uint8_t *)B + B->size);
    meta->size = 2*SBRK_SIZE - 968 - 3*TOTAL_METADATA_SIZE;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    my_free(meta + 1);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 2 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(!freelist->next->next);

    // Freelist should be:
    // freelist->A->B+meta+C
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next, B);
    ck_assert_int_eq(freelist->next->size, 2*SBRK_SIZE - 896 - TOTAL_METADATA_SIZE);
}
END_TEST

//
// calloc() tests
//

// Make sure calloc allocates right amount of space
START_TEST(test_malloc_calloc_initial) {
    init_malloc_test(0, true);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_calloc(8, 64);

    // Check return value
    ck_assert(ret);
    uint8_t *split_left_choice = my_sbrk_fake_heap + sizeof (metadata_t);
    uint8_t *split_right_choice =  my_sbrk_fake_heap + SBRK_SIZE - sizeof (int) - 512;
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check that memory is zeroed out
    for (unsigned int i = 0; i < 512; i++) {
        ck_assert(!ret[i]);
    }

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 512 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They called sbrk()
    ck_assert(my_sbrk_called);
    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains one node
    ck_assert(freelist);
    ck_assert(!freelist->next);
    ck_assert_int_eq(freelist->size, SBRK_SIZE - 512 - TOTAL_METADATA_SIZE);
    // Check address of this node
    if (ret == split_left_choice) {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap + 512 + TOTAL_METADATA_SIZE);
    } else {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap);
    }
}
END_TEST

START_TEST(test_malloc_calloc_zero) {
    init_malloc_test(0, false);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    void *ret = my_calloc(0, 69);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);
}
END_TEST

// Make sure they're not clobbering errno by saying errno = NO_ERROR in
// calloc()
START_TEST(test_malloc_calloc_clobber_errno) {
    init_malloc_test(0, false);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    void *ret = my_calloc(SBRK_SIZE, 1);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, SINGLE_REQUEST_TOO_LARGE);
}
END_TEST

//
// realloc() tests
//

START_TEST(test_malloc_realloc_initial) {
    init_malloc_test(0, true);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_realloc(NULL, 1024);

    // Check return value
    ck_assert(ret);
    uint8_t *split_left_choice = my_sbrk_fake_heap + sizeof (metadata_t);
    uint8_t *split_right_choice =  my_sbrk_fake_heap + SBRK_SIZE - sizeof (int) - 1024;
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check canaries
    metadata_t *meta = (metadata_t *)ret - 1;
    ck_assert_int_eq(meta->size, 1024 + TOTAL_METADATA_SIZE);
    unsigned int canary_expected = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    ck_assert_int_eq(meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // They called sbrk()
    ck_assert(my_sbrk_called);
    // They set errno
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains one node
    ck_assert(freelist);
    ck_assert(!freelist->next);
    ck_assert_int_eq(freelist->size, SBRK_SIZE - 1024 - TOTAL_METADATA_SIZE);
    // Check address of this node
    if (ret == split_left_choice) {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap + 1024 + TOTAL_METADATA_SIZE);
    } else {
        ck_assert_ptr_eq((uint8_t *)freelist, my_sbrk_fake_heap);
    }
}
END_TEST

START_TEST(test_malloc_realloc_zero) {
    init_malloc_test(1, false);

    // Put something in the freelist to check they don't touch it
    metadata_t *meta = (metadata_t *)my_sbrk_fake_heap;
    meta->size = SBRK_SIZE;
    meta->next = NULL;
    freelist = meta;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_realloc(NULL, 0);

    // Check return value
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // freelist with one node
    ck_assert(freelist);
    ck_assert(!freelist->next);

    // freelist is just the silly thing we created
    ck_assert_ptr_eq(freelist, meta);
    ck_assert_int_eq(freelist->size, SBRK_SIZE);
}
END_TEST

START_TEST(test_malloc_realloc_copy) {
    init_malloc_test(1, false);

    // Counting the null terminator, this is 55 bytes
    char *important_data = "Watch your fingers 'cause the cactus dangerous (yeah)\n";

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    // Put this important data in memory somewhere
    metadata_t *meta = (metadata_t *)((uint8_t *)B + B->size + 128);
    meta->size = 55 + TOTAL_METADATA_SIZE;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    // Copy in our important data
    strcpy((char *)(meta + 1), important_data);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_realloc(meta + 1, 128);

    // Check return value
    ck_assert(ret);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);
    // Handle splitting A from either the left or the right
    uint8_t *split_left_choice = (uint8_t *)(A + 1);
    uint8_t *split_right_choice = (uint8_t *)A + expected_A_size - 128 - TOTAL_METADATA_SIZE + sizeof (metadata_t);
    ck_assert(ret == split_left_choice || ret == split_right_choice);

    // Check our important data is intact.
    // Use strncmp() so this test doesn't hang/segfault by walking way off into
    // memory looking for a null terminator
    ck_assert(!strncmp((char *)ret, important_data, 55));

    // freelist should look like this:
    // freelist->meta->B->A remainder->C

    // freelist with four nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(freelist->next->next->next);
    ck_assert(!freelist->next->next->next->next);

    ck_assert_ptr_eq(freelist, meta);
    ck_assert_int_eq(freelist->size, 55 + TOTAL_METADATA_SIZE);
    ck_assert_ptr_eq(freelist->next, B);
    ck_assert_int_eq(freelist->next->size, expected_B_size);
    if (ret == split_left_choice) {
        ck_assert_ptr_eq(freelist->next->next, (metadata_t *)((uint8_t *)A + 128 + TOTAL_METADATA_SIZE));
    } else {
        ck_assert_ptr_eq(freelist->next->next, A);
    }
    ck_assert_int_eq(freelist->next->next->size, expected_A_size - 128 - TOTAL_METADATA_SIZE);
    ck_assert_ptr_eq(freelist->next->next->next, C);
    ck_assert_int_eq(freelist->next->next->next->size, expected_C_size);
}
END_TEST

// Test copying data into smaller block
START_TEST(test_malloc_realloc_copy_smaller) {
    init_malloc_test(1, false);

    // Counting the null terminator, this is 242 bytes
    char *important_data = "Uh, yeah, mansion it sit on the hill, (woo) after my last arrest (woo, woo)\n"
                           "[Individuals] out here in the field, don't need a mask or vest (brrt, brrt)\n"
                           "Uh, back to back Lambos, repeat it, you like to run when it's heated (run, back to back)\n";

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    // I'm lazy, so shrink A a little bit so that it'll be first in the new
    // freelist instead of the block passed in
    A->size -= 64;
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    // Put this important data in memory somewhere
    metadata_t *meta = (metadata_t *)my_sbrk_fake_heap;
    meta->size = 242 + TOTAL_METADATA_SIZE;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    // Copy in our important data
    strcpy((char *)(meta + 1), important_data);
    // B will be chosen, so to make sure they're not writing past the end of B,
    // zero out the byte directly following B and check that it stays zero.
    uint8_t *byte_after_B = (uint8_t *)B + expected_B_size;
    *byte_after_B = 0;

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    uint8_t *ret = my_realloc(meta + 1, 64);

    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Check return value
    ck_assert(ret);
    // Check canaries
    metadata_t *ret_meta = (metadata_t *)ret - 1;
    ck_assert_ptr_eq(ret_meta, B);
    ck_assert_int_eq(ret_meta->size, expected_B_size);
    unsigned int canary_expected = ((uintptr_t)ret_meta ^ CANARY_MAGIC_NUMBER) - ret_meta->size;
    ck_assert_int_eq(ret_meta->canary, canary_expected);
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)ret_meta + ret_meta->size - sizeof (int));
    ck_assert_int_eq(*trailing_canary, canary_expected);

    // Check our important data is intact.
    // Use strncmp() so this test doesn't hang/segfault by walking way off into
    // memory looking for a null terminator
    ck_assert(!strncmp((char *)ret, important_data, 64));
    // As described above, make sure they didn't write past B. The canary check
    // should be enough, but it's possible they set the canary again after
    // calling malloc(), and we don't want to miss that case
    ck_assert(*byte_after_B != important_data[64 + sizeof (int)]);

    // freelist should look like this:
    // freelist->A->meta->C

    // freelist with three nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(freelist->next->next);
    ck_assert(!freelist->next->next->next);

    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next, meta);
    ck_assert_int_eq(freelist->next->size, 242 + TOTAL_METADATA_SIZE);
    ck_assert_ptr_eq(freelist->next->next, C);
    ck_assert_int_eq(freelist->next->next->size, expected_C_size);
}
END_TEST

// This is a copy-paste of free_double_merge2
START_TEST(test_malloc_realloc_free) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // Setup the freelist and fake heap to represent situation 1
    metadata_t *A, *B, *C;
    create_situation_1(&A, &B, &C);
    unsigned int expected_A_size = A->size;
    unsigned int expected_B_size = B->size;
    unsigned int expected_C_size = C->size;

    metadata_t *meta = (metadata_t *)((uint8_t *)B + B->size);
    meta->size = 256;
    unsigned int *tail_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    meta->canary = *tail_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    uint8_t *ret = my_realloc(meta + 1, 0);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, NO_ERROR);

    // Freelist contains 2 nodes
    ck_assert(freelist);
    ck_assert(freelist->next);
    ck_assert(!freelist->next->next);

    // Freelist should be:
    // freelist->A->B+meta+C
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);
    ck_assert_ptr_eq(freelist->next, B);
    ck_assert_int_eq(freelist->next->size, expected_B_size + 256 + expected_C_size);
}
END_TEST

// this is a copy-paste of malloc_toobig
START_TEST(test_malloc_realloc_toobig) {
    init_malloc_test(0, false);

    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;
    void *ret = my_realloc(NULL, SBRK_SIZE);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, SINGLE_REQUEST_TOO_LARGE);
}
END_TEST

// this is a copy-paste of free_bad_meta_canary
START_TEST(test_malloc_realloc_bad_meta_canary) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // 54 bytes including the null terminator
    char *important_data = "Rap's Jackie Chan, we ain't pullin' them fake stunts\n";

    metadata_t *meta = (metadata_t *)(my_sbrk_fake_heap + 128);
    meta->size = 54 + TOTAL_METADATA_SIZE;
    meta->canary = 0xBEEF;
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    *trailing_canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;

    // Create a free block A at the end of the heap
    metadata_t *A = (metadata_t *)(my_sbrk_fake_heap + SBRK_SIZE - 128 - TOTAL_METADATA_SIZE);
    A->size = 128 + TOTAL_METADATA_SIZE;
    unsigned int expected_A_size = A->size;
    A->next = NULL;
    freelist = A;

    void *ret = my_realloc(meta + 1, 128);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, CANARY_CORRUPTED);

    // Check the freelist wasn't changed
    ck_assert(freelist);
    ck_assert(!freelist->next);
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);

    // Check they didn't copy anyway
    ck_assert(*(uint8_t *)(A + 1) != important_data[0]);
}
END_TEST

// copy-and-paste of the above except with a broken trailng canary instead
START_TEST(test_malloc_realloc_bad_trailing_canary) {
    // Set errno to something else to check if student is setting it
    my_malloc_errno = OUT_OF_MEMORY;

    // 54 bytes including the null terminator
    char *important_data = "Rap's Jackie Chan, we ain't pullin' them fake stunts\n";

    metadata_t *meta = (metadata_t *)(my_sbrk_fake_heap + 128);
    meta->size = 54 + TOTAL_METADATA_SIZE;
    meta->canary = ((uintptr_t)meta ^ CANARY_MAGIC_NUMBER) - meta->size;
    unsigned int *trailing_canary = (unsigned int *)((uint8_t *)meta + meta->size - sizeof (int));
    *trailing_canary = 0xBEEF;

    // Create a free block A at the end of the heap
    metadata_t *A = (metadata_t *)(my_sbrk_fake_heap + SBRK_SIZE - 128 - TOTAL_METADATA_SIZE);
    A->size = 128 + TOTAL_METADATA_SIZE;
    unsigned int expected_A_size = A->size;
    A->next = NULL;
    freelist = A;

    void *ret = my_realloc(meta + 1, 128);
    ck_assert(!ret);
    ck_assert_int_eq(my_malloc_errno, CANARY_CORRUPTED);

    // Check the freelist wasn't changed
    ck_assert(freelist);
    ck_assert(!freelist->next);
    ck_assert_ptr_eq(freelist, A);
    ck_assert_int_eq(freelist->size, expected_A_size);

    // Check they didn't copy anyway
    ck_assert(*(uint8_t *)(A + 1) != important_data[0]);
}
END_TEST

Suite *malloc_suite(void) {
    Suite *s;
    TCase *tc;

    s = suite_create("malloc");

    // malloc() tests
    tc = tcase_create("malloc");
    tcase_add_checked_fixture(tc, setup_malloc_malloc, teardown_malloc_malloc);
    tcase_add_test(tc, test_malloc_malloc_initial);
    tcase_add_test(tc, test_malloc_malloc_initial_sbrked);
    tcase_add_test(tc, test_malloc_malloc_sbrk_merge);
    tcase_add_test(tc, test_malloc_malloc_perfect1);
    tcase_add_test(tc, test_malloc_malloc_perfect2);
    tcase_add_test(tc, test_malloc_malloc_perfect3);
    tcase_add_test(tc, test_malloc_malloc_split1);
    tcase_add_test(tc, test_malloc_malloc_split2);
    tcase_add_test(tc, test_malloc_malloc_split3);
    tcase_add_test(tc, test_malloc_malloc_waste1);
    tcase_add_test(tc, test_malloc_malloc_waste2);
    tcase_add_test(tc, test_malloc_malloc_waste3);
    tcase_add_test(tc, test_malloc_malloc_zero);
    tcase_add_test(tc, test_malloc_malloc_toobig);
    tcase_add_test(tc, test_malloc_malloc_oom);
    suite_add_tcase(s, tc);

    // free() tests
    tc = tcase_create("free");
    tcase_add_checked_fixture(tc, setup_malloc_free, teardown_malloc_free);
    tcase_add_test(tc, test_malloc_free_null);
    tcase_add_test(tc, test_malloc_free_bad_meta_canary);
    tcase_add_test(tc, test_malloc_free_bad_trailing_canary);
    tcase_add_test(tc, test_malloc_free_empty_freelist);
    tcase_add_test(tc, test_malloc_free_no_merge1);
    tcase_add_test(tc, test_malloc_free_no_merge2);
    tcase_add_test(tc, test_malloc_free_left_merge1);
    tcase_add_test(tc, test_malloc_free_left_merge2);
    tcase_add_test(tc, test_malloc_free_left_merge3);
    tcase_add_test(tc, test_malloc_free_right_merge1);
    tcase_add_test(tc, test_malloc_free_right_merge2);
    tcase_add_test(tc, test_malloc_free_right_merge3);
    tcase_add_test(tc, test_malloc_free_double_merge1);
    tcase_add_test(tc, test_malloc_free_double_merge2);
    tcase_add_test(tc, test_malloc_free_double_merge3);
    suite_add_tcase(s, tc);

    // calloc() tests
    tc = tcase_create("calloc");
    tcase_add_checked_fixture(tc, setup_malloc_malloc, teardown_malloc_malloc);
    tcase_add_test(tc, test_malloc_calloc_initial);
    tcase_add_test(tc, test_malloc_calloc_zero);
    tcase_add_test(tc, test_malloc_calloc_clobber_errno);
    suite_add_tcase(s, tc);

    // realloc() tests
    tc = tcase_create("realloc");
    tcase_add_checked_fixture(tc, setup_malloc_malloc, teardown_malloc_malloc);
    tcase_add_test(tc, test_malloc_realloc_initial);
    tcase_add_test(tc, test_malloc_realloc_zero);
    tcase_add_test(tc, test_malloc_realloc_copy);
    tcase_add_test(tc, test_malloc_realloc_copy_smaller);
    tcase_add_test(tc, test_malloc_realloc_free);
    tcase_add_test(tc, test_malloc_realloc_toobig);
    tcase_add_test(tc, test_malloc_realloc_bad_meta_canary);
    tcase_add_test(tc, test_malloc_realloc_bad_trailing_canary);
    suite_add_tcase(s, tc);

    return s;
}

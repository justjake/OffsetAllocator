#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OFFSET_ALLOCATOR_IMPLEMENT
#include "offsetAllocator.h"

struct harness {
    void *storage;
    OffsetAllocator *allocator;
};

static int failures;

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                      \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
        failures++;                                                          \
    }                                                                        \
} while (0)

static struct harness create(uint32_t bytes, uint32_t max_allocations)
{
    struct harness result = { 0 };
    size_t storage_bytes = OffsetAllocator_GetRequiredBytes(max_allocations);
    result.storage = malloc(storage_bytes);
    if (!result.storage) {
        fputs("out of memory\n", stderr);
        exit(2);
    }
    result.allocator = OffsetAllocator_Create(bytes, max_allocations,
                                               result.storage, storage_bytes);
    return result;
}

static void destroy(struct harness *harness)
{
    OffsetAllocator_Destroy(harness->allocator);
    free(harness->storage);
    memset(harness, 0, sizeof(*harness));
}

static OffsetAllocatorAllocation allocate(struct harness *harness, uint32_t bytes)
{
    OffsetAllocatorAllocation allocation = {
        OFFSET_ALLOCATOR_NO_SPACE, OFFSET_ALLOCATOR_NO_SPACE,
    };
    OffsetAllocator_Allocate(harness->allocator, bytes, &allocation);
    return allocation;
}

static int is_allocated(OffsetAllocatorAllocation allocation)
{
    return allocation.offset != OFFSET_ALLOCATOR_NO_SPACE;
}

static void test_false_out_of_space_large_remainder(void)
{
    struct harness harness = create(699050, 16);
    OffsetAllocatorAllocation allocation = allocate(&harness, 664908);
    CHECK(is_allocated(allocation));
    CHECK(allocation.offset == 0);
    if (is_allocated(allocation))
        OffsetAllocator_Free(harness.allocator, &allocation);
    destroy(&harness);
}

static void test_false_out_of_space_tail(void)
{
    struct harness harness = create(324, 1024);
    OffsetAllocatorAllocation allocation[255];
    for (uint32_t index = 0; index < 255; index++) {
        allocation[index] = allocate(&harness, 1);
        CHECK(is_allocated(allocation[index]));
        CHECK(allocation[index].offset == index);
    }
    OffsetAllocatorAllocation tail = allocate(&harness, 67);
    CHECK(is_allocated(tail));
    CHECK(tail.offset == 255);
    if (is_allocated(tail))
        OffsetAllocator_Free(harness.allocator, &tail);
    for (uint32_t index = 0; index < 255; index++)
        if (is_allocated(allocation[index]))
            OffsetAllocator_Free(harness.allocator, &allocation[index]);
    destroy(&harness);
}

static void test_exact_last_remainder(void)
{
    const uint32_t pool = 5u * 1024u * 1024u;
    struct harness harness = create(pool, 16);
    OffsetAllocatorAllocation first = allocate(&harness, 1000000);
    OffsetAllocatorAllocation second = allocate(&harness, 2936012);
    OffsetAllocatorAllocation remainder = allocate(&harness,
        pool - 1000000 - 2936012);
    CHECK(is_allocated(first));
    CHECK(is_allocated(second));
    CHECK(is_allocated(remainder));
    CHECK(first.offset == 0);
    CHECK(second.offset == 1000000);
    CHECK(remainder.offset == 3936012);
    if (is_allocated(remainder))
        OffsetAllocator_Free(harness.allocator, &remainder);
    if (is_allocated(second))
        OffsetAllocator_Free(harness.allocator, &second);
    if (is_allocated(first))
        OffsetAllocator_Free(harness.allocator, &first);
    destroy(&harness);
}

static void test_two_allocations_at_limit(void)
{
    struct harness harness = create(324, 2);
    OffsetAllocatorAllocation first = allocate(&harness, 1);
    OffsetAllocatorAllocation second = allocate(&harness, 1);
    OffsetAllocatorAllocation third = allocate(&harness, 1);
    CHECK(is_allocated(first));
    CHECK(is_allocated(second));
    CHECK(first.offset == 0);
    CHECK(second.offset == 1);
    CHECK(!is_allocated(third));
    if (is_allocated(second))
        OffsetAllocator_Free(harness.allocator, &second);
    third = allocate(&harness, 1);
    CHECK(is_allocated(third));
    CHECK(third.offset == 1);
    if (is_allocated(third))
        OffsetAllocator_Free(harness.allocator, &third);
    if (is_allocated(first))
        OffsetAllocator_Free(harness.allocator, &first);
    destroy(&harness);
}

struct live_allocation {
    OffsetAllocatorAllocation allocation;
    uint32_t bytes;
};

#include "regression_overlap.inc"

static void test_discussion_overlap_repro(void)
{
    struct harness harness = create(64u << 10, 64u << 10);
    struct live_allocation allocations[414] = { 0 };
    unsigned char live[414] = { 0 };
    uint32_t next_allocation = 0;

    for (uint32_t operation = 0;
         operation < sizeof(overlap_operations) / sizeof(overlap_operations[0]);
         operation++) {
        uint32_t kind = overlap_operations[operation][0];
        uint32_t value = overlap_operations[operation][1];
        if (kind == 0) {
            OffsetAllocatorAllocation allocation = allocate(&harness, value);
            CHECK(is_allocated(allocation));
            if (!is_allocated(allocation))
                break;
            CHECK(next_allocation < sizeof(allocations) / sizeof(allocations[0]));
            for (uint32_t index = 0; index < next_allocation; index++) {
                if (!live[index])
                    continue;
                uint32_t start = allocations[index].allocation.offset;
                uint32_t end = start + allocations[index].bytes;
                CHECK(allocation.offset + value <= start || end <= allocation.offset);
            }
            allocations[next_allocation] = (struct live_allocation) { allocation, value };
            live[next_allocation++] = 1;
        } else {
            CHECK(value < next_allocation);
            if (value >= next_allocation || !live[value])
                continue;
            OffsetAllocator_Free(harness.allocator, &allocations[value].allocation);
            live[value] = 0;
        }
    }

    for (uint32_t index = 0; index < next_allocation; index++)
        if (live[index])
            OffsetAllocator_Free(harness.allocator, &allocations[index].allocation);
    OffsetAllocatorAllocation all = allocate(&harness, 64u << 10);
    CHECK(is_allocated(all));
    if (is_allocated(all))
        OffsetAllocator_Free(harness.allocator, &all);
    destroy(&harness);
}

static uint32_t random_word(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void test_random_ranges_never_overlap(void)
{
    const uint32_t pool = 64u * 1024u;
    const uint32_t capacity = 4096;
    struct harness harness = create(pool, capacity);
    struct live_allocation *live = calloc(capacity, sizeof(*live));
    uint32_t live_count = 0;
    uint32_t state = 0x6d2b79f5u;
    if (!live) {
        CHECK(0);
        destroy(&harness);
        return;
    }

    for (uint32_t step = 0; step < 100000; step++) {
        uint32_t choose = random_word(&state);
        if (live_count && (choose & 3u) == 0) {
            uint32_t index = random_word(&state) % live_count;
            OffsetAllocator_Free(harness.allocator, &live[index].allocation);
            live[index] = live[--live_count];
            continue;
        }
        uint32_t bytes = 1u + random_word(&state) % 1024u;
        OffsetAllocatorAllocation allocation = allocate(&harness, bytes);
        if (!is_allocated(allocation))
            continue;
        for (uint32_t index = 0; index < live_count; index++) {
            uint32_t start = live[index].allocation.offset;
            uint32_t end = start + live[index].bytes;
            CHECK(allocation.offset + bytes <= start || end <= allocation.offset);
        }
        CHECK(live_count < capacity);
        if (live_count == capacity) {
            OffsetAllocator_Free(harness.allocator, &allocation);
            break;
        }
        live[live_count++] = (struct live_allocation){ allocation, bytes };
    }

    while (live_count)
        OffsetAllocator_Free(harness.allocator,
                             &live[--live_count].allocation);
    OffsetAllocatorAllocation all = allocate(&harness, pool);
    CHECK(is_allocated(all));
    CHECK(all.offset == 0);
    if (is_allocated(all))
        OffsetAllocator_Free(harness.allocator, &all);
    free(live);
    destroy(&harness);
}

int main(void)
{
    test_false_out_of_space_large_remainder();
    test_false_out_of_space_tail();
    test_exact_last_remainder();
    test_two_allocations_at_limit();
    test_discussion_overlap_repro();
    test_random_ranges_never_overlap();
    if (failures) {
        fprintf(stderr, "%d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    puts("OffsetAllocator regression tests passed");
    return 0;
}

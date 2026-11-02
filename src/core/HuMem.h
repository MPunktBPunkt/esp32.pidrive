#pragma once

/**
 * @file HuMem.h
 * @brief PSRAM-preferring allocation helpers.
 */

#include <Arduino.h>
#include <esp_heap_caps.h>

/**
 * @brief Prefer PSRAM for large durable buffers (offline capture, EventBus ring slots).
 *
 * Falls back to internal heap when PSRAM is absent or exhausted.
 */
namespace HuMem {

/**
 * @brief Allocate n bytes, from PSRAM if available, else from the internal heap.
 * @param n  Size in bytes.
 * @return Pointer to uninitialized memory, or nullptr if n == 0 or both heaps are exhausted.
 *         Release with freePreferPsram().
 */
inline void* allocPreferPsram(size_t n) {
    if (n == 0) return nullptr;
#ifdef BOARD_HAS_PSRAM
    if (ESP.getPsramSize() > 0) {
        void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }
#endif
    return malloc(n);
}

/**
 * @brief Allocate a zeroed array, from PSRAM if available, else from the internal heap.
 * @param count  Number of elements.
 * @param size   Size of one element in bytes.
 * @return Pointer to zeroed memory, or nullptr if count * size == 0 or both heaps are
 *         exhausted. Release with freePreferPsram().
 */
inline void* callocPreferPsram(size_t count, size_t size) {
    const size_t n = count * size;
    if (n == 0) return nullptr;
#ifdef BOARD_HAS_PSRAM
    if (ESP.getPsramSize() > 0) {
        void* p = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }
#endif
    return calloc(count, size);
}

/**
 * @brief Free memory from allocPreferPsram()/callocPreferPsram() (PSRAM or internal heap).
 * @param p  Pointer to free; nullptr is ignored.
 */
inline void freePreferPsram(void* p) {
    if (!p) return;
    // heap_caps_free works for both SPIRAM and internal caps allocations;
    // plain malloc also goes through the heap_caps path on Arduino-ESP32.
    heap_caps_free(p);
}

/**
 * @brief Report whether a pointer lies in external RAM (PSRAM).
 * @param p  Pointer to test.
 * @return true if p is non-null and in PSRAM; always false in builds without BOARD_HAS_PSRAM.
 */
inline bool ptrInPsram(const void* p) {
    if (!p) return false;
#ifdef BOARD_HAS_PSRAM
    return esp_ptr_external_ram(p);
#else
    (void)p;
    return false;
#endif
}

}  // namespace HuMem

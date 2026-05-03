// wrap_sqrt_poc.c
#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <math.h>

/*
 * Test-only fault-injection wrapper for sqrt().
 *
 * Link with:
 *   -Wl,--wrap=sqrt
 *
 * This models a compromised libm/soft-float square-root routine.
 * It deliberately corrupts sqrt(x) for positive integer inputs in
 * [2^13, 2^14 - 1] by clearing the top stored mantissa bit of the
 * result. This is not a correctness workaround.
 */
double __real_sqrt(double);

static inline uint64_t
ct_is_zero_u64(uint64_t x)
{
    /*
     * Return all-ones if x == 0, else zero.
     */
    return (uint64_t)0 - (uint64_t)(((x | (uint64_t)(-x)) >> 63) ^ 1);
}

static inline uint64_t
ct_select_u64(uint64_t mask, uint64_t a, uint64_t b)
{
    /*
     * If mask == ~0, return a; if mask == 0, return b.
     */
    return b ^ (mask & (a ^ b));
}

double
__wrap_sqrt(double x)
{
    uint64_t ux, uy;
    double y;

    memcpy(&ux, &x, sizeof ux);

    y = __real_sqrt(x);
    memcpy(&uy, &y, sizeof uy);
    
    /*
     * Positive binary64 integers in [2^13, 2^14 - 1]:
     *
     *   sign     = 0
     *   exponent = 1023 + 13 = 1036 = 0x40c
     *   mantissa low 39 bits are zero
     *
     * Hence:
     *
     *   (ux & 0xFFF0007FFFFFFFFF) == 0x40C0000000000000
     */
    {
        const uint64_t in_mask  = UINT64_C(0xFFF0007FFFFFFFFC);
        const uint64_t in_value = UINT64_C(0x40C0000000000000);

        /*
         * Clear bit 51 of the stored mantissa of sqrt(x).
         *
         * For these inputs, sqrt(x) is around [2^6.5, 2^7), so clearing
         * this bit causes a large but not obviously exceptional finite
         * perturbation.
         */
        const uint64_t out_fault_bit = UINT64_C(1) << 51;

        uint64_t trigger = ct_is_zero_u64((ux & in_mask) ^ in_value);
        uint64_t faulty  = uy & ~out_fault_bit;

        uy = ct_select_u64(trigger, faulty, uy);
    }

    memcpy(&y, &uy, sizeof y);
    return y;
}

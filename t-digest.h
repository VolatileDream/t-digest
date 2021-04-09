#ifndef __T_DIGEST_H__
#define __T_DIGEST_H__

#include <stdbool.h>
#include <stdint.h>

struct tdigest_t;
typedef struct tdigest_t tdigest;

bool td_alloc(uint32_t compaction, tdigest** outptr);
bool td_free(tdigest* td);

// Add value to the digest.
// Equivalent to td_addw(td, value, 1)
bool td_add(tdigest* td, double value);

// Add values to the digest.
bool td_addw(tdigest* td, double value, uint64_t weight);

// Counts the data points added.
uint64_t td_count(tdigest* td);

// Returns the percentile.
double td_percentile(tdigest* td, double percentile);

#endif /* __T_DIGEST_H__ */

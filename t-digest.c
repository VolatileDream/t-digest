#include "t-digest.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

// The design of the t-digest implementation is constrained
// primarily by the speed at which new points can be added to the
// structure, and how quickly a compaction can be performed.

// Internals.

struct centroid_t {
  double mean;
  uint64_t count;
};
typedef struct centroid_t centroid;

struct tdigest_t {
  // Size of the backing array, set by the compression factor on init.
  uint32_t capacity;
  uint32_t compacted_count; // number of compacted nodes, prefix of centroids.
  uint32_t uncompacted_count; // number of uncompacted nodes, suffix of centroids.

  double min;
  double max;
  uint64_t point_count;
  centroid* centroids;
};

// Called after an insert, do we need to compact now?
static bool td_needs_compacting(tdigest* td);

// Are there any uncompressed nodes?
static bool td_is_compact(tdigest* td);

// Compress all the nodes.
void td_compact(tdigest* td);

static double min(double v1, double v2) {
  if (v1 < v2) {
    return v1;
  }
  return v2;
}

static centroid c(double mean, uint64_t count) {
  centroid c = {
    .mean = mean,
    .count = count,
  };
  return c;
}

static double weighted_avg(double d1, double w1, double d2, double w2) {
  return d1 + (d2 - d1) * w2 / (w1 + w2);
}

static centroid weighted_mean(centroid c1, centroid c2) {
  double delta = c2.mean - c1.mean;
  uint64_t sum = c1.count + c2.count;
  return c(c1.mean + delta * c2.count / sum, sum);
}

static uint32_t td_compression(tdigest* td) {
  return td->capacity >> 3;
}

bool td_add(tdigest* td, double value) {
  return td_addw(td, value, 1);
}

bool td_alloc(uint32_t compression, tdigest** outptr) {
  uint32_t nodes = compression << 3; 
  tdigest* ptr = malloc(sizeof(tdigest) + sizeof(centroid) * (nodes + 1));
  if (!ptr) {
    return false;
  }

  memset(ptr, 0, sizeof(tdigest) + sizeof(centroid) * nodes);

  ptr->capacity = nodes;
  ptr->max = -INFINITY;
  ptr->min = INFINITY;
  ptr->centroids = (centroid*)(ptr + 1);

  *outptr = ptr;

  return true;
}

bool td_free(tdigest* ptr) {
  free(ptr);
  return true;
}

uint32_t td_next(tdigest* td) {
  return td->compacted_count + td->uncompacted_count;
}

bool td_addw(tdigest* td, double value, uint64_t weight) {
  if (value < td->min) {
    td->min = value;
  }
  if (value > td->max) {
    td->max = value;
  }

  uint32_t next = td_next(td);
  // We're guaranteed to have space because
  // compaction always runs after we run out.
  td->centroids[next] = c(value, weight);
  td->uncompacted_count++;

  td->point_count += weight;

  if (td_needs_compacting(td)) {
    td_compact(td);
  }
}

uint64_t td_count(tdigest* td) {
  return td->point_count;
}

static bool td_is_compact(tdigest* td) {
  return td->uncompacted_count == 0;
}

static bool td_needs_compacting(tdigest* td) {
  return td_next(td) >= td->capacity;
}

static bool _very_small(double val) {
  return -1e-15f < val && val < 1e-15f;
}

static int centroid_compare(const void* v1, const void* v2) {
  const centroid* c1 = (const centroid*)v1;
  const centroid* c2 = (const centroid*)v2;
  double delta = c1->mean - c2->mean;
  if (!_very_small(delta)) {
    return delta;
  }
  return c1->count - c2->count;
}

void td_compact(tdigest* td) {
  if (td_is_compact(td)) {
    return;
  }

  const uint32_t length = td->compacted_count + td->uncompacted_count;

  // sort
  qsort((void*)td->centroids, length, sizeof(centroid), &centroid_compare);

  // Compacting runs two pointers forward through the array,
  // output index, and node we're looking at.

  const double total_weight = td->point_count;
  const double compression = td_compression(td);
  const double Z = 4 * log(total_weight / compression) + 21;
  const double normalizer =
    //compression / (4 * log(total_weight / compression) + 24);
    compression / Z;

  double cumulative_sum = 0;
  uint32_t output = 0;
  for (uint32_t i = 1; i < length; i++) {
    if (output == i) {
      // skip it, can't join a node to itself.
      continue;
    }
    double proposed_count = td->centroids[output].count + td->centroids[i].count;
    double projected_sum = cumulative_sum + proposed_count;
    double q0 = cumulative_sum / total_weight;
    double q2 = projected_sum / total_weight;

    //double bound = (total_weight * min(q0 *(1 - q0), q2 *(1 - q2)) / normalizer);
    double bound = total_weight * Z * min(min(q0,1 - q0), min(q2, 1 - q2)) / compression;

    if (proposed_count <= bound) {
      td->centroids[output] = weighted_mean(td->centroids[output], td->centroids[i]);
    } else {
      cumulative_sum += td->centroids[output].count;
      output++;
      td->centroids[output] = td->centroids[i];
    }
    if (output != i) {
      // When advancing output, it might become equal to i.

      // Regardless of combining, td->centroids[i] was moved or consumed.
      // Zero out old contents so we don't accidentally re-process it.
      td->centroids[i] = c(0, 0);
    }
  }

  if (output + 1 >= td->capacity) {
    fprintf(stderr, "compaction failed: %d + 1 >= %d\n", output, td->capacity);
    exit(-1);
  }
  //fprintf(stderr, "compaction success: %d to %d\n", length, output + 1);

  td->compacted_count = output + 1; // count, not index
  td->uncompacted_count = 0;
}

static double __td_interpolate(const double cumulative, const double delta, const double index, centroid c1, centroid c2) {
  double left_unit = 0.0;
  if (c1.count == 1) {
    if (index - cumulative < 0.5) {
      return c1.mean;
    }
    left_unit = 0.5;
  }
  double right_unit = 0.0;
  if (c2.count == 1) {
    if (cumulative + delta - index <= 0.5) {
      return c2.mean;
    }
    right_unit = 0.5;
  }

  double z1 = index - cumulative - left_unit;
  double z2 = cumulative + delta - index - right_unit;
  return weighted_avg(c1.mean, z2, c2.mean, z1);
}

double td_percentile(tdigest* td, double percentile) {
  if (td->point_count == 0 || percentile < 0 || percentile > 1.0) {
    return NAN;
  }
  if (!td_is_compact(td)) {
    td_compact(td);
  }

  const double index = percentile * td->point_count;

  // Maybe round out to the min/max.
  if (index < 1) {
    return td->min;
  } else if (index > td->point_count - 1) {
    return td->max;
  }

  // Special case extreme edge interpolation.
  const int last = td->compacted_count - 1;

  {
    centroid min = td->centroids[0];
    centroid max = td->centroids[last];

    if (min.count > 1 && index < min.count / 2) {
      return td->min + (index - 1) / (min.count / 2 - 1) * (min.mean - td->min);
    } else if (max.count > 1 && td->point_count - index <= max.count / 2) {
      return td->max - (td->point_count - index - 1) / (max.count / 2 - 1) * (td->max - max.mean);
    }
  }

  // Find the centroids that border the index.
  double cumulative_count = td->centroids[0].count / 2;
  for (int i = 0; i < last; i++) {
    centroid c1 = td->centroids[i];
    centroid c2 = td->centroids[i + 1];

    const double delta = (c1.count + c2.count) / 2;

    if (cumulative_count + delta > index) {
      //fprintf(stderr, "%f + %f > %f\n", cumulative_count, delta, index);
      return __td_interpolate(cumulative_count, delta, index, c1, c2);
    }
    cumulative_count += delta;
  }

  centroid lastc = td->centroids[last];
  double z1 = index - td->point_count - lastc.count / 2;
  double z2 = lastc.count / 2 - z1;

  return weighted_avg(lastc.mean, z1, td->max, z2);
}

void td_dump(tdigest* td) {
  if (td->centroids[0].count == 0) {
    fprintf(stderr, "empty tdigest\n");
  }

  uint64_t count = 0;
  for (int i = 0; i < td->capacity; i++) {
    centroid v = td->centroids[i];
    count += v.count;
    if (v.count == 0) {
      break;
    }
    fprintf(stderr, "%d = (%f, %ld)\n", i, v.mean, v.count);
  }
  int64_t delta = td->point_count - count;
  if (delta) {
    fprintf(stderr, "centroids missing %ld included values\n", delta);
  }
}

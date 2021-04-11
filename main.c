#include "t-digest.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

void usage(char *arg0) {
  printf("usage: %s\n\n", arg0);
  printf("Create a new filter, not compatible with -l\n");
  printf("  --compression|-c <100> : sets the t-digest compression factor\n");
  printf("  --dump|-d        : dump the internal storage of the tdigest before computing percentiles\n");
  printf("  --load|-l <file> : tries to load the specified filter\n");
  printf("                     Can be specified any number of times.\n");
  printf("                     Attempting to load incompatible filters\n");
  printf("                     will cause all but the first to be ignored.\n");
  printf("  --save|-s <file> : saves the filter upon exiting the program.\n");
  printf("                     (assuming no error occured)\n");
  printf("                     Can only be set once.\n\n");
  printf("  --help|-h|-? : prints this usage message\n");
}

int run(char* compression, int pc, char* pv[], bool dump);

int main(int argc, char* argv[]) {
  struct option options[] = {
    // { char* name, has_arg, int*flag, int }, // short name
    // File io options
    { "save", required_argument, 0, 's' }, // s:
    { "load", required_argument, 0, 'l' }, // l:

    // Filter setup options
    { "compression", required_argument, 0, 'c' }, // p:

    { "dump", required_argument, 0, 'd' }, // d

    // Funny options
    { "help", no_argument, 0, 'h' }, // h

    { 0, 0, 0, 0 },
  };

  bool dump = false;
  char* compression = 0;

  while(true) {
    const int c = getopt_long(argc, argv, "s:l:c:hd", options, 0);
    if (c == -1) {
      break;
    }

    switch(c) {
     case 'c':
      if (compression != 0) {
        fprintf(stderr, "--compression|-c passed more than once: %s and %s.\n", compression, optarg);
        return 2;
      }
      compression = optarg;
      break;
     case 's':
      fprintf(stderr, "--save|-s not yet supported.\n");
      return 3;
     case 'l':
      fprintf(stderr, "load not yet supported\n");
      return 4;
     case 'd':
      dump = true;
      break;
     case '?':
     case 'h':
      usage(argv[0]);
      return 1;
    }
  }

  return run(compression, argc - optind, &argv[optind], dump);
}

int __double_cmp(const void* v1, const void* v2) {
  const double* d1 = (const double*)v1;
  const double* d2 = (const double*)v2;
  if (*d1 < *d2) {
    return -1;
  } else if (*d1 > *d2) {
    return 1;
  }

  return 0;
}

bool read_line(FILE* in, char* buf, const uint32_t length, uint32_t *read) {
  uint32_t len = 0;
  while (len < length) {
    int c = fgetc(in);
    if (c == EOF) {
      return false;
    } else if (c == '\n') {
      break;
    }
    buf[len] = (char) c;
    len++;
  }

  *read = len;

  if (len >= length) {
    // consume rest of the line
    int c = 0;
    while (c != '\n' && c != EOF) {
      c = fgetc(in);
    }
  }
  return true;
}

int run(char* compression, int pc, char* pv[], bool dump) {
  uint32_t compress = 100;
  if (compression) {
    compress = atol(compression);
  }

  // Parse the percentiles to make sure they're numbers.
  double* percentiles = (double*)calloc(pc, sizeof(double));
  for (int i = 0; i < pc; i++) {
    char* end = 0;
    percentiles[i] = strtod(pv[i], &end);
    if (end == pv[i]) {
      fprintf(stderr, "Bad percentile value: %s\n", pv[i]);
      return 5;
    }

    if (percentiles[i] < 0 || 100.0 < percentiles[i]) {
      fprintf(stderr, "Percentile out of valid range (0-100): %s\n", pv[i]);
      return 6;
    }
  }

  qsort(percentiles, pc, sizeof(double), &__double_cmp);

  tdigest* td = 0;
  if (!td_alloc(compress, &td)) {
    fprintf(stderr, "Failed to allocate t-digest with compression: %d\n", compress);
    return 7;
  }

  char buf[4097] = {0}; // +1 on length to write nul byte at the end.
  uint32_t length = 0;
  while (!feof(stdin) && read_line(stdin, buf, 4096, &length)) {
    buf[length] = 0;
    char* end = 0;
    double value = strtod(buf, &end);

    if (end == buf) {
      fprintf(stderr, "bad line: %s\n", buf);
      continue;
    }

    td_add(td, value);
  }

  if (dump) {
    td_dump(td, stderr);
  }

  const uint64_t count = td_count(td);
  for (int i = 0; i < pc; i++) {
    double p = percentiles[i];
    fprintf(stdout, "%f = %f (%f)\n", p, td_percentile(td, p / 100.0), count * p);
  }

  return 0;
}

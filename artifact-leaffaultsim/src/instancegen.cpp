#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <omp.h>


#include <iostream>
#include <iomanip>
#include <map>
#include <string>
#include "Eigen/Dense"
#include "FalconKey.h"

using namespace Eigen;


int
main(int argc, char* argv[])
{
    IOFormat SingleLineFmt(FullPrecision, DontAlignCols, ", ", ", ", "[", "]", "[", "]");
    size_t  nsigs, subspacedim = 1;
    size_t  sigs_so_far = 0, batch_size = 1024;

    if(argc < 2) {
        std::cerr << "usage: " << argv[0] << " <keyfile> \n";
        exit(1);
    }

    FalconKey key;
    key.load(argv[1]);
    double sensitive_value = key.extractvalue();

    char fname_buf[1024];
    snprintf(fname_buf, sizeof fname_buf, "%s/sensitive_value", argv[1]);
    auto file = fopen(fname_buf, "wbx");
    if (file == NULL) {
        fprintf(stderr, "%s: already exists, skipping.\n", fname_buf);
    } else {
        size_t len = fwrite(&sensitive_value, sizeof(double), 1, file);
        fclose(file);
    }

    return 0;
}

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
//#include <complex>
#include <map>
#include <string>
#include "FalconKey.h"
#include <termcolor/termcolor.hpp>

int
main(int argc, char* argv[])
{
    if(argc < 2) {
        std::cerr << "usage: " << argv[0] << " <keyfile>\n";
        exit(1);
    }

    FalconKey key;
    key.load(argv[1]);
    double leaf = key.extractvalue();
    std::cout << key.extractvalue() << std::endl;

    if(leaf < 0.724) {
        std::cerr << termcolor::bright_green << "Erroneous leaf found! "
                  << "Key likely recoverable with leaffaultsim."
                  << termcolor::reset << std::endl;
        return 0;
    }

    std::cerr << termcolor::red << "No leaf error detected."
              << termcolor::reset << std::endl;
    
    return -1;
}

/*
    This file is part of the ChipWhisperer Example Targets
    Copyright (C) 2012-2020 NewAE Technology Inc.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "hal.h"
#include <stdint.h>
#include <stdlib.h>

#include "simpleserial.h"
#include "sign_inner.h"

uint8_t comp_sqrt(uint8_t cmd, uint8_t scmd, uint8_t len, uint8_t* in)
{
    fpr x, y;
    if(len != 8)
        return SS_ERR_LEN;

    trigger_high();
    x = *(uint64_t*)in;
    y = fpr_sqrt(x);

    trigger_low();
    simpleserial_put('r', 8, (uint8_t*)&y);

    return SS_ERR_OK;
}

// #pragma GCC pop_options

int main(void)
{
    platform_init();
    init_uart();
    trigger_setup();

    /* Device reset detected */
    putch('r');
    putch('R');
    putch('E');
    putch('S');
    putch('E');
    putch('T');
    putch(' ');
    putch(' ');
    putch(' ');
    putch('\n');

    simpleserial_init();
    simpleserial_addcmd('s', 8, comp_sqrt);
    while(1)
        simpleserial_get();
}

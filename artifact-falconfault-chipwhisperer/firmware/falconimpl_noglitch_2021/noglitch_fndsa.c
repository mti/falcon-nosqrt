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
#include "inner.h"
#include "falcon.h"

#define PRIVKEY_SIZE FALCON_PRIVKEY_SIZE(9)
#define EXPKEY_SIZE FALCON_EXPANDEDKEY_SIZE(9)
#define TMP_SIZE FALCON_TMPSIZE_EXPANDPRIV(9)
#define LOADKEY_PAYLOAD_SIZE 224

static uint8_t expkey[EXPKEY_SIZE];
static uint8_t privkey[PRIVKEY_SIZE];
static uint8_t tmp[TMP_SIZE];

uint8_t load_key(uint8_t cmd, uint8_t scmd, uint8_t len, uint8_t* in)
{
    if(len < 1)
        return SS_ERR_LEN;

    size_t start = LOADKEY_PAYLOAD_SIZE * (size_t)in[0], i;
    if(len > LOADKEY_PAYLOAD_SIZE+1 || start + len > PRIVKEY_SIZE+1)
        return SS_ERR_LEN;

    for(i=1; i<len; i++)
        privkey[start+i-1] = in[i];

    return SS_ERR_OK;
}

uint8_t send_privkey(uint8_t cmd, uint8_t scmd, uint8_t len, uint8_t* in)
{
    if(len < 1)
        return SS_ERR_LEN;

    size_t start = LOADKEY_PAYLOAD_SIZE * (size_t)in[0], rlen = LOADKEY_PAYLOAD_SIZE;
    if(start > PRIVKEY_SIZE)
	return SS_ERR_CMD;
    if(start + rlen > PRIVKEY_SIZE)
	rlen = PRIVKEY_SIZE - start;

    simpleserial_put('r', rlen, privkey + start);

    return SS_ERR_OK;
}


uint8_t expand_key(uint8_t cmd, uint8_t scmd, uint8_t len, uint8_t* in)
{
    int r;
    uint8_t rv;

    r = falcon_expand_privkey(
      expkey, EXPKEY_SIZE, privkey, PRIVKEY_SIZE, tmp, TMP_SIZE);
    
    rv = (r<0);

    simpleserial_put('r', 1, &rv);
    return rv;
}

static inline fpr * align_fpr(void *tmp)
{
    uint8_t *atmp;
    unsigned off;

    atmp = tmp;
    off = (uintptr_t)atmp & 7u;
    if (off != 0) {
    	atmp += 8u - off;
    }
    return (fpr *)atmp;
}

int main(void)
{
    platform_init();
    init_uart();

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
    simpleserial_addcmd('x', 1, expand_key);
    simpleserial_addcmd('l', LOADKEY_PAYLOAD_SIZE + 1, load_key);
    simpleserial_addcmd('L', 1, send_privkey);
    while(1)
        simpleserial_get();
}

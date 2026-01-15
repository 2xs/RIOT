#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "periph/flashpage.h"
#include "gnrc_xipfs.h"
#include "interface.h"
#include "saul.h"
#include "saul_reg.h"

#include "exec_common.h"

int get_temp(void)
{
    saul_reg_t *dev;
    phydat_t res;
    int dim;

    if ((dev = saul_reg_find_nth(5)) == NULL) {
        return 0;
    }

    if ((dim = saul_reg_read(dev, &res)) <= 0) {
        return 0;
    }

    return res.val[0];
}

int get_led(int pos)
{
    saul_reg_t *dev;
    phydat_t res;
    int dim;

    if ((unsigned int)pos > 3) {
        return -1;
    }

    if ((dev = saul_reg_find_nth(pos)) == NULL) {
        return -1;
    }

    if ((dim = saul_reg_read(dev, &res)) <= 0) {
        return -1;
    }

    return res.val[0];
}

int set_led(int pos, int val)
{
    saul_reg_t *dev;
    phydat_t res;
    int dim;

    if ((unsigned int)pos > 3) {
        return -1;
    }

    if ((unsigned int)val > 1) {
        return -1;
    }

    if ((dev = saul_reg_find_nth(pos)) == NULL) {
        return -1;
    }

    res.val[0] = val;

    if ((dim = saul_reg_write(dev, &res)) <= 0) {
        return -1;
    }

    return 0;
}

ssize_t copy_file(const char *name, void *buf, size_t nbyte)
{
    file_t *file;
    size_t i;

    if ((file = tinyfs_file_search(name)) == NULL) {
        return -1;
    }

    for (i = 0; i < nbyte && i < file->size; i++) {
        ((char *)buf)[i] = ((char *)file + sizeof(*file))[i];
    }

    return i;
}

int get_file_size(const char *name, size_t *size)
{
    file_t *file;

    if ((file = tinyfs_file_search(name)) == NULL) {
        return -1;
    }

    *size = file->size;
    return 0;
}

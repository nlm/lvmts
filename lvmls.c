/*
 * Copyright (C) 2011 Hubert Kario <kario@wsisiz.edu.pl>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 */
#include <lvm2cmd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lvmls.h"

struct pv_allocations *pv_segments=NULL;
size_t pv_segments_num=0;

// helper function to qsort, used to sort physical extents
static int
_compare_segments(const void *a, const void *b)
{
    struct pv_allocations *alloc_a, *alloc_b;
    int r;

    alloc_a = (struct pv_allocations *)a;
    alloc_b = (struct pv_allocations *)b;

    r = strcmp(alloc_a->vg_name, alloc_b->vg_name);
    if(r != 0)
        return r;

    r = strcmp(alloc_a->lv_name, alloc_b->lv_name);
    if(r != 0)
        return r;

    if (alloc_a->lv_start == alloc_b->lv_start)
        return 0;
    else if (alloc_a->lv_start > alloc_b->lv_start)
        return 1;
    else
        return -1;
}

static int
_find_segment(const void *key, const void *b)
{
    struct pv_allocations *alloc_a, *alloc_b;
    int r;

    alloc_a = (struct pv_allocations *)key;
    alloc_b = (struct pv_allocations *)b;

    r = strcmp(alloc_a->vg_name, alloc_b->vg_name);
    if(r != 0)
        return r;

    r = strcmp(alloc_a->lv_name, alloc_b->lv_name);
    if(r != 0)
        return r;

    if (alloc_a->lv_start >= alloc_b->lv_start
        && alloc_a->lv_start < alloc_b->lv_start + alloc_b->pv_length)
        return 0;
    else if (alloc_a->lv_start > alloc_b->lv_start)
        return 1;
    else
        return -1;
}

void sort_segments(struct pv_allocations *segments, size_t nmemb)
{
    qsort(segments, nmemb, sizeof(struct pv_allocations), _compare_segments);
}

// add information about physical segment to global variable pv_segments
void add_segment(char *pv_name, char *vg_name, char *vg_format, char *vg_attr,
    char *lv_name, char *pv_type, uint64_t pv_start, uint64_t pv_length,
    uint64_t lv_start)
{
    if(pv_segments_num==0)
        pv_segments = calloc(sizeof(struct pv_allocations), 1);

    if(!pv_segments)
        goto segment_failure;

#define str_copy_alloc(N, X) pv_segments[(N)].X = strdup(X);            \
    if(!pv_segments[(N)].X)                                             \
        goto segment_failure;

    if (pv_segments_num==0) {
        str_copy_alloc(0, pv_name);
        str_copy_alloc(0, vg_name);
        str_copy_alloc(0, vg_format);
        str_copy_alloc(0, vg_attr);
        str_copy_alloc(0, lv_name);
        str_copy_alloc(0, pv_type);

        pv_segments[0].pv_start = pv_start;
        pv_segments[0].pv_length = pv_length;
        pv_segments[0].lv_start = lv_start;
        pv_segments_num=1;
        return;
    }

    pv_segments = realloc(pv_segments,
        sizeof(struct pv_allocations)*(pv_segments_num+1));
    if (!pv_segments)
      goto segment_failure;

    str_copy_alloc(pv_segments_num, pv_name);
    str_copy_alloc(pv_segments_num, vg_name);
    str_copy_alloc(pv_segments_num, vg_format);
    str_copy_alloc(pv_segments_num, vg_attr);
    str_copy_alloc(pv_segments_num, lv_name);
    str_copy_alloc(pv_segments_num, pv_type);

    pv_segments[pv_segments_num].pv_start = pv_start;
    pv_segments[pv_segments_num].pv_length = pv_length;
    pv_segments[pv_segments_num].lv_start = lv_start;
    pv_segments_num+=1;

    return;

segment_failure:
    fprintf(stderr, "Out of memory\n");
    exit(1);

#undef str_copy_alloc
}

// helper funcion for lvm2cmd, used to parse mappings between
// logical extents and physical extents
void parse_pvs_segments(int level, const char *file, int line,
                 int dm_errno, const char *format)
{
    // disregard debug output
    if (level != 4)
      return;

    char pv_name[4096]={0}, vg_name[4096]={0}, vg_format[4096]={0},
         vg_attr[4096]={0}, pv_size[4096]={0}, pv_free[4096]={0},
         lv_name[4096]={0}, pv_type[4096]={0};
    int pv_start=0, pv_length=0, lv_start=0;
    int r;

    // try to match standard format (allocated extents)
    r = sscanf(format, " %4095s %4095s %4095s %4095s %4095s "
        "%4095s %40u %40u %4095s %40d %4095s ",
        pv_name, vg_name, vg_format, vg_attr, pv_size, pv_free,
        &pv_start, &pv_length, lv_name, &lv_start, pv_type);

    if (r==EOF) {
        fprintf(stderr, "Error matching line %i: %s\n", line, format);
        goto parse_error;
    } else if (r != 11) {
        // segments with state "free" require matching format without "lv_name"
        lv_name[0]='\000';

        r = sscanf(format, " %4095s %4095s %4095s %4095s %4095s %4095s "
            "%40u %40u %40d %4095s ",
            pv_name, vg_name, vg_format, vg_attr, pv_size, pv_free,
            &pv_start, &pv_length, &lv_start, pv_type);

        if (r==EOF || r != 10) {
            fprintf(stderr, "Error matching line %i: %s\n", line, format);
            goto parse_error;
        }
    } else { // r == 11
        // do nothing, correct parse
    }

    add_segment(pv_name, vg_name, vg_format, vg_attr, lv_name, pv_type,
        pv_start, pv_length, lv_start);

    // DEBUG
//    printf("matched %i fields:", r);

//    printf("%s,%s,%s,%s,%s,%s,%i,%i,%s,%i,%s\n",
//        pv_name, vg_name, vg_format, vg_attr, pv_size, pv_free,
//        pv_start, pv_length, lv_name, lv_start, pv_type);

    // DEBUG
    //printf("%s\n", format);
parse_error:
    return;
}

// convert logical extent from logical volume specified by lv_name,
// vg_name and logical extent number (le_num) to physical extent
// on specific device
struct pv_info *LE_to_PE(const char *vg_name, const char *lv_name, uint64_t le_num)
{
    struct pv_allocations pv_alloc = { .lv_name = (char *)lv_name,
                                       .vg_name = (char *)vg_name,
                                       .lv_start = le_num };

    struct pv_allocations *needle;

    needle = bsearch(&pv_alloc, pv_segments, pv_segments_num,
                sizeof(struct pv_allocations), _find_segment);

    if (!needle)
        return NULL;

    struct pv_info *pv_info;

    pv_info = malloc(sizeof(struct pv_info));
    if (!pv_info) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    pv_info->pv_name = strdup(needle->pv_name);
    if (!pv_info->pv_name) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    pv_info->start_seg = needle->pv_start +
      (le_num - needle->lv_start);

    return pv_info;
}

struct vg_pe_sizes {
    char *vg_name;
    uint64_t pe_size;
};

struct vg_pe_sizes *vg_pe_sizes;
size_t vg_pe_sizes_len;

// parse output from lvm2cmd about extent sizes
void parse_vgs_pe_size(int level, const char *file, int line,
                 int dm_errno, const char *format)
{
    // disregard debug output
    if (level != 4)
      return;

    char vg_name[4096], pe_size[4096];
    uint64_t pe_size_bytes=0;
    int r;

    r = sscanf(format, " %4095s %4095s ", vg_name, pe_size);
    if (r == EOF || r != 2) {
        fprintf(stderr, "%s:%i Error parsing line %i: %s\n",
            __FILE__, __LINE__, line, format);
        return;
    }

    double temp;
    char *tail;

    temp = strtod(pe_size, &tail);
    if (temp == 0.0) {
        fprintf(stderr, "%s:%i Error parsing line %i: %s\n",
            __FILE__, __LINE__, line, format);
        return;
    }

    switch(tail[0]){
	    case 'b':
	    case 'B':
	        pe_size_bytes = temp;
	        break;
	    case 'S':
	        pe_size_bytes = temp * 512;
            break;
        case 'k':
	        pe_size_bytes = temp * 1024;
	        break;
        case 'K':
            pe_size_bytes = temp * 1000;
            break;
        case 'm':
            pe_size_bytes = temp * 1024 * 1024;
	        break;
        case 'M':
	        pe_size_bytes = temp * 1000 * 1000;
            break;
        case 'g':
            pe_size_bytes = temp * 1024 * 1024 * 1024;
            break;
        case 'G':
            pe_size_bytes = temp * 1000 * 1000 * 1000;
            break;
        case 't':
            pe_size_bytes = temp * 1024 * 1024 * 1024 * 1024;
            break;
        case 'T':
            pe_size_bytes = temp * 1000 * 1000 * 1000 * 1000;
            break;
        case 'p':
            pe_size_bytes = temp * 1024 * 1024 * 1024 * 1024 * 1024;
            break;
        case 'P':
            pe_size_bytes = temp * 1000 * 1000 * 1000 * 1000 * 1000;
            break;
        case 'e':
            pe_size_bytes = temp * 1024 * 1024 * 1024 * 1024 * 1024 * 1024;
            break;
        case 'E':
            pe_size_bytes = temp * 1000 * 1000 * 1000 * 1000 * 1000 * 1000;
            break;
        default:
            pe_size_bytes = temp;
            /* break; */
    }

    // save info about first volume group
    if (vg_pe_sizes_len == 0) {
        vg_pe_sizes = malloc(sizeof(struct vg_pe_sizes));
        if (!vg_pe_sizes)
            goto vgs_failure;

        vg_pe_sizes[0].vg_name = strdup(vg_name);
        if (!vg_pe_sizes[0].vg_name)
            goto vgs_failure;

        vg_pe_sizes[0].pe_size = pe_size_bytes;

        vg_pe_sizes_len=1;

        return;
    }

    // save info about subsequent groups
    vg_pe_sizes = realloc(vg_pe_sizes, sizeof(struct vg_pe_sizes)*
        (vg_pe_sizes_len+1));
    if (!vg_pe_sizes)
        goto vgs_failure;

    vg_pe_sizes[vg_pe_sizes_len].vg_name = malloc(strlen(vg_name)+1);
    if(!vg_pe_sizes[vg_pe_sizes_len].vg_name)
        goto vgs_failure;
    strcpy(vg_pe_sizes[vg_pe_sizes_len].vg_name, vg_name);
    vg_pe_sizes[vg_pe_sizes_len].pe_size = pe_size_bytes;

    vg_pe_sizes_len+=1;

    return;

vgs_failure:
    fprintf(stderr, "Out of memory\n");
    exit(1);
}

// return size of extents in provided volume group
uint64_t get_pe_size(const char *vg_name)
{
    for(size_t i=0; i<vg_pe_sizes_len; i++)
        if (!strcmp(vg_pe_sizes[i].vg_name, vg_name))
            return vg_pe_sizes[i].pe_size;

    return 0;
}

// free allocated memory and objects
void le_to_pe_exit(struct program_params *pp)
{
    for(size_t i=0; i<pv_segments_num; i++){
        free(pv_segments[i].pv_name);
        free(pv_segments[i].vg_name);
        free(pv_segments[i].vg_format);
        free(pv_segments[i].vg_attr);
        free(pv_segments[i].lv_name);
        free(pv_segments[i].pv_type);
    }
    free(pv_segments);
    pv_segments = NULL;
    pv_segments_num = 0;

    for(size_t i=0; i<vg_pe_sizes_len; i++)
        free(vg_pe_sizes[i].vg_name);

    free(vg_pe_sizes);
    vg_pe_sizes = NULL;
    vg_pe_sizes_len = 0;
}

// initialize or reload cache variables
void init_le_to_pe(struct program_params *pp)
{
//    int r;

    if(pv_segments)
        le_to_pe_exit(pp);

    vg_pe_sizes = NULL;
    vg_pe_sizes_len = 0;


    lvm2_log_fn(parse_pvs_segments);
    if (!pp->lvm2_handle)
        pp->lvm2_handle = lvm2_init();

    lvm2_log_level(pp->lvm2_handle, 1);
//    r =
      lvm2_run(pp->lvm2_handle, "pvs --noheadings --segments -o+lv_name,"
        "seg_start_pe,segtype --units=b");

//    if (r)
//      fprintf(stderr, "command failed\n");

    sort_segments(pv_segments, pv_segments_num);

    lvm2_log_fn(parse_vgs_pe_size);

//    r =
      lvm2_run(pp->lvm2_handle, "vgs -o vg_name,vg_extent_size --noheadings --units=b");

    return;
}

// return number of free extents in PV in specified volume group
// or in whole volume group if pv_name is NULL
uint64_t get_free_extent_number(const char *vg_name, const char *pv_name)
{
    if (!vg_name)
        return 0;

    uint64_t sum=0;

    if(pv_name)
        for(size_t i=0; i < pv_segments_num; i++) {
            if (!strcmp(pv_segments[i].vg_name, vg_name) &&
                !strcmp(pv_segments[i].pv_name, pv_name) &&
                !strcmp(pv_segments[i].pv_type, "free"))
              sum+=pv_segments[i].pv_length;
        }
    else
        for(size_t i=0; i < pv_segments_num; i++)
            if (!strcmp(pv_segments[i].vg_name, vg_name) &&
                !strcmp(pv_segments[i].pv_type, "free"))
              sum+=pv_segments[i].pv_length;

    return sum;
}

struct le_info
get_first_LE_info(const char *vg_name, const char *lv_name,
    const char *pv_name)
{
    struct le_info ret = { .dev = NULL };

    for (size_t i=0; i < pv_segments_num; i++) {
        if (!strcmp(pv_segments[i].vg_name, vg_name) &&
            !strcmp(pv_segments[i].pv_name, pv_name) &&
            !strcmp(pv_segments[i].lv_name, lv_name)) {

            if (ret.dev == NULL) { // save first segment info
                ret.le = pv_segments[i].lv_start;
                ret.pe = pv_segments[i].pv_start;
                ret.dev = pv_segments[i].pv_name;
            } else {
                if (ret.le > pv_segments[i].lv_start) {
                    ret.le = pv_segments[i].lv_start;
                    ret.pe = pv_segments[i].pv_start;
                    ret.dev = pv_segments[i].pv_name;
                }
            }
        }
    }

    return ret;
}

struct le_info
get_PE_allocation(const char *vg_name, const char *pv_name,
    uint64_t pe_num)
{
    const char *free_str = "free";

    struct le_info ret = { .dev = NULL, .lv_name = NULL };

    for (size_t i=0; i < pv_segments_num; i++) {
        if (!strcmp(pv_segments[i].vg_name, vg_name) &&
            !strcmp(pv_segments[i].pv_name, pv_name) &&
            pv_segments[i].pv_start <= pe_num &&
            pv_segments[i].pv_start + pv_segments[i].pv_length > pe_num) {

            ret.dev = pv_segments[i].pv_name;
            if (!strcmp(pv_segments[i].pv_type, free_str))
                ret.lv_name = free_str;
            else
                ret.lv_name = pv_segments[i].lv_name;

            uint64_t diff = pe_num - pv_segments[i].pv_start;

            ret.le = pv_segments[i].lv_start + diff;
            ret.pe = pv_segments[i].pv_start + diff;

            return ret;
        }
    }

    return ret;
}

// return used number of extents by LV on provided PV
uint64_t get_used_space_on_pv(const char *vg_name, const char *lv_name,
    const char *pv_name)
{
    assert(vg_name);
    assert(lv_name);
    assert(pv_name);

    uint64_t sum = 0;

    for(size_t i=0; i < pv_segments_num; i++) {
        if(!strcmp(pv_segments[i].lv_name, lv_name) &&
           !strcmp(pv_segments[i].vg_name, vg_name) &&
           !strcmp(pv_segments[i].pv_name, pv_name)) {

            sum += pv_segments[i].pv_length;
        }
    }

    return sum;
}

void
pv_info_free(struct pv_info *pv)
{
  if (pv->pv_name)
    free(pv->pv_name);
  free(pv);
}

#ifdef STANDALONE
int main(int argc, char **argv)
{
    struct program_params pp = { .lvm2_handle = NULL };

    init_le_to_pe(&pp);

    if (argc != 4) {
        printf("Usage: %s VolumeGroupName LogicalVolumeName"
            " LogicalVolumeExtent\n", argv[0]);
        le_to_pe_exit(&pp);
        return 1;
    }

    for(int i=0; i < pv_segments_num; i++)
        if(!strcmp(pv_segments[i].lv_name, argv[1]))
            printf("%s %llu-%llu (%llu-%llu)\n", pv_segments[i].pv_name,
                pv_segments[i].pv_start,
                pv_segments[i].pv_start+pv_segments[i].pv_length,
                pv_segments[i].lv_start,
                pv_segments[i].lv_start+pv_segments[i].pv_length);

    if (argc <= 2)
        return 0;
    struct pv_info *pv_info;
    pv_info = LE_to_PE(argv[1], argv[2], atoi(argv[3]));
    if (pv_info)
        printf("LE no %i of %s-%s is at: %s:%llu\n", atoi(argv[3]), argv[1], argv[2],
            pv_info->pv_name, pv_info->start_seg);
    else
        printf("no LE found\n");

    printf("vg: %s, extent size: %llu bytes\n", argv[1], get_pe_size(argv[1]));

    long int free_extents = get_free_extent_number(argv[1], pv_info->pv_name);
    printf("vg: %s, pv: %s, free space: %lue (%lluB)\n", argv[1],
        pv_info->pv_name,
        free_extents,
        free_extents * get_pe_size(argv[1]));

    long int used_extents = get_used_space_on_pv(argv[1], argv[2],
        pv_info->pv_name);
    printf("Space used by lv %s on pv %s: %lue (%lluB)\n",
        argv[2],
        pv_info->pv_name,
        used_extents,
        used_extents * get_pe_size(argv[1]));

    struct le_info le_inf;

    le_inf = get_first_LE_info(argv[1], argv[2], pv_info->pv_name);

    printf("First LE on %s is %llu at PE %llu\n",
        le_inf.dev, le_inf.le, le_inf.pe);

    long int optimal_pe = le_inf.pe + atoi(argv[3]) - le_inf.le;

    printf("Optimal position for LE %i is at %s:%li\n", atoi(argv[3]),
        le_inf.dev, optimal_pe);

    printf("%s:%li is ", le_inf.dev, optimal_pe);

    if (optimal_pe == pv_info->start_seg) {
        printf("allocated correctly\n");
    } else {
        struct le_info optimal;
        optimal = get_PE_allocation(argv[1],  pv_info->pv_name, optimal_pe);

        if (optimal.dev == NULL)
            printf("after the end of the device\n");
        else if (!strcmp(optimal.lv_name, "free"))
            printf("free\n");
        else
            printf("allocated to %s, LE: %llu\n", optimal.lv_name, optimal.le);

    }


    pv_info_free(pv_info);

    le_to_pe_exit(&pp);

    lvm2_exit(pp.lvm2_handle);

    return 0;
}
#endif

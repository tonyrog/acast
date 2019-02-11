#ifndef __ACAST_CHANNEL_H__
#define __ACAST_CHANNEL_H__

#include <stdint.h>

typedef enum {
    ACAST_OP_SRC,
    ACAST_OP_CONST,
    ACAST_OP_ADD,
    ACAST_OP_ADDC,
    ACAST_OP_SUB,
    ACAST_OP_SUBC,
} acast_channel_op_t;

typedef struct
{
    acast_channel_op_t op;
    int src1;
    int src2;
    int dst;
} acast_op_t;

#define MAP_SRC(i,s1) { .op=OP_SRC, .src1=(s1), .src2 = 0, .dst = (i)}
#define MAP_CONST(i,v2) { .op=OP_CONST, .src1 = 0, .src2 = (v2), .dst = (i)}
#define MAP_ADD(i,s1,s2) { .op=OP_ADD, .src1 = (s1), .src2 = (s2), .dst = (i)}
#define MAP_ADDC(i,s1,v2) { .op=OP_ADD, .src1 = (s1), .src2 = (v2), .dst = (i)}
#define MAP_SUB(i,s1,s2) { .op=OP_SUB, .src1 = (s1), .src2 = (s2), .dst = (i)}
#define MAP_SUBC(i,s1,v2) { .op=OP_SUBC, .src1 = (s1), .src2 = (v2), .dst = (i)}

#define TYPE_MAX_uint8_t 0xff
#define TYPE_MIN_uint8_t 0x00

#define TYPE_MAX_int16_t 0x7fff
#define TYPE_MIN_int16_t -0x8000

#define TYPE_MAX_int32_t 0x7fffffff
#define TYPE_MIN_int32_t -0x80000000

#define CAT_HELPER2(x,y) x ## y
#define CAT2(x,y) CAT_HELPER2(x,y)

#define MAX_CHANNEL_OP  16
#define MAX_CHANNEL_MAP 8

typedef enum {
    ACAST_MAP_INVALID,
    ACAST_MAP_ID,
    ACAST_MAP_PERMUTE,
    ACAST_MAP_OP
} acast_map_type_t;

typedef struct
{
    acast_map_type_t type;
    size_t num_channel_ops;
    acast_op_t channel_op[MAX_CHANNEL_OP];
    uint8_t    channel_map[MAX_CHANNEL_MAP];
} acast_channel_ctx_t;


extern void print_channel_ctx(FILE* f, acast_channel_ctx_t* ctx);

extern int parse_channel_ctx(char* map, acast_channel_ctx_t* ctx,
			     int num_input_channels, int* num_output_channels);


#endif

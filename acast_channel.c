//
//  Channel map operations
//
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "acast_channel.h"

static char* parse_int(char* ptr, int* valp)
{
    int v = 0;
    while(isdigit(*ptr)) {
	v = v*10 + (*ptr - '0');
	ptr++;
    }
    *valp = v;
    return ptr;
}


int parse_channel_ops(char* map, acast_op_t* channel_op, size_t max_ops)
{
    int i = 0;
    char* ptr = map;

    while((i < max_ops) && (*ptr != '\0')) {
	switch(ptr[0]) {
	case '+':
	    if (!isdigit(ptr[1])) return -1;
	    channel_op[i].src1 = (ptr[1]-'0');
	    if ((ptr[2] == 'd') && isdigit(ptr[3])) {
		channel_op[i].op = ACAST_OP_ADDC;
		channel_op[i].dst  = i;
		ptr = parse_int(ptr+3, &channel_op[i].src2);
	    }
	    else if (isdigit(ptr[2])) {
		channel_op[i].src2 = (ptr[2]-'0');
		channel_op[i].dst  = i;
		channel_op[i].op   = ACAST_OP_ADD;
		ptr += 3;
	    }
	    else
		return -1;
	    break;
	case '-':
	    if (!isdigit(ptr[1])) return -1;
	    channel_op[i].src1 = ptr[1]-'0';
	    if ((ptr[2] == 'd') && isdigit(ptr[3])) {
		channel_op[i].op = ACAST_OP_SUBC;
		channel_op[i].dst  = i;
		ptr = parse_int(ptr+3, &channel_op[i].src2);
	    }
	    else if (isdigit(ptr[2])) {
		channel_op[i].src2 = ptr[2]-'0';
		channel_op[i].dst  = i;
		channel_op[i].op   = ACAST_OP_SUB;
		ptr += 3;
	    }
	    else
		return -1;
	    break;
	case 'd': {
	    int v = 0;
	    int s = 1;
	    if (ptr[1] == '-') { s = -1; ptr += 2; }
	    else if (ptr[1] == '+') { s = 1; ptr += 2; }
	    else ptr += 1;
	    if (!isdigit(*ptr)) return -1;
	    ptr = parse_int(ptr, &v);
	    channel_op[i].src1 = (s < 0) ? -v : v;
	    channel_op[i].src2 = 0;
	    channel_op[i].dst  = i;
	    channel_op[i].op   = ACAST_OP_CONST;
	    break;
	}
	case ',':  // separator
	    ptr++;
	    continue;
	case 'z':
	    channel_op[i].src1 = 0;
	    channel_op[i].src2 = 0;
	    channel_op[i].dst  = i;
	    channel_op[i].op   = ACAST_OP_CONST;
	    ptr++;
	    break;
	default:
	    if (!isdigit(ptr[0]))
		return -1;
	    channel_op[i].src1 = ptr[0]-'0';
	    channel_op[i].src2 = ptr[0]-'0';
	    channel_op[i].dst  = i;
	    channel_op[i].op   = ACAST_OP_SRC;
	    ptr++;
	    break;
	}
	i++;
    }
    if (*ptr == '\0')
	return i;
    return -1;
}

void print_channel_ops(FILE* f, acast_op_t* channel_op, size_t num_ops)
{
    int i;
    for (i = 0; i < num_ops; i++) {
	switch(channel_op[i].op) {
	case ACAST_OP_SRC:
	    fprintf(f, "%d", channel_op[i].src1);
	    break;
	case ACAST_OP_CONST:
	    fprintf(f, "d%d", channel_op[i].src1);
	    break;
	case ACAST_OP_ADD:
	    fprintf(f, "+%d%d", channel_op[i].src1, channel_op[i].src2);
	    break;
	case ACAST_OP_ADDC:
	    fprintf(f, "+%dd%d", channel_op[i].src1, channel_op[i].src2);
	    break;	    
	case ACAST_OP_SUB:
	    fprintf(f, "-%d%d", channel_op[i].src1, channel_op[i].src2);
	    break;
	case ACAST_OP_SUBC:
	    fprintf(f, "-%dd%d", channel_op[i].src1, channel_op[i].src2);
	    break; 
	}
    }
    fprintf(f,"\n");
}

void print_channel_ctx(FILE* f, acast_channel_ctx_t* ctx)
{
    int i;
    switch(ctx->type) {
    case ACAST_MAP_PERMUTE:
	fprintf(f, "map_type: permute\n");
	fprintf(f, "     map: ");
	for (i = 0; i < ctx->num_channel_ops; i++) {
	    fprintf(f, "%d", ctx->channel_map[i]);
	}
	fprintf(f, "\n");
	break;
    case ACAST_MAP_ID:
	fprintf(f, "map_type: id\n");
	fprintf(f, "     map: ");
	for (i = 0; i < ctx->num_channel_ops; i++) {
	    fprintf(f, "%d", ctx->channel_map[i]);
	}
	fprintf(f, "\n");
	break;
    case ACAST_MAP_OP:
	fprintf(f, "map_type: op\n");
	fprintf(f, "     map: op\n");	
	print_channel_ops(f, ctx->channel_op, ctx->num_channel_ops);
	break;
    default:
	fprintf(f, "  map_type: invalid\n"); break;
    }
}

// try build a simple channel map from channel_op if possible
// return 0 if not possible
// return 1 if simple id channel_map channel_map[i] = i
// return 2 if channel_map channel_map[i] = j where i != j for some i,j
//
static acast_map_type_t build_channel_map(
    acast_op_t* channel_op, size_t num_channel_ops,
    uint8_t* channel_map, size_t max_channel_map,
    int num_src_channels, int* num_dst_channels)
{
    int i;
    int max_dst_channel = -1;
    int use_channel_map = 1;
    int id_channel_map = 1;
	
    for (i = 0; (i < num_channel_ops) && (i < max_channel_map); i++) {
	if (channel_op[i].dst > max_dst_channel)
	    max_dst_channel = channel_op[i].dst;
	if ((channel_op[i].dst != i) ||
	    (channel_op[i].op != ACAST_OP_SRC)) {
	    use_channel_map = 0;
	    id_channel_map = 0;
	}
	else if (use_channel_map) {
	    channel_map[i] = channel_op[i].src1;
	    if (channel_map[i] != i)
		id_channel_map = 0;
	}
    }
    if (i >= max_channel_map)
	use_channel_map = 0;
	
    if (*num_dst_channels == 0)
	*num_dst_channels = max_dst_channel+1;
    
    if (*num_dst_channels != num_src_channels)
	id_channel_map = 0;

    if (use_channel_map && id_channel_map)
	return ACAST_MAP_ID;
    else if (use_channel_map)
	return ACAST_MAP_PERMUTE;
    return
	ACAST_MAP_OP;
}


acast_map_type_t parse_channel_map(char* map,
				   acast_op_t* channel_op,
				   size_t max_channel_ops,
				   size_t* num_channel_ops,
				   uint8_t* channel_map,
				   size_t max_channel_map,
				   int num_src_channels, int* num_dst_channels)
{
    int nc;
    
    if (strcmp(map, "auto") == 0) {
	int i;
	nc = (*num_dst_channels == 0) ? num_src_channels : *num_dst_channels;
	if (nc > max_channel_ops) nc = max_channel_ops;
	for (i = 0; i < nc; i++) {
	    channel_op[i].op = ACAST_OP_SRC;
	    channel_op[i].src1 = i % num_src_channels;
	    channel_op[i].src2 = 0;
	    channel_op[i].dst = i;
	}
    }
    else {
	if ((nc = parse_channel_ops(map, channel_op, max_channel_ops)) < 0)
	    return ACAST_MAP_INVALID;
    }
    *num_channel_ops = nc;
    return build_channel_map(channel_op, nc,
			     channel_map, max_channel_map,
			     num_src_channels, num_dst_channels);
}


int parse_channel_ctx(char* map, acast_channel_ctx_t* ctx,
		      int num_input_channels, int* num_output_channels)
{
    acast_map_type_t r;
    r = parse_channel_map(map,
			  ctx->channel_op, MAX_CHANNEL_OP,
			  &ctx->num_channel_ops,
			  ctx->channel_map, MAX_CHANNEL_MAP,
			  num_input_channels, num_output_channels);
    if (r == ACAST_MAP_INVALID)
	return -1;
    ctx->type = r;
    return r;
}

// typed map operation

static inline TYPE CAT2(sum_,TYPE)(TYPE a, TYPE b)
{
    TYPE2 s = a+b;
    if (s > CAT2(TYPE_MAX_,TYPE)) return CAT2(TYPE_MAX_,TYPE);
    else if (s < CAT2(TYPE_MIN_,TYPE)) return CAT2(TYPE_MIN_,TYPE);
    return s;
}

static inline TYPE CAT2(diff_,TYPE)(TYPE a, TYPE b)
{
    TYPE2 d = a-b;
    if (d > CAT2(TYPE_MAX_,TYPE)) return CAT2(TYPE_MAX_,TYPE);
    else if (d < CAT2(TYPE_MIN_,TYPE)) return CAT2(TYPE_MIN_,TYPE);
    return d;
}

// permute channels
static void CAT2(permute_ii_,TYPE)
    (TYPE* src, size_t nsrc,
     TYPE* dst,	size_t ndst,
     uint8_t* channel_map, uint32_t frames)
{
    while(frames--) {
	int i;
	for (i = 0;  i < ndst; i++)
	    *dst++ = src[channel_map[i]];
	src += nsrc;
    }
}


static void CAT2(permute_ni_,TYPE)
    (TYPE** src, size_t* src_stride, size_t nsrc,
     TYPE* dst,	size_t ndst,
     uint8_t* channel_map,
     uint32_t frames)
{

    int i;
    TYPE* src1[nsrc];

    for (i = 0; i < nsrc; i++) { src1[i] = src[i]; }

    while(frames--) {
	for (i = 0; i < ndst; i++)
	    *dst++ = *src1[channel_map[i]];
	for (i = 0; i < nsrc; i++) { src1[i] += src_stride[i]; }
    }
}

// general case where vectors are unreleated

static void CAT2(scatter_gather_nn_,TYPE)
    (TYPE** src, size_t* src_stride, size_t nsrc,
     TYPE** dst, size_t* dst_stride, size_t ndst,
     acast_op_t* map,  size_t nmap,
     size_t frames)
{
    int i;
    TYPE* src1[nsrc];
    TYPE* dst1[nsrc];

    for (i = 0; i < nsrc; i++) { src1[i] = src[i]; }
    for (i = 0; i < ndst; i++) { dst1[i] = dst[i]; }

    while(frames--) {
	for (i = 0; i < nmap; i++) {
	    TYPE v1, v2;
	    switch(map[i].op) {
	    case ACAST_OP_SRC:
		v1 = *src1[map[i].src1];
		break;
	    case ACAST_OP_CONST:
		v1 = map[i].src2;
		break;
	    case ACAST_OP_ADD:
		v1 = *src1[map[i].src1];
		v2 = *src1[map[i].src2];
		v1 = CAT2(sum_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_ADDC:
		v1 = *src1[map[i].src1];
		v2 = map[i].src2;
		v1 = CAT2(sum_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_SUB:
		v1 = *src1[map[i].src1];
		v2 = *src1[map[i].src2];
		v1 = CAT2(diff_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_SUBC:
		v1 = *src1[map[i].src1];
		v2 = map[i].src2;
		v1 = CAT2(diff_,TYPE)(v1,v2);
		break;
	    default:
		v1 = 0;
		break;
	    }
	    *dst1[map[i].dst] = v1;
	}
	for (i = 0; i < nsrc; i++) { src1[i] += src_stride[i]; }
	for (i = 0; i < ndst; i++) { dst1[i] += dst_stride[i]; }	
    }
}

// case where vectors are grouped
static void CAT2(scatter_gather_ii_,TYPE)
    (TYPE* src, size_t src_stride,
     TYPE* dst, size_t dst_stride,
     acast_op_t* map, size_t nmap,
     size_t frames)
{
    while(frames--) {
	int i;
	for (i = 0;  i < nmap; i++) {
	    TYPE v;
	    switch(map[i].op) {
	    case ACAST_OP_SRC:
		v = src[map[i].src1];
		break;
	    case ACAST_OP_CONST:
		v = map[i].src1;
		break;
	    case ACAST_OP_ADD:
		v = CAT2(sum_,TYPE)(src[map[i].src1],src[map[i].src2]);
		break;
	    case ACAST_OP_ADDC:
		v = CAT2(sum_,TYPE)(src[map[i].src1],map[i].src2);
		break;
	    case ACAST_OP_SUB:
		v = CAT2(diff_,TYPE)(src[map[i].src1],src[map[i].src2]);
		break;
	    case ACAST_OP_SUBC:
		v = CAT2(diff_,TYPE)(src[map[i].src1],map[i].src2);
		break;
	    default:
		v = 0;
		break;
	    }
	    dst[map[i].dst] = v;
	}
	dst += dst_stride;
	src += src_stride;
    }
}

// case where destination is interleaved ectors are grouped
static void CAT2(scatter_gather_ni_,TYPE)
    (TYPE** src, size_t* src_stride, size_t nsrc,
     TYPE* dst, size_t dst_stride,
     acast_op_t* map,  size_t nmap,
     size_t frames)
{
    int i;
    TYPE* src1[nsrc];
    
    for (i = 0; i < nsrc; i++) { src1[i] = src[i]; }

    while(frames--) {
	for (i = 0; i < nmap; i++) {
	    TYPE v1, v2;
	    switch(map[i].op) {
	    case ACAST_OP_SRC:
		v1 = *src1[map[i].src1];
		break;
	    case ACAST_OP_CONST:
		v1 = map[i].src2;
		break;
	    case ACAST_OP_ADD:
		v1 = *src1[map[i].src1];
		v2 = *src1[map[i].src2];
		v1 = CAT2(sum_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_ADDC:
		v1 = *src1[map[i].src1];
		v2 = map[i].src2;
		v1 = CAT2(sum_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_SUB:
		v1 = *src1[map[i].src1];
		v2 = *src1[map[i].src2];
		v1 = CAT2(diff_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_SUBC:
		v1 = *src1[map[i].src1];
		v2 = map[i].src2;
		v1 = CAT2(diff_,TYPE)(v1,v2);
		break;
	    default:
		v1 = 0;
		break;
	    }
	    dst[map[i].dst] = v1;
	}
	for (i = 0; i < nsrc; i++) { src1[i] += src_stride[i]; }
	dst += dst_stride;
    }
}


#undef TYPE
#undef TYPE2

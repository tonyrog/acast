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
    (TYPE** src, size_t nsrc,
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
	for (i = 0; i < nsrc; i++) { src1[i]++; }
    }
}

// general case where vectors are unreleated

static void CAT2(scatter_gather_,TYPE)
    (TYPE** src, size_t* src_stride,
     TYPE** dst, size_t* dst_stride,
     acast_op_t* map,  size_t nmap,
     size_t nsamples)
{
    int i;

    for (i = 0; i < nsamples; i++) {
	int j;
	for (j = 0; j < nmap; j++) {
	    int s1, s2, d;
	    TYPE v1, v2;
	    switch(map[j].op) {
	    case ACAST_OP_SRC:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		break;
	    case ACAST_OP_CONST:
		v1 = map[j].src2;
		break;
	    case ACAST_OP_ADD:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		s2 = map[j].src2;
		v2 = src[s2][src_stride[s2]*i];
		v1 = CAT2(sum_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_ADDC:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		v2 = map[j].src2;
		v1 = CAT2(sum_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_SUB:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		s2 = map[j].src2;
		v2 = src[s2][src_stride[s2]*i];
		v1 = CAT2(diff_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_SUBC:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		v2 = map[j].src2;
		v1 = CAT2(diff_,TYPE)(v1,v2);
		break;
	    default:
		v1 = 0;
		break;
	    }
	    d = map[j].dst;
	    dst[d][dst_stride[d]*i] = v1;
	}
    }
}

// case where vectors are grouped
static void CAT2(scatter_gather_ii_,TYPE)
    (TYPE* src, size_t src_stride,
     TYPE* dst, size_t dst_stride,
     acast_op_t* map, size_t nmap,
     size_t nsamples)
{
    while(nsamples--) {
	int j;
	for (j = 0;  j < nmap; j++) {
	    TYPE v;
	    switch(map[j].op) {
	    case ACAST_OP_SRC:
		v = src[map[j].src1];
		break;
	    case ACAST_OP_CONST:
		v = map[j].src1;
		break;
	    case ACAST_OP_ADD:
		v = CAT2(sum_,TYPE)(src[map[j].src1],src[map[j].src2]);
		break;
	    case ACAST_OP_ADDC:
		v = CAT2(sum_,TYPE)(src[map[j].src1],map[j].src2);
		break;
	    case ACAST_OP_SUB:
		v = CAT2(diff_,TYPE)(src[map[j].src1],src[map[j].src2]);
		break;
	    case ACAST_OP_SUBC:
		v = CAT2(diff_,TYPE)(src[map[j].src1],map[j].src2);
		break;
	    default:
		v = 0;
		break;
	    }
	    dst[map[j].dst] = v;
	}
	dst += dst_stride;
	src += src_stride;
    }
}

// case where destination is interleaved ectors are grouped
static void CAT2(scatter_gather_ni_,TYPE)
    (TYPE** src, size_t* src_stride,
     TYPE* dst, size_t dst_stride,
     acast_op_t* map,  size_t nmap,
     size_t nsamples)
{
    int i;
    for (i = 0; i < nsamples; i++) {
	int j;
	for (j = 0; j < nmap; j++) {
	    int s1, s2;
	    TYPE v1, v2;
	    switch(map[j].op) {
	    case ACAST_OP_SRC:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		break;
	    case ACAST_OP_CONST:
		v1 = map[j].src2;
		break;
	    case ACAST_OP_ADD:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		s2 = map[j].src2;
		v2 = src[s2][src_stride[s2]*i];
		v1 = CAT2(sum_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_ADDC:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		v2 = map[j].src2;
		v1 = CAT2(sum_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_SUB:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		s2 = map[j].src2;
		v2 = src[s2][src_stride[s2]*i];
		v1 = CAT2(diff_,TYPE)(v1,v2);
		break;
	    case ACAST_OP_SUBC:
		s1 = map[j].src1;
		v1 = src[s1][src_stride[s1]*i];
		v2 = map[j].src2;
		v1 = CAT2(diff_,TYPE)(v1,v2);
		break;
	    default:
		v1 = 0;
		break;
	    }
	    dst[map[j].dst] = v1;
	}
	dst += dst_stride;
    }
}


#undef TYPE
#undef TYPE2

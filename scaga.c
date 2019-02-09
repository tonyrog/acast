//
// Scatter gather interface
//
#include <stdio.h>
#include <stdint.h>

#include <alsa/asoundlib.h>
#include "g711.h"

#define TEST

#define OP_SRC   0
#define OP_CONST 1
#define OP_ADD   2
#define OP_ADDC  3

#define MAP_SRC(i,s1) { .op=OP_SRC, .src1=(s1), .src2 = 0, .dst = (i) }
#define MAP_CONST(i,v2) { .op=OP_CONST, .src1 = 0, .src2 = (v2), .dst = (i) }
#define MAP_ADD(i,s1,s2) { .op=OP_ADD, .src1 = (s1), .src2 = (s2), .dst = (i) }
#define MAP_ADDC(i,s1,v2) { .op=OP_ADD, .src1 = (s1), .src2 = (v2), .dst = (i) }

typedef struct
{
  int op;
  int src1;
  int src2;
  int dst;
} chan_map_t;


#define SWAPu16(x) __builtin_bswap16((x))
#define SWAPu32(x) __builtin_bswap32((x))
#define SWAPi16(x) ((int16_t) __builtin_bswap16((x)))
#define SWAPi32(x) ((int32_t) __builtin_bswap32((x)))

int64_t read_pcm(void* ptr, snd_pcm_format_t format)
{
  switch(format) {
  case SND_PCM_FORMAT_S8: return *((int8_t*) ptr);
  case SND_PCM_FORMAT_U8: return *((uint8_t*) ptr);
#if __BYTE_ORDER == __LITTLE_ENDIAN
  case SND_PCM_FORMAT_S16_LE: return *((int16_t*) ptr);
  case SND_PCM_FORMAT_U16_LE: return *((uint16_t*) ptr);
  case SND_PCM_FORMAT_S32_LE: return *((int32_t*) ptr);
  case SND_PCM_FORMAT_U32_LE: return *((uint32_t*) ptr);
  case SND_PCM_FORMAT_S16_BE: return SWAPi16(*((uint16_t*) ptr));
  case SND_PCM_FORMAT_U16_BE: return SWAPu16(*((uint16_t*) ptr));
  case SND_PCM_FORMAT_S32_BE: return SWAPi32(*((uint32_t*) ptr));
  case SND_PCM_FORMAT_U32_BE: return SWAPu32(*((uint32_t*) ptr));
#endif
#if __BYTE_ORDER == __BIG_ENDIAN
  case SND_PCM_FORMAT_S16_BE: return *((int16_t*) ptr);
  case SND_PCM_FORMAT_U16_BE: return *((uint16_t*) ptr);
  case SND_PCM_FORMAT_S32_BE: return *((int32_t*) ptr);
  case SND_PCM_FORMAT_U32_BE: return *((uint32_t*) ptr);
  case SND_PCM_FORMAT_S16_LE: return SWAPi16(*((uint16_t*) ptr));
  case SND_PCM_FORMAT_U16_LE: return SWAPu16(*((uint16_t*) ptr));
  case SND_PCM_FORMAT_S32_LE: return SWAPi32(*((uint32_t*) ptr));
  case SND_PCM_FORMAT_U32_LE: return SWAPu32(*((uint32_t*) ptr));
#endif
  case SND_PCM_FORMAT_MU_LAW: return ulaw2linear(*(uint8_t*)ptr);
  case SND_PCM_FORMAT_A_LAW:  return alaw2linear(*(uint8_t*)ptr);
  default: return 0;
  }
}


void write_pcm(void* ptr, snd_pcm_format_t format, int64_t val)
{
  switch(format) {
  case SND_PCM_FORMAT_S8: *((int8_t*) ptr) = val; break;
  case SND_PCM_FORMAT_U8: *((uint8_t*) ptr) = val; break;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  case SND_PCM_FORMAT_S16_LE: *((int16_t*)ptr) = val; break;
  case SND_PCM_FORMAT_U16_LE: *((uint16_t*)ptr) = val; break;
  case SND_PCM_FORMAT_S32_LE: *((int32_t*)ptr) = val; break;
  case SND_PCM_FORMAT_U32_LE: *((uint32_t*)ptr) = val; break;
  case SND_PCM_FORMAT_S16_BE: *((int16_t*)ptr) = SWAPi16((uint16_t)val); break;
  case SND_PCM_FORMAT_U16_BE: *((uint16_t*)ptr) = SWAPu16((uint16_t)val); break;
  case SND_PCM_FORMAT_S32_BE: *((int32_t*)ptr) = SWAPi32((uint32_t)val); break;
  case SND_PCM_FORMAT_U32_BE: *((uint32_t*)ptr) = SWAPu32((uint32_t)val); break;
#endif
#if __BYTE_ORDER == __BIG_ENDIAN
  case SND_PCM_FORMAT_S16_BE: *((int16_t*) ptr) = val; break;
  case SND_PCM_FORMAT_U16_BE: *((uint16_t*) ptr) = val; break;
  case SND_PCM_FORMAT_S32_BE: *((int32_t*) ptr) = val; break;
  case SND_PCM_FORMAT_U32_BE: *((uint32_t*) ptr) = val; break;
  case SND_PCM_FORMAT_S16_LE: *((int16_t*)ptr) = SWAPi16((uint16_t)val); break;
  case SND_PCM_FORMAT_U16_LE: *((uint16_t*)ptr) = SWAPu16((uint16_t)val); break;
  case SND_PCM_FORMAT_S32_LE: *((int32_t*)ptr) = SWAPi32((uint32_t)val); break;
  case SND_PCM_FORMAT_U32_LE: *((uint32_t*)ptr) = SWAPu32((uint32_t)val); break;
#endif
  case SND_PCM_FORMAT_MU_LAW: *((uint8_t*)ptr) = linear2ulaw(val); break;
  case SND_PCM_FORMAT_A_LAW:  *((uint8_t*)ptr) = linear2alaw(val); break;
  default: break;
  }
}

// "simple" copy
int is_chan_map_id(chan_map_t* map, size_t nmap)
{
  int i;
  for (i = 0; i < nmap; i++) {
    if (map[i].op != OP_SRC) return 0;
    if (map[i].src1 != i) return 0;
    if (map[i].dst != i) return 0;
  }
  return 1;
}

// "permute" channels
int is_chan_map_one_to_one(chan_map_t* map, size_t nmap)
{
  uint8_t sm[nmap];
  uint8_t dm[nmap];
  int i;
  for (i = 0; i < nmap; i++) { sm[i] = 0; dm[i] = 0; }
  for (i = 0; i < nmap; i++) {
    if (map[i].op != OP_SRC) return 0;
    if ((map[i].src1 < 0) || (map[i].src1 >= nmap)) return 0;
    if ((map[i].dst < 0) || (map[i].dst >= nmap)) return 0;
    if (sm[map[i].src1]) return 0;
    if (dm[map[i].dst]) return 0;
    sm[map[i].src1] = 1;
    dm[map[i].dst] = 1;
  }
  return 1;
}

// check for interleaved pointers (must be size nptr)
// we could have a version where we try to find the stride
int is_interleaved_16(int16_t** ptr, int* stride, size_t nptr)
{
  int i;
  for (i = 0; i < nptr-1; i++) {
    if (ptr[i]+1 != ptr[i+1]) return 0;
    if (stride[0] != nptr) return 0;
  }
  if (ptr[nptr-1]+1 != ptr[0]+nptr)
    return 0;
  if (stride[nptr-1] != nptr) return 0;
  return 1;
}


// scatter 6 interleaved channels into 6 individual channels
void scatter_16_i2(int16_t* src, int x0,
		   int16_t* d0, int y0,
		   int16_t* d1, int y1,
		   size_t nsamples)
{
  while (nsamples--) {
    int16_t* src0 = src;
    *d0 = *src0++; d0 += y0;
    *d1 = *src0++; d1 += y1;
    src += x0;
  }
}

void gather_16_i2(int16_t* s0, int x0,
		  int16_t* s1, int x1,
		  int16_t* dst, int y0,
		  size_t nsamples)
{
  while (nsamples--) {
    int16_t* dst0 = dst;
    *dst0++ = *s0; s0 += x0;
    *dst0++ = *s1; s1 += x1;
    dst += y0;
  }
}

// scatter 6 interleaved channels into 6 individual channels
void scatter_16_i6(int16_t* src, int x0,
		   int16_t* d0, int y0,
		   int16_t* d1, int y1,
		   int16_t* d2, int y2,
		   int16_t* d3, int y3,
		   int16_t* d4, int y4,
		   int16_t* d5, int y5,
		   size_t nsamples)
{
  while (nsamples--) {
    int16_t* src0 = src;
    *d0 = *src0++; d0 += y0;
    *d1 = *src0++; d1 += y1;
    *d2 = *src0++; d2 += y2;
    *d3 = *src0++; d3 += y3;
    *d4 = *src0++; d4 += y4;
    *d5 = *src0++; d5 += y5;
    src += x0;
  }
}

// gather channels into interleaved channels
void gather_16_i6(int16_t* s0, int x0,
		  int16_t* s1, int x1,
		  int16_t* s2, int x2,
		  int16_t* s3, int x3,
		  int16_t* s4, int x4,
		  int16_t* s5, int x5,
		  int16_t* dst, int y0,
		  size_t nsamples)
{
  while (nsamples--) {
    int16_t* dst0 = dst;
    *dst0++ = *s0; s0 += x0;
    *dst0++ = *s1; s1 += x1;
    *dst0++ = *s2; s2 += x2;
    *dst0++ = *s3; s3 += x3;
    *dst0++ = *s4; s4 += x4;
    *dst0++ = *s5; s5 += x5;
    dst += y0;
  }
}

void scatter_gather_16(int16_t** src, int* src_stride, size_t nsrc,
		       int16_t** dst, int* dst_stride, size_t ndst,
		       chan_map_t* map,  size_t nmap,
		       size_t nsamples)
{
  int i;

  if ((nsrc == 2) && (ndst == 2) && (nmap == 2) &&
      is_chan_map_one_to_one(map, nmap)) {
    if (is_interleaved_16(src, src_stride, nsrc)) {
      scatter_16_i2(src[0], 2,
		    dst[map[0].dst], dst_stride[map[0].dst],
		    dst[map[1].dst], dst_stride[map[1].dst],
		    nsamples);
      return;
    }
    if (is_interleaved_16(dst, dst_stride, ndst)) {
      gather_16_i2(src[map[0].src1], src_stride[map[0].src1],
		   src[map[1].src1], src_stride[map[1].src1],
		   dst[0], 2,
		   nsamples);
      return;
    }
  }

  if ((nsrc == 6) && (ndst == 6) && (nmap == 6) &&
      is_chan_map_one_to_one(map, nmap)) {
    if (is_interleaved_16(src, src_stride, nsrc)) {
      scatter_16_i6(src[0], 6,
		    dst[map[0].dst], dst_stride[map[0].dst],
		    dst[map[1].dst], dst_stride[map[1].dst],
		    dst[map[2].dst], dst_stride[map[2].dst],
		    dst[map[3].dst], dst_stride[map[3].dst],
		    dst[map[4].dst], dst_stride[map[4].dst],
		    dst[map[5].dst], dst_stride[map[5].dst],
		    nsamples);
      return;
    }
    if (is_interleaved_16(dst, dst_stride, ndst)) {
      gather_16_i6(src[map[0].src1], src_stride[map[0].src1],
		   src[map[1].src1], src_stride[map[1].src1],
		   src[map[2].src1], src_stride[map[2].src1],
		   src[map[3].src1], src_stride[map[3].src1],
		   src[map[4].src1], src_stride[map[4].src1],
		   src[map[5].src1], src_stride[map[5].src1],
		   dst[0], 6,
		   nsamples);
      return;
    }
  }

  // general case
  for (i = 0; i < nsamples; i++) {
    int j;
    for (j = 0; j < nmap; j++) {
      int s1, s2, d, v1, v2;
	
      switch(map[j].op) {
      case OP_SRC:
	s1 = map[j].src1;
	v1 = src[s1][src_stride[s1]*i];
	d = map[j].dst;
	dst[d][dst_stride[d]*i] = v1;
	break;
      case OP_CONST:
	v2 = map[j].src2;
	d = map[j].dst;
	dst[d][dst_stride[d]*i] = v2;
	break;
      case OP_ADD:
	s1 = map[j].src1;
	v1 = src[s1][src_stride[s1]*i];
	s2 = map[j].src2;
	v2 = src[s2][src_stride[s2]*i];
	d = map[j].dst;
	dst[d][dst_stride[d]*i] = v1+v2;
	break;
      case OP_ADDC:
	s1 = map[j].src1;
	v1 = src[s1][src_stride[s1]*i];
	v2 = map[j].src2;
	d = map[j].dst;
	dst[d][dst_stride[d]*i] = v1+v2;
	break;
      default:
	break;
      }
    }
  }
}

//
// General scatter gather for any pcm type, conversion and mapping
//
void scatter_gather_x(void** src, int* src_stride, size_t nsrc,
		      snd_pcm_format_t src_fmt,
		      void** dst, int* dst_stride, size_t ndst,
		      snd_pcm_format_t dst_fmt,
		      chan_map_t* map,  size_t nmap,
		      size_t nsamples)
{
  void* sptr[nsrc];
  void* dptr[ndst];
  size_t sbytes = (snd_pcm_format_physical_width(src_fmt)+7)/8;
  size_t dbytes = (snd_pcm_format_physical_width(dst_fmt)+7)/8;
  int slen[nsrc];
  int dlen[nsrc];
  int i;
    
  for (i = 0; i < nsrc; i++) {
    sptr[i] = src[i];
    slen[i] = src_stride[i]*sbytes;
  }
  for (i = 0; i < ndst; i++) {
    dptr[i] = dst[i];
    dlen[i] = dst_stride[i]*dbytes;
  }

  while(nsamples--) {
    int j;
    for (j = 0; j < nmap; j++) {
      int64_t v;
      
      switch(map[j].op) {
      case OP_SRC:
	v = read_pcm(sptr[map[j].src1], src_fmt);
	break;
      case OP_CONST:
	v = map[j].src2;
	break;
      case OP_ADD:
	v = read_pcm(sptr[map[j].src1], src_fmt);
	v += read_pcm(sptr[map[j].src2], src_fmt);
	break;
      case OP_ADDC:
	v = read_pcm(sptr[map[j].src1], src_fmt);
	v += map[j].src2;
	break;
      default:
	v = 0;
	break;
      }
      write_pcm(dptr[map[j].dst], dst_fmt, v);
    }
    for (i = 0; i < nsrc; i++) sptr[i] += slen[i];
    for (i = 0; i < ndst; i++) dptr[i] += dlen[i];
  }
}


void scatter_gather(void** src, int* src_stride, size_t nsrc,
		    snd_pcm_format_t src_fmt,
		    void** dst, int* dst_stride, size_t ndst,
		    snd_pcm_format_t dst_fmt,
		    chan_map_t* map,  size_t nmap,
		    size_t nsamples)
{
  if (0 /*src_fmt == dst_fmt*/) {
    switch(snd_pcm_format_physical_width(src_fmt)) {
      //    case 8:
      //      scatter_gather_8(src, nsrc, dst, ndst, map, nmap, nsamples); break;
    case 16:
      // must check for native endian!
      scatter_gather_16((int16_t**)src, src_stride, nsrc,
			(int16_t**)dst, dst_stride, ndst,
			map, nmap, nsamples);
      break;
      //    case 32:
      // scatter_gather_32(src, nsrc, dst, ndst, map, nmap, nsamples); break;
    default: break;
    }
  }
  else {
    scatter_gather_x(src, src_stride, nsrc, src_fmt,
		     dst, dst_stride, ndst, dst_fmt,
		     map, nmap, nsamples);
  }
}

#ifdef TEST

void print_elem(char* name, int i, void* ptr, snd_pcm_format_t fmt)
{
  switch(fmt) {
  case SND_PCM_FORMAT_S8:
    printf("%s[%d] = %d\n", name, i, *((int8_t*)ptr)); break;
  case SND_PCM_FORMAT_S16_LE:
    printf("%s[%d] = %d\n", name, i, *((int16_t*)ptr)); break;
  case SND_PCM_FORMAT_S32_LE:
    printf("%s[%d] = %d\n", name, i, *((int32_t*)ptr)); break;    
  case SND_PCM_FORMAT_U8:
    printf("%s[%d] = %d\n", name, i, *((uint8_t*)ptr)); break;    
  case SND_PCM_FORMAT_U16_LE:
    printf("%s[%d] = %d\n", name, i, *((uint16_t*)ptr)); break;    
  case SND_PCM_FORMAT_U32_LE:
    printf("%s[%d] = %d\n", name, i, *((uint32_t*)ptr)); break;
  case SND_PCM_FORMAT_MU_LAW:
    printf("%s[%d] = 0x%02x\n", name, i, *((uint8_t*)ptr)); break;
  case SND_PCM_FORMAT_A_LAW:
    printf("%s[%d] = 0x%02x\n", name, i, *((uint8_t*)ptr)); break;    
  default:
    printf("%s[%d] = ???\n", name, i); break;
    break;
  }
}    

void print_vector(char* name, void* vec, size_t len, snd_pcm_format_t fmt)
{
  size_t elem_size = (snd_pcm_format_physical_width(fmt)+7) / 8;
  int i;
  
  for (i = 0; i < (int)len; i++) {
    print_elem(name, i, vec, fmt);
    vec += elem_size;
  }
}

int main(int argc, char** argv)
{
  int16_t A[5] = {104, 408, 688, 1008, 1312 };
  int dummy1;
  int16_t B[5] = {20, 50, 80, 110, 140 };
  int dummy2;
  int16_t C[5] = {3008, 6016, 8960, 12032, 15140 };
  int dummy3;
  uint8_t ABC[15];
  int dummy4;
  int16_t A1[5] = {0, 0, 0, 0, 0 };
  int dummy5;
  int16_t B1[5] = {0, 0, 0, 0, 0 };
  int dummy6;
  int16_t C1[5] = {0, 0, 0, 0, 0 };
  int dummy7;

  int16_t* src[3] = { A, B, C };
  int      src_stride[3] = { 1, 1, 1 };

  uint8_t* dst[3] = { &ABC[0], &ABC[1], &ABC[2] };
  int      dst_stride[3] = { 3, 3, 3 };

  int16_t* src1[3] = { A1, B1, C1 };
  int      src1_stride[3] = { 1, 1, 1 };
  
  chan_map_t map[3] = { MAP_SRC(0,0), MAP_SRC(1,1), MAP_SRC(2,2) };

  // gather A,B,C into interleaved ABC

  print_vector("A", A, 5, SND_PCM_FORMAT_S16_LE);
  print_vector("B", B, 5, SND_PCM_FORMAT_S16_LE);
  print_vector("C", C, 5, SND_PCM_FORMAT_S16_LE);

  scatter_gather((void**)src, src_stride, 3, SND_PCM_FORMAT_S16_LE,
		 (void**)dst, dst_stride, 3, SND_PCM_FORMAT_A_LAW,
		 map, 3, 5);

  print_vector("ABC", ABC, 15, SND_PCM_FORMAT_A_LAW);

  scatter_gather((void**)dst, dst_stride, 3, SND_PCM_FORMAT_A_LAW,
		 (void**)src1, src1_stride, 3, SND_PCM_FORMAT_S16_LE,
		 map, 3, 5);

  print_vector("A1", A1, 5, SND_PCM_FORMAT_S16_LE);
  print_vector("B1", B1, 5, SND_PCM_FORMAT_S16_LE);
  print_vector("C1", C1, 5, SND_PCM_FORMAT_S16_LE);

  exit(0);
}

#endif

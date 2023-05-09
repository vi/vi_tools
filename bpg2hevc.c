// This does not work.
// See https://github.com/vi/bpg2hevc instead.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

unsigned int ue7_32() {
		int i;
		unsigned int buffer = 0;
		for(i=0; i<5; ++i) {
				int c = fgetc(stdin);
				if (c == EOF) return buffer; // should actually fail
				buffer = (buffer << 7) | (c & 0x7F);
				if ((c & 0x80) == 0) {
						return buffer;
				}
		}
		return buffer;
}

// Copied and adapted from libbpg.c
static int build_msps(unsigned char **pbuf, int *pbuf_len,
                      int width, int height, int chroma_format_idc,
                      int bit_depth)
{
    int idx, msps_len, buf_len, i;
    unsigned long len;
    unsigned char *buf, *msps_buf;

    *pbuf = NULL;

    /* build the modified SPS header to please libavcodec */
    len = ue7_32();

    msps_len = 1 + 4 + 4 + 1 + len;
    msps_buf = malloc(msps_len);
    idx = 0;
    msps_buf[idx++] = chroma_format_idc;
    msps_buf[idx++] = (width >> 24);
    msps_buf[idx++] = (width >> 16);
    msps_buf[idx++] = (width >> 8);
    msps_buf[idx++] = (width >> 0);
    msps_buf[idx++] = (height >> 24);
    msps_buf[idx++] = (height >> 16);
    msps_buf[idx++] = (height >> 8);
    msps_buf[idx++] = (height >> 0);
    msps_buf[idx++] = bit_depth - 8;
    fread(msps_buf + idx, 1, len,  stdin);
    idx += len;
    assert(idx == msps_len);
    
    buf_len = 4 + 2 + msps_len * 2;
    buf = malloc(buf_len);

    idx = 0;
    /* NAL header */
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;
    buf[idx++] = 0x01; 
    buf[idx++] = (48 << 1); /* application specific NAL unit type */
    buf[idx++] = 1;

    /* add the modified SPS with the correct escape codes */
    i = 0;
    while (i < msps_len) {
        if ((i + 1) < msps_len && msps_buf[i] == 0 && msps_buf[i + 1] == 0) {
            buf[idx++] = 0x00;
            buf[idx++] = 0x00;
            buf[idx++] = 0x03;
            i += 2;
        } else {
            buf[idx++] = msps_buf[i++];
        }
    }
    /* the last byte cannot be 0 */
    if (idx == 0 || buf[idx - 1] == 0x00)
        buf[idx++] = 0x80;
    free(msps_buf);
    
    *pbuf_len = idx;
    *pbuf = buf;
    return 0; // input_data_len1 - input_data_len;
}

int main(int argc, char* argv[]) {
		if (argc != 1) {
				fprintf(stderr, "Usage: bpg2hevc < file.bpg > file.265\n");
				fprintf(stderr, "Should convert a BPG (Better Portable Graphics) file to raw HEVC bitstream losslessly\n");
				fprintf(stderr, "Does not support animation, alpha. Metadata like colour format is lost.\n");
				return 1;
		}
		unsigned char buf[4] = {0,0,0,0};
		fread(buf, 4, 1, stdin);
		if (!!memcmp(buf, "BPG\xFB", 4)) {
				fprintf(stderr, "BPG signature invalid\n");
				return 2;
		}
		fread(buf, 2, 1, stdin);

		int pixfmt = (buf[0]&0xE0) >> 5;
		int alpha1 = (buf[0]&0x10) ? 1 : 0;
		int bit_depth = 8 + (buf[0] & 0x0F);

        fprintf(stderr, "pixfmt=%d alpha1=%d bit_depth=%d\n", pixfmt, alpha1, bit_depth);

		int colspc = (buf[1]&0xF0) >> 4;
		int ext_flag = (buf[1]&0x08) ? 1 : 0;
		int alpha2 =   (buf[1]&0x04) ? 1 : 0;
		int limrange = (buf[1]&0x02) ? 1 : 0;
		int animat =   (buf[1]&0x01) ? 1 : 0;

        fprintf(stderr, "colspc=%d ext=%d alpha2=%d limrange=%d anim=%d\n", colspc, ext_flag, alpha2, limrange, animat);

		unsigned int width = ue7_32();
		unsigned int height = ue7_32();
		unsigned int datalen = ue7_32();


		fprintf(stderr, "width=%u height=%u datalen=%u\n", width, height, datalen);

		if (alpha1 || alpha2) {
				fprintf(stderr, "Alpha channel is not supported\n");
				return 3;
		}
        
        /*static int build_msps(unsigned char **pbuf, int *pbuf_len,
                      int width, int height, int chroma_format_idc,
                      int bit_depth)*/

        unsigned char *spsbuf = NULL;
        int spsbuf_len = 0;

        build_msps(&spsbuf, &spsbuf_len,   width, height,  pixfmt,  bit_depth);

        if (!spsbuf) {
            fprintf(stderr, "Header decoding error\n");
            return 4;
        }

        // TODO: turn this custom VPS into a normal SPS+VPS pair
        fwrite(spsbuf, 1, spsbuf_len, stdout);
        free(spsbuf);

		fwrite("\x00\x00\x00\x01", 4, 1, stdout);

        // Just copy the rest as is
		for(;;) {
				int c = fgetc(stdin);
				if (c == EOF) break;
				fputc(c, stdout);
		}


		return 0;
}

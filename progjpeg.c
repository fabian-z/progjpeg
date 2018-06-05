#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include "jpeglib.h"
#include "_cgo_export.h"

#define DEBUG_LEVEL 0

struct transcode_state {
	int errorcb;
	j_common_ptr jpeg_decompress;
	j_common_ptr jpeg_compress;
};

static void error_exit (j_common_ptr cinfo)
{
	char buffer[JMSG_LENGTH_MAX] = {0};
	(*cinfo->err->format_message) (cinfo, buffer);

	struct transcode_state* tstate;
	tstate = (struct transcode_state *) (cinfo->client_data);
 
	if (tstate == NULL) {
		// our error handling failed, kill execution because of
		// indeterminate state
		printf("Failed error handler in libjpeg\n");
		(*cinfo->err->output_message) (cinfo);
  		jpeg_destroy(cinfo);
		exit(EXIT_FAILURE);
	}

	if (tstate->jpeg_decompress != NULL) {
		jpeg_destroy(tstate->jpeg_decompress);
		tstate->jpeg_decompress = NULL;
	}
	if (tstate->jpeg_compress != NULL) {
		jpeg_destroy(tstate->jpeg_compress);
		tstate->jpeg_compress = NULL;
	}

	go_jpeg_callback(tstate->errorcb, buffer);
}

static void emit_message (j_common_ptr cinfo, int msg_level)
{
  struct jpeg_error_mgr * err = cinfo->err;

  if (msg_level < 0) {
	//treat warnings as fatal
    (*cinfo->err->error_exit) (cinfo);
  } else {
    /* It's a trace message.  Show it if trace_level >= msg_level. */
    if (err->trace_level >= msg_level)
      (*err->output_message) (cinfo);
  }
}

void lossless_progressive_jpeg(void *jpg_buffer, uint64_t jpg_size, void **outbuffer, uint64_t *outlen, int errorcb, int successcb)
{
	
	struct jpeg_decompress_struct srcinfo;
	struct jpeg_compress_struct dstinfo;
	struct jpeg_error_mgr jsrcerr;
	struct jpeg_error_mgr jdsterr;

	struct transcode_state tstate;
	tstate.jpeg_compress = NULL;
	tstate.jpeg_decompress = NULL;
	srcinfo.client_data = &tstate;
	dstinfo.client_data = &tstate;
	
	jvirt_barray_ptr * coef_arrays;

	tstate.errorcb = errorcb;

	srcinfo.err = jpeg_std_error(&jsrcerr);    
	jsrcerr.trace_level = DEBUG_LEVEL;
	jsrcerr.error_exit = error_exit;
	jsrcerr.emit_message = emit_message;

	jpeg_create_decompress(&srcinfo);
	tstate.jpeg_decompress = (j_common_ptr) &srcinfo;

	jpeg_mem_src(&srcinfo, (unsigned char *) jpg_buffer, jpg_size);
	
    dstinfo.err = jpeg_std_error(&jdsterr);
	jdsterr.trace_level = DEBUG_LEVEL;
	jdsterr.error_exit = error_exit;
	jdsterr.emit_message = emit_message;
    
	jpeg_create_compress(&dstinfo);
	tstate.jpeg_compress = (j_common_ptr) &dstinfo;
    jpeg_mem_dest(&dstinfo, (unsigned char **) outbuffer, outlen);
    
	// Check if jpeg is valid.
	if (jpeg_read_header(&srcinfo, TRUE) != 1) {
		jpeg_destroy_compress(&dstinfo);
		jpeg_destroy_decompress(&srcinfo);
		go_jpeg_callback(errorcb, "Invalid JPEG header");
		return;
	}
	
	/* Read source file as DCT coefficients */
	coef_arrays = jpeg_read_coefficients(&srcinfo);
	
	/* Initialize destination compression parameters from source values */
	jpeg_copy_critical_parameters(&srcinfo, &dstinfo);
	
    jpeg_simple_progression(&dstinfo);
	
	/* Start compressor (note no image data is actually written here) */
	jpeg_write_coefficients(&dstinfo, coef_arrays);
	
	/* Finish compression and release memory */
	jpeg_finish_compress(&dstinfo);
	jpeg_destroy_compress(&dstinfo);
	
	(void) jpeg_finish_decompress(&srcinfo);
	jpeg_destroy_decompress(&srcinfo);

	go_jpeg_callback(successcb, "");
}

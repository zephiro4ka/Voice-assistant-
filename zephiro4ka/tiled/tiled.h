#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal compatibility stub for the missing tiled backend.
// The project does not ship the original tiled implementation, so the helpers
// are provided as no-op inline functions to satisfy the build.

static inline size_t ggml_tiled_wdata_size(size_t n_threads, const void * node) {
	(void) n_threads;
	(void) node;
	return 0;
}

static inline int ggml_compute_forward_mul_mat_id_tiled(
	void * params,
	void * dst,
	void * cur_a,
	int cne1,
	const int32_t * ids,
	void * scratch) {
	(void) params;
	(void) dst;
	(void) cur_a;
	(void) cne1;
	(void) ids;
	(void) scratch;
	return 0;
}

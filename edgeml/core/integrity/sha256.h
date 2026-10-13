/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal, allocation-free SHA-256 (FIPS 180-4) used for model, config and
 * binary integrity checks. Kept in C so it can run next to the EmbedIDS core
 * on constrained targets without pulling in a crypto library.
 */

#ifndef EDGEML_SHA256_H
#define EDGEML_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDGEML_SHA256_DIGEST_LEN 32
#define EDGEML_SHA256_HEX_LEN 65 /* 64 hex chars + NUL */

typedef struct {
  uint32_t state[8];
  uint64_t bit_length;
  uint8_t block[64];
  uint32_t block_length;
} edgeml_sha256_ctx_t;

void edgeml_sha256_init(edgeml_sha256_ctx_t *ctx);
void edgeml_sha256_update(edgeml_sha256_ctx_t *ctx, const void *data,
                          size_t length);
void edgeml_sha256_final(edgeml_sha256_ctx_t *ctx,
                         uint8_t digest[EDGEML_SHA256_DIGEST_LEN]);

/** Convert a digest to lowercase hex. */
void edgeml_sha256_to_hex(const uint8_t digest[EDGEML_SHA256_DIGEST_LEN],
                          char hex[EDGEML_SHA256_HEX_LEN]);

/** Hash a memory buffer into lowercase hex. */
void edgeml_sha256_hex(const void *data, size_t length,
                       char hex[EDGEML_SHA256_HEX_LEN]);

/**
 * Hash a file into lowercase hex using a fixed stack buffer.
 * @return 0 on success, -1 if the file cannot be read.
 */
int edgeml_sha256_file(const char *path, char hex[EDGEML_SHA256_HEX_LEN]);

#ifdef __cplusplus
}
#endif

#endif

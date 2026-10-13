/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sha256.h"

#include <stdio.h>
#include <string.h>

static const uint32_t k_round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_transform(edgeml_sha256_ctx_t *ctx, const uint8_t *data) {
  uint32_t w[64];
  for (uint32_t i = 0; i < 16; i++) {
    w[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
           ((uint32_t)data[i * 4 + 2] << 8) | ((uint32_t)data[i * 4 + 3]);
  }
  for (uint32_t i = 16; i < 64; i++) {
    uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2],
           d = ctx->state[3], e = ctx->state[4], f = ctx->state[5],
           g = ctx->state[6], h = ctx->state[7];

  for (uint32_t i = 0; i < 64; i++) {
    uint32_t s1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + k_round_constants[i] + w[i];
    uint32_t s0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

void edgeml_sha256_init(edgeml_sha256_ctx_t *ctx) {
  ctx->bit_length = 0;
  ctx->block_length = 0;
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
}

void edgeml_sha256_update(edgeml_sha256_ctx_t *ctx, const void *data,
                          size_t length) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t i = 0; i < length; i++) {
    ctx->block[ctx->block_length++] = bytes[i];
    if (ctx->block_length == 64) {
      sha256_transform(ctx, ctx->block);
      ctx->bit_length += 512;
      ctx->block_length = 0;
    }
  }
}

void edgeml_sha256_final(edgeml_sha256_ctx_t *ctx,
                         uint8_t digest[EDGEML_SHA256_DIGEST_LEN]) {
  uint32_t i = ctx->block_length;
  ctx->bit_length += (uint64_t)ctx->block_length * 8;

  ctx->block[i++] = 0x80;
  if (i > 56) {
    while (i < 64) {
      ctx->block[i++] = 0x00;
    }
    sha256_transform(ctx, ctx->block);
    i = 0;
  }
  while (i < 56) {
    ctx->block[i++] = 0x00;
  }
  for (int shift = 7; shift >= 0; shift--) {
    ctx->block[63 - shift] = (uint8_t)(ctx->bit_length >> (shift * 8));
  }
  sha256_transform(ctx, ctx->block);

  for (i = 0; i < 8; i++) {
    digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
    digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
    digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
    digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
  }
}

void edgeml_sha256_to_hex(const uint8_t digest[EDGEML_SHA256_DIGEST_LEN],
                          char hex[EDGEML_SHA256_HEX_LEN]) {
  static const char digits[] = "0123456789abcdef";
  for (int i = 0; i < EDGEML_SHA256_DIGEST_LEN; i++) {
    hex[i * 2] = digits[digest[i] >> 4];
    hex[i * 2 + 1] = digits[digest[i] & 0x0f];
  }
  hex[EDGEML_SHA256_HEX_LEN - 1] = '\0';
}

void edgeml_sha256_hex(const void *data, size_t length,
                       char hex[EDGEML_SHA256_HEX_LEN]) {
  edgeml_sha256_ctx_t ctx;
  uint8_t digest[EDGEML_SHA256_DIGEST_LEN];
  edgeml_sha256_init(&ctx);
  edgeml_sha256_update(&ctx, data, length);
  edgeml_sha256_final(&ctx, digest);
  edgeml_sha256_to_hex(digest, hex);
}

int edgeml_sha256_file(const char *path, char hex[EDGEML_SHA256_HEX_LEN]) {
  if (path == NULL || hex == NULL) {
    return -1;
  }
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return -1;
  }

  edgeml_sha256_ctx_t ctx;
  uint8_t buffer[4096];
  uint8_t digest[EDGEML_SHA256_DIGEST_LEN];
  size_t read_bytes;

  edgeml_sha256_init(&ctx);
  while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    edgeml_sha256_update(&ctx, buffer, read_bytes);
  }
  int failed = ferror(file);
  fclose(file);
  if (failed) {
    return -1;
  }

  edgeml_sha256_final(&ctx, digest);
  edgeml_sha256_to_hex(digest, hex);
  return 0;
}

#ifndef CBOR_H
#define CBOR_H

#include <stdint.h>
#include <stddef.h>

int cbor_encode_uint(uint64_t value, uint8_t *out, size_t out_size);
int cbor_encode_bstr(const uint8_t *data, size_t len, uint8_t *out, size_t out_size);
int cbor_encode_tstr(const char *str, uint8_t *out, size_t out_size);
int cbor_encode_array(uint64_t count, uint8_t *out, size_t out_size);
int cbor_encode_indef_array_start(uint8_t *out, size_t out_size);
int cbor_encode_break(uint8_t *out, size_t out_size);

int cbor_decode_uint(const uint8_t *buf, size_t len, uint64_t *value);
int cbor_decode_bstr(const uint8_t *buf, size_t len, const uint8_t **data, size_t *data_len);
int cbor_decode_tstr(const uint8_t *buf, size_t len, const char **str, size_t *str_len);
int cbor_decode_array(const uint8_t *buf, size_t len, uint64_t *count);
int cbor_is_indef_array(const uint8_t *buf, size_t len);
int cbor_is_break(const uint8_t *buf, size_t len);

#endif

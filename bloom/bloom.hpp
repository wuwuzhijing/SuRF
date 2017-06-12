// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef LEVELDB_BLOOM_H_
#define LEVELDB_BLOOM_H_

#include <stdint.h>
#include <string.h>

#include <vector>
#include <string>

using namespace std;

inline uint32_t DecodeFixed32(const char* ptr) {
    uint32_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
}

inline uint32_t Hash(const char* data, size_t n, uint32_t seed) {
    // Similar to murmur hash
    const uint32_t m = 0xc6a4a793;
    const uint32_t r = 24;
    const char* limit = data + n;
    uint32_t h = seed ^ (n * m);

    // Pick up four bytes at a time
    while (data + 4 <= limit) {
	uint32_t w = DecodeFixed32(data);
	data += 4;
	h += w;
	h *= m;
	h ^= (h >> 16);
    }

    // Pick up remaining bytes
    switch (limit - data) {
    case 3:
	h += static_cast<unsigned char>(data[2]) << 16;
    case 2:
	h += static_cast<unsigned char>(data[1]) << 8;
    case 1:
	h += static_cast<unsigned char>(data[0]);
	h *= m;
	h ^= (h >> r);
	break;
    }
    return h;
}

static uint32_t BloomHash(const string &key) {
    return Hash(key.c_str(), key.size(), 0xbc9f1d34);
}

static uint32_t BloomHash(const uint64_t key) {
    return Hash((const char*)(&key), sizeof(uint64_t), 0xbc9f1d34);
}

class BloomFilter {
 private:
    size_t bits_per_key_;
    size_t k_;

 public:
 BloomFilter(int bits_per_key)
     : bits_per_key_(bits_per_key) {
	// We intentionally round down to reduce probing cost a little bit
	k_ = static_cast<size_t>(bits_per_key * 0.69);  // 0.69 =~ ln(2)
	if (k_ < 1) k_ = 1;
	if (k_ > 30) k_ = 30;
    }

    void CreateFilter(vector<string> keys, int n, string* dst) const {
	// Compute bloom filter size (in both bits and bytes)
	size_t bits = n * bits_per_key_;

	// For small n, we can see a very high false positive rate.  Fix it
	// by enforcing a minimum bloom filter length.
	if (bits < 64) bits = 64;

	size_t bytes = (bits + 7) / 8;
	bits = bytes * 8;

	const size_t init_size = dst->size();
	dst->resize(init_size + bytes, 0);
	dst->push_back(static_cast<char>(k_));  // Remember # of probes in filter
	char* array = &(*dst)[init_size];
	for (int i = 0; i < n; i++) {
	    // Use double-hashing to generate a sequence of hash values.
	    // See analysis in [Kirsch,Mitzenmacher 2006].
	    uint32_t h = BloomHash(keys[i]);
	    const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
	    for (size_t j = 0; j < k_; j++) {
		const uint32_t bitpos = h % bits;
		array[bitpos/8] |= (1 << (bitpos % 8));
		h += delta;
	    }
	}
    }

    void CreateFilter(vector<uint64_t> keys, int n, string* dst) const {
	// Compute bloom filter size (in both bits and bytes)
	size_t bits = n * bits_per_key_;

	// For small n, we can see a very high false positive rate.  Fix it
	// by enforcing a minimum bloom filter length.
	if (bits < 64) bits = 64;

	size_t bytes = (bits + 7) / 8;
	bits = bytes * 8;

	const size_t init_size = dst->size();
	dst->resize(init_size + bytes, 0);
	dst->push_back(static_cast<char>(k_));  // Remember # of probes in filter
	char* array = &(*dst)[init_size];
	for (int i = 0; i < n; i++) {
	    // Use double-hashing to generate a sequence of hash values.
	    // See analysis in [Kirsch,Mitzenmacher 2006].
	    uint32_t h = BloomHash(keys[i]);
	    const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
	    for (size_t j = 0; j < k_; j++) {
		const uint32_t bitpos = h % bits;
		array[bitpos/8] |= (1 << (bitpos % 8));
		h += delta;
	    }
	}
    }

    bool KeyMayMatch(const string& key, const string& bloom_filter) const {
	const size_t len = bloom_filter.size();
	if (len < 2) return false;

	const char* array = bloom_filter.c_str();
	const size_t bits = (len - 1) * 8;

	// Use the encoded k so that we can read filters generated by
	// bloom filters created using different parameters.
	const size_t k = array[len-1];
	if (k > 30) {
	    // Reserved for potentially new encodings for short bloom filters.
	    // Consider it a match.
	    return true;
	}

	uint32_t h = BloomHash(key);
	const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
	for (size_t j = 0; j < k; j++) {
	    const uint32_t bitpos = h % bits;
	    if ((array[bitpos/8] & (1 << (bitpos % 8))) == 0) return false;
	    h += delta;
	}
	return true;
    }

    bool KeyMayMatch(const uint64_t key, const string& bloom_filter) const {
	const size_t len = bloom_filter.size();
	if (len < 2) return false;

	const char* array = bloom_filter.c_str();
	const size_t bits = (len - 1) * 8;

	// Use the encoded k so that we can read filters generated by
	// bloom filters created using different parameters.
	const size_t k = array[len-1];
	if (k > 30) {
	    // Reserved for potentially new encodings for short bloom filters.
	    // Consider it a match.
	    return true;
	}

	uint32_t h = BloomHash(key);
	const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
	for (size_t j = 0; j < k; j++) {
	    const uint32_t bitpos = h % bits;
	    if ((array[bitpos/8] & (1 << (bitpos % 8))) == 0) return false;
	    h += delta;
	}
	return true;
    }
};


#endif  // LEVELDB_BLOOM_H_

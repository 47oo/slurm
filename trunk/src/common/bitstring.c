/*
 * $Id$
 * $Source$
 *
 * See comments about origin, limitations, and internal structure in 
 * bitstring.h.
 *
 * J. Garlick April 2002
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bitstring.h"

/* 
 * Allocate a bitstring.
 *   nbits (IN)		valid bits in new bitstring, initialized to all clear
 *   RETURN		new bitstring
 */
bitstr_t *
bit_alloc(bitoff_t nbits)
{
	bitstr_t *new;

	new = (bitstr_t *)calloc(_bitstr_words(nbits), sizeof(bitstr_t));
	if (new) {
		_bitstr_magic(new) = BITSTR_MAGIC;
		_bitstr_bits(new) = nbits;
	}
	return new;
}

/* 
 * Reallocate a bitstring (expand or contract size).
 *   b (IN)		pointer to old bitstring 
 *   nbits (IN)		valid bits in new bitstr
 *   RETURN		new bitstring
 */
bitstr_t *
bit_realloc(bitstr_t *b, bitoff_t nbits)
{
	bitoff_t obits;
	bitstr_t *new = NULL;
       
	_assert_bitstr_valid(b);
	obits = _bitstr_bits(b);
	new = realloc(b, _bitstr_words(nbits) * sizeof(bitstr_t));
	if (new) {
		_assert_bitstr_valid(new);
		_bitstr_bits(new) = nbits;
		if (nbits > obits)
			bit_nclear(new, obits, nbits - 1);
	}
	return new;
}

/* 
 * Free a bitstr.
 *   b (IN/OUT)	bitstr to be freed
 */ 
void
bit_free(bitstr_t *b)
{
	assert(b);
	assert(_bitstr_magic(b) == BITSTR_MAGIC);
	_bitstr_magic(b) = 0;
	free(b);
}

/*
 * Return the number of possible bits in a bitstring.
 *   b (IN)		bitstring to check
 *   RETURN		number of bits allocated
 */
bitoff_t
bit_size(bitstr_t *b)
{
	_assert_bitstr_valid(b);
	return _bitstr_bits(b);
}

/* 
 * Is bit N of bitstring b set? 
 *   b (IN)		bitstring to test
 *   bit (IN)		bit position to test
 *   RETURN		1 if bit set, 0 if clear
 */
int
bit_test(bitstr_t *b, bitoff_t bit)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, bit);
	return ((b[_bit_word(bit)] & _bit_mask(bit)) ? 1 : 0);
}

/* 
 * Set bit N of bitstring.
 *   b (IN)		target bitstring
 *   bit (IN)		bit position to set
 */
void
bit_set(bitstr_t *b, bitoff_t bit)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, bit);
	b[_bit_word(bit)] |= _bit_mask(bit);
}

/* 
 * Clear bit N of bitstring
 *   b (IN)		target bitstring
 *   bit (IN)		bit position to clear
 */
void
bit_clear(bitstr_t *b, bitoff_t bit)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, bit);
	b[_bit_word(bit)] &= ~_bit_mask(bit);
}

/* 
 * Set bits start ... stop in bitstring
 *   b (IN)		target bitstring
 *   start (IN)		starting (low numbered) bit position
 *   stop (IN)		ending (higher numbered) bit position
 */
void
bit_nset(bitstr_t *b, bitoff_t start, bitoff_t stop)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, start);
	_assert_bit_valid(b, stop);

	while (start <= stop && start % 8 > 0) 		/* partial first byte? */
		bit_set(b, start++);
	while (stop >= start && (stop+1) % 8 > 0)	/* partial last byte? */
		bit_set(b, stop--);
	if (stop > start) {				/* now do whole bytes */
		assert((stop-start+1) % 8 == 0);
		memset(_bit_byteaddr(b, start), 0xff, (stop-start+1) / 8);
	}
}

/* 
 * Clear bits start ... stop in bitstring
 *   b (IN)		target bitstring
 *   start (IN)		starting (low numbered) bit position
 *   stop (IN)		ending (higher numbered) bit position
 */
void
bit_nclear(bitstr_t *b, bitoff_t start, bitoff_t stop)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, start);
	_assert_bit_valid(b, stop);

	while (start <= stop && start % 8 > 0) 	/* partial first byte? */
		bit_clear(b, start++);
	while (stop >= start && (stop+1) % 8 > 0)/* partial last byte? */
		bit_clear(b, stop--);
	if (stop > start) {			/* now do whole bytes */
		assert((stop-start+1) % 8 == 0);
		memset(_bit_byteaddr(b, start), 0, (stop-start+1) / 8);
	}
}

/* 
 * Find first bit clear in bitstring.
 *   b (IN)		bitstring to search
 *   nbits (IN)		number of bits to search
 *   RETURN      	resulting bit position (-1 if none found)
 */
bitoff_t
bit_ffc(bitstr_t *b)
{
	bitoff_t bit = 0, value = -1;

	_assert_bitstr_valid(b);

	while (bit < _bitstr_bits(b) && value == -1) {
		int word = _bit_word(bit);

		if (b[word] == BITSTR_MAXPOS) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}
		while (bit < _bitstr_bits(b) && _bit_word(bit) == word) {
			if (!bit_test(b, bit)) {
				value = bit;
				break;
			}
			bit++;
		}
	}
	return value;
}

/* 
 * Find first bit set in b.
 *   b (IN)		bitstring to search
 *   RETURN 		resulting bit position (-1 if none found)
 */
bitoff_t 
bit_ffs(bitstr_t *b)
{
	bitoff_t bit = 0, value = -1;

	_assert_bitstr_valid(b);

	while (bit < _bitstr_bits(b) && value == -1) {
		int word = _bit_word(bit);

		if (b[word] == 0) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}
		while (bit < _bitstr_bits(b) && _bit_word(bit) == word) {
			if (bit_test(b, bit)) {
				value = bit;
				break;
			}
			bit++;
		}
	}
	return value;
}

/* 
 * Find last bit set in b.
 *   b (IN)		bitstring to search
 *   RETURN 		resulting bit position (-1 if none found)
 */
bitoff_t 
bit_fls(bitstr_t *b)
{
	bitoff_t bit, value = -1;
	int word;

	_assert_bitstr_valid(b);

	bit = _bitstr_bits(b) - 1;	/* zero origin */
	while (bit >= 0 && 		/* test partitial words */
		(_bit_word(bit) == _bit_word(bit + 1))) {
		if (bit_test(b, bit)) {
			value = bit;
			break;
		}
		bit--;
	}

	while (bit >= 0 && value == -1) {	/* test whole words */
		word = _bit_word(bit);

		if (b[word] == 0) {
			bit -= sizeof(bitstr_t) * 8;
			continue;
		}
		while (bit >= 0) {
			if (bit_test(b, bit)) {
				value = bit;
				break;
			}
			bit--;
		}
	}
	return value;
}


/* 
 * set all bits between the first and last bits set (i.e. fill in the gaps 
 *	to make set bits contiguous)
 */
void
bit_fill_gaps(bitstr_t *b) {
	bitoff_t first, last;

	_assert_bitstr_valid(b);

	first = bit_ffs(b);
	if (first == -1)
		return;

	last = bit_fls(b);
	bit_nset(b, first, last);

	return;
}

/*
 * return 1 if all bits set in b1 are also set in b2, 0 0therwise
 */
int
bit_super_set(bitstr_t *b1, bitstr_t *b2)  {
	bitoff_t bit;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	assert(_bitstr_bits(b1) == _bitstr_bits(b2));

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t)*8) {
		if (b1[_bit_word(bit)] != (b1[_bit_word(bit)] & b2[_bit_word(bit)]))
			return 0;
	}

	return 1;
}

/*
 * b1 &= b2
 *   b1 (IN/OUT)	first string
 *   b2 (IN)		second bitstring
 */
void
bit_and(bitstr_t *b1, bitstr_t *b2) {
	bitoff_t bit;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	assert(_bitstr_bits(b1) == _bitstr_bits(b2));

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t)*8)
		b1[_bit_word(bit)] &= b2[_bit_word(bit)];
}

/* 
 * b1 |= b2
 *   b1 (IN/OUT)	first bitmap
 *   b2 (IN)		second bitmap
 */
void
bit_or(bitstr_t *b1, bitstr_t *b2) {
	bitoff_t bit;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	assert(_bitstr_bits(b1) == _bitstr_bits(b2));

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t)*8)
		b1[_bit_word(bit)] |= b2[_bit_word(bit)];
}

/* 
 * return a copy of the supplied bitmap
 */
bitstr_t *
bit_copy(bitstr_t *b)
{
	bitstr_t *new;
	int newsize_words, newsize_bits;

	_assert_bitstr_valid(b);

	newsize_bits  = bit_size(b);
	newsize_words = (newsize_bits + 7) / 8;
	new = bit_alloc(newsize_bits);
	if (new)
		memcpy(new, b, newsize_words);

	return new;
}

#if !defined(USE_64BIT_BITSTR)
/*
 * Returns the hamming weight (i.e. the number of bits set) in a word.
 * NOTE: This routine borrowed from Linux 2.4.9 <linux/bitops.h>.
 */
static uint32_t
hweight(uint32_t w)
{
	uint32_t res;
       
	res = (w   & 0x55555555) + ((w >> 1)    & 0x55555555);
	res = (res & 0x33333333) + ((res >> 2)  & 0x33333333);
	res = (res & 0x0F0F0F0F) + ((res >> 4)  & 0x0F0F0F0F);
	res = (res & 0x00FF00FF) + ((res >> 8)  & 0x00FF00FF);
	res = (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);

	return res;
}
#else
/*
 * A 64 bit version crafted from 32-bit one borrowed above.
 */
static uint64_t
hweight(uint64_t w)
{
	uint64_t res;
       
	res = (w   & 0x5555555555555555) + ((w >> 1)    & 0x5555555555555555);
	res = (res & 0x3333333333333333) + ((res >> 2)  & 0x3333333333333333);
	res = (res & 0x0F0F0F0F0F0F0F0F) + ((res >> 4)  & 0x0F0F0F0F0F0F0F0F);
	res = (res & 0x00FF00FF00FF00FF) + ((res >> 8)  & 0x00FF00FF00FF00FF);
	res = (res & 0x0000FFFF0000FFFF) + ((res >> 16) & 0x0000FFFF0000FFFF);
	res = (res & 0x00000000FFFFFFFF) + ((res >> 32) & 0x00000000FFFFFFFF);

	return res;
}
#endif /* !USE_64BIT_BITSTR */

/*
 * Count the number of bits set in bitstring.
 *   b (IN)		bitstring to check
 *   RETURN		count of set bits 
 */
int
bit_set_count(bitstr_t *b)
{
	int count = 0;
	bitoff_t bit;

	_assert_bitstr_valid(b);

	for (bit = 0; bit < _bitstr_bits(b); bit += sizeof(bitstr_t)*8)
		count += hweight(b[_bit_word(bit)]);

	return count;
}

/*
 * Count the number of bits clear in bitstring.
 *   b (IN)		bitstring to check
 *   RETURN		count of clear bits 
 */
int
bit_clear_count(bitstr_t *b)
{
	_assert_bitstr_valid(b);
	return (_bitstr_bits(b) - bit_set_count(b));
}

/* 
 * XXX the relationship between stdint types and "unsigned [long] long"
 * types is architecture/compiler dependent, so this may have to be tweaked.
 */
#ifdef	USE_64BIT_BITSTR
#define BITSTR_RANGE_FMT	"%llu-%llu,"
#define BITSTR_SINGLE_FMT	"%llu,"
#else
#define BITSTR_RANGE_FMT	"%u-%u,"
#define BITSTR_SINGLE_FMT	"%u,"
#endif

/*
 * Convert to range string format, e.g. 0-5,42
 */
char *
bit_fmt(char *str, int len, bitstr_t *b)
{
	int count = 0, ret, word;
	bitoff_t start, bit;

	_assert_bitstr_valid(b);
	assert(len > 0);
	*str = '\0';
	for (bit = 0; bit < _bitstr_bits(b); ) {
		word = _bit_word(bit);
		if (b[word] == 0) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}

		if (bit_test(b, bit)) {
			count++;
			start = bit;
			while (bit+1 < _bitstr_bits(b) && bit_test(b, bit+1))
				bit++;
			if (bit == start)	/* add single bit position */
				ret = snprintf(str+strlen(str), len-strlen(str),
						BITSTR_SINGLE_FMT, start);
			else 			/* add bit position range */
				ret = snprintf(str+strlen(str), len-strlen(str),
						BITSTR_RANGE_FMT, start, bit);
			assert(ret != -1);
		}
		bit++;
	}
	if (count > 0)
		str[strlen(str) - 1] = '\0'; 	/* zap trailing comma */
	if (count > 1) { 			/* add braces */
		assert(strlen(str) + 3 < len);
		memmove(str + 1, str, strlen(str));
		str[0] = '[';
		strcat(str, "]");
	} 
	return str;
}


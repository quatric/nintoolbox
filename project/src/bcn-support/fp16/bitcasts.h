#pragma once
#ifndef FP16_BITCASTS_H
#define FP16_BITCASTS_H

#include <cstdint>

static inline float fp32_from_bits (uint32_t w)
{
	union
	{
		uint32_t as_bits;
		float as_value;
	} value = { w };
	return value.as_value;
}

static inline uint32_t fp32_to_bits (float f)
{
	union
	{
		float as_value;
		uint32_t as_bits;
	} value = { f };
	return value.as_bits;
}

static inline double fp64_from_bits (uint64_t w)
{
	union
	{
		uint64_t as_bits;
		double as_value;
	} value = { w };
	return value.as_value;
}

static inline uint64_t fp64_to_bits (double f)
{
	union
	{
		double as_value;
		uint64_t as_bits;
	} value = { f };
	return value.as_bits;
}

#endif

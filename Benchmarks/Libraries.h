#pragma once

// There is one small function per library and operation, and each one calls nothing but the library's published API.
// Reads return NaN when the library reports an error, so that the checks count it. Writes return the length of the text
// they wrote.

#include "NumberTextThree.h"

#include "dragonbox/dragonbox_to_chars.h"
#include "fast_float/fast_float.h"
#include "ftoa.h"
#include "ryu/ryu.h"
#include "zmij.h"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

// The room that every writer may use at its output, whatever it needs. This is also the size we give snprintf.
constexpr int kWriteRoom = 64;

template <class Number>
constexpr Number kFailed = std::numeric_limits<Number>::quiet_NaN();

// Reading doubles

inline double ReadNtt( const char* begin, const char* end )
{
    double value;

    return ntt_parse_json_f64( begin, end, &value, nullptr ) == NUMBERTEXTTHREE_OK ? value : kFailed<double>;
}

inline double ReadFastFloat( const char* begin, const char* end )
{
    double value;

    return fast_float::from_chars( begin, end, value ).ec == std::errc() ? value : kFailed<double>;
}

inline double ReadFromChars( const char* begin, const char* end )
{
    double value;

    return std::from_chars( begin, end, value ).ec == std::errc() ? value : kFailed<double>;
}

// strtod takes no end pointer, but every text is followed by '\n', which stops it.
inline double ReadStrtod( const char* begin, const char* end )
{
    char*  stop;
    double value = strtod( begin, &stop );

    return stop == end ? value : kFailed<double>;
}

// Reading floats

inline bool IsDigit( char c )
{
    return c >= '0' && c <= '9';
}

inline const char* SkipDigits( const char* at, const char* end )
{
    while ( at < end && IsDigit( *at ) )
    {
        at++;
    }

    return at;
}

inline ntt_i64 ReadExponent( const char* at, const char* end )
{
    bool    negative = *at == '-';
    ntt_i64 exponent = 0;

    at += ( *at == '-' || *at == '+' ) ? 1 : 0;

    for ( ; at < end; at++ )
    {
        exponent = exponent < 1000000000 ? exponent * 10 + ( *at - '0' ) : exponent;
    }

    return negative ? -exponent : exponent;
}

// NumberTextThree reads floats from an ntt_decimal, which holds the digits and exponent of a number that another
// grammar has parsed. This function splits a JSON number into an ntt_decimal. The digits must be contiguous, so a copy
// is needed only when the number has both a whole part other than 0 and a fraction. The time this split takes is
// counted in the ntt column.
inline float ReadNttF32( const char* begin, const char* end )
{
    char        joined[ 768 ];
    ntt_decimal decimal;
    const char* whole         = begin + ( *begin == '-' ? 1 : 0 );
    const char* wholeEnd      = SkipDigits( whole, end );
    const char* fraction      = wholeEnd < end && *wholeEnd == '.' ? wholeEnd + 1 : wholeEnd;
    const char* fractionEnd   = SkipDigits( fraction, end );
    size_t      wholeCount    = size_t( wholeEnd - whole );
    size_t      fractionCount = size_t( fractionEnd - fraction );
    float       value;

    decimal.negative = *begin == '-';
    decimal.exponent = ( fractionEnd < end ? ReadExponent( fractionEnd + 1, end ) : 0 ) - ntt_i64( fractionCount );

    if ( fractionCount == 0 || ( wholeCount == 1 && *whole == '0' ) )
    {
        decimal.digits = fractionCount == 0 ? whole : fraction;
        decimal.count  = ntt_i32( fractionCount == 0 ? wholeCount : fractionCount );
    }
    else if ( wholeCount + fractionCount <= sizeof( joined ) )
    {
        memcpy( joined, whole, wholeCount );
        memcpy( joined + wholeCount, fraction, fractionCount );
        decimal.digits = joined;
        decimal.count  = ntt_i32( wholeCount + fractionCount );
    }
    else
    {
        return kFailed<float>;
    }

    return ntt_decimal_to_f32( &decimal, &value ) == NUMBERTEXTTHREE_OK ? value : kFailed<float>;
}

inline float ReadFastFloatF32( const char* begin, const char* end )
{
    float value;

    return fast_float::from_chars( begin, end, value ).ec == std::errc() ? value : kFailed<float>;
}

inline float ReadFromCharsF32( const char* begin, const char* end )
{
    float value;

    return std::from_chars( begin, end, value ).ec == std::errc() ? value : kFailed<float>;
}

inline float ReadStrtof( const char* begin, const char* end )
{
    char* stop;
    float value = strtof( begin, &stop );

    return stop == end ? value : kFailed<float>;
}

// Writing doubles

inline int WriteNtt( double value, char* output )
{
    return ntt_write_f64( value, output );
}

inline int WriteZmij( double value, char* output )
{
    return int( zmij::write( output, zmij::double_buffer_size, value ) - output );
}

inline int WriteXjb( double value, char* output )
{
    return int( xjb_ftoa( value, output ) - output );
}

inline int WriteRyu( double value, char* output )
{
    return d2s_buffered_n( value, output );
}

inline int WriteDragonbox( double value, char* output )
{
    return int( jkj::dragonbox::to_chars_n( value, output ) - output );
}

inline int WriteToChars( double value, char* output )
{
    return int( std::to_chars( output, output + kWriteRoom, value ).ptr - output );
}

inline int WritePrintf( double value, char* output )
{
    return snprintf( output, kWriteRoom, "%.17g", value );
}

// Writing floats

inline int WriteNttF32( float value, char* output )
{
    return ntt_write_f32( value, output );
}

inline int WriteZmijF32( float value, char* output )
{
    return int( zmij::write( output, zmij::float_buffer_size, value ) - output );
}

inline int WriteXjbF32( float value, char* output )
{
    return int( xjb_ftoa( value, output ) - output );
}

inline int WriteRyuF32( float value, char* output )
{
    return f2s_buffered_n( value, output );
}

inline int WriteDragonboxF32( float value, char* output )
{
    return int( jkj::dragonbox::to_chars_n( value, output ) - output );
}

inline int WriteToCharsF32( float value, char* output )
{
    return int( std::to_chars( output, output + kWriteRoom, value ).ptr - output );
}

inline int WritePrintfF32( float value, char* output )
{
    return snprintf( output, kWriteRoom, "%.9g", double( value ) );
}

// Writing integers

struct NttIntegers
{
    static int Write( uint32_t value, char* output )
    {
        return ntt_write_u32( value, output );
    }

    static int Write( uint64_t value, char* output )
    {
        return ntt_write_u64( value, output );
    }

    static int Write( int32_t value, char* output )
    {
        return ntt_write_i32( value, output );
    }

    static int Write( int64_t value, char* output )
    {
        return ntt_write_i64( value, output );
    }
};

struct ToCharsIntegers
{
    template <class Integer>
    static int Write( Integer value, char* output )
    {
        return int( std::to_chars( output, output + kWriteRoom, value ).ptr - output );
    }
};

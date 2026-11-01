#include "FailOnCrtError.h"
#include "NumberTextThree.h"

#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

static std::atomic<long> failures { 0 };
static std::mutex        printLock;

static void Fail( const char* what, const std::string& detail )
{
    if ( failures++ < 30 )
    {
        std::lock_guard<std::mutex> lock( printLock );
        printf( "FAILED %s: %s\n", what, detail.c_str() );
    }
}

static uint32_t Bits( float value )
{
    uint32_t bits;
    memcpy( &bits, &value, 4 );
    return bits;
}

static float FromBits( uint32_t bits )
{
    float value;
    memcpy( &value, &bits, 4 );
    return value;
}

static std::string Describe( float value )
{
    char text[ 64 ];
    snprintf( text, sizeof( text ), "%.9g (%08x)", (double)value, Bits( value ) );
    return text;
}

// Holds the value 0.digits x 10^exponent, where digits has no leading or trailing zeros. Zero is stored with empty
// digits.
struct Decimal
{
    std::string digits;
    long        exponent = 0;
    bool        negative = false;

    bool operator==( const Decimal& other ) const
    {
        return digits == other.digits && exponent == other.exponent && negative == other.negative;
    }
};

static Decimal Normalize( const std::string& text )
{
    Decimal     decimal;
    std::string raw;
    size_t      at        = 0;
    long        fraction  = 0;
    bool        seenPoint = false;

    if ( at < text.size() && text[ at ] == '-' )
    {
        decimal.negative = true;
        at++;
    }

    for ( ; at < text.size() && text[ at ] != 'e' && text[ at ] != 'E'; at++ )
    {
        if ( text[ at ] == '.' )
        {
            seenPoint = true;
            continue;
        }

        raw += text[ at ];
        fraction += seenPoint ? 1 : 0;
    }

    long   explicitExponent = ( at < text.size() ) ? atol( text.c_str() + at + 1 ) : 0;
    size_t first            = raw.find_first_not_of( '0' );

    if ( first == std::string::npos )
    {
        return decimal;
    }

    size_t last      = raw.find_last_not_of( '0' );
    decimal.digits   = raw.substr( first, last - first + 1 );
    decimal.exponent = explicitExponent - fraction + (long)( raw.size() - first );
    return decimal;
}

static ntt_status ReadDecimal( const Decimal& decimal, float* value )
{
    ntt_decimal input;

    input.digits   = decimal.digits.c_str();
    input.count    = (ntt_i32)decimal.digits.size();
    input.exponent = decimal.exponent - (ntt_i64)decimal.digits.size();
    input.negative = decimal.negative;

    return ntt_decimal_to_f32( &input, value );
}

static float ReferenceRead( const std::string& text )
{
    float                  value  = 0;
    std::from_chars_result result = std::from_chars( text.data(), text.data() + text.size(), value );

    if ( result.ec == std::errc::result_out_of_range )
    {
        return strtof( text.c_str(), nullptr );
    }

    return value;
}

// Builds the text the writer is expected to produce. It takes std::to_chars' shortest digits and lays them out as plain
// decimal for values from 1e-6 up to 1e21, and in exponent form outside that range.
static std::string Layout( const char* scientific, const char* end )
{
    std::string text( scientific, end );
    std::string sign     = ( text[ 0 ] == '-' ) ? "-" : "";
    size_t      at       = text.find( 'e' );
    long        exponent = atol( text.c_str() + at + 1 );
    std::string digits;

    for ( size_t a = sign.size(); a < at; a++ )
    {
        if ( text[ a ] != '.' )
        {
            digits += text[ a ];
        }
    }

    if ( digits == "0" )
    {
        return sign + "0";
    }

    if ( exponent < -6 || exponent >= 21 )
    {
        std::string point = ( digits.size() > 1 ) ? "." + digits.substr( 1 ) : "";
        return sign + digits.substr( 0, 1 ) + point + ( exponent < 0 ? "e-" : "e+" ) + std::to_string( exponent < 0 ? -exponent : exponent );
    }

    long point = exponent + 1;

    if ( point <= 0 )
    {
        return sign + "0." + std::string( (size_t)-point, '0' ) + digits;
    }

    if ( point >= (long)digits.size() )
    {
        return sign + digits + std::string( (size_t)point - digits.size(), '0' );
    }

    return sign + digits.substr( 0, (size_t)point ) + "." + digits.substr( (size_t)point );
}

static void CheckWrite( float value )
{
    if ( !std::isfinite( value ) )
    {
        char    text[ NUMBERTEXTTHREE_F32_MAX_CHARS ];
        ntt_i32 length = ntt_write_f32( value, text );

        if ( length != 0 || text[ 0 ] != 0 )
        {
            Fail( "non-finite write", Describe( value ) );
        }

        return;
    }

    char text[ 64 ];
    char shortest[ 64 ];

    memset( text, 0x5A, sizeof( text ) );

    ntt_i32              length = ntt_write_f32( value, text );
    std::string          written( text );
    std::to_chars_result result = std::to_chars( shortest, shortest + sizeof( shortest ), value, std::chars_format::scientific );

    if ( length != (ntt_i32)written.size() || length >= NUMBERTEXTTHREE_F32_MAX_CHARS )
    {
        Fail( "ntt_write_f32 length", Describe( value ) + " gave " + written );
        return;
    }

    for ( size_t a = NUMBERTEXTTHREE_F32_MAX_CHARS; a < sizeof( text ); a++ )
    {
        if ( text[ a ] != 0x5A )
        {
            Fail( "ntt_write_f32 wrote past NUMBERTEXTTHREE_F32_MAX_CHARS", Describe( value ) );
            break;
        }
    }

    if ( Bits( ReferenceRead( written ) ) != Bits( value ) )
    {
        Fail( "written text does not read back", Describe( value ) + " gave " + written );
    }

    if ( written != Layout( shortest, result.ptr ) )
    {
        Fail( "text differs from the shortest digits in the documented layout", Describe( value ) + " gave " + written );
    }
}

static void CheckRead( const std::string& text )
{
    float      value  = 0;
    ntt_status status = ReadDecimal( Normalize( text ), &value );
    float      system = ReferenceRead( text );

    if ( Bits( value ) != Bits( system ) )
    {
        Fail( "read differs from from_chars", text + " gave " + Describe( value ) + " from_chars " + Describe( system ) );
    }

    if ( ( status == NUMBERTEXTTHREE_OUT_OF_RANGE ) != std::isinf( value ) )
    {
        Fail( "out-of-range status", text );
    }
}

static std::string Exact( double value )
{
    char                 text[ 256 ];
    std::to_chars_result result = std::to_chars( text, text + sizeof( text ), value, std::chars_format::scientific, 150 );
    return std::string( text, result.ptr );
}

static std::string Shortest( double value )
{
    char                 text[ 64 ];
    std::to_chars_result result = std::to_chars( text, text + sizeof( text ), value );
    return std::string( text, result.ptr );
}

// Reads the exact midpoint between a float and the next float up, and also the doubles on either side of that
// midpoint.
static void CheckHalfway( float value )
{
    float next = std::nextafter( value, INFINITY );

    if ( !std::isfinite( next ) )
    {
        return;
    }

    double middle = ( (double)value + (double)next ) / 2;

    CheckRead( Exact( middle ) );
    CheckRead( Shortest( std::nextafter( middle, INFINITY ) ) );
    CheckRead( Shortest( std::nextafter( middle, -INFINITY ) ) );
}

template <class Integer>
static void CheckInteger( Integer value, ntt_i32 ( *write )( Integer, char* ) )
{
    char                 expected[ 32 ];
    char                 text[ NUMBERTEXTTHREE_INTEGER_MAX_CHARS ];
    std::to_chars_result result = std::to_chars( expected, expected + sizeof( expected ), value );
    std::string          reference( expected, result.ptr );
    ntt_i32              length = write( value, text );

    if ( length != (ntt_i32)reference.size() || reference != text )
    {
        Fail( "integer write", reference + " gave " + text );
    }
}

static void CheckIntegers( std::mt19937_64& random )
{
    const uint64_t unsignedEdges[] = {
        0, 1, 9, 10, 99, 100, 999, 1000, 4294967295ULL, 4294967296ULL, 9999999999999999999ULL, 10000000000000000000ULL, UINT64_MAX };
    const int64_t signedEdges[] = { 0, -1, 1, -9, -10, INT32_MIN, INT32_MAX, INT64_MIN, INT64_MAX, -999999999999999999LL };

    for ( uint64_t value : unsignedEdges )
    {
        CheckInteger<ntt_u64>( value, ntt_write_u64 );
        CheckInteger<ntt_u32>( (ntt_u32)value, ntt_write_u32 );
    }

    for ( int64_t value : signedEdges )
    {
        CheckInteger<ntt_i64>( value, ntt_write_i64 );
        CheckInteger<ntt_i32>( (ntt_i32)value, ntt_write_i32 );
    }

    std::vector<uint64_t> boundaries;
    uint64_t              power = 1;

    for ( int exponent = 0; exponent <= 19; exponent++, power *= 10 )
    {
        boundaries.insert( boundaries.end(), { power - 1, power, power + 1 } );
    }

    for ( int bit = 0; bit < 64; bit++ )
    {
        boundaries.insert( boundaries.end(), { ( 1ULL << bit ) - 1, 1ULL << bit } );
    }

    for ( uint64_t value : boundaries )
    {
        CheckInteger<ntt_u64>( value, ntt_write_u64 );
        CheckInteger<ntt_i64>( (ntt_i64)value, ntt_write_i64 );
        CheckInteger<ntt_i64>( (ntt_i64)( 0 - value ), ntt_write_i64 );
        CheckInteger<ntt_u32>( (ntt_u32)value, ntt_write_u32 );
        CheckInteger<ntt_i32>( (ntt_i32)(ntt_u32)value, ntt_write_i32 );
        CheckInteger<ntt_i32>( (ntt_i32)( 0u - (ntt_u32)value ), ntt_write_i32 );
    }

    for ( int round = 0; round < 200000; round++ )
    {
        uint64_t value = random() >> ( random() % 64 );

        CheckInteger<ntt_u64>( value, ntt_write_u64 );
        CheckInteger<ntt_i64>( (ntt_i64)value * ( ( round & 1 ) ? -1 : 1 ), ntt_write_i64 );
        CheckInteger<ntt_u32>( (ntt_u32)value, ntt_write_u32 );
        CheckInteger<ntt_i32>( (ntt_i32)value, ntt_write_i32 );
    }
}

static void CheckEdges()
{
    const uint32_t bits[] = { 0x00000000,
                              0x80000000,
                              0x00000001,
                              0x007FFFFF,
                              0x00800000,
                              0x7F7FFFFF,
                              0x3F800000,
                              0x3DCCCCCD,
                              0x60AD78EC,
                              0xE0AD78EC,
                              0x358637BD,
                              0x501502F9,
                              0x50800000,
                              0x4B800000,
                              0x34000000,
                              0x7F800000,
                              0xFF800000,
                              0x7FC00000 };

    for ( uint32_t pattern : bits )
    {
        CheckWrite( FromBits( pattern ) );
    }

    for ( int exponent = -149; exponent <= 127; exponent++ )
    {
        CheckWrite( std::ldexp( 1.0F, exponent ) );
        CheckHalfway( std::ldexp( 1.0F, exponent ) );
    }

    for ( const char* text : { "16777215", "16777216", "16777217", "-16777217", "33554433", "0.01234567", "12345678", "123456789012" } )
    {
        CheckRead( text );
    }

    const char* reads[] = { "0",
                            "-0",
                            "1",
                            "0.1",
                            "3.4028235e38",
                            "3.4028236e38",
                            "3.40282357e38",
                            "1e39",
                            "1e-46",
                            "7e-46",
                            "7.1e-46",
                            "1.401298464e-45",
                            "1.17549435e-38",
                            "16777217",
                            "123456789012345678901234567890" };

    for ( const char* text : reads )
    {
        CheckRead( text );
    }
}

static void CheckRandom( long count, uint64_t seed )
{
    std::mt19937_64 random( seed );

    for ( long round = 0; round < count; round++ )
    {
        float value = FromBits( (uint32_t)random() );

        CheckWrite( value );

        if ( std::isfinite( value ) )
        {
            CheckHalfway( std::fabs( value ) );
        }

        std::string text = std::to_string( random() % 1000000000ULL ) + "e" + std::to_string( (long)( random() % 110 ) - 65 );
        CheckRead( text );
    }
}

static void CheckEveryFloat()
{
    unsigned                 workerCount = std::thread::hardware_concurrency();
    std::vector<std::thread> workers;

    workerCount = workerCount == 0 ? 4 : workerCount;

    for ( unsigned worker = 0; worker < workerCount; worker++ )
    {
        workers.emplace_back(
            [ worker, workerCount ]()
            {
                for ( uint64_t pattern = worker; pattern <= UINT32_MAX; pattern += workerCount )
                {
                    CheckWrite( FromBits( (uint32_t)pattern ) );
                }
            } );
    }

    for ( std::thread& thread : workers )
    {
        thread.join();
    }
}

int main( int argumentCount, char** arguments )
{
    std::mt19937_64 random( 20261025 );

    CheckEdges();
    CheckIntegers( random );

    if ( argumentCount > 1 && strcmp( arguments[ 1 ], "every" ) == 0 )
    {
        CheckEveryFloat();
        printf( "checked every one of the 2^32 float bit patterns\n" );
    }
    else
    {
        long count = ( argumentCount > 1 ) ? atol( arguments[ 1 ] ) : 300000;
        CheckRandom( count, 7 );
        printf( "checked %ld random floats, halfway reads and decimal reads\n", count );
    }

    printf( "%ld failures\n", failures.load() );
    return failures == 0 ? 0 : 1;
}

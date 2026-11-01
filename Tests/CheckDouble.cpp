#include "FailOnCrtError.h"
#include "NumberTextThree.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

int SecondUnitWrite( double value, char* output );

static long failures = 0;

static void Fail( const char* what, const std::string& detail )
{
    if ( failures++ < 30 )
    {
        printf( "FAILED %s: %s\n", what, detail.c_str() );
    }
}

static uint64_t Bits( double value )
{
    uint64_t bits;
    memcpy( &bits, &value, 8 );
    return bits;
}

static double FromBits( uint64_t bits )
{
    double value;
    memcpy( &value, &bits, 8 );
    return value;
}

static std::string Describe( double value )
{
    char text[ 64 ];
    snprintf( text, sizeof( text ), "%.17g (%016llx)", value, (unsigned long long)Bits( value ) );
    return text;
}

static long writeChecked = 0, readChecked = 0, readFast = 0, strtodDisagreements = 0;

static double Reference( const std::string& text )
{
    double                 value  = 0;
    std::from_chars_result result = std::from_chars( text.data(), text.data() + text.size(), value );

    if ( result.ec == std::errc::result_out_of_range )
    {
        return strtod( text.c_str(), nullptr );
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

static void CheckWrite( double value )
{
    if ( !std::isfinite( value ) )
    {
        return;
    }

    char text[ 64 ];
    char shortest[ 64 ];

    writeChecked++;
    memset( text, 0x5A, sizeof( text ) );

    ntt_i32              length = ntt_write_f64( value, text );
    std::string          written( text );
    std::to_chars_result result = std::to_chars( shortest, shortest + sizeof( shortest ), value, std::chars_format::scientific );

    if ( length != (ntt_i32)written.size() || length >= NUMBERTEXTTHREE_F64_MAX_CHARS )
    {
        Fail( "ntt_write_f64 length", Describe( value ) + " gave " + written );
        return;
    }

    for ( size_t a = NUMBERTEXTTHREE_F64_MAX_CHARS; a < sizeof( text ); a++ )
    {
        if ( text[ a ] != 0x5A )
        {
            Fail( "ntt_write_f64 wrote past NUMBERTEXTTHREE_F64_MAX_CHARS", Describe( value ) );
            break;
        }
    }

    if ( Bits( Reference( written ) ) != Bits( value ) )
    {
        Fail( "written text does not read back", Describe( value ) + " gave " + written );
    }

    if ( written != Layout( shortest, result.ptr ) )
    {
        Fail( "text differs from the shortest digits in the documented layout", Describe( value ) + " gave " + written );
    }

    if ( SecondUnitWrite( value, text ) != length || written != text )
    {
        Fail( "the NUMBERTEXTTHREE_NO_SIMD translation unit wrote differently", Describe( value ) + " gave " + text );
    }
}

static void CheckRead( const std::string& text )
{
    readChecked++;

    // Copy the text into a buffer of the same length and without a terminator, so that a sanitizer catches any read
    // past the end.
    std::vector<char> bounded( text.begin(), text.end() );
    double            value  = 0;
    const char*       stop   = nullptr;
    ntt_status        status = ntt_parse_json_f64( bounded.data(), bounded.data() + bounded.size(), &value, &stop );
    double            system = Reference( text );

    if ( Bits( strtod( text.c_str(), nullptr ) ) != Bits( system ) )
    {
        strtodDisagreements++;
    }

    if ( status == NUMBERTEXTTHREE_INVALID )
    {
        Fail( "valid number rejected", text );
        return;
    }

    if ( stop != bounded.data() + bounded.size() )
    {
        Fail( "number not fully consumed", text );
    }

    if ( Bits( value ) != Bits( system ) )
    {
        Fail( "read differs from from_chars", text + " gave " + Describe( value ) + " from_chars " + Describe( system ) );
    }

    if ( ( status == NUMBERTEXTTHREE_OUT_OF_RANGE ) != std::isinf( value ) )
    {
        Fail( "out-of-range status", text );
    }

    std::string digits;
    ntt_i64     exponent  = 0;
    bool        fraction  = false;
    size_t      at        = ( text[ 0 ] == '-' ) ? 1 : 0;
    ntt_i64     explicitE = 0;
    bool        negativeE = false;
    for ( ; at < text.size() && text[ at ] != 'e' && text[ at ] != 'E'; at++ )
    {
        if ( text[ at ] == '.' )
        {
            fraction = true;
            continue;
        }
        digits += text[ at ];
        exponent -= fraction ? 1 : 0;
    }
    if ( at < text.size() )
    {
        at++;
        if ( text[ at ] == '-' || text[ at ] == '+' )
        {
            negativeE = ( text[ at ] == '-' );
            at++;
        }
        for ( ; at < text.size() && explicitE < 1000000000000LL; at++ )
        {
            explicitE = explicitE * 10 + ( text[ at ] - '0' );
        }
    }
    exponent += negativeE ? -explicitE : explicitE;

    std::vector<char> exactDigits( digits.begin(), digits.end() );
    ntt_decimal       decimal;
    decimal.digits   = exactDigits.data();
    decimal.count    = (ntt_i32)exactDigits.size();
    decimal.exponent = exponent;
    decimal.negative = ( text[ 0 ] == '-' );

    double exact       = ntt_detail_read_f64_exact( &decimal );
    double fromDecimal = 0;
    ntt_decimal_to_f64( &decimal, &fromDecimal );

    if ( Bits( exact ) != Bits( system ) || Bits( fromDecimal ) != Bits( system ) )
    {
        Fail( "decimal conversion differs from from_chars", text );
    }

    ntt_u64 significand = 0;
    ntt_i32 count       = 0;
    for ( char c : digits )
    {
        if ( count == 0 && c == '0' )
        {
            continue;
        }
        significand = significand * 10 + ntt_u64( c - '0' );
        count++;
    }

    double fast = 0;
    if ( count <= 19 && ntt_detail_read_f64_fast( decimal.negative, significand, exponent, &fast ) )
    {
        readFast++;
    }
}

static std::string Shortest( double value )
{
    char                 text[ 64 ];
    std::to_chars_result result = std::to_chars( text, text + sizeof( text ), value );
    return std::string( text, result.ptr );
}

static std::string Precision( double value, int precision )
{
    char text[ 64 ];
    snprintf( text, sizeof( text ), "%.*g", precision, value );
    return text;
}

static ntt_u32 BigDivideSmall( ntt_detail_big* big, ntt_u32 divisor )
{
    ntt_u64 remainder = 0;

    for ( ntt_i32 a = big->count - 1; a >= 0; a-- )
    {
        ntt_u64 current = ( remainder << 32 ) | big->word[ a ];

        big->word[ a ] = (ntt_u32)( current / divisor );
        remainder      = current % divisor;
    }

    ntt_detail_big_trim( big );

    return (ntt_u32)remainder;
}

static ntt_i32 BigToDecimal( ntt_detail_big* big, char* digits )
{
    char    reversed[ 1200 ];
    ntt_i32 count = 0;

    while ( big->count != 0 )
    {
        ntt_u32 chunk = BigDivideSmall( big, 1000000000U );

        for ( int a = 0; a < 9; a++ )
        {
            reversed[ count++ ] = (char)( '0' + chunk % 10 );
            chunk /= 10;
        }
    }

    while ( count > 1 && reversed[ count - 1 ] == '0' )
    {
        count--;
    }

    for ( ntt_i32 a = 0; a < count; a++ )
    {
        digits[ a ] = reversed[ count - 1 - a ];
    }

    return count;
}

static void CheckHalfway( double value )
{
    uint64_t bits           = Bits( value );
    int      biasedExponent = int( ( bits >> 52 ) & 0x7FF );
    uint64_t mantissa       = bits & 0x000FFFFFFFFFFFFFULL;

    if ( biasedExponent == 0 || biasedExponent >= 0x7FE )
    {
        return;
    }

    mantissa |= 0x0010000000000000ULL;
    int exponent = biasedExponent - 1075;

    ntt_detail_big midpoint;
    ntt_detail_big_set( &midpoint, 2 * mantissa + 1 );

    int scale = exponent - 1;
    int point = 0;

    if ( scale >= 0 )
    {
        ntt_detail_big_multiply_power2( &midpoint, scale );
    }
    else
    {
        ntt_detail_big_multiply_power5( &midpoint, -scale );
        point = -scale;
    }

    char    digits[ 1200 ];
    ntt_i32 count = BigToDecimal( &midpoint, digits );

    std::string all( digits, digits + count );
    int         trailing = 0;
    while ( all.size() > 1 && all.back() == '0' )
    {
        all.pop_back();
        trailing++;
    }

    long long allExponent = (long long)trailing - point;
    CheckRead( all + "e" + std::to_string( allExponent ) );

    for ( int keep : { 17, 18, 19, 20 } )
    {
        if ( (int)all.size() <= keep )
        {
            continue;
        }

        std::string down = all.substr( 0, keep );
        std::string up   = down;
        int         a    = keep - 1;
        while ( a >= 0 && up[ a ] == '9' )
        {
            up[ a-- ] = '0';
        }
        if ( a < 0 )
        {
            continue;
        }
        up[ a ]++;

        long long shift = allExponent + (long long)( all.size() - keep );
        CheckRead( down + "e" + std::to_string( shift ) );
        CheckRead( up + "e" + std::to_string( shift ) );
    }
}

static void CheckGrammar()
{
    struct Case
    {
        const char* text;
        ntt_status  status;
        int         stop;
    };

    const Case cases[] = {
        { "0", NUMBERTEXTTHREE_OK, 1 },
        { "-0", NUMBERTEXTTHREE_OK, 2 },
        { "12.5e-3,", NUMBERTEXTTHREE_OK, 7 },
        { "1E+2]", NUMBERTEXTTHREE_OK, 4 },
        { "1.5.3", NUMBERTEXTTHREE_OK, 3 },
        { "1e400", NUMBERTEXTTHREE_OUT_OF_RANGE, 5 },
        { "-1e400", NUMBERTEXTTHREE_OUT_OF_RANGE, 6 },
        { "1e-400", NUMBERTEXTTHREE_OK, 6 },
        { "", NUMBERTEXTTHREE_INVALID, 0 },
        { "-", NUMBERTEXTTHREE_INVALID, 1 },
        { "+1", NUMBERTEXTTHREE_INVALID, 0 },
        { ".5", NUMBERTEXTTHREE_INVALID, 0 },
        { "01", NUMBERTEXTTHREE_INVALID, 1 },
        { "-01", NUMBERTEXTTHREE_INVALID, 2 },
        { "1.", NUMBERTEXTTHREE_INVALID, 2 },
        { "1.e5", NUMBERTEXTTHREE_INVALID, 2 },
        { "1e", NUMBERTEXTTHREE_INVALID, 2 },
        { "1e+", NUMBERTEXTTHREE_INVALID, 3 },
        { "- 1", NUMBERTEXTTHREE_INVALID, 1 },
    };

    for ( const Case& c : cases )
    {
        double      value = 0;
        const char* stop  = nullptr;
        ntt_status  s     = ntt_parse_json_f64( c.text, c.text + strlen( c.text ), &value, &stop );

        if ( s != c.status || stop != c.text + c.stop )
        {
            Fail( "grammar", std::string( c.text ) + " status " + std::to_string( s ) + " stop " + std::to_string( stop - c.text ) );
        }
    }

    double value = 1;
    if ( ntt_parse_json_f64( "-0", nullptr, &value, nullptr ) != NUMBERTEXTTHREE_INVALID )
    {
        Fail( "grammar", "a null end must read nothing" );
    }

    ntt_decimal decimal = { "", 0, 0, 1 };
    if ( ntt_decimal_to_f64( &decimal, &value ) != NUMBERTEXTTHREE_OK || Bits( value ) != 0x8000000000000000ULL )
    {
        Fail( "decimal contract", "an empty digit sequence is a signed zero" );
    }

    decimal.digits = "12a";
    decimal.count  = 3;
    if ( ntt_decimal_to_f64( &decimal, &value ) != NUMBERTEXTTHREE_INVALID )
    {
        Fail( "decimal contract", "a non-digit must be rejected" );
    }

    decimal.digits   = "1";
    decimal.count    = 1;
    decimal.exponent = 0x7FFFFFFFFFFFFFFFLL;
    decimal.negative = 0;
    if ( ntt_decimal_to_f64( &decimal, &value ) != NUMBERTEXTTHREE_OUT_OF_RANGE || !std::isinf( value ) )
    {
        Fail( "decimal contract", "a huge exponent overflows to infinity" );
    }

    decimal.exponent = -0x7FFFFFFFFFFFFFFFLL;
    if ( ntt_decimal_to_f64( &decimal, &value ) != NUMBERTEXTTHREE_OK || value != 0 )
    {
        Fail( "decimal contract", "a huge negative exponent underflows to zero" );
    }

    char text[ NUMBERTEXTTHREE_F64_MAX_CHARS ];
    if ( ntt_write_f64( -0.0, text ) != 2 || strcmp( text, "-0" ) != 0 )
    {
        Fail( "write", "-0.0 is written as -0" );
    }
    if ( ntt_write_f64( INFINITY, text ) != 0 || ntt_write_f64( NAN, text ) != 0 || text[ 0 ] != 0 )
    {
        Fail( "write", "non-finite values write nothing" );
    }

    const struct
    {
        double      value;
        const char* text;
    } layouts[] = {
        { 0.1, "0.1" },
        { 100, "100" },
        { 1e20, "100000000000000000000" },
        { 1e21, "1e+21" },
        { 1e-6, "0.000001" },
        { 1e-7, "1e-7" },
        { 123.456, "123.456" },
        { 1.5e300, "1.5e+300" },
        { 5e-324, "5e-324" },
        { 1.7976931348623157e308, "1.7976931348623157e+308" },
    };

    for ( const auto& layout : layouts )
    {
        ntt_write_f64( layout.value, text );
        if ( strcmp( text, layout.text ) != 0 )
        {
            Fail( "layout", std::string( layout.text ) + " written as " + text );
        }
    }

    if ( SecondUnitWrite( 0.1, text ) != 3 || strcmp( text, "0.1" ) != 0 )
    {
        Fail( "second translation unit", text );
    }
}

#if defined( NUMBERTEXTTHREE_DETAIL_SSE2 ) || defined( NUMBERTEXTTHREE_DETAIL_NEON )

static void CheckSixteenDigits()
{
    unsigned char digits[ 16 ];

    for ( ntt_u64 high = 0; high < 100000000; high++ )
    {
        ntt_u64 low    = 99999999 - high;
        ntt_u64 first  = high;
        ntt_u64 second = low;
        bool    right  = true;

#    if defined( NUMBERTEXTTHREE_DETAIL_SSE2 )
        _mm_storeu_si128( (__m128i*)(void*)digits, ntt_detail_sixteen_digits( high, low, 1 ) );
#    else
        vst1q_u8( digits, ntt_detail_sixteen_digits( high, low, 1 ) );
#    endif

        for ( int a = 7; a >= 0; a-- )
        {
            right  = right && digits[ a ] == first % 10 && digits[ 8 + a ] == second % 10;
            first  = first / 10;
            second = second / 10;
        }

        if ( !right )
        {
            Fail( "sixteen digits", std::to_string( high ) + " " + std::to_string( low ) );
        }
    }
}

#endif

// The reader takes runs of digits eight bytes at a time, so we try runs of every length. Each run ends either at the
// end of the input or at a byte just outside '0' to '9', and the reader must stop right at the end of the run without
// ever reading past the input.
static void CheckDigitRuns()
{
    const std::string digits = "12345678901234567890123456";

    for ( size_t length = 1; length <= digits.size(); length++ )
    {
        for ( const char* prefix : { "", "-", "0.", "-0.00", "7." } )
        {
            std::string number = prefix + digits.substr( 0, length );

            CheckRead( number );

            // Put the number alone in an allocation of its own size, so that the address sanitizer catches any read
            // before the first byte or past the last. The reader loads the tail of a run in one go, and that load must
            // stay inside these bounds.
            {
                std::unique_ptr<char[]> exact( new char[ number.size() ] );
                double                  value = 0;
                const char*             stop  = nullptr;

                memcpy( exact.get(), number.data(), number.size() );
                ntt_parse_json_f64( exact.get(), exact.get() + number.size(), &value, &stop );

                if ( stop != exact.get() + number.size() || Bits( value ) != Bits( Reference( number ) ) )
                {
                    Fail( "digit run alone in its allocation", number );
                }
            }

            for ( char terminator : { '/', ':', '\0', ',', '\xB0', '\xFF' } )
            {
                std::vector<char> text( number.begin(), number.end() );
                double            value = 0;
                const char*       stop  = nullptr;

                text.push_back( terminator );
                ntt_parse_json_f64( text.data(), text.data() + text.size(), &value, &stop );

                if ( stop != text.data() + number.size() || Bits( value ) != Bits( Reference( number ) ) )
                {
                    Fail( "digit run before a terminator", number );
                }
            }
        }
    }

    for ( const char* text : { "9007199254740991",
                               "9007199254740992",
                               "9007199254740993",
                               "-9007199254740993",
                               "18014398509481985",
                               "0.0123456789012345",
                               "0.000000001234567890123",
                               "12345678",
                               "-1234567",
                               "1000000000000000000000" } )
    {
        CheckRead( text );
    }
}

int main( int argc, char** argv )
{
    long            count = ( argc > 1 ) ? atol( argv[ 1 ] ) : 200000;
    std::mt19937_64 random( 20261025 );

    CheckGrammar();
    CheckDigitRuns();

#if defined( NUMBERTEXTTHREE_DETAIL_SSE2 ) || defined( NUMBERTEXTTHREE_DETAIL_NEON )
    CheckSixteenDigits();
#endif

    for ( uint64_t bits : { 0x3FF8000000000000ULL,
                            0x4029000000000000ULL,
                            0x3FD3333333333333ULL,
                            0x3EB0000000000000ULL,
                            0x3EB0C6F7A0B5ED8DULL,
                            0x3E7AD7F29ABCAF48ULL,
                            0x444B1AE4D6E2EF50ULL,
                            0x444B1AE4D6E2EF4FULL,
                            0x4F70000000000001ULL,
                            0x4341C37937E08000ULL,
                            0x43D1D6D23D7354F1ULL } )
    {
        CheckWrite( FromBits( bits ) );
        CheckWrite( -FromBits( bits ) );
    }

    for ( int e = -330; e <= 310; e++ )
    {
        double power = std::pow( 10.0, e );
        if ( power == 0 || !std::isfinite( power ) )
        {
            continue;
        }
        for ( double value : { power, std::nextafter( power, 0.0 ), std::nextafter( power, INFINITY ), -power } )
        {
            CheckWrite( value );
            CheckRead( Shortest( value ) );
            CheckRead( Precision( value, 17 ) );
        }
    }

    for ( int e = -1074; e <= 1023; e++ )
    {
        double power = std::ldexp( 1.0, e );
        CheckWrite( power );
        CheckWrite( std::nextafter( power, 0.0 ) );
        CheckWrite( std::nextafter( power, INFINITY ) );
        CheckHalfway( power );
    }

    for ( long a = 0; a < count; a++ )
    {
        double value = FromBits( random() & 0x7FFFFFFFFFFFFFFFULL );
        if ( !std::isfinite( value ) )
        {
            continue;
        }
        CheckWrite( value );
        if ( ( a & 3 ) == 0 )
        {
            CheckRead( Shortest( value ) );
            CheckRead( Precision( value, 17 ) );
            CheckRead( Precision( value, 16 ) );
        }
        if ( ( a & 15 ) == 0 )
        {
            CheckHalfway( value );
        }
    }

    for ( long a = 0; a < count / 2; a++ )
    {
        double value = double( int64_t( random() % 20000000001LL ) - 10000000000LL ) / std::pow( 10.0, double( random() % 16 ) );
        CheckWrite( value );
        CheckRead( Shortest( value ) );
    }

    for ( long a = 0; a < count / 4; a++ )
    {
        double value = double( random() >> ( random() % 64 ) );
        CheckWrite( value );
        CheckRead( Shortest( value ) );
    }

    const char digits[] = "0123456789";
    for ( long a = 0; a < count; a++ )
    {
        std::string text;
        if ( random() & 1 )
        {
            text += '-';
        }
        if ( random() % 4 == 0 )
        {
            text += "0.";
            for ( int b = int( random() % 20 ); b > 0; b-- )
            {
                text += '0';
            }
            for ( int b = 1 + int( random() % 19 ); b > 0; b-- )
            {
                text += digits[ random() % 10 ];
            }
        }
        else
        {
            int integerDigits = 1 + int( random() % 24 );
            text += digits[ 1 + random() % 9 ];
            for ( int b = 1; b < integerDigits; b++ )
            {
                text += digits[ random() % 10 ];
            }
            if ( random() & 1 )
            {
                text += '.';
                for ( int b = 1 + int( random() % 24 ); b > 0; b-- )
                {
                    text += digits[ random() % 10 ];
                }
            }
        }
        if ( random() & 1 )
        {
            text += ( random() & 1 ) ? 'e' : 'E';
            int exponent = int( random() % 760 ) - 380;
            if ( exponent >= 0 && ( random() & 1 ) )
            {
                text += '+';
            }
            text += std::to_string( exponent );
        }
        CheckRead( text );
    }

    if ( argc > 2 )
    {
        std::ifstream file( argv[ 2 ] );
        std::string   line;
        while ( std::getline( file, line ) )
        {
            if ( !line.empty() && line.back() == '\r' )
            {
                line.pop_back();
            }

            if ( !line.empty() )
            {
                CheckRead( line );
            }
        }
    }

    printf( "write: %ld checked; read: %ld checked, %.2f%% fast; %ld failures; strtod disagreed with from_chars %ld times\n",
            writeChecked,
            readChecked,
            100.0 * readFast / readChecked,
            failures,
            strtodDisagreements );

    return failures ? 1 : 0;
}

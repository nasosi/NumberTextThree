#include "Datasets.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <random>
#include <string_view>

using Random = std::mt19937_64;

static void AddText( std::string& characters, std::vector<Span>& texts, std::string_view text )
{
    Span span;

    span.begin = uint32_t( characters.size() );
    characters.append( text );
    span.end = uint32_t( characters.size() );
    characters.push_back( '\n' );
    texts.push_back( span );
}

static std::string Plural( size_t count, const char* noun )
{
    return std::to_string( count ) + " " + noun + ( count == 1 ? "" : "s" );
}

// Reads every text with std::from_chars. Texts outside the double range are left out of the dataset, and the note
// mentions them.
static void ReadValues( Dataset& dataset )
{
    std::string       characters;
    std::vector<Span> texts;
    size_t            outsideDouble = 0;
    size_t            outsideFloat  = 0;

    for ( const Span& span : dataset.texts )
    {
        const char* begin  = dataset.characters.data() + span.begin;
        const char* end    = dataset.characters.data() + span.end;
        double      value  = 0;
        float       single = 0;

        if ( std::from_chars( begin, end, value ).ec != std::errc() )
        {
            outsideDouble++;
            continue;
        }

        if ( std::from_chars( begin, end, single ).ec != std::errc() )
        {
            outsideFloat++;
        }

        AddText( characters, texts, std::string_view( begin, size_t( end - begin ) ) );
        dataset.doubles.push_back( value );
        dataset.floats.push_back( single );
    }

    dataset.characters = std::move( characters );
    dataset.texts      = std::move( texts );

    if ( outsideDouble > 0 )
    {
        dataset.note = Plural( outsideDouble, "number" ) + " outside the double range left out. ";
    }

    if ( outsideFloat > 0 )
    {
        dataset.floats.clear();
        dataset.note += "No f32: " + Plural( outsideFloat, "number" ) + " outside the float range.";
    }
}

static uint64_t PowerOfTen( int exponent )
{
    uint64_t power = 1;

    while ( exponent-- > 0 )
    {
        power *= 10;
    }

    return power;
}

// Returns a random integer that has `digits` decimal digits and is no larger than `largest`.
static uint64_t WithDigits( Random& random, int digits, uint64_t largest )
{
    uint64_t lowest  = digits == 1 ? 0 : PowerOfTen( digits - 1 );
    uint64_t highest = digits >= 20 ? largest : std::min( PowerOfTen( digits ) - 1, largest );

    return lowest + random() % ( highest - lowest + 1 );
}

static std::string WithSign( Random& random, std::string text )
{
    return ( random() & 1 ) ? "-" + text : text;
}

template <class Number>
static std::string Shortest( Number value )
{
    char text[ 64 ];

    return std::string( text, std::to_chars( text, text + sizeof( text ), value ).ptr );
}

static std::string RandomDouble( Random& random )
{
    double value = std::bit_cast<double>( uint64_t( random() ) );

    return std::isfinite( value ) ? Shortest( value ) : std::string();
}

static std::string RandomFloat( Random& random )
{
    float value = std::bit_cast<float>( uint32_t( random() >> 32 ) );

    return std::isfinite( value ) ? Shortest( value ) : std::string();
}

// A short decimal such as 0.25 or 12.5, with 1 to 6 significant digits and up to 6 decimals. It never has a trailing
// zero after the point.
static std::string ShortDecimal( Random& random )
{
    int      digits   = 1 + int( random() % 6 );
    size_t   decimals = size_t( random() % 7 );
    uint64_t value    = WithDigits( random, digits, UINT64_MAX );

    if ( decimals > 0 && value % 10 == 0 )
    {
        value++;
    }

    std::string text = std::to_string( value );

    if ( decimals > 0 )
    {
        if ( text.size() <= decimals )
        {
            text.insert( 0, decimals + 1 - text.size(), '0' );
        }

        text.insert( text.size() - decimals, 1, '.' );
    }

    return WithSign( random, text );
}

// A number with 17 significant digits, from d.dddddddddddddddde-30 to d.dddddddddddddddde30, which keeps it inside
// the float range as well.
static std::string SeventeenDigits( Random& random )
{
    std::string digits   = std::to_string( WithDigits( random, 17, UINT64_MAX ) );
    int         exponent = int( random() % 61 ) - 30;

    return WithSign( random, digits.substr( 0, 1 ) + "." + digits.substr( 1 ) + "e" + std::to_string( exponent ) );
}

// An integer of 1 to 15 digits, which is few enough that every one is exact as a double.
static std::string Integer( Random& random )
{
    int digits = 1 + int( random() % 15 );

    return WithSign( random, std::to_string( WithDigits( random, digits, UINT64_MAX ) ) );
}

// A value uniform in [-1000, 1000) that uses every bit of the double, the way map or mesh coordinates do.
static std::string Coordinate( Random& random )
{
    double unit = double( random() >> 11 ) * 0x1p-53;

    return Shortest( -1000.0 + 2000.0 * unit );
}

// Builds a dataset of `count` texts from `make`. When `make` returns an empty text, that draw is skipped.
template <class Make>
static Dataset Generate( const char* name, size_t count, Make make )
{
    Dataset dataset;
    Random  random( kSeed );

    dataset.name = name;

    while ( dataset.texts.size() < count )
    {
        std::string text = make( random );

        if ( !text.empty() )
        {
            AddText( dataset.characters, dataset.texts, text );
        }
    }

    ReadValues( dataset );
    return dataset;
}

std::vector<Dataset> SyntheticDatasets( size_t count )
{
    std::vector<Dataset> datasets;

    datasets.push_back( Generate( "random doubles", count, RandomDouble ) );
    datasets.push_back( Generate( "random floats", count, RandomFloat ) );
    datasets.push_back( Generate( "short decimals", count, ShortDecimal ) );
    datasets.push_back( Generate( "17 digits", count, SeventeenDigits ) );
    datasets.push_back( Generate( "integers", count, Integer ) );
    datasets.push_back( Generate( "coordinates", count, Coordinate ) );
    return datasets;
}

// Integers whose number of digits is uniformly random, from 1 up to as many as the type can hold.
static Dataset Integers( const char* name, IntegerType type, size_t count, int mostDigits, uint64_t largest, bool isSigned )
{
    Dataset dataset;
    Random  random( kSeed );

    dataset.name        = name;
    dataset.integerType = type;

    while ( dataset.integers.size() < count )
    {
        uint64_t magnitude = WithDigits( random, 1 + int( random() % uint64_t( mostDigits ) ), largest );
        bool     negative  = isSigned && ( random() & 1 );

        dataset.integers.push_back( negative ? 0 - magnitude : magnitude );
    }

    return dataset;
}

std::vector<Dataset> IntegerDatasets( size_t count )
{
    std::vector<Dataset> datasets;

    datasets.push_back( Integers( "u32", IntegerType::u32, count, 10, UINT32_MAX, false ) );
    datasets.push_back( Integers( "u64", IntegerType::u64, count, 20, UINT64_MAX, false ) );
    datasets.push_back( Integers( "i32", IntegerType::i32, count, 10, INT32_MAX, true ) );
    datasets.push_back( Integers( "i64", IntegerType::i64, count, 19, INT64_MAX, true ) );
    return datasets;
}

static bool IsDigit( char c )
{
    return c >= '0' && c <= '9';
}

static bool IsWordCharacter( char c )
{
    return IsDigit( c ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_' || c == '.';
}

static const char* SkipDigits( const char* at, const char* end )
{
    while ( at < end && IsDigit( *at ) )
    {
        at++;
    }

    return at;
}

// Returns the end of the JSON number -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][-+]?[0-9]+)? that starts at `at`, or `at`
// itself if no number starts there.
static const char* MatchNumber( const char* at, const char* end )
{
    const char* next = at;

    if ( *next == '-' )
    {
        next++;
    }

    if ( next == end || !IsDigit( *next ) )
    {
        return at;
    }

    next = *next == '0' ? next + 1 : SkipDigits( next, end );

    if ( next < end && *next == '.' )
    {
        const char* fraction = SkipDigits( next + 1, end );

        if ( fraction == next + 1 )
        {
            return at;
        }

        next = fraction;
    }

    if ( next < end && ( *next == 'e' || *next == 'E' ) )
    {
        const char* exponent = next + 1;

        if ( exponent < end && ( *exponent == '+' || *exponent == '-' ) )
        {
            exponent++;
        }

        next = SkipDigits( exponent, end );

        if ( next == exponent )
        {
            return at;
        }
    }

    return next;
}

static const char* SkipString( const char* at, const char* end )
{
    while ( at < end && *at != '"' )
    {
        if ( *at == '\\' && at + 1 < end )
        {
            at++;
        }

        at++;
    }

    return at < end ? at + 1 : end;
}

// Collects the numbers that are outside quoted strings and not part of a word. For example, the 3 in float[3] counts,
// but the 1 in node1 and 0x3F do not.
static void FindNumbers( const std::string& text, Dataset& dataset )
{
    const char* begin = text.data();
    const char* end   = begin + text.size();
    const char* at    = begin;

    while ( at < end )
    {
        if ( *at == '"' )
        {
            at = SkipString( at + 1, end );
            continue;
        }

        if ( ( at == begin || !IsWordCharacter( at[ -1 ] ) ) && ( *at == '-' || IsDigit( *at ) ) )
        {
            const char* stop = MatchNumber( at, end );

            if ( stop > at && ( stop == end || !IsWordCharacter( *stop ) ) )
            {
                AddText( dataset.characters, dataset.texts, std::string_view( at, size_t( stop - at ) ) );
                at = stop;
                continue;
            }
        }

        at++;
    }
}

static bool ReadWholeFile( const char* path, std::string& text )
{
    FILE*  file = fopen( path, "rb" );
    char   buffer[ 65536 ];
    size_t count;

    if ( !file )
    {
        return false;
    }

    while ( ( count = fread( buffer, 1, sizeof( buffer ), file ) ) > 0 )
    {
        text.append( buffer, count );
    }

    bool failed = ferror( file ) != 0;
    fclose( file );
    return !failed;
}

static std::string FileName( std::string_view path )
{
    size_t slash = path.find_last_of( "/\\" );

    return std::string( slash == std::string_view::npos ? path : path.substr( slash + 1 ) );
}

bool LoadFile( const char* path, Dataset& dataset, std::string& error )
{
    std::string text;

    if ( !ReadWholeFile( path, text ) )
    {
        error = std::string( "cannot read " ) + path;
        return false;
    }

    // Span holds 32-bit offsets, and the texts we collect take at most twice the size of the file.
    if ( text.size() >= UINT32_MAX / 2 )
    {
        error = std::string( "larger than 2 GB: " ) + path;
        return false;
    }

    dataset.name = FileName( path );
    FindNumbers( text, dataset );
    ReadValues( dataset );
    return true;
}

// Times NumberTextThree against other libraries. The tables have one row per dataset and one column per library, and
// each cell shows the minimum and median nanoseconds per number over several runs. See README.md.

#include "Datasets.h"
#include "Libraries.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined( _WIN32 )
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#elif defined( __linux__ )
#    include <sched.h>
#endif

#if defined( _MSC_VER ) && !defined( __clang__ )
#    include <intrin.h>
#endif

// Each sample times at least this many conversions, which means that even small files give a steady reading.
constexpr size_t kNumbersPerSample = 1000000;

using Clock = std::chrono::steady_clock;

static double NanosecondsSince( Clock::time_point start )
{
    return std::chrono::duration<double, std::nano>( Clock::now() - start ).count();
}

// Tells the compiler that the text at `output` is used, so that it can't optimise away any part of a write.
#if defined( _MSC_VER ) && !defined( __clang__ )
static char* volatile writtenText;

static inline void KeepWritten( char* output )
{
    writtenText = output;
    _ReadWriteBarrier();
}
#else
static inline void KeepWritten( char* output )
{
    __asm__ __volatile__( "" : : "r"( output ) : "memory" );
}
#endif

static uint64_t BitsOf( double value )
{
    return std::bit_cast<uint64_t>( value );
}

static uint64_t BitsOf( float value )
{
    return std::bit_cast<uint32_t>( value );
}

template <class Number>
static const std::vector<Number>& ValuesOf( const Dataset& data )
{
    if constexpr ( std::is_same_v<Number, double> )
    {
        return data.doubles;
    }
    else
    {
        return data.floats;
    }
}

template <class Number>
Number ArgumentOf( int ( *write )( Number, char* ) ); // used in decltype and never defined

template <class Number>
static std::string Shortest( Number value )
{
    char text[ kWriteRoom ];

    return std::string( text, std::to_chars( text, text + sizeof( text ), value ).ptr );
}

static std::string_view TextOf( const Dataset& data, size_t index )
{
    const Span& span = data.texts[ index ];

    return std::string_view( data.characters.data() + span.begin, span.end - span.begin );
}

// Calls `visit` with a zero of the dataset's integer type, so that the lambda can get that type with decltype.
template <class Visit>
static auto WithIntegerType( IntegerType type, Visit visit )
{
    switch ( type )
    {
        case IntegerType::u32:
            return visit( uint32_t() );
        case IntegerType::u64:
            return visit( uint64_t() );
        case IntegerType::i32:
            return visit( int32_t() );
        default:
            return visit( int64_t() );
    }
}

// Timing. Each function runs `repeats` passes over the data and returns the nanoseconds they took. Whatever the
// library produced is added into `checksum`, so that the compiler can't optimise any of the work away.

template <auto Read>
static double TimeReads( const Dataset& data, int repeats, uint64_t& checksum )
{
    const char*       characters = data.characters.data();
    uint64_t          total      = 0;
    Clock::time_point start      = Clock::now();

    for ( int repeat = 0; repeat < repeats; repeat++ )
    {
        for ( const Span& text : data.texts )
        {
            total += BitsOf( Read( characters + text.begin, characters + text.end ) );
        }
    }

    double elapsed = NanosecondsSince( start );
    checksum += total;
    return elapsed;
}

template <class Values, class Write>
static double TimeWritePasses( const Values& values, int repeats, uint64_t& checksum, Write write )
{
    char              output[ kWriteRoom ];
    uint64_t          total = 0;
    Clock::time_point start = Clock::now();

    for ( int repeat = 0; repeat < repeats; repeat++ )
    {
        for ( auto value : values )
        {
            int length = write( value, output );
            KeepWritten( output );
            total += uint64_t( length );
        }
    }

    double elapsed = NanosecondsSince( start );
    checksum += total;
    return elapsed;
}

template <auto Write>
static double TimeWrites( const Dataset& data, int repeats, uint64_t& checksum )
{
    using Number = decltype( ArgumentOf( Write ) );

    return TimeWritePasses( ValuesOf<Number>( data ), repeats, checksum, []( Number value, char* output ) { return Write( value, output ); } );
}

template <class Writer>
static double TimeIntegers( const Dataset& data, int repeats, uint64_t& checksum )
{
    return WithIntegerType( data.integerType,
                            [ & ]( auto zero )
                            {
                                using Integer = decltype( zero );

                                return TimeWritePasses( data.integers,
                                                        repeats,
                                                        checksum,
                                                        []( uint64_t bits, char* output ) { return Writer::Write( static_cast<Integer>( bits ), output ); } );
                            } );
}

// Checks. Each function returns how many numbers disagree with the reference, and describes the first of them in
// `example`.

static void Count( size_t& disagreements, std::string& example, const std::string& description )
{
    if ( disagreements == 0 )
    {
        example = description;
    }

    disagreements++;
}

template <auto Read>
static size_t CheckReads( const Dataset& data, std::string& example )
{
    using Number = decltype( Read( nullptr, nullptr ) );

    const std::vector<Number>& expected      = ValuesOf<Number>( data );
    size_t                     disagreements = 0;

    for ( size_t at = 0; at < data.texts.size(); at++ )
    {
        std::string_view text  = TextOf( data, at );
        Number           value = Read( text.data(), text.data() + text.size() );

        if ( BitsOf( value ) != BitsOf( expected[ at ] ) )
        {
            Count( disagreements,
                   example,
                   "\"" + std::string( text ) + "\" read as " + Shortest( value ) + ", std::from_chars reads " + Shortest( expected[ at ] ) );
        }
    }

    return disagreements;
}

template <class Number>
static bool ReadsBackExactly( const char* text, int length, Number value )
{
    Number                 back   = 0;
    std::from_chars_result result = std::from_chars( text, text + length, back );

    return result.ec == std::errc() && result.ptr == text + length && BitsOf( back ) == BitsOf( value );
}

template <auto Write>
static size_t CheckWrites( const Dataset& data, std::string& example )
{
    using Number = decltype( ArgumentOf( Write ) );

    size_t disagreements = 0;

    for ( Number value : ValuesOf<Number>( data ) )
    {
        char output[ kWriteRoom ];
        int  length = Write( value, output );

        if ( length <= 0 || length >= kWriteRoom || !ReadsBackExactly( output, length, value ) )
        {
            std::string text( output, size_t( std::clamp( length, 0, kWriteRoom ) ) );
            Count( disagreements, example, Shortest( value ) + " written as \"" + text + "\"" );
        }
    }

    return disagreements;
}

template <class Writer>
static size_t CheckIntegers( const Dataset& data, std::string& example )
{
    return WithIntegerType( data.integerType,
                            [ & ]( auto zero )
                            {
                                using Integer = decltype( zero );

                                size_t disagreements = 0;

                                for ( uint64_t bits : data.integers )
                                {
                                    char    written[ kWriteRoom ];
                                    char    expected[ kWriteRoom ];
                                    Integer value          = static_cast<Integer>( bits );
                                    int     writtenLength  = Writer::Write( value, written );
                                    int     expectedLength = ToCharsIntegers::Write( value, expected );

                                    if ( std::string_view( written, size_t( writtenLength ) ) != std::string_view( expected, size_t( expectedLength ) ) )
                                    {
                                        Count( disagreements,
                                               example,
                                               std::string( expected, size_t( expectedLength ) ) + " written as \"" +
                                                   std::string( written, size_t( writtenLength ) ) + "\"" );
                                    }
                                }

                                return disagreements;
                            } );
}

// The libraries

struct Library
{
    const char* name;
    double ( *time )( const Dataset& data, int repeats, uint64_t& checksum );
    size_t ( *check )( const Dataset& data, std::string& example );
};

struct Operation
{
    const char* name;
    bool ( *applies )( const Dataset& data );
    std::vector<Library> libraries;
};

template <auto Read>
static Library ReadLibrary( const char* name )
{
    return { name, TimeReads<Read>, CheckReads<Read> };
}

template <auto Write>
static Library WriteLibrary( const char* name )
{
    return { name, TimeWrites<Write>, CheckWrites<Write> };
}

template <class Writer>
static Library IntegerLibrary( const char* name )
{
    return { name, TimeIntegers<Writer>, CheckIntegers<Writer> };
}

static bool HasDoubles( const Dataset& data )
{
    return !data.doubles.empty();
}

static bool HasFloats( const Dataset& data )
{
    return !data.floats.empty();
}

static bool HasIntegers( const Dataset& data )
{
    return !data.integers.empty();
}

static std::vector<Operation> Operations()
{
    return {
        { "read-f64",
          HasDoubles,
          { ReadLibrary<ReadNtt>( "ntt" ),
            ReadLibrary<ReadFastFloat>( "fast_float" ),
            ReadLibrary<ReadFromChars>( "from_chars" ),
            ReadLibrary<ReadStrtod>( "strtod" ) } },
        { "write-f64",
          HasDoubles,
          { WriteLibrary<WriteNtt>( "ntt" ),
            WriteLibrary<WriteZmij>( "zmij" ),
            WriteLibrary<WriteXjb>( "xjb" ),
            WriteLibrary<WriteRyu>( "ryu" ),
            WriteLibrary<WriteDragonbox>( "dragonbox" ),
            WriteLibrary<WriteToChars>( "to_chars" ),
            WriteLibrary<WritePrintf>( "printf" ) } },
        { "read-f32",
          HasFloats,
          { ReadLibrary<ReadNttF32>( "ntt" ),
            ReadLibrary<ReadFastFloatF32>( "fast_float" ),
            ReadLibrary<ReadFromCharsF32>( "from_chars" ),
            ReadLibrary<ReadStrtof>( "strtof" ) } },
        { "write-f32",
          HasFloats,
          { WriteLibrary<WriteNttF32>( "ntt" ),
            WriteLibrary<WriteZmijF32>( "zmij" ),
            WriteLibrary<WriteXjbF32>( "xjb" ),
            WriteLibrary<WriteRyuF32>( "ryu" ),
            WriteLibrary<WriteDragonboxF32>( "dragonbox" ),
            WriteLibrary<WriteToCharsF32>( "to_chars" ),
            WriteLibrary<WritePrintfF32>( "printf" ) } },
        { "write-int", HasIntegers, { IntegerLibrary<NttIntegers>( "ntt" ), IntegerLibrary<ToCharsIntegers>( "to_chars" ) } },
    };
}

// Results

struct Cell
{
    std::vector<double> samples; // nanoseconds per number, one sample for each run
    size_t              disagreements = 0;
    std::string         example;
};

// Indexed as results[ operation ][ dataset ][ library ]. When an operation doesn't apply to a dataset, the cells for
// that dataset have no samples.
using Results = std::vector<std::vector<std::vector<Cell>>>;

static Results Check( const std::vector<Operation>& operations, const std::vector<Dataset>& datasets )
{
    Results results( operations.size() );

    for ( size_t operation = 0; operation < operations.size(); operation++ )
    {
        const Operation& current = operations[ operation ];

        results[ operation ].resize( datasets.size(), std::vector<Cell>( current.libraries.size() ) );

        for ( size_t dataset = 0; dataset < datasets.size(); dataset++ )
        {
            for ( size_t library = 0; library < current.libraries.size() && current.applies( datasets[ dataset ] ); library++ )
            {
                Cell& cell         = results[ operation ][ dataset ][ library ];
                cell.disagreements = current.libraries[ library ].check( datasets[ dataset ], cell.example );
            }
        }
    }

    return results;
}

// Every run times every library once on every dataset it applies to. The libraries take turns, and the turns start
// one place later in each run, so that no library always goes first.
static void Measure( const std::vector<Operation>& operations, const std::vector<Dataset>& datasets, int runs, Results& results, uint64_t& checksum )
{
    for ( int run = 0; run < runs; run++ )
    {
        for ( size_t operation = 0; operation < operations.size(); operation++ )
        {
            const std::vector<Library>& libraries = operations[ operation ].libraries;

            for ( size_t dataset = 0; dataset < datasets.size(); dataset++ )
            {
                const Dataset& data = datasets[ dataset ];

                if ( !operations[ operation ].applies( data ) )
                {
                    continue;
                }

                int    repeats = int( std::max<size_t>( 1, kNumbersPerSample / data.Count() ) );
                double numbers = double( repeats ) * double( data.Count() );

                for ( size_t turn = 0; turn < libraries.size(); turn++ )
                {
                    size_t library     = ( turn + size_t( run ) ) % libraries.size();
                    double nanoseconds = libraries[ library ].time( data, repeats, checksum );
                    results[ operation ][ dataset ][ library ].samples.push_back( nanoseconds / numbers );
                }
            }
        }
    }
}

static double Minimum( std::vector<double> samples )
{
    return *std::min_element( samples.begin(), samples.end() );
}

static double Median( std::vector<double> samples )
{
    std::sort( samples.begin(), samples.end() );
    size_t middle = samples.size() / 2;
    return samples.size() % 2 == 1 ? samples[ middle ] : ( samples[ middle - 1 ] + samples[ middle ] ) / 2;
}

// Printing

static std::string CompilerName()
{
#if defined( __clang__ )
    return "clang " __clang_version__;
#elif defined( _MSC_VER )
    return "MSVC " + std::to_string( _MSC_FULL_VER );
#elif defined( __GNUC__ )
    return "gcc " __VERSION__;
#else
    return "an unknown compiler";
#endif
}

static void PrintDatasets( FILE* out, const std::vector<Dataset>& datasets )
{
    fprintf( out, "datasets (synthetic ones from seed %llu)\n", (unsigned long long)kSeed );

    for ( const Dataset& data : datasets )
    {
        fprintf( out, "  %-18s %9zu  %s\n", data.name.c_str(), data.Count(), data.note.c_str() );
    }
}

static void PrintTables( const std::vector<Operation>& operations, const std::vector<Dataset>& datasets, const Results& results, int runs )
{
    for ( size_t operation = 0; operation < operations.size(); operation++ )
    {
        const Operation& current = operations[ operation ];

        printf( "\n%s: nanoseconds per number, min and median of %d run%s\n", current.name, runs, runs == 1 ? "" : "s" );
        printf( "%-18s %9s", "dataset", "numbers" );

        for ( const Library& library : current.libraries )
        {
            printf( " %15s", library.name );
        }

        printf( "\n" );

        for ( size_t dataset = 0; dataset < datasets.size(); dataset++ )
        {
            if ( !current.applies( datasets[ dataset ] ) )
            {
                continue;
            }

            printf( "%-18s %9zu", datasets[ dataset ].name.c_str(), datasets[ dataset ].Count() );

            for ( const Cell& cell : results[ operation ][ dataset ] )
            {
                printf( " %7.2f %7.2f", Minimum( cell.samples ), Median( cell.samples ) );
            }

            printf( "\n" );
        }
    }
}

// Quotes a CSV field when it contains a comma or a quote, and doubles any quotes inside it.
static std::string CsvField( const std::string& text )
{
    if ( text.find_first_of( ",\"" ) == std::string::npos )
    {
        return text;
    }

    std::string quoted = "\"";

    for ( char c : text )
    {
        quoted += c == '"' ? std::string( "\"\"" ) : std::string( 1, c );
    }

    return quoted + "\"";
}

static void PrintCsv( const std::vector<Operation>& operations, const std::vector<Dataset>& datasets, const Results& results )
{
    printf( "operation,dataset,library,numbers,min_ns,median_ns,disagreements\n" );

    for ( size_t operation = 0; operation < operations.size(); operation++ )
    {
        for ( size_t dataset = 0; dataset < datasets.size(); dataset++ )
        {
            for ( size_t library = 0; library < operations[ operation ].libraries.size(); library++ )
            {
                const Cell& cell = results[ operation ][ dataset ][ library ];

                if ( !cell.samples.empty() )
                {
                    printf( "%s,%s,%s,%zu,%.3f,%.3f,%zu\n",
                            operations[ operation ].name,
                            CsvField( datasets[ dataset ].name ).c_str(),
                            operations[ operation ].libraries[ library ].name,
                            datasets[ dataset ].Count(),
                            Minimum( cell.samples ),
                            Median( cell.samples ),
                            cell.disagreements );
                }
            }
        }
    }
}

// Prints one line per operation with each library's number of disagreements, and then the first disagreement of
// every cell that has any. Returns the total.
static size_t PrintChecks( FILE* out, const std::vector<Operation>& operations, const std::vector<Dataset>& datasets, const Results& results )
{
    size_t total = 0;

    fprintf( out, "\nchecks: reads against std::from_chars, writes read back to the same bits, integers against std::to_chars\n" );

    for ( size_t operation = 0; operation < operations.size(); operation++ )
    {
        fprintf( out, "%-10s", operations[ operation ].name );

        for ( size_t library = 0; library < operations[ operation ].libraries.size(); library++ )
        {
            size_t disagreements = 0;

            for ( size_t dataset = 0; dataset < datasets.size(); dataset++ )
            {
                disagreements += results[ operation ][ dataset ][ library ].disagreements;
            }

            fprintf( out, "  %s %zu", operations[ operation ].libraries[ library ].name, disagreements );
            total += disagreements;
        }

        fprintf( out, "\n" );
    }

    for ( size_t operation = 0; operation < operations.size(); operation++ )
    {
        for ( size_t dataset = 0; dataset < datasets.size(); dataset++ )
        {
            for ( size_t library = 0; library < operations[ operation ].libraries.size(); library++ )
            {
                const Cell& cell = results[ operation ][ dataset ][ library ];

                if ( cell.disagreements > 0 )
                {
                    fprintf( out,
                             "  %s %s on %s: %zu disagree, first %s\n",
                             operations[ operation ].name,
                             operations[ operation ].libraries[ library ].name,
                             datasets[ dataset ].name.c_str(),
                             cell.disagreements,
                             cell.example.c_str() );
                }
            }
        }
    }

    if ( total == 0 )
    {
        fprintf( out, "every check agrees\n" );
    }
    else
    {
        fprintf( out, "%zu disagreements\n", total );
    }

    return total;
}

// Command line

struct Options
{
    int                      runs  = 5;
    int                      pin   = -1;
    size_t                   count = 100000;
    bool                     csv   = false;
    std::vector<const char*> files;
};

static bool ReadNumber( const char* text, long long lowest, long long highest, long long& value )
{
    char* stop;

    value = strtoll( text, &stop, 10 );
    return *text != '\0' && *stop == '\0' && value >= lowest && value <= highest;
}

static bool ParseOptions( int argumentCount, char** arguments, Options& options )
{
    for ( int at = 1; at < argumentCount; at++ )
    {
        std::string_view argument = arguments[ at ];
        bool             hasValue = at + 1 < argumentCount;
        long long        value    = 0;

        if ( argument == "--csv" )
        {
            options.csv = true;
        }
        else if ( argument == "--runs" && hasValue && ReadNumber( arguments[ ++at ], 1, 1000, value ) )
        {
            options.runs = int( value );
        }
        else if ( argument == "--pin" && hasValue && ReadNumber( arguments[ ++at ], 0, 4095, value ) )
        {
            options.pin = int( value );
        }
        else if ( argument == "--count" && hasValue && ReadNumber( arguments[ ++at ], 1, 100000000, value ) )
        {
            options.count = size_t( value );
        }
        else if ( argument.starts_with( "-" ) )
        {
            return false;
        }
        else
        {
            options.files.push_back( arguments[ at ] );
        }
    }

    return true;
}

static void PrintUsage()
{
    fprintf( stderr,
             "usage: Bench [--runs N] [--pin CPU] [--count N] [--csv] [file ...]\n"
             "  --runs N    timed runs; each cell shows their min and median (default 5)\n"
             "  --pin CPU   run on this logical CPU only (Windows and Linux)\n"
             "  --count N   numbers in each synthetic set (default 100000)\n"
             "  --csv       print comma-separated rows instead of tables\n"
             "  file        any text file; its JSON-grammar numbers become one more dataset\n" );
}

static bool PinToCpu( int cpu )
{
#if defined( _WIN32 )
    return cpu < 64 && SetThreadAffinityMask( GetCurrentThread(), DWORD_PTR( 1 ) << cpu ) != 0;
#elif defined( __linux__ )
    cpu_set_t set;
    CPU_ZERO( &set );
    CPU_SET( cpu, &set );
    return sched_setaffinity( 0, sizeof( set ), &set ) == 0;
#else
    (void)cpu;
    return false;
#endif
}

int main( int argumentCount, char** arguments )
{
    Options options;

    if ( !ParseOptions( argumentCount, arguments, options ) )
    {
        PrintUsage();
        return 2;
    }

    if ( options.pin >= 0 && !PinToCpu( options.pin ) )
    {
        fprintf( stderr, "cannot pin to CPU %d\n", options.pin );
        return 2;
    }

    std::vector<Dataset> datasets = SyntheticDatasets( options.count );

    for ( const char* path : options.files )
    {
        Dataset     dataset;
        std::string error;

        if ( !LoadFile( path, dataset, error ) )
        {
            fprintf( stderr, "%s\n", error.c_str() );
            return 2;
        }

        datasets.push_back( std::move( dataset ) );
    }

    for ( Dataset& dataset : IntegerDatasets( options.count ) )
    {
        datasets.push_back( std::move( dataset ) );
    }

    FILE*       notes   = options.csv ? stderr : stdout;
    std::string pinning = options.pin >= 0 ? "pinned to CPU " + std::to_string( options.pin ) : "not pinned";

    fprintf( notes,
             "NumberTextThree %d.%d.%d, %s, %s\n",
             NUMBERTEXTTHREE_VERSION_MAJOR,
             NUMBERTEXTTHREE_VERSION_MINOR,
             NUMBERTEXTTHREE_VERSION_PATCH,
             CompilerName().c_str(),
             pinning.c_str() );
    PrintDatasets( notes, datasets );

    std::vector<Operation> operations = Operations();
    Results                results    = Check( operations, datasets );
    uint64_t               checksum   = 0;

    Measure( operations, datasets, options.runs, results, checksum );

    if ( options.csv )
    {
        PrintCsv( operations, datasets, results );
    }
    else
    {
        PrintTables( operations, datasets, results, options.runs );
    }

    size_t disagreements = PrintChecks( notes, operations, datasets, results );
    fprintf( notes, "checksum %016llx\n", (unsigned long long)checksum );
    return disagreements == 0 ? 0 : 1;
}

/* Reads numbers in a grammar of its own and leaves the rounding to NumberTextThree.

   This grammar accepts three things that JSON refuses: a leading '+', a missing integer part (".5"), and '_' between
   digits ("1_000_000"). The parser does no more than collect the digits and the decimal exponent into an ntt_decimal.
   Then ntt_decimal_to_f64 and ntt_decimal_to_f32 do the conversion to a correctly rounded double and float. */

#include "NumberTextThree.h"

#include <stdio.h>

enum
{
    kMaxDigits = 800
};

/* Returns 1 and fills in decimal when the whole of text is a number in this grammar, and returns 0 otherwise. The
   digits buffer must have room for kMaxDigits characters. */
static int ReadNumber( const char* text, ntt_decimal* decimal, char* digits )
{
    const char* cursor          = text;
    int         sawDigit        = 0;
    int         afterPoint      = 0;
    ntt_i64     exponent        = 0;
    ntt_i64     writtenExponent = 0;
    int         exponentSign    = 1;

    decimal->digits   = digits;
    decimal->count    = 0;
    decimal->negative = 0;

    if ( *cursor == '+' || *cursor == '-' )
    {
        decimal->negative = *cursor == '-';
        ++cursor;
    }

    for ( ;; ++cursor )
    {
        if ( *cursor >= '0' && *cursor <= '9' )
        {
            if ( decimal->count == kMaxDigits )
            {
                return 0;
            }

            digits[ decimal->count++ ] = *cursor;
            exponent -= afterPoint;
            sawDigit = 1;
        }
        else if ( *cursor == '_' && sawDigit && cursor[ 1 ] >= '0' && cursor[ 1 ] <= '9' )
        {
            continue;
        }
        else if ( *cursor == '.' && !afterPoint )
        {
            afterPoint = 1;
        }
        else
        {
            break;
        }
    }

    if ( !sawDigit )
    {
        return 0;
    }

    if ( *cursor == 'e' || *cursor == 'E' )
    {
        ++cursor;

        if ( *cursor == '+' || *cursor == '-' )
        {
            exponentSign = *cursor == '-' ? -1 : 1;
            ++cursor;
        }

        if ( *cursor < '0' || *cursor > '9' )
        {
            return 0;
        }

        for ( ; *cursor >= '0' && *cursor <= '9'; ++cursor )
        {
            if ( writtenExponent < 100000000 )
            {
                writtenExponent = writtenExponent * 10 + ( *cursor - '0' );
            }
        }
    }

    decimal->exponent = exponent + exponentSign * writtenExponent;
    return *cursor == '\0';
}

int main( void )
{
    static const char* const inputs[] = { "+1_000_000", ".5", "-0.000_001e-3", "3.40282357e38", "1e-46", "nan" };
    char                     digits[ kMaxDigits ];
    unsigned                 index;

    for ( index = 0; index < sizeof( inputs ) / sizeof( inputs[ 0 ] ); ++index )
    {
        ntt_decimal decimal;
        double      f64 = 0.0;
        float       f32 = 0.0F;
        char        f64Text[ NUMBERTEXTTHREE_F64_MAX_CHARS ];
        char        f32Text[ NUMBERTEXTTHREE_F32_MAX_CHARS ];

        if ( !ReadNumber( inputs[ index ], &decimal, digits ) )
        {
            printf( "%-16s not a number in this grammar\n", inputs[ index ] );
            continue;
        }

        ntt_decimal_to_f64( &decimal, &f64 );
        ntt_write_f64( f64, f64Text );

        if ( ntt_decimal_to_f32( &decimal, &f32 ) == NUMBERTEXTTHREE_OUT_OF_RANGE )
        {
            printf( "%-16s double %-16s float out of range\n", inputs[ index ], f64Text );
            continue;
        }

        ntt_write_f32( f32, f32Text );
        printf( "%-16s double %-16s float %s\n", inputs[ index ], f64Text, f32Text );
    }

    return 0;
}

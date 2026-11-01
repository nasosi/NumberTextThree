#include "NumberTextThree.h"

int main( void )
{
    const char  text[] = "-12.5e-1";
    double      value  = 0;
    const char* stop   = 0;
    float       single = 0;
    char        output[ NUMBERTEXTTHREE_F64_MAX_CHARS ];
    char        integer[ NUMBERTEXTTHREE_INTEGER_MAX_CHARS ];
    ntt_decimal decimal  = { "125", 3, -2, 0 };
    int         failures = 0;

    if ( ntt_parse_json_f64( text, text + sizeof( text ) - 1, &value, &stop ) != NUMBERTEXTTHREE_OK || value != -1.25 || stop != text + sizeof( text ) - 1 )
    {
        failures++;
    }

    if ( ntt_decimal_to_f64( &decimal, &value ) != NUMBERTEXTTHREE_OK || value != 1.25 )
    {
        failures++;
    }

    if ( ntt_write_f64( 0.1, output ) != 3 || output[ 0 ] != '0' || output[ 1 ] != '.' || output[ 2 ] != '1' || output[ 3 ] != 0 )
    {
        failures++;
    }

    if ( ntt_decimal_to_f32( &decimal, &single ) != NUMBERTEXTTHREE_OK || single != 1.25F )
    {
        failures++;
    }

    if ( ntt_write_f32( 0.1F, output ) != 3 || output[ 0 ] != '0' || output[ 1 ] != '.' || output[ 2 ] != '1' || output[ 3 ] != 0 )
    {
        failures++;
    }

    if ( ntt_write_i64( -42, integer ) != 3 || integer[ 0 ] != '-' || integer[ 1 ] != '4' || integer[ 2 ] != '2' || integer[ 3 ] != 0 )
    {
        failures++;
    }

    if ( ntt_write_u32( 7, integer ) != 1 || integer[ 0 ] != '7' || integer[ 1 ] != 0 )
    {
        failures++;
    }

    return failures;
}

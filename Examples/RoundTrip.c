/* Reads each argument as a JSON number and writes it back out in the shortest form that reads back to the same
   double. For example:

       RoundTrip 0.1 1e23 -0 3.14159265358979323846 1e400 12abc */

#include "NumberTextThree.h"

#include <stdio.h>
#include <string.h>

int main( int argumentCount, char** arguments )
{
    int index;

    for ( index = 1; index < argumentCount; ++index )
    {
        const char* text = arguments[ index ];
        const char* stop;
        double      value  = 0.0;
        ntt_status  status = ntt_parse_json_f64( text, text + strlen( text ), &value, &stop );
        char        output[ NUMBERTEXTTHREE_F64_MAX_CHARS ];

        if ( status == NUMBERTEXTTHREE_INVALID || *stop != '\0' )
        {
            printf( "%-28s not a JSON number (stopped at offset %d)\n", text, (int)( stop - text ) );
            continue;
        }

        if ( status == NUMBERTEXTTHREE_OUT_OF_RANGE )
        {
            printf( "%-28s out of range, read as %sinfinity\n", text, value < 0.0 ? "minus " : "" );
            continue;
        }

        ntt_write_f64( value, output );
        printf( "%-28s %s\n", text, output );
    }

    return 0;
}

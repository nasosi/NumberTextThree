// When MSVC's debug heap finds an overrun it reports it and then carries on, which means a test that writes past a
// buffer would still exit with 0. Including this header in a debug build turns every CRT error or assertion report into
// an exit with code 3.

#pragma once

#if defined( _MSC_VER ) && defined( _DEBUG )

#    include <crtdbg.h>
#    include <cstdio>
#    include <cstdlib>

static int __cdecl FailOnCrtError( int reportType, char* message, int* )
{
    if ( reportType != _CRT_ERROR && reportType != _CRT_ASSERT )
    {
        return 0;
    }

    fputs( message, stderr );
    fflush( stderr );
    _exit( 3 );
}

static const int failOnCrtErrorInstalled = ( _CrtSetReportHook( FailOnCrtError ), 0 );

#endif

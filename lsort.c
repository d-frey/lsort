// Copyright (c) 2019 Daniel Frey
// Please see LICENSE for license or visit https://github.com/d-frey/lsort/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

char* prg;

void print_version()
{
   fprintf( stdout, "%s 0.0.1\n", prg );
}

void print_help()
{
   fprintf( stdout, "Usage: %s [OPTION]... FILE...\n"
                    "Sort almost-sorted FILE(s), works in-place\n"
                    "\n"
                    "OPTIONs\n"
                    "  -c, --compare N            compare no more than N characters per line\n"
                    "  -d, --distance N           maximum allowed shift distance\n"
                    "\n"
                    "  -q, --quiet                suppress progress output\n"
                    "  -V, --version              print program version\n"
                    "  -?, --help                 give this help list\n"
                    "\n"
                    "Mandatory or optional arguments to long options are also mandatory or optional\n"
                    "for any corresponding short options.\n"
                    "\n"
                    "Report bugs to d.frey@gmx.de.\n",
            prg );
}

size_t compare = 0;
size_t distance = 0;
int quiet = 0;

size_t calccmpsize( size_t a, size_t b )
{
   size_t m = ( a < b ) ? a : b;
   if( ( compare > 0 ) && ( m > compare ) ) {
      return compare;
   }
   return m;
}

#ifndef _GNU_SOURCE
void* memrchr( void* s, int c, size_t n )
{
   if( n != 0 ) {
      unsigned char* cp = (unsigned char*)s + n;
      do {
         if( *( --cp ) == (unsigned char)c )
            return cp;
      } while( --n != 0 );
   }
   return NULL;
}
#endif

char* find( char* pos, char* end )
{
   char* result = (char*)memchr( pos, '\n', end - pos );
   if( result != NULL ) {
      return ++result;
   }
   return end;
}

char* rfind( char* data, char* prev )
{
   char* result = (char*)memrchr( data, '\n', prev - data - 1 );
   if( result != NULL ) {
      return ++result;
   }
   return data;
}

size_t parse( char* p )
{
   char* endptr;
   errno = 0;
   size_t result = strtoul( p, &endptr, 0 );
   if( ( ( errno == ERANGE ) && ( result == ULONG_MAX ) ) || ( ( errno != 0 ) && ( result == 0 ) ) ) {
      perror( prg );
      exit( EXIT_FAILURE );
   }
   if( !isdigit( *p ) || ( *endptr != '\0' ) ) {
      fprintf( stderr, "%s: Argument requires an unsigned number, got '%s'\n", prg, p );
      exit( EXIT_FAILURE );
   }
   return result;
}

volatile sig_atomic_t status = 0;

void stop( int signal )
{
   status = signal;
}

int main( int argc, char** argv )
{
   signal( SIGTERM, stop );
   signal( SIGINT, stop );

   prg = argv[ 0 ];
   quiet = !isatty( fileno( stdout ) );

   static struct option long_options[] = {
      { "compare", required_argument, NULL, 'c' },
      { "distance", required_argument, NULL, 'd' },
      { "quiet", no_argument, NULL, 'q' },
      { "version", no_argument, NULL, 'V' },
      { "help", no_argument, NULL, 'h' },
      { NULL, 0, NULL, 0 }
   };

   int opt = 0;
   int long_index = 0;
   while( ( opt = getopt_long( argc, argv, "c:d:qVh?", long_options, &long_index ) ) != -1 ) {
      switch( opt ) {
         case 'c':
            compare = parse( optarg );
            break;
         case 'd':
            distance = parse( optarg );
            break;
         case 'q':
            quiet = 1;
            break;
         case 'V':
            print_version();
            return EXIT_SUCCESS;
         case 'h':
            print_help();
            return EXIT_SUCCESS;
         default:
            fprintf( stderr, "Try '%s --help' for more information.\n", prg );
            exit( EXIT_FAILURE );
      }
   }

   if( optind >= argc ) {
      fprintf( stderr, "%s: Missing FILE\nTry '%s --help' for more information.\n", prg, prg );
      exit( EXIT_FAILURE );
   }

   while( ( status == 0 ) && ( optind < argc ) ) {
      char* filename = argv[ optind++ ];

      errno = 0;
      int fd = open( filename, O_RDWR );
      if( fd < 0 ) {
         perror( filename );
         exit( EXIT_FAILURE );
      }

      struct stat st;
      errno = 0;
      if( fstat( fd, &st ) < 0 ) {
         perror( filename );
         exit( EXIT_FAILURE );
      }

      size_t size = st.st_size;
      if( size == 0 ) {
         return EXIT_SUCCESS;
      }

      char* data = (char*)mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
      if( data == (void*)-1 ) {
         perror( filename );
         exit( EXIT_FAILURE );
      }

      char* end = data + size;
      char* prev = data;
      char* current = find( prev, end );
      char* tmp = NULL;
      size_t ts = 0;
      size_t pc = 1000;
      size_t cnt = 0;

      while( ( status == 0 ) && ( current != end ) ) {
         if( !quiet && ( ( cnt++ % 65536 ) == 0 ) ) {
            size_t npc = 100 * ( current - data ) / ( end - data );
            if( npc != pc ) {
               fprintf( stdout, "\r%s... %lu%%", filename, npc );
               fflush( stdout );
               sleep( 1 );
               pc = npc;
            }
         }

         char* next = find( current, end );
         if( memcmp( prev, current, calccmpsize( current - prev, next - current ) ) > 0 ) {
            while( ( status == 0 ) && ( prev != data ) ) {
               char* peek = rfind( data, prev );
               if( memcmp( peek, current, calccmpsize( prev - peek, next - current ) ) > 0 ) {
                  prev = peek;
               }
               else {
                  break;
               }
            }
            size_t s = next - current;
            if( s + 1 > ts ) {
               ts = s + 1;
               tmp = (char*)realloc( tmp, ts );
               if( tmp == NULL ) {
                  if( !quiet ) {
                     putchar( '\n' );
                  }
                  fprintf( stderr, "%s: Out of memory\n", prg );
                  exit( EXIT_FAILURE );
               }
            }
            size_t final = current - prev;
            if( ( distance != 0 ) && ( final > distance ) ) {
               if( !quiet ) {
                  putchar( '\n' );
               }
               fprintf( stderr, "%s: Required distance of %lu exceeds allowed distance of %lu\n", prg, final, distance );
               exit( EXIT_FAILURE );
            }
            memcpy( tmp, current, s );
            if( tmp[ s - 1 ] != '\n' ) {
               tmp[ s++ ] = '\n';
            }
            memmove( prev + s, prev, current - prev - 1 );
            memcpy( prev, tmp, s );
            msync( prev, next - prev, MS_ASYNC );
            current = next - ( current - prev );
            prev = rfind( data, current );
         }
         else {
            prev = current;
            current = next;
         }
      }

      munmap( data, size );
      close( fd );

      if( ( status == 0 ) && !quiet ) {
         fprintf( stdout, "\r%s... done\n", filename );
      }
   }

   if( status != 0 ) {
      if( !quiet ) {
         putchar( '\n' );
      }
      fprintf( stderr, "%s: ABORTED\n", prg );
      return EXIT_FAILURE;
   }

   return EXIT_SUCCESS;
}

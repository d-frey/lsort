// sort an almost-sorted file in-place

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void print_version()
{
   puts( "lsort 0.0.1" );
}

void print_help()
{
   puts( "Usage: lsort [OPTION] FILE\n"
         "\n"
         "  -c, --compare N            compare no more than N characters per line\n"
         "  -d, --distance N           allow maximum distance of N bytes for swapping lines\n"
         "\n"
         "  -v, --verbose              show progress and statistics\n"
         "  -V, --version              print program version\n"
         "  -?, --help                 give this help list\n"
         "\n"
         "Mandatory or optional arguments to long options are also mandatory or optional\n"
         "for any corresponding short options.\n"
         "\n"
         "Report bugs to d.frey@gmx.de." );
}

int compare = 0;
int distance = 0;

size_t calccmpsize( size_t a, size_t b )
{
   size_t m = ( a < b ) ? a : b;
   if( compare > 0 && m > (size_t)compare ) {
      return compare;
   }
   return m;
}

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
   char* result = (char*)memchr( data, '\n', prev - data - 1 );
   if( result != NULL ) {
      return ++result;
   }
   return data;
}

int main( int argc, char** argv )
{
   static struct option long_options[] = {
      { "compare", required_argument, NULL, 'c' },
      { "distance", required_argument, NULL, 'd' },
      { "version", no_argument, NULL, 'V' },
      { "help", no_argument, NULL, 'h' },
      { NULL, 0, NULL, 0 }
   };

   int opt = 0;
   int long_index = 0;
   while( ( opt = getopt_long( argc, argv, "c:d:vVh?", long_options, &long_index ) ) != -1 ) {
      switch( opt ) {
         case 'c':
            compare = atoi( optarg );
            break;
         case 'd':
            distance = atoi( optarg );
            break;
         case 'V':
            print_version();
            exit( EXIT_SUCCESS );
         case 'h':
            print_help();
            exit( EXIT_SUCCESS );
         default:
            fprintf( stderr, "Try '%s --help' for more information.\n", argv[ 0 ] );
            exit( EXIT_FAILURE );
      }
   }

   if( optind != ( argc - 1 ) ) {
      fprintf( stderr, "%s: missing FILE\nTry '%s --help' for more information.\n", argv[ 0 ], argv[ 0 ] );
      exit( EXIT_FAILURE );
   }

   char* filename = argv[ optind ];

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
      return 0;
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

   while( current != end ) {
      char* next = find( current, end );
      if( memcmp( prev, current, calccmpsize( current - prev, next - current ) ) > 0 ) {
         while( prev != data ) {
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
               fprintf( stderr, "%s: out of memory\n", argv[ 0 ] );
               exit( EXIT_FAILURE );
            }
         }
         size_t final = current - prev;
         if( distance != 0 && final > (size_t)distance ) {
            fprintf( stderr, "%s: required distance of %lu exceeds allowed distance of %i\n", argv[ 0 ], final, distance );
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
}

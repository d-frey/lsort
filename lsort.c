// sort an almost-sorted file in-place

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

size_t min( size_t a, size_t b )
{
   return ( a < b ) ? a : b;
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
   // TODO: Check # of arguments, print usage, help, version...
   char* filename = argv[ 1 ];
   (void)argc;

   errno = 0;
   int fd = open( filename, O_RDWR );
   if( fd < 0 ) {
      perror( filename );
      puts( "open() failed" );
      exit( 1 );
   }

   struct stat st;
   errno = 0;
   if( fstat( fd, &st ) < 0 ) {
      perror( filename );
      puts( "fstat() failed" );
      exit( 1 );
   }

   size_t size = st.st_size;
   if( size == 0 ) {
      return 0;
   }

   char* data = (char*)mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
   if( data == (void*)-1 ) {
      perror( filename );
      puts( "mmap() failed" );
      exit( 1 );
   }

   char* end = data + size;
   char* prev = data;
   char* current = find( prev, end );
   char* tmp = NULL;
   size_t ts = 0;

   while( current != end ) {
      char* next = find( current, end );
      if( memcmp( prev, current, min( current - prev, next - current ) ) > 0 ) {
         while( prev != data ) {
            char* peek = rfind( data, prev );
            if( memcmp( peek, current, min( prev - peek, next - current ) ) > 0 ) {
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
               puts( "out of memory" );
               exit( 1 );
            }
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

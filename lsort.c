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

size_t max_compare = 0;
size_t max_distance = 0;
int quiet = 0;
int verbose = 0;
int msync_mode = MS_ASYNC;

char* buffer = NULL;
size_t bufsize = 0;

void print_version()
{
   fprintf( stdout, "%s 0.0.1\n", prg );
}

void print_help()
{
   fprintf( stdout, "Usage: %s [OPTION]... FILE...\n"
                    "Sort almost-sorted FILE(s), works in-place\n"
                    "\n"
                    "Options:\n"
                    "  -c, --compare N            compare no more than N characters per line\n"
                    "  -d, --distance N           maximum shift distance in bytes, default: 1M\n"
                    "      --sync                 use synchronous writes\n"
                    "\n"
                    "  -q, --quiet                suppress progress output\n"
                    "  -v, --verbose              report changes to the file\n"
                    "      --help                 display this help and exit\n"
                    "      --version              output version information and exit\n"
                    "\n"
                    "N may be followed by the following multiplicative suffixes:\n"
                    "B=1, K=1024, and so on for M, G, T, P, E.\n"
                    "\n"
                    "By default, --compare is 0, meaning no limit when comparing lines.\n"
                    "A non-zero value for --compare may result in non-sorted files.\n"
                    "\n"
                    "Report bugs to: <https://github.com/d-frey/lsort/>\n",
            prg );
}

size_t zmin( size_t a, size_t b )
{
   return ( a < b ) ? a : b;
}

char* cmin( char* a, char* b )
{
   return ( a == NULL ) ? b : ( ( a < b ) ? a : b );
}

char* cmax( char* a, char* b )
{
   return ( a == NULL ) ? b : ( ( a > b ) ? a : b );
}

// lhs <= rhs
int le( char* lhs_begin, char* lhs_end, char* rhs_begin, char* rhs_end )
{
   if( ( lhs_end != lhs_begin ) && ( *( lhs_end - 1 ) == '\n' ) ) {
      --lhs_end;
   }
   if( ( rhs_end != rhs_begin ) && ( *( rhs_end - 1 ) == '\n' ) ) {
      --rhs_end;
   }
   const size_t lhs_size = lhs_end - lhs_begin;
   const size_t rhs_size = rhs_end - rhs_begin;
   size_t size = zmin( lhs_size, rhs_size );
   if( ( max_compare != 0 ) && ( size > max_compare ) ) {
      size = max_compare;
   }
   int r = memcmp( lhs_begin, rhs_begin, size );
   if( r != 0 ) {
      return r < 0;
   }
   if( ( max_compare != 0 ) && ( size == max_compare ) ) {
      return 1;
   }
   return lhs_size <= rhs_size;
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
   if( !isdigit( *p ) ) {
      fprintf( stderr, "%s: Invalid argument '%s'\n", prg, p );
      exit( EXIT_FAILURE );
   }
   char* endptr;
   errno = 0;
   const size_t n = strtoul( p, &endptr, 10 );
   if( ( ( errno == ERANGE ) && ( n == ULONG_MAX ) ) || ( ( errno != 0 ) && ( n == 0 ) ) ) {
      perror( prg );
      exit( EXIT_FAILURE );
   }
   size_t f = 1;
   switch( *endptr ) {
      case '\0':
         break;
      case 'E':
         f *= 1024;
         // fall-through
      case 'P':
         f *= 1024;
         // fall-through
      case 'T':
         f *= 1024;
         // fall-through
      case 'G':
         f *= 1024;
         // fall-through
      case 'M':
         f *= 1024;
         // fall-through
      case 'K':
         f *= 1024;
         // fall-through
      case 'B':
         ++endptr;
         break;
   }
   if( *endptr != '\0' ) {
      fprintf( stderr, "%s: Invalid argument '%s'\n", prg, p );
      exit( EXIT_FAILURE );
   }
   const size_t result = n * f;
   if( result / f != n ) {
      errno = ERANGE;
      perror( prg );
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
      { "sync", no_argument, NULL, 0 },
      { "quiet", no_argument, NULL, 'q' },
      { "verbose", no_argument, NULL, 'v' },
      { "help", no_argument, NULL, 0 },
      { "version", no_argument, NULL, 0 },
      { NULL, 0, NULL, 0 }
   };

   int opt = 0;
   int long_index = 0;
   while( ( opt = getopt_long( argc, argv, "c:d:qv", long_options, &long_index ) ) != -1 ) {
      switch( opt ) {
         case 'c':
            max_compare = parse( optarg );
            break;
         case 'd':
            max_distance = parse( optarg );
            break;
         case 'q':
            quiet = 1;
            break;
         case 'v':
            verbose = 1;
            break;
         case 0: {
            const char* name = long_options[ long_index ].name;
            if( strcmp( name, "sync" ) == 0 ) {
               msync_mode = MS_SYNC;
               break;
            }
            if( strcmp( name, "help" ) == 0 ) {
               print_help();
               return EXIT_SUCCESS;
            }
            if( strcmp( name, "version" ) == 0 ) {
               print_version();
               return EXIT_SUCCESS;
            }
            exit( EXIT_FAILURE );
         }
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
      const char* const filename = argv[ optind++ ];

      errno = 0;
      const int fd = open( filename, O_RDWR );
      if( fd < 0 ) {
         perror( filename );
         exit( EXIT_FAILURE );
      }

      struct stat st;
      errno = 0;
      if( fstat( fd, &st ) < 0 ) {
         perror( filename );
         close( fd );
         exit( EXIT_FAILURE );
      }

      const size_t size = st.st_size;
      if( size == 0 ) {
         if( !quiet ) {
            fprintf( stdout, "%s: done\n", filename );
         }
         close( fd );
         continue;
      }

      char* const data = (char*)mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
      if( data == (void*)-1 ) {
         perror( filename );
         close( fd );
         exit( EXIT_FAILURE );
      }

      char* const end = data + size;

      char* prev = data;
      char* current = find( prev, end );

      size_t line = 2;
      size_t last_progress = 1000;

      char* msync_begin = NULL;
      char* msync_end = NULL;

      while( ( status == 0 ) && ( current != end ) ) {
         if( !quiet ) {
            const size_t progress = 100 * ( current - data ) / ( end - data );
            if( last_progress != progress ) {
               fprintf( stdout, "\r%s: %lu%%", filename, progress );
               fflush( stdout );
               last_progress = progress;
            }
         }

         char* next = find( current, end );
         if( !le( prev, current, current, next ) ) {
            size_t prev_line = line - 1;
            while( ( status == 0 ) && ( prev != data ) ) {
               if( max_distance != 0 ) {
                  const size_t distance = next - prev;
                  if( distance > max_distance ) {
                     if( !quiet ) {
                        putchar( '\n' );
                     }
                     fprintf( stderr, "%s:%lu: Distance exceeds allowed maximum of %lu\n", filename, line, max_distance );
                     goto exit_with_error;
                  }
               }

               char* const peek = rfind( data, prev );
               if( !le( peek, prev, current, next ) ) {
                  prev = peek;
                  --prev_line;
               }
               else {
                  break;
               }
            }

            size_t next_line = line;
            if( prev_line + 1 == line ) {
               while( ( status == 0 ) && ( next != end ) ) {
                  if( max_distance != 0 ) {
                     const size_t distance = next - prev;
                     if( distance > max_distance ) {
                        if( !quiet ) {
                           putchar( '\n' );
                        }
                        fprintf( stderr, "%s:%lu: Distance exceeds allowed maximum of %lu\n", filename, line, max_distance );
                        goto exit_with_error;
                     }
                  }

                  char* const peek = find( next, end );
                  if( !le( prev, current, next, peek ) ) {
                     next = peek;
                     ++next_line;
                  }
                  else {
                     break;
                  }
               }
            }

            char* new_begin = cmin( msync_begin, prev );
            char* new_end = cmax( msync_end, next );

            if( max_distance != 0 ) {
               const size_t new_size = new_end - new_begin;
               if( new_size > max_distance ) {
                  msync( msync_begin, msync_end - msync_begin, msync_mode );
                  new_begin = prev;
                  new_end = next;
               }
            }

            const size_t prev_size = current - prev;
            size_t current_size = next - current;

            const size_t required_bufsize = zmin( prev_size, current_size + 1 );
            if( bufsize < required_bufsize ) {
               bufsize = required_bufsize;
               buffer = (char*)realloc( buffer, bufsize );
               if( buffer == NULL ) {
                  if( !quiet ) {
                     putchar( '\n' );
                  }
                  fprintf( stderr, "%s:%lu: Out of memory reserving %ld bytes\n", filename, line, bufsize );
                  goto exit_with_error;
               }
            }

            if( current_size <= prev_size ) {
               if( verbose ) {
                  fprintf( stdout, "\r%s:%lu: moved back to line %lu\n", filename, line, prev_line );
                  if( !quiet ) {
                     fprintf( stdout, "%s: %lu%%", filename, last_progress );
                     fflush( stdout );
                  }
               }

               memcpy( buffer, current, current_size );
               if( buffer[ current_size - 1 ] != '\n' ) {
                  buffer[ current_size++ ] = '\n';
               }
               memmove( prev + current_size, prev, prev_size - 1 );
               memcpy( prev, buffer, current_size );

               current = rfind( data, next );
               prev = rfind( data, current );
            }
            else {
               if( verbose ) {
                  fprintf( stdout, "\r%s:%lu: moved forward to line %lu\n", filename, prev_line, next_line );
                  if( !quiet ) {
                     fprintf( stdout, "%s: %lu%%", filename, last_progress );
                     fflush( stdout );
                  }
               }

               memcpy( buffer, prev, prev_size );
               memmove( prev, prev + prev_size, current_size );
               if( prev[ current_size - 1 ] != '\n' ) {
                  prev[ current_size++ ] = '\n';
               }
               memcpy( prev + current_size, buffer, prev_size - 1 );

               current = find( prev, end );
            }

            msync_begin = new_begin;
            msync_end = new_end;
         }
         else {
            if( msync_begin != NULL ) {
               msync( msync_begin, msync_end - msync_begin, msync_mode );
               msync_begin = NULL;
               msync_end = NULL;
            }
            prev = current;
            current = next;
            ++line;
         }
      }

      if( msync_begin != NULL ) {
         msync( msync_begin, msync_end - msync_begin, msync_mode );
      }

      munmap( data, size );
      close( fd );

      if( ( status == 0 ) && !quiet ) {
         fprintf( stdout, "\r%s: done\n", filename );
      }
      continue;

   exit_with_error:
      if( msync_begin != NULL ) {
         msync( msync_begin, msync_end - msync_begin, msync_mode );
      }
      munmap( data, size );
      close( fd );
      exit( EXIT_FAILURE );
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

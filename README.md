```
Usage: lsort [OPTION]... FILE...
Sort almost-sorted FILE(s), works in-place

Options:
  -c, --compare N            compare no more than N characters per line
  -d, --distance N           maximum shift distance in bytes, default: 1M

  -q, --quiet                suppress progress output
  -V, --version              print program version
  -?, --help                 give this help list

N may be followed by the following multiplicative suffixes:
B=1, K=1024, and so on for M, G, T, P, E.

By default, --compare is 0, meaning no limit when comparing lines.
A non-zero value for --compare may result in non-sorted files.

Report bugs to: <https://github.com/d-frey/lsort/>
```

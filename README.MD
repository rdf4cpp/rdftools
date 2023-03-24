⚠️ This repo is work-in-progress! Before v0.1.0 all APIs are considered unstable and might be subject to change. ⚠️

# rdftools

This repository hosts handy tools based on [rdf4cpp](https://github.com/rdf4cpp/rdf4cpp) to process RDF data. You can
expect all tools to work fast and fairly resource efficient.

For now, only one tools is available:

- `deduprdf`: Deduplicates RDF files (TURTLE, NTRIPLE).

## Download

Binaries are available for download in [releases](https://github.com/rdf4cpp/rdftools/releases/latest).

## Usage

`deduprdf` supports piping:

```shell script
cat swdf.nt | ./deduprdf > swdf_dedup.nt
```

and working with files:

```shell
./deduprdf --file swdf.nt --output swdf_dedup.nt```
```
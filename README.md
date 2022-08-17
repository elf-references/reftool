# reftool

`reftool` provides a reference implementation of the
[ELF references specification][spec].

   [spec]: https://github.com/elf-references/spec

## Dependencies

* libelf development headers
  - Alpine: `apk add elfutils-dev`
  - Arch: `pacman -Sy elfutils`
  - Debian/Ubuntu: `apt install libelf-dev`
  - Fedora: `dnf install elfutils-libelf-devel`

## Usage

### Adding references

The `reftool add` command adds references to a binary:

```shell
$ reftool add ./example text/spdx https://example.com/example.spdx
```

### Listing references

The `reftool list` command lists references contained in
a binary:

```shell
$ reftool list ./example
```
```
https://example.com/example.spdx (text/spdx)
```

## Troubleshooting

### I get `reftool: reading program header: invalid ELF handle`

`reftool` and the ELF references specification only work on
ELF binary programs.  It does not support programs written in
scripting language -- for those, you should use the language's
preferred mechanism for describing program references, if
one is available.


# umbf-convert

**umbf-convert** is a CLI utility to inspect UMBF files, extract their payloads, and convert external files into UMBF format.

## Features

* **show** - print summary information about a UMBF file (metadata, structure, sizes).
* **extract** - extract the raw payload from a UMBF file without modifications.
* **convert** - create a UMBF file from an external source in one of the supported input formats:

  * `raw` - take an arbitrary binary file as is and store it into a UMBF block.
  * `json` - read a JSON descriptor (e.g., a material/asset description) and produce a UMBF file.
  * `image` - import an image into a UMBF image.
  * `scene` - import a scene/mesh file into a UMBF scene.

Optional flag `--compressed` (for `convert`) enables writing the resulting UMBF in compressed form (when supported by the block type).

## Usage

General help:

```bash
umbf-convert --help
```

Show version:

```bash
umbf-convert -v
```

CLI synopsis:

```
Commands:
  show      Show UMBF file info
  extract   Extract UMBF file
  convert   Convert INTO UMBF from an external source

Global options:
  -h, --help       Show help
  -v, --version    Show version

show:
  -i, --input <path>                 (required)  UMBF file

extract:
  -i, --input <path>                 (required)  UMBF file
  -o, --output <path>                (required)  destination file

convert:
  -i, --input <path>                 (required)  external source file
      --format <raw|json|image|scene> (required) input interpretation
  -o, --output <path>                (required)  output UMBF file
      --compressed                               write compressed UMBF
```

## Building

### Supported compilers:
- GNU GCC
- Clang

### Supported OS:
- Linux
- Microsoft Windows

### Cmake options:
- `USE_ASAN`: Enable address sanitizer
- `BUILD_TESTS`: Enable testing
- `ENABLE_COVERAGE`: Enable code coverage

### Bundled submodules
The following dependencies are included as git submodules and must be checked out when cloning:

- [acbt](https://git.homedatasrv.ru/app3d/acbt)
- [acul](https://git.homedatasrv.ru/app3d/acul)
- [umbf](https://git.homedatasrv.ru/app3d/umbf)
- [aecl](https://git.homedatasrv.ru/app3d/aecl)
- [args](https://github.com/Taywee/args)

## License
This project is licensed under the [MIT License](LICENSE).

## Contacts
For any questions or feedback, you can reach out via [email](mailto:wusikijeronii@gmail.com) or open a new issue.
# EmbedIDS Examples

Example applications demonstrating EmbedIDS usage for embedded intrusion detection.

## Examples

- **`simple_example.c`** - Basic EmbedIDS API usage
- **`user_application.c`** - Real-world application integration  
- **`extensible_example.c`** - Advanced features and custom algorithms
- **`tutorial_example.c`** - Step-by-step tutorial implementation

## Quick Build

### Independent Build (Recommended)
```bash
cd examples/
mkdir build && cd build
cmake ..
make
```

### From EmbedIDS Source Tree
```bash
# From main EmbedIDS directory
mkdir build && cd build
cmake -DBUILD_EXAMPLES=ON ..
make
```

## Usage

Run individual examples:
```bash
./embedids_example
./user_application_example
./extensible_example
./tutorial_example
```

## Integration

To use EmbedIDS in your project:
```cmake
find_package(EmbedIDS REQUIRED)
add_executable(my_app main.c)
target_link_libraries(my_app EmbedIDS::embedids)
```

## Dependencies

- EmbedIDS library (installed or source build)
- CMake 3.16+
- C11 compiler (GCC/Clang)

## Troubleshooting

**EmbedIDS not found?**
- Install EmbedIDS: `cd /path/to/embedids && mkdir build && cd build && cmake .. && make && sudo make install`
- Or set: `cmake -DCMAKE_PREFIX_PATH=/path/to/embedids/install ..`

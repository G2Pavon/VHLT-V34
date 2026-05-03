
## Build

```bat
git clone https://github.com/G2Pavon/VHLT-V34
cd VHLT-V34
```

```bat
mkdir build
cd build
```

Windows
```bat
cmake -S .. -B . -G "Visual Studio 18 2026" -A x64
cmake --build . --config Release
```

Linux
```bat
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
cmake --build .
```
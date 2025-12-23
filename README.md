# ECS-Lab

Lightweight ECS prototype with snapshot and prefab support.

## Build (CMake + vcpkg)

This project expects `doctest` to be available via vcpkg (or any other CMake package provider).

```powershell
cmake -S . -B out/build/x64-debug -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build out/build/x64-debug
ctest --test-dir out/build/x64-debug -C Debug --output-on-failure
```

## Layout

- `include/ecs_lab`: ECS headers
- `src/ecs_lab_headers.cpp`: header TU for tooling / compile_commands
- `tests/test_ecs_lab.cpp`: unit tests (doctest)
- `tests/bench_signature.cpp`: micro-bench for signature rank
- `docs/ecs_lab_api.md`: API + evaluation
- `docs/ECS.md`: design notes

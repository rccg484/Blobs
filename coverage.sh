rm -rf build/coverage

cmake --preset coverage
cmake --build --preset coverage
mkdir -p build/coverage/profraw
ctest --preset coverage

cd build/coverage
llvm-profdata merge -sparse profraw/*.profraw -o merged.profdata

llvm-cov report \
    -object=tests/test_bloballoc.exe \
    -object=tests/test_blobbox.exe \
    -object=tests/test_blobvec.exe \
    -object=tests/test_integration.exe \
    -instr-profile=merged.profdata \
    -ignore-filename-regex='(_deps|test_)'

llvm-cov show \
    -object=tests/test_bloballoc.exe \
    -object=tests/test_blobbox.exe \
    -object=tests/test_blobvec.exe \
    -object=tests/test_integration.exe \
    -instr-profile=merged.profdata \
    -ignore-filename-regex='(_deps|test_)' \
    -format=html \
    -output-dir=html

cd ../..
# trihexor
this project has no objective

# notes for me

from build directory:

```bash
# get debug dependencies
conan install ../trihexor -b missing -s build_type=Debug
# get release dependencies
conan install ../trihexor -b missing -s build_type=Release
# generate
cmake ../trihexor {blah blah cmake args}
```

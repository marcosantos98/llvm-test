# llvm test

Generate llvm-ir from a basic stack virtual machine and compile it to the host machine target.

## OPs:

| opname | description | operand |
| ------ | ----------- | ------- |
| pushi | push integer | number |
| pushs | push string literal | "urmom"|
| puts |print string literal | none |
| exit |exit program | none |
| ; | end operation | none |

## Sample:

```asm
pushs "Hello world!\n";
puts;
pushi 0;
exit;
```

## Run:

Requires `llvm`, `clang`, `make`

```bash
make run
```
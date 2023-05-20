# Compilation

## Compiling options

- `make DEBUG=-DDEBUG` : Enable debugging mode with logging
- `make NOASSERT=-DNDEBUG` : Disable assertions
- `make DEADLOCK=-DALLOWDEADLOCK` : Disable anti-deadlock support. Tests 81 to 84 will not pass

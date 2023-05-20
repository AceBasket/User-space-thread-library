# Compilation

## Compiling options

- `make DEBUG=-DDEBUG` : Enable debugging mode with logging
- `make DEADLOCK=-DALLOWDEADLOCK` : Disable anti-deadlock support. Tests 81 to 84 will not pass
- `make PREEMPTION=-DPREEMPTION` : Enable preemption functionnality (bêta, has memory errors)
